/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/pacing/bitrate_prober.h"

#include <algorithm>

#include "absl/memory/memory.h"
#include "api/rtc_event_log/rtc_event.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "logging/rtc_event_log/events/rtc_event_probe_cluster_created.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {

namespace {
// The min probe packet size is scaled with the bitrate we're probing at.
// This defines the max min probe packet size, meaning that on high bitrates
// we have a min probe packet size of 200 bytes.
constexpr size_t kMinProbePacketSize = 200;

constexpr TimeDelta kProbeClusterTimeout = TimeDelta::Seconds(5);

}  // namespace

BitrateProberConfig::BitrateProberConfig(
    const WebRtcKeyValueConfig* key_value_config)
    : min_probe_packets_sent("min_probe_packets_sent", 5),
      min_probe_delta("min_probe_delta", TimeDelta::Millis(1)),
      min_probe_duration("min_probe_duration", TimeDelta::Millis(15)),
      max_probe_delay("max_probe_delay", TimeDelta::Millis(3)) {
  ParseFieldTrial({&min_probe_packets_sent, &min_probe_delta,
                   &min_probe_duration, &max_probe_delay},
                  key_value_config->Lookup("WebRTC-Bwe-ProbingConfiguration"));
  ParseFieldTrial({&min_probe_packets_sent, &min_probe_delta,
                   &min_probe_duration, &max_probe_delay},
                  key_value_config->Lookup("WebRTC-Bwe-ProbingBehavior"));
}

BitrateProber::~BitrateProber() {
  RTC_HISTOGRAM_COUNTS_1000("WebRTC.BWE.Probing.TotalProbeClustersRequested",
                            total_probe_count_);
  RTC_HISTOGRAM_COUNTS_1000("WebRTC.BWE.Probing.TotalFailedProbeClusters",
                            total_failed_probe_count_);
}

BitrateProber::BitrateProber(const WebRtcKeyValueConfig& field_trials)
    : probing_state_(ProbingState::kDisabled),
      next_probe_time_(Timestamp::PlusInfinity()),
      total_probe_count_(0),
      total_failed_probe_count_(0),
      config_(&field_trials) {
  SetEnabled(true);
}

void BitrateProber::SetEnabled(bool enable) {
  if (enable) {
    if (probing_state_ == ProbingState::kDisabled) {
      probing_state_ = ProbingState::kInactive;
      RTC_LOG(LS_INFO) << "Bandwidth probing enabled, set to inactive";
    }
  } else {
    probing_state_ = ProbingState::kDisabled;
    RTC_LOG(LS_INFO) << "Bandwidth probing disabled";
  }
}

void BitrateProber::OnIncomingPacket(size_t packet_size) {
  // Don't initialize probing unless we have something large enough to start
  // probing.
  if (probing_state_ == ProbingState::kInactive && !clusters_.empty() &&
      packet_size >=
          std::min<size_t>(RecommendedMinProbeSize(), kMinProbePacketSize)) {
    // Send next probe right away.
    next_probe_time_ = Timestamp::MinusInfinity();
    probing_state_ = ProbingState::kActive;
  }
}

void BitrateProber::CreateProbeCluster(DataRate bitrate,
                                       Timestamp now,
                                       int cluster_id) {  // 创建探测簇
  RTC_DCHECK(probing_state_ != ProbingState::kDisabled);
  RTC_DCHECK_GT(bitrate, DataRate::Zero());

  total_probe_count_++;
  while (!clusters_.empty() &&
         now - clusters_.front().created_at > kProbeClusterTimeout) {
    clusters_.pop();  // 删除过期的探测簇
    total_failed_probe_count_++;
  }

  ProbeCluster cluster;
  cluster.created_at = now;  // 探测簇创建时间
  cluster.pace_info.probe_cluster_min_probes =
      config_.min_probe_packets_sent;          // 最小探测包数
  cluster.pace_info.probe_cluster_min_bytes =  // 最小探测字节数
      (bitrate * config_.min_probe_duration.Get())
          .bytes();  // 根据目标码率和最小探测持续时间计算
  RTC_DCHECK_GE(cluster.pace_info.probe_cluster_min_bytes, 0);
  cluster.pace_info.send_bitrate_bps = bitrate.bps();  // 发送码率
  cluster.pace_info.probe_cluster_id = cluster_id;     // 探测簇id
  clusters_.push(cluster);                             // 添加探测簇

  RTC_LOG(LS_INFO) << "Probe cluster (bitrate:min bytes:min packets): ("
                   << cluster.pace_info.send_bitrate_bps << ":"
                   << cluster.pace_info.probe_cluster_min_bytes << ":"
                   << cluster.pace_info.probe_cluster_min_probes << ")";
  // If we are already probing, continue to do so. Otherwise set it to
  // kInactive and wait for OnIncomingPacket to start the probing.
  if (probing_state_ != ProbingState::kActive)  // 非探测中
    probing_state_ = ProbingState::kInactive;   // 设置为待探测状态
}

