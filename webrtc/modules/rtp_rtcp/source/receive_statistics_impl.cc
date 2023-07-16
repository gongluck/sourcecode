/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/receive_statistics_impl.h"

#include <cmath>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "modules/remote_bitrate_estimator/test/bwe_test_logging.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_config.h"
#include "modules/rtp_rtcp/source/time_util.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

const int64_t kStatisticsTimeoutMs = 8000;
const int64_t kStatisticsProcessIntervalMs = 1000;

StreamStatistician::~StreamStatistician() {}

StreamStatisticianImpl::StreamStatisticianImpl(uint32_t ssrc,
                                               Clock* clock,
                                               int max_reordering_threshold)
    : ssrc_(ssrc),
      clock_(clock),
      incoming_bitrate_(kStatisticsProcessIntervalMs,
                        RateStatistics::kBpsScale),
      max_reordering_threshold_(max_reordering_threshold),
      enable_retransmit_detection_(false),
      jitter_q4_(0),
      cumulative_loss_(0),
      cumulative_loss_rtcp_offset_(0),
      last_receive_time_ms_(0),
      last_received_timestamp_(0),
      received_seq_first_(-1),
      received_seq_max_(-1),
      last_report_cumulative_loss_(0),
      last_report_seq_max_(-1) {}

StreamStatisticianImpl::~StreamStatisticianImpl() = default;

bool StreamStatisticianImpl::UpdateOutOfOrder(const RtpPacketReceived& packet,
                                              int64_t sequence_number,
                                              int64_t now_ms) {
  // Check if |packet| is second packet of a stream restart.
  if (received_seq_out_of_order_) {  // 上一个包的序号与之前的最大包序号差值过大，判断是否是流重新开始的情况
    // Count the previous packet as a received; it was postponed below.
    --cumulative_loss_;  // 丢包数减1，减少记录received_seq_out_of_order_时增加的丢包记录

    uint16_t expected_sequence_number =  // 期望的包序号
        *received_seq_out_of_order_ + 1;
    received_seq_out_of_order_ = absl::nullopt;                 // 复位标记
    if (packet.SequenceNumber() == expected_sequence_number) {  // 包按序到达
      // Ignore sequence number gap caused by stream restart for packet loss
      // calculation, by setting received_seq_max_ to the sequence number just
      // before the out-of-order seqno. This gives a net zero change of
      // |cumulative_loss_|, for the two packets interpreted as a stream reset.
      //
      // Fraction loss for the next report may get a bit off, since we don't
      // update last_report_seq_max_ and last_report_cumulative_loss_ in a
      // consistent way.

      // 忽略由于数据包丢失而重新启动流导致的序列号间隙，下一个包到达时间隙就不会超过max_reordering_threshold_了
      last_report_seq_max_ = sequence_number - 2;
      received_seq_max_ = sequence_number - 2;
      return false;
    }
  }

  if (std::abs(sequence_number - received_seq_max_) >
      max_reordering_threshold_) {  // 新包序号与上周期的最大包序号间隔过大
    // Sequence number gap looks too large, wait until next packet to check
    // for a stream restart.
    received_seq_out_of_order_ =  // 缓存包序号，延迟参数的计算到下一个包到达时
        packet.SequenceNumber();
    // Postpone counting this as a received packet until we know how to update
    // |received_seq_max_|, otherwise we temporarily decrement
    // |cumulative_loss_|. The
    // ReceiveStatisticsTest.StreamRestartDoesntCountAsLoss test expects
    // |cumulative_loss_| to be unchanged by the reception of the first packet
    // after stream reset.
    ++cumulative_loss_;  // 简单增加丢包数
    return true;
  }

  if (sequence_number >
      received_seq_max_)  // 当前包序号比上个周期的最大包序号大，为非乱序包
    return false;

  // 下面是乱序情况

  // Old out of order packet, may be retransmit.
  if (enable_retransmit_detection_ &&  // 判断是否开启重传包判断
      IsRetransmitOfOldPacket(packet, now_ms))  // 判断是否重传乱序包
    receive_counters_.retransmitted.AddPacket(packet);  // 增加重传计数
  return true;
}