Timestamp BitrateProber::NextProbeTime(Timestamp now) const {
  // Probing is not active or probing is already complete.
  if (probing_state_ != ProbingState::kActive || clusters_.empty()) {
    return Timestamp::PlusInfinity();
  }

  if (next_probe_time_.IsFinite() &&
      now - next_probe_time_ > config_.max_probe_delay.Get()) {
    RTC_DLOG(LS_WARNING) << "Probe delay too high"
                            " (next_ms:"
                         << next_probe_time_.ms() << ", now_ms: " << now.ms()
                         << ")";
    return Timestamp::PlusInfinity();
  }

  return next_probe_time_;
}

PacedPacketInfo BitrateProber::CurrentCluster() const {  // 获取当前探测簇
  RTC_DCHECK(!clusters_.empty());
  RTC_DCHECK(probing_state_ == ProbingState::kActive);
  PacedPacketInfo info = clusters_.front().pace_info;
  info.probe_cluster_bytes_sent = clusters_.front().sent_bytes;
  return info;
}

// Probe size is recommended based on the probe bitrate required. We choose
// a minimum of twice |kMinProbeDeltaMs| interval to allow scheduling to be
// feasible.
size_t BitrateProber::RecommendedMinProbeSize() const {  // 获取最小探测码率
  RTC_DCHECK(!clusters_.empty());
  return clusters_.front().pace_info.send_bitrate_bps *
         2 *  // 2倍最小探测间隔的数据量平衡延迟
         config_.min_probe_delta->ms() / (8 * 1000);
}

void BitrateProber::ProbeSent(Timestamp now,
                              size_t bytes) {  // 更新探测已发送码率
  RTC_DCHECK(probing_state_ == ProbingState::kActive);
  RTC_DCHECK_GT(bytes, 0);

  if (!clusters_.empty()) {
    ProbeCluster* cluster = &clusters_.front();
    if (cluster->sent_probes == 0) {  // 当前探测簇未发送数据
      RTC_DCHECK(cluster->started_at.IsInfinite());
      cluster->started_at = now;  // 设置探测簇开始时间
    }
    cluster->sent_bytes += static_cast<int>(bytes);  // 增加已发送数据量
    cluster->sent_probes += 1;                       // 增加已发送探测数
    next_probe_time_ = CalculateNextProbeTime(*cluster);  // 下一个探测时间
    if (cluster->sent_bytes >=
            cluster->pace_info
                .probe_cluster_min_bytes &&  // 已发送探测字节数超过探测簇最小字节数
        cluster->sent_probes >=
            cluster->pace_info
                .probe_cluster_min_probes) {  // 已发送探测数超过探测簇最小探测数
      RTC_HISTOGRAM_COUNTS_100000("WebRTC.BWE.Probing.ProbeClusterSizeInBytes",
                                  cluster->sent_bytes);
      RTC_HISTOGRAM_COUNTS_100("WebRTC.BWE.Probing.ProbesPerCluster",
                               cluster->sent_probes);
      RTC_HISTOGRAM_COUNTS_10000("WebRTC.BWE.Probing.TimePerProbeCluster",
                                 (now - cluster->started_at).ms());

      clusters_.pop();  // 移除当前探测簇
    }
    if (clusters_.empty())                        // 没有探测簇
      probing_state_ = ProbingState::kSuspended;  // 暂停探测状态
  }
}

Timestamp BitrateProber::CalculateNextProbeTime(  // 获取下一个探测时间
    const ProbeCluster& cluster) const {
  RTC_CHECK_GT(cluster.pace_info.send_bitrate_bps, 0);
  RTC_CHECK(cluster.started_at.IsFinite());

  // Compute the time delta from the cluster start to ensure probe bitrate stays
  // close to the target bitrate. Result is in milliseconds.
  DataSize sent_bytes = DataSize::Bytes(cluster.sent_bytes);  // 已发送数据量
  DataRate send_bitrate =                                     // 发送码率
      DataRate::BitsPerSec(cluster.pace_info.send_bitrate_bps);
  TimeDelta delta =
      sent_bytes / send_bitrate;  // 已发送数据在发送码率下需要的时间
  return cluster.started_at + delta;
}

}  // namespace webrtc