// 更新计数
void StreamStatisticianImpl::UpdateCounters(const RtpPacketReceived& packet) {
  RTC_DCHECK_EQ(ssrc_, packet.Ssrc());
  int64_t now_ms = clock_->TimeInMilliseconds();

  // 数据接收统计
  incoming_bitrate_.Update(packet.size(), now_ms);
  receive_counters_.last_packet_received_timestamp_ms = now_ms;
  receive_counters_.transmitted.AddPacket(packet);

  --cumulative_loss_;  // 丢包数减1，如果要想得到原始丢包率，重传包就不能进入这里统计了，WebRTC提供了RTX机制，重传包用额外SSRC的包发送，这样重传包就不会干扰原始媒体包的统计。

  int64_t sequence_number =
      seq_unwrapper_.UnwrapWithoutUpdate(packet.SequenceNumber());

  if (!ReceivedRtpPacket()) {  // 第一个包
    received_seq_first_ = sequence_number;
    last_report_seq_max_ = sequence_number - 1;
    received_seq_max_ = sequence_number - 1;
    receive_counters_.first_packet_time_ms = now_ms;
  } else if (UpdateOutOfOrder(packet, sequence_number,
                              now_ms) /*检查是否乱序包*/) {
    return;
  }

  // 下面为非乱序情况

  // In order packet.
  cumulative_loss_ += sequence_number - received_seq_max_;  // 修正丢包数
  received_seq_max_ =
      sequence_number;  // 非乱序下，接收到的最大包序号自然是当前包序号
  seq_unwrapper_.UpdateLast(sequence_number);

  // If new time stamp and more than one in-order packet received, calculate
  // new jitter statistics.
  if (packet.Timestamp() != last_received_timestamp_ &&
      (receive_counters_.transmitted.packets -
       receive_counters_.retransmitted.packets) > 1) {
    UpdateJitter(packet, now_ms);  // 更新抖动
  }
  last_received_timestamp_ = packet.Timestamp();
  last_receive_time_ms_ = now_ms;
}

void StreamStatisticianImpl::UpdateJitter(const RtpPacketReceived& packet,
                                          int64_t receive_time_ms) {
  int64_t receive_diff_ms =  // 包到达时间间隔
      receive_time_ms - last_receive_time_ms_;
  RTC_DCHECK_GE(receive_diff_ms, 0);

  uint32_t receive_diff_rtp =  // 包到达时间间隔，rtp时间戳
      static_cast<uint32_t>(
          (receive_diff_ms * packet.payload_type_frequency()) / 1000);

  int32_t time_diff_samples =  // 抖动延时
      receive_diff_rtp - (packet.Timestamp() - last_received_timestamp_);

  time_diff_samples = std::abs(time_diff_samples);

  // lib_jingle sometimes deliver crazy jumps in TS for the same stream.
  // If this happens, don't update jitter value. Use 5 secs video frequency
  // as the threshold.
  if (time_diff_samples < 450000) {  // 5秒视频频率作为阈值
    // jitter(i) = jitter(i-1) + (time_diff(i, i-1) - jitter(i-1)) / 16
    // Note we calculate in Q4 to avoid using float.
    int32_t jitter_diff_q4 =                    // time_diff*16 - jitter(i-1)*16
        (time_diff_samples << 4) - jitter_q4_;  // 左移4位，避免使用浮点运算
    jitter_q4_ +=  //(time_diff*16-jitter(i-1)*16)/16 == time_diff-jitter(i-1)
        ((jitter_diff_q4 + 8) >> 4);
  }
}

void StreamStatisticianImpl::SetMaxReorderingThreshold(
    int max_reordering_threshold) {
  max_reordering_threshold_ = max_reordering_threshold;
}

void StreamStatisticianImpl::EnableRetransmitDetection(bool enable) {
  enable_retransmit_detection_ = enable;
}

RtpReceiveStats StreamStatisticianImpl::GetStats() const {
  RtpReceiveStats stats;
  stats.packets_lost = cumulative_loss_;
  // TODO(nisse): Can we return a float instead?
  // Note: internal jitter value is in Q4 and needs to be scaled by 1/16.
  stats.jitter = jitter_q4_ >> 4;
  stats.last_packet_received_timestamp_ms =
      receive_counters_.last_packet_received_timestamp_ms;
  stats.packet_counter = receive_counters_.transmitted;
  return stats;
}

bool StreamStatisticianImpl::GetActiveStatisticsAndReset(
    RtcpStatistics* statistics) {
  if (clock_->TimeInMilliseconds() - last_receive_time_ms_ >=
      kStatisticsTimeoutMs) {
    // Not active.
    return false;
  }
  if (!ReceivedRtpPacket()) {
    return false;
  }
  *statistics = CalculateRtcpStatistics();
  return true;
}

RtcpStatistics StreamStatisticianImpl::CalculateRtcpStatistics() {
  RtcpStatistics stats;
  // Calculate fraction lost.
  int64_t exp_since_last =  // 计算期望收到的包数
      received_seq_max_ - last_report_seq_max_;
  RTC_DCHECK_GE(exp_since_last, 0);

  int32_t lost_since_last =  // 计算本周期的丢包数
      cumulative_loss_ - last_report_cumulative_loss_;
  if (exp_since_last > 0 && lost_since_last > 0) {
    // Scale 0 to 255, where 255 is 100% loss.
    stats.fraction_lost =  // 计算丢包率
        static_cast<uint8_t>(255 * lost_since_last / exp_since_last);
  } else {
    stats.fraction_lost = 0;
  }

  // TODO(danilchap): Ensure |stats.packets_lost| is clamped to fit in a signed
  // 24-bit value.
  stats.packets_lost = cumulative_loss_ + cumulative_loss_rtcp_offset_;
  if (stats.packets_lost < 0) {  // 防止符号位溢出
    // Clamp to zero. Work around to accomodate for senders that misbehave with
    // negative cumulative loss.
    stats.packets_lost = 0;
    cumulative_loss_rtcp_offset_ = -cumulative_loss_;
  }

  // 当前收到的最大包序列号
  stats.extended_highest_sequence_number =
      static_cast<uint32_t>(received_seq_max_);

  // Note: internal jitter value is in Q4 and needs to be scaled by 1/16.
  stats.jitter = jitter_q4_ >> 4;

  // Only for report blocks in RTCP SR and RR.
  last_report_cumulative_loss_ = cumulative_loss_;
  last_report_seq_max_ = received_seq_max_;
  BWE_TEST_LOGGING_PLOT_WITH_SSRC(1, "cumulative_loss_pkts",
                                  clock_->TimeInMilliseconds(),
                                  cumulative_loss_, ssrc_);
  BWE_TEST_LOGGING_PLOT_WITH_SSRC(
      1, "received_seq_max_pkts", clock_->TimeInMilliseconds(),
      (received_seq_max_ - received_seq_first_), ssrc_);

  return stats;
}

absl::optional<int> StreamStatisticianImpl::GetFractionLostInPercent() const {
  if (!ReceivedRtpPacket()) {
    return absl::nullopt;
  }
  int64_t expected_packets =  // 期望接收总包数
      1 + received_seq_max_ - received_seq_first_;
  if (expected_packets <= 0) {
    return absl::nullopt;
  }
  if (cumulative_loss_ <= 0) {  // 总丢包数
    return 0;
  }
  // 百分比丢包率
  return 100 * static_cast<int64_t>(cumulative_loss_) / expected_packets;
}

StreamDataCounters StreamStatisticianImpl::GetReceiveStreamDataCounters()
    const {
  return receive_counters_;
}

uint32_t StreamStatisticianImpl::BitrateReceived() const {
  return incoming_bitrate_.Rate(clock_->TimeInMilliseconds()).value_or(0);
}

bool StreamStatisticianImpl::IsRetransmitOfOldPacket(
    const RtpPacketReceived& packet,
    int64_t now_ms) const {
  // 负载类型的频率
  uint32_t frequency_khz = packet.payload_type_frequency() / 1000;
  RTC_DCHECK_GT(frequency_khz, 0);

  // 现实时间间隔
  int64_t time_diff_ms = now_ms - last_receive_time_ms_;

  // Diff in time stamp since last received in order.
  uint32_t timestamp_diff =  // 包时间戳间隔
      packet.Timestamp() - last_received_timestamp_;
  uint32_t rtp_time_stamp_diff_ms =  // 现实时间间隔
      timestamp_diff / frequency_khz;

  int64_t max_delay_ms = 0;

  // Jitter standard deviation in samples.
  float jitter_std = std::sqrt(static_cast<float>(jitter_q4_ >> 4));

  // 2 times the standard deviation => 95% confidence.
  // And transform to milliseconds by dividing by the frequency in kHz.
  max_delay_ms = static_cast<int64_t>((2 * jitter_std) / frequency_khz);

  // Min max_delay_ms is 1.
  if (max_delay_ms == 0) {
    max_delay_ms = 1;
  }

  // 根据时间计算是否重传的包
  return time_diff_ms > rtp_time_stamp_diff_ms + max_delay_ms;
}

std::unique_ptr<ReceiveStatistics> ReceiveStatistics::Create(Clock* clock) {
  return std::make_unique<ReceiveStatisticsLocked>(
      clock, [](uint32_t ssrc, Clock* clock, int max_reordering_threshold) {
        return std::make_unique<StreamStatisticianLocked>(
            ssrc, clock, max_reordering_threshold);
      });
}

std::unique_ptr<ReceiveStatistics> ReceiveStatistics::CreateThreadCompatible(
    Clock* clock) {
  return std::make_unique<ReceiveStatisticsImpl>(
      clock, [](uint32_t ssrc, Clock* clock, int max_reordering_threshold) {
        return std::make_unique<StreamStatisticianImpl>(
            ssrc, clock, max_reordering_threshold);
      });
}

ReceiveStatisticsImpl::ReceiveStatisticsImpl(
    Clock* clock,
    std::function<std::unique_ptr<StreamStatisticianImplInterface>(
        uint32_t ssrc,
        Clock* clock,
        int max_reordering_threshold)> stream_statistician_factory)
    : clock_(clock),
      stream_statistician_factory_(std::move(stream_statistician_factory)),
      last_returned_ssrc_(0),
      max_reordering_threshold_(kDefaultMaxReorderingThreshold) {}

void ReceiveStatisticsImpl::OnRtpPacket(const RtpPacketReceived& packet) {
  // StreamStatisticianImpl instance is created once and only destroyed when
  // this whole ReceiveStatisticsImpl is destroyed. StreamStatisticianImpl has
  // it's own locking so don't hold receive_statistics_lock_ (potential
  // deadlock).
  GetOrCreateStatistician(packet.Ssrc())->UpdateCounters(packet);
}

StreamStatistician* ReceiveStatisticsImpl::GetStatistician(
    uint32_t ssrc) const {
  const auto& it = statisticians_.find(ssrc);
  if (it == statisticians_.end())
    return nullptr;
  return it->second.get();
}

StreamStatisticianImplInterface* ReceiveStatisticsImpl::GetOrCreateStatistician(
    uint32_t ssrc) {
  std::unique_ptr<StreamStatisticianImplInterface>& impl = statisticians_[ssrc];
  if (impl == nullptr) {  // new element
    impl =
        stream_statistician_factory_(ssrc, clock_, max_reordering_threshold_);
  }
  return impl.get();
}

void ReceiveStatisticsImpl::SetMaxReorderingThreshold(
    int max_reordering_threshold) {
  max_reordering_threshold_ = max_reordering_threshold;
  for (auto& statistician : statisticians_) {
    statistician.second->SetMaxReorderingThreshold(max_reordering_threshold);
  }
}

void ReceiveStatisticsImpl::SetMaxReorderingThreshold(
    uint32_t ssrc,
    int max_reordering_threshold) {
  GetOrCreateStatistician(ssrc)->SetMaxReorderingThreshold(
      max_reordering_threshold);
}

void ReceiveStatisticsImpl::EnableRetransmitDetection(uint32_t ssrc,
                                                      bool enable) {
  GetOrCreateStatistician(ssrc)->EnableRetransmitDetection(enable);
}

std::vector<rtcp::ReportBlock> ReceiveStatisticsImpl::RtcpReportBlocks(
    size_t max_blocks) {
  std::vector<rtcp::ReportBlock> result;
  result.reserve(
      std::min(max_blocks, statisticians_.size()));  // 一个ssrc一个记录

  auto add_report_block = [&result](
                              uint32_t media_ssrc,
                              StreamStatisticianImplInterface* statistician) {
    // Do we have receive statistics to send?
    RtcpStatistics stats;
    if (!statistician->GetActiveStatisticsAndReset(&stats))  // 获取统计记录
      return;
    result.emplace_back();
    rtcp::ReportBlock& block = result.back();
    block.SetMediaSsrc(media_ssrc);
    block.SetFractionLost(stats.fraction_lost);
    if (!block.SetCumulativeLost(stats.packets_lost)) {  // 丢包数字段溢出
      RTC_LOG(LS_WARNING) << "Cumulative lost is oversized.";
      result.pop_back();
      return;
    }
    block.SetExtHighestSeqNum(stats.extended_highest_sequence_number);
    block.SetJitter(stats.jitter);
  };

  const auto start_it = statisticians_.upper_bound(
      last_returned_ssrc_);  // 防止总记录数超过max_blocks的情况，记录上一次的最大记录位置，下一次生成记录时从该位置后开始
  for (auto it = start_it;
       result.size() < max_blocks && it != statisticians_.end(); ++it)
    add_report_block(it->first, it->second.get());
  for (auto it = statisticians_.begin();
       result.size() < max_blocks && it != start_it; ++it)
    add_report_block(it->first, it->second.get());

  if (!result.empty())
    last_returned_ssrc_ = result.back().source_ssrc();
  return result;
}

}  // namespace webrtc
