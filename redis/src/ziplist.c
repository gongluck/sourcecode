/* The ziplist is a specially encoded dually linked list that is designed
 * to be very memory efficient. It stores both strings and integer values,
 * where integers are encoded as actual integers instead of a series of
 * characters. It allows push and pop operations on either side of the list
 * in O(1) time. However, because every operation requires a reallocation of
 * the memory used by the ziplist, the actual complexity is related to the
 * amount of memory used by the ziplist.
 *
 * ----------------------------------------------------------------------------
 *
 * ZIPLIST OVERALL LAYOUT:
 * The general layout of the ziplist is as follows:
 * <zlbytes><zltail><zllen><entry><entry><zlend>
 *
 * <zlbytes> is an unsigned integer to hold the number of bytes that the
 * ziplist occupies. This value needs to be stored to be able to resize the
 * entire structure without the need to traverse it first.
 *
 * <zltail> is the offset to the last entry in the list. This allows a pop
 * operation on the far side of the list without the need for full traversal.
 *
 * <zllen> is the number of entries.When this value is larger than 2**16-2,
 * we need to traverse the entire list to know how many items it holds.
 *
 * <zlend> is a single byte special value, equal to 255, which indicates the
 * end of the list.
 *
 * ZIPLIST ENTRIES:
 * Every entry in the ziplist is prefixed by a header that contains two pieces
 * of information. First, the length of the previous entry is stored to be
 * able to traverse the list from back to front. Second, the encoding with an
 * optional string length of the entry itself is stored.
 *
 * The length of the previous entry is encoded in the following way:
 * If this length is smaller than 254 bytes, it will only consume a single
 * byte that takes the length as value. When the length is greater than or
 * equal to 254, it will consume 5 bytes. The first byte is set to 254 to
 * indicate a larger value is following. The remaining 4 bytes take the
 * length of the previous entry as value.
 *
 * The other header field of the entry itself depends on the contents of the
 * entry. When the entry is a string, the first 2 bits of this header will hold
 * the type of encoding used to store the length of the string, followed by the
 * actual length of the string. When the entry is an integer the first 2 bits
 * are both set to 1. The following 2 bits are used to specify what kind of
 * integer will be stored after this header. An overview of the different
 * types and encodings is as follows:
 *
 * |00pppppp| - 1 byte
 *      String value with length less than or equal to 63 bytes (6 bits).
 * |01pppppp|qqqqqqqq| - 2 bytes
 *      String value with length less than or equal to 16383 bytes (14 bits).
 * |10______|qqqqqqqq|rrrrrrrr|ssssssss|tttttttt| - 5 bytes
 *      String value with length greater than or equal to 16384 bytes.
 * |11000000| - 1 byte
 *      Integer encoded as int16_t (2 bytes).
 * |11010000| - 1 byte
 *      Integer encoded as int32_t (4 bytes).
 * |11100000| - 1 byte
 *      Integer encoded as int64_t (8 bytes).
 * |11110000| - 1 byte
 *      Integer encoded as 24 bit signed (3 bytes).
 * |11111110| - 1 byte
 *      Integer encoded as 8 bit signed (1 byte).
 * |1111xxxx| - (with xxxx between 0000 and 1101) immediate 4 bit integer.
 *      Unsigned integer from 0 to 12. The encoded value is actually from
 *      1 to 13 because 0000 and 1111 can not be used, so 1 should be
 *      subtracted from the encoded 4 bit value to obtain the right value.
 * |11111111| - End of ziplist.
 *
 * All the integers are represented in little endian byte order.
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include "redisassert.h"

#define ZIP_END 255    //压缩链表结束标记
#define ZIP_BIGLEN 254 // 1字节表示节点长度的最大值

/* Different encoding/length possibilities */
#define ZIP_STR_MASK 0xc0 // 1100 0000
#define ZIP_INT_MASK 0x30 // 0011 0000
// 00xx xxxx
// 01xx xxxx
// 10xx xxxx
//以上都为字符数组
#define ZIP_STR_06B (0 << 6) // 0000 0000
#define ZIP_STR_14B (1 << 6) // 0100 0000
#define ZIP_STR_32B (2 << 6) // 1000 0000
// 11xx xxxx才是整数
#define ZIP_INT_16B (0xc0 | 0 << 4) // 1100 0000
#define ZIP_INT_32B (0xc0 | 1 << 4) // 1101 0000
#define ZIP_INT_64B (0xc0 | 2 << 4) // 1110 0000
#define ZIP_INT_24B (0xc0 | 3 << 4) // 1111 0000
#define ZIP_INT_8B 0xfe             // 1111 1110
/* 4 bit integer immediate encoding */
#define ZIP_INT_IMM_MASK 0x0f // 0000 1111
#define ZIP_INT_IMM_MIN 0xf1  /* 11110001 */
#define ZIP_INT_IMM_MAX 0xfd  /* 11111101 */
#define ZIP_INT_IMM_VAL(v) (v & ZIP_INT_IMM_MASK)

#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24_MAX - 1)

//判断是否字节数组编码
//非11xx xxxx(小于1100 0000)即为字符数组
/* Macro to determine type */
#define ZIP_IS_STR(enc) (((enc)&ZIP_STR_MASK) < ZIP_STR_MASK)

/* Utility macros */
#define ZIPLIST_BYTES(zl) (*((uint32_t *)(zl)))                               //整个压缩列表的字节数 4字节
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t *)((zl) + sizeof(uint32_t))))    //结束标记地址偏移
#define ZIPLIST_LENGTH(zl) (*((uint16_t *)((zl) + sizeof(uint32_t) * 2)))     //压缩列表长度 2字节 可能会溢出
#define ZIPLIST_HEADER_SIZE (sizeof(uint32_t) * 2 + sizeof(uint16_t))         //压缩链表头所占长度 等于以上3个字段所占总和
#define ZIPLIST_ENTRY_HEAD(zl) ((zl) + ZIPLIST_HEADER_SIZE)                   //头节点
#define ZIPLIST_ENTRY_TAIL(zl) ((zl) + intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))) //结束标记
#define ZIPLIST_ENTRY_END(zl) ((zl) + intrev32ifbe(ZIPLIST_BYTES(zl)) - 1)    //尾节点

//增加压缩列表长度
/* We know a positive increment can only be 1 because entries can only be
 * pushed one at a time. */
#define ZIPLIST_INCR_LENGTH(zl, incr)                                             \
  {                                                                               \
    if (ZIPLIST_LENGTH(zl) < UINT16_MAX) /*长度未溢出的*/                   \
      ZIPLIST_LENGTH(zl) = intrev16ifbe(intrev16ifbe(ZIPLIST_LENGTH(zl)) + incr); \
  }

//压缩列表节点
//实际内存布局 prevlength + encoding + content
typedef struct zlentry
{
  unsigned int prevrawlensize /*prevrawlen字节大小*/, prevrawlen /*前置节点长度*/;
  unsigned int lensize /*len字节大小*/, len /*节点长度*/;
  unsigned int headersize; //节点头大小 等于 prevrawlensize + lensize
  unsigned char encoding;  //节点编码类型
  unsigned char *p;        //指向节点数据
} zlentry;

//取出节点编码类型
/* Extract the encoding from the byte pointed by 'ptr' and set it into
 * 'encoding'. */
#define ZIP_ENTRY_ENCODING(ptr, encoding)           \
  do                                                \
  {                                                 \
    (encoding) = (ptr[0]);                          \
    if ((encoding) < ZIP_STR_MASK) /*字符数组*/ \
      (encoding) &= ZIP_STR_MASK;                   \
  } while (0)

//返回编码类型所需字节数
/* Return bytes needed to store integer encoded by 'encoding' */
static unsigned int zipIntSize(unsigned char encoding)
{
  switch (encoding)
  {
  case ZIP_INT_8B:
    return 1;
  case ZIP_INT_16B:
    return 2;
  case ZIP_INT_24B:
    return 3;
  case ZIP_INT_32B:
    return 4;
  case ZIP_INT_64B:
    return 8;
  default:
    return 0; /* 4 bit immediate */
  }
  assert(NULL);
  return 0;
}

//将长度值编码并写入
/* Encode the length 'rawlen' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length. */
static unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen)
{
  unsigned char len = 1, buf[5];

  if (ZIP_IS_STR(encoding)) //字节数组
  {
    /* Although encoding is given it may not be set for strings,
     * so we determine it here using the raw length. */
    if (rawlen <= 0x3f) //小于63(0011 1111)的字节数组
    {
      if (!p)
        return len;
      buf[0] = ZIP_STR_06B | rawlen; // encoding字段
    }
    else if (rawlen <= 0x3fff) //小于16383(0011 1111 1111 1111)的字节数组
    {
      len += 1; // 2个字节存储encoding
      if (!p)
        return len;
      // encoding字段
      buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
      buf[1] = rawlen & 0xff;
    }
    else //大于16383(0011 1111 1111 1111)的字节数组
    {
      len += 4; // 5个字节存储encoding
      if (!p)
        return len;
      // encoding字段
      buf[0] = ZIP_STR_32B;
      buf[1] = (rawlen >> 24) & 0xff;
      buf[2] = (rawlen >> 16) & 0xff;
      buf[3] = (rawlen >> 8) & 0xff;
      buf[4] = rawlen & 0xff;
    }
  }
  else //整数编码
  {
    /* Implies integer encoding, so length is always 1. */
    if (!p)
      return len;
    buf[0] = encoding;
  }

  //写入编码后的长度
  /* Store this length at p */
  memcpy(p, buf, len);
  return len;
}

//解码节点长度
/* Decode the length encoded in 'ptr'. The 'encoding' variable will hold the
 * entries encoding, the 'lensize' variable will hold the number of bytes
 * required to encode the entries length, and the 'len' variable will hold the
 * entries length. */
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) \
  do                                                   \
  {                                                    \
    /*取出编码类型*/                             \
    ZIP_ENTRY_ENCODING((ptr), (encoding));             \
    /*解码长度值*/                                \
    if ((encoding) < ZIP_STR_MASK)                     \
    {                                                  \
      if ((encoding) == ZIP_STR_06B)                   \
      {                                                \
        (lensize) = 1;                                 \
        (len) = (ptr)[0] & 0x3f;                       \
      }                                                \
      else if ((encoding) == ZIP_STR_14B)              \
      {                                                \
        (lensize) = 2;                                 \
        (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];   \
      }                                                \
      else if (encoding == ZIP_STR_32B)                \
      {                                                \
        (lensize) = 5;                                 \
        (len) = ((ptr)[1] << 24) |                     \
                ((ptr)[2] << 16) |                     \
                ((ptr)[3] << 8) |                      \
                ((ptr)[4]);                            \
      }                                                \
      else                                             \
      {                                                \
        assert(NULL);                                  \
      }                                                \
    }                                                  \
    else                                               \
    {                                                  \
      (lensize) = 1;                                   \
      (len) = zipIntSize(encoding);                    \
    }                                                  \
  } while (0);

//编码节点长度并写入
/* Encode the length of the previous entry and write it to "p". Return the
 * number of bytes needed to encode this length if "p" is NULL. */
static unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len)
{
  if (p == NULL)
  {
    return (len < ZIP_BIGLEN) ? 1 : sizeof(len) + 1;
  }
  else
  {
    if (len < ZIP_BIGLEN)
    {
      p[0] = len;
      return 1;
    }
    else
    {
      // p[0]使用ZIP_BIGLEN标记
      p[0] = ZIP_BIGLEN;
      // p[1-4]填入长度
      memcpy(p + 1, &len, sizeof(len));
      memrev32ifbe(p + 1);
      return 1 + sizeof(len); // 5
    }
  }
}

//按4+1字节编码节点长度并写入
/* Encode the length of the previous entry and write it to "p". This only
 * uses the larger encoding (required in __ziplistCascadeUpdate). */
static void zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len)
{
  if (p == NULL)
    return;
  p[0] = ZIP_BIGLEN;
  memcpy(p + 1, &len, sizeof(len));
  memrev32ifbe(p + 1);
}

//解析前节点大小字段所占字节
/* Decode the number of bytes required to store the length of the previous
 * element, from the perspective of the entry pointed to by 'ptr'. */
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) \
  do                                             \
  {                                              \
    if ((ptr)[0] < ZIP_BIGLEN)                   \
    {                                            \
      (prevlensize) = 1;                         \
    }                                            \
    else                                         \
    {                                            \
      (prevlensize) = 5;                         \
    }                                            \
  } while (0);

//解码前节点长度
/* Decode the length of the previous element, from the perspective of the entry
 * pointed to by 'ptr'. */
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) \
  do                                                  \
  {                                                   \
    ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);         \
    if ((prevlensize) == 1)                           \
    {                                                 \
      (prevlen) = (ptr)[0];                           \
    }                                                 \
    else if ((prevlensize) == 5)                      \
    {                                                 \
      assert(sizeof((prevlensize)) == 4);             \
      memcpy(&(prevlen), ((char *)(ptr)) + 1, 4);     \
      memrev32ifbe(&prevlen);                         \
    }                                                 \
  } while (0);

//返回编码len所需的长度减去编码p的前一个节点的大小所需的长度之差
/* Return the difference in number of bytes needed to store the length of the
 * previous element 'len', in the entry pointed to by 'p'. */
static int zipPrevLenByteDiff(unsigned char *p, unsigned int len)
{
  unsigned int prevlensize;
  //计算前节点编码长度所需字节数
  ZIP_DECODE_PREVLENSIZE(p, prevlensize);
  return zipPrevEncodeLength(NULL, len) - prevlensize;
}

//返回节点所需内存大小
/* Return the total number of bytes used by the entry pointed to by 'p'. */
static unsigned int zipRawEntryLength(unsigned char *p)
{
  unsigned int prevlensize, encoding, lensize, len;
  //解析prevlensize
  ZIP_DECODE_PREVLENSIZE(p, prevlensize);
  //解析encoding lensize len
  ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
  return prevlensize + lensize + len;
}

//尝试将节点数据转换成整数
/* Check if string pointed to by 'entry' can be encoded as an integer.
 * Stores the integer value in 'v' and its encoding in 'encoding'. */
static int zipTryEncoding(unsigned char *entry, unsigned int entrylen, long long *v, unsigned char *encoding)
{
  long long value;

  if (entrylen >= 32 || entrylen == 0)
    return 0;
  if (string2ll((char *)entry, entrylen, &value)) //尝试转换
  {
    /* Great, the string can be encoded. Check what's the smallest
     * of our encoding types that can hold this value. */
    if (value >= 0 && value <= 12)
    {
      *encoding = ZIP_INT_IMM_MIN + value;
    }
    else if (value >= INT8_MIN && value <= INT8_MAX)
    {
      *encoding = ZIP_INT_8B;
    }
    else if (value >= INT16_MIN && value <= INT16_MAX)
    {
      *encoding = ZIP_INT_16B;
    }
    else if (value >= INT24_MIN && value <= INT24_MAX)
    {
      *encoding = ZIP_INT_24B;
    }
    else if (value >= INT32_MIN && value <= INT32_MAX)
    {
      *encoding = ZIP_INT_32B;
    }
    else
    {
      *encoding = ZIP_INT_64B;
    }
    *v = value;
    return 1;
  }
  return 0;
}

//按编码保存整数
/* Store integer 'value' at 'p', encoded as 'encoding' */
static void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding)
{
  int16_t i16;
  int32_t i32;
  int64_t i64;
  if (encoding == ZIP_INT_8B)
  {
    ((int8_t *)p)[0] = (int8_t)value;
  }
  else if (encoding == ZIP_INT_16B)
  {
    i16 = value;
    memcpy(p, &i16, sizeof(i16));
    memrev16ifbe(p);
  }
  else if (encoding == ZIP_INT_24B)
  {
    i32 = value << 8;
    memrev32ifbe(&i32);
    memcpy(p, ((uint8_t *)&i32) + 1, sizeof(i32) - sizeof(uint8_t));
  }
  else if (encoding == ZIP_INT_32B)
  {
    i32 = value;
    memcpy(p, &i32, sizeof(i32));
    memrev32ifbe(p);
  }
  else if (encoding == ZIP_INT_64B)
  {
    i64 = value;
    memcpy(p, &i64, sizeof(i64));
    memrev64ifbe(p);
  }
  // 值和编码保存在同一个字节
  else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX)
  {
    /* Nothing to do, the value is stored in the encoding itself. */
  }
  else
  {
    assert(NULL);
  }
}

//按编码格式读取值
/* Read integer encoded as 'encoding' from 'p' */
static int64_t zipLoadInteger(unsigned char *p, unsigned char encoding)
{
  int16_t i16;
  int32_t i32;
  int64_t i64, ret = 0;
  if (encoding == ZIP_INT_8B)
  {
    ret = ((int8_t *)p)[0];
  }
  else if (encoding == ZIP_INT_16B)
  {
    memcpy(&i16, p, sizeof(i16));
    memrev16ifbe(&i16);
    ret = i16;
  }
  else if (encoding == ZIP_INT_32B)
  {
    memcpy(&i32, p, sizeof(i32));
    memrev32ifbe(&i32);
    ret = i32;
  }
  else if (encoding == ZIP_INT_24B)
  {
    i32 = 0;
    memcpy(((uint8_t *)&i32) + 1, p, sizeof(i32) - sizeof(uint8_t));
    memrev32ifbe(&i32);
    ret = i32 >> 8;
  }
  else if (encoding == ZIP_INT_64B)
  {
    memcpy(&i64, p, sizeof(i64));
    memrev64ifbe(&i64);
    ret = i64;
  }
  // 值和编码保存在同一个 byte
  else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX)
  {
    ret = (encoding & ZIP_INT_IMM_MASK) - 1;
  }
  else
  {
    assert(NULL);
  }
  return ret;
}

//解析节点
/* Return a struct with all information about an entry. */
static zlentry zipEntry(unsigned char *p)
{
  zlentry e;
  //解析prevlensize prevlen
  ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);
  //解析encoding lensize len
  ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);
  // headersize = prevlensize + lensize
  e.headersize = e.prevrawlensize + e.lensize;
  e.p = p;
  return e;
}

//创建空压缩链表
/* Create a new empty ziplist. */
unsigned char *ziplistNew(void)
{
  //头部字段
  unsigned int bytes = ZIPLIST_HEADER_SIZE + 1; //+1尾部标记
  unsigned char *zl = zmalloc(bytes);
  //整个压缩链表所占字节数
  ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);
  //尾部结束标记位置
  ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);
  //节点数
  ZIPLIST_LENGTH(zl) = 0;
  //尾部结束标记
  zl[bytes - 1] = ZIP_END;
  return zl;
}

//调整压缩链表大小
/* Resize the ziplist. */
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len)
{
  zl = zrealloc(zl, len);
  //整个压缩链表所占字节数
  ZIPLIST_BYTES(zl) = intrev32ifbe(len);
  //尾部结束标记
  zl[len - 1] = ZIP_END;
  return zl;
}

/* When an entry is inserted, we need to set the prevlen field of the next
 * entry to equal the length of the inserted entry. It can occur that this
 * length cannot be encoded in 1 byte and the next entry needs to be grow
 * a bit larger to hold the 5-byte encoded prevlen. This can be done for free,
 * because this only happens when an entry is already being inserted (which
 * causes a realloc and memmove). However, encoding the prevlen may require
 * that this entry is grown as well. This effect may cascade throughout
 * the ziplist when there are consecutive entries with a size close to
 * ZIP_BIGLEN, so we need to check that the prevlen can be encoded in every
 * consecutive entry.
 *
 * Note that this effect can also happen in reverse, where the bytes required
 * to encode the prevlen field can shrink. This effect is deliberately ignored,
 * because it can cause a "flapping" effect where a chain prevlen fields is
 * first grown and then shrunk again after consecutive inserts. Rather, the
 * field is allowed to stay larger than necessary, because a large prevlen
 * field implies the ziplist is holding large entries anyway.
 *
 * The pointer "p" points to the first entry that does NOT need to be
 * updated, i.e. consecutive fields MAY need an update. */
static unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p)
{
  size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)) /*整个压缩链表所占字节数*/, rawlen, rawlensize;
  size_t offset, noffset, extra;
  unsigned char *np;
  zlentry cur, next;

  while (p[0] != ZIP_END)
  {
    cur = zipEntry(p);
    //节点内存占用
    rawlen = cur.headersize + cur.len;
    //计算编码当前节点长度所需字节数
    rawlensize = zipPrevEncodeLength(NULL, rawlen);

    /* Abort if there is no next entry. */
    if (p[rawlen] == ZIP_END) //没有后继节点
      break;
    //后继节点
    next = zipEntry(p + rawlen);

    //编码长度的字节数没有变化 更不会连锁更新 所以退出循环
    /* Abort when "prevlen" has not changed. */
    if (next.prevrawlen == rawlen)
      break;

    if (next.prevrawlensize < rawlensize) //需要增加字节数
    {
      /* The "prevlen" field of "next" needs more bytes to hold
       * the raw length of "cur". */
      offset = p - zl; //保存p到压缩链表的偏移
      //扩展字节数
      extra = rawlensize - next.prevrawlensize;
      // resize
      zl = ziplistResize(zl, curlen + extra);
      //更新p的地址
      p = zl + offset;

      /* Current pointer and offset for next element. */
      np = p + rawlen; // next节点
      noffset = np - zl;

      //更新结束标记偏移
      /* Update tail offset when next element is not the tail element. */
      if ((zl + intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))) != np) //未溢出16位
      {
        ZIPLIST_TAIL_OFFSET(zl) =
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + extra);
      }

      //移动后面的元素
      /* Move the tail to the back. */
      //拷贝next节点的prevrawlen后的数据,next节点的prevrawlen之后再更新
      memmove(np + rawlensize,          //到新next的prevlensize后
              np + next.prevrawlensize, //从旧next的prevlensize后开始
              curlen /*原总长*/ - noffset /*next节点前所占*/ - next.prevrawlensize /*next的prevlensize所占*/ - 1 /*结束标记所占*/);
      //写入编码长度
      zipPrevEncodeLength(np, rawlen);

      /* Advance the cursor */
      p += rawlen;
      curlen += extra;
    }
    else
    {
      if (next.prevrawlensize > rawlensize) //可以减少字节数
      {
        //但是没有做缩容 强制按大长度写
        /* This would result in shrinking, which we want to avoid.
         * So, set "rawlen" in the available bytes. */
        zipPrevEncodeLengthForceLarge(p + rawlen, rawlen);
      }
      else
      {
        zipPrevEncodeLength(p + rawlen, rawlen);
      }

      //编码长度的字节数不再触发扩容 退出循环
      /* Stop here, as the raw length of "next" has not changed. */
      break;
    }
  }
  return zl;
}

//删除节点
/* Delete "num" entries, starting at "p". Returns pointer to the ziplist. */
static unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num)
{
  unsigned int i, totlen, deleted = 0;
  size_t offset;
  int nextdiff = 0;
  zlentry first, tail;

  //遍历要删除的节点
  first = zipEntry(p);
  for (i = 0; p[0] != ZIP_END && i < num; i++)
  {
    p += zipRawEntryLength(p);
    deleted++;
  }
  //要删除的总长度
  totlen = p - first.p;
  if (totlen > 0)
  {
    if (p[0] != ZIP_END)
    {
      // 更新最后一个被删除的节点之后的一个节点
      // 将它的prevlan值设置为 first.prevrawlen
      // 也即是被删除的第一个节点的前一个节点的长度
      /* Storing `prevrawlen` in this entry may increase or decrease the
       * number of bytes required compare to the current `prevrawlen`.
       * There always is room to store this, because it was previously
       * stored by an entry that is now being deleted. */
      nextdiff = zipPrevLenByteDiff(p, first.prevrawlen);
      p -= nextdiff; //少删除nextdiff字节，用来更新prevrawlen
      zipPrevEncodeLength(p, first.prevrawlen);

      //更新结束标记
      /* Update offset for tail */
      ZIPLIST_TAIL_OFFSET(zl) =
          intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) - totlen);

      /* When the tail contains more than one entry, we need to take
       * "nextdiff" in account as well. Otherwise, a change in the
       * size of prevlen doesn't have an effect on the *tail* offset. */
      tail = zipEntry(p);                           //删除区段后的节点
      if (p[tail.headersize + tail.len] != ZIP_END) //不是最后一个节点
      {
        ZIPLIST_TAIL_OFFSET(zl) =
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + nextdiff);
      }

      //移动元素
      /* Move tail to the front of the ziplist */
      memmove(first.p, p,
              intrev32ifbe(ZIPLIST_BYTES(zl)) - (p - zl) - 1);
    }
    else
    {
      //后部所有元素都需要删除
      /* The entire tail was deleted. No need to move memory. */
      ZIPLIST_TAIL_OFFSET(zl) =
          intrev32ifbe((first.p - zl) - first.prevrawlen);
    }

    //调整大小并更新字段
    /* Resize and update length */
    offset = first.p - zl;
    zl = ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl)) - totlen + nextdiff);
    ZIPLIST_INCR_LENGTH(zl, -deleted);
    p = zl + offset;

    //检查调整链式更新prevlensize
    /* When nextdiff != 0, the raw length of the next entry has changed, so
     * we need to cascade the update throughout the ziplist */
    if (nextdiff != 0)
      zl = __ziplistCascadeUpdate(zl, p);
  }
  return zl;
}

//插入元素
/* Insert item at "p". */
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen)
{
  size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)) /*压缩链表总字节数*/, reqlen;
  unsigned int prevlensize, prevlen = 0;
  size_t offset;
  int nextdiff = 0;
  unsigned char encoding = 0;
  long long value = 123456789; /* initialized to avoid warning. Using a value
                                  that is easy to see if for some reason
                                  we use it uninitialized. */
  zlentry tail;

  /* Find out prevlen for the entry that is inserted. */
  if (p[0] != ZIP_END)
  {
    //解析前继节点长度
    ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
  }
  else // p为结束标记
  {
    //获取尾节点
    unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
    if (ptail[0] != ZIP_END)
    {
      prevlen = zipRawEntryLength(ptail);
    }
  }

  //尝试将节点数据转换成整数
  /* See if the entry can be encoded */
  if (zipTryEncoding(s, slen, &value, &encoding))
  {
    //编码类型所需字节数
    /* 'encoding' is set to the appropriate integer encoding */
    reqlen = zipIntSize(encoding);
  }
  else
  {
    /* 'encoding' is untouched, however zipEncodeLength will use the
     * string length to figure out how to encode it. */
    reqlen = slen;
  }
  /* We need space for both the length of the previous entry and
   * the length of the payload. */
  reqlen += zipPrevEncodeLength(NULL, prevlen);    //加上prevlensize所占大小
  reqlen += zipEncodeLength(NULL, encoding, slen); //加上lensize所占大小

  // 如果添加的位置不是表尾，那么必须确定后继节点的prevlen空间足以保存新节点的编码长度
  // zipPrevLenByteDiff的返回值有三种可能：
  // 1）新旧两个节点的编码长度相等，返回0
  // 2）新节点编码长度 > 旧节点编码长度，返回 5 - 1 = 4
  // 3）旧节点编码长度 > 新编码节点长度，返回 1 - 5 = -4
  /* When the insert position is not equal to the tail, we need to
   * make sure that the next entry can hold this entry's length in
   * its prevlen field. */
  nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;

  //保存偏移
  /* Store offset because a realloc may change the address of zl. */
  offset = p - zl;
  // resize
  zl = ziplistResize(zl, curlen + reqlen + nextdiff);
  //更新偏移
  p = zl + offset;

  /* Apply memory move when necessary and update tail offset. */
  if (p[0] != ZIP_END)
  {
    //右移元素
    /* Subtract one because of the ZIP_END bytes */
    memmove(p + reqlen, p - nextdiff, curlen - offset - 1 + nextdiff);

    //更新插入节点后继节点的prevlen
    /* Encode this entry's raw length in the next entry. */
    zipPrevEncodeLength(p + reqlen, reqlen);

    //更新结束标记
    /* Update offset for tail */
    ZIPLIST_TAIL_OFFSET(zl) =
        intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + reqlen);

    /* When the tail contains more than one entry, we need to take
     * "nextdiff" in account as well. Otherwise, a change in the
     * size of prevlen doesn't have an effect on the *tail* offset. */
    tail = zipEntry(p + reqlen);
    if (p[reqlen + tail.headersize + tail.len] != ZIP_END)
    {
      ZIPLIST_TAIL_OFFSET(zl) =
          intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + nextdiff);
    }
  }
  else
  {
    //尾部插入
    /* This element will be the new tail. */
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p - zl);
  }

  //处理连锁更新
  /* When nextdiff != 0, the raw length of the next entry has changed, so
   * we need to cascade the update throughout the ziplist */
  if (nextdiff != 0)
  {
    offset = p - zl;
    zl = __ziplistCascadeUpdate(zl, p + reqlen);
    p = zl + offset;
  }

  //写入节点
  /* Write the entry */
  p += zipPrevEncodeLength(p, prevlen);
  p += zipEncodeLength(p, encoding, slen);
  if (ZIP_IS_STR(encoding))
  {
    memcpy(p, s, slen);
  }
  else
  {
    zipSaveInteger(p, value, encoding);
  }
  ZIPLIST_INCR_LENGTH(zl, 1);
  return zl;
}

//表头或者表尾插入
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where)
{
  unsigned char *p;
  p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);
  return __ziplistInsert(zl, p, s, slen);
}

//查找节点
/* Returns an offset to use for iterating with ziplistNext. When the given
 * index is negative, the list is traversed back to front. When the list
 * doesn't contain an element at the provided index, NULL is returned. */
unsigned char *ziplistIndex(unsigned char *zl, int index)
{
  unsigned char *p;
  unsigned int prevlensize, prevlen = 0;
  if (index < 0)
  {
    index = (-index) - 1;
    p = ZIPLIST_ENTRY_TAIL(zl);
    if (p[0] != ZIP_END)
    {
      ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
      while (prevlen > 0 && index--)
      {
        p -= prevlen;
        ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
      }
    }
  }
  else
  {
    p = ZIPLIST_ENTRY_HEAD(zl);
    while (p[0] != ZIP_END && index--)
    {
      p += zipRawEntryLength(p);
    }
  }
  return (p[0] == ZIP_END || index > 0) ? NULL : p;
}

//返回后继节点
/* Return pointer to next entry in ziplist.
 *
 * zl is the pointer to the ziplist
 * p is the pointer to the current element
 *
 * The element after 'p' is returned, otherwise NULL if we are at the end. */
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p)
{
  ((void)zl);

  /* "p" could be equal to ZIP_END, caused by ziplistDelete,
   * and we should return NULL. Otherwise, we should return NULL
   * when the *next* element is ZIP_END (there is no next entry). */
  if (p[0] == ZIP_END)
  {
    return NULL;
  }

  p += zipRawEntryLength(p);
  if (p[0] == ZIP_END)
  {
    return NULL;
  }

  return p;
}

//返回前继节点
/* Return pointer to previous entry in ziplist. */
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p)
{
  unsigned int prevlensize, prevlen = 0;

  /* Iterating backwards from ZIP_END should return the tail. When "p" is
   * equal to the first element of the list, we're already at the head,
   * and should return NULL. */
  if (p[0] == ZIP_END)
  {
    p = ZIPLIST_ENTRY_TAIL(zl);
    return (p[0] == ZIP_END) ? NULL : p;
  }
  else if (p == ZIPLIST_ENTRY_HEAD(zl))
  {
    return NULL;
  }
  else
  {
    ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
    assert(prevlen > 0);
    return p - prevlen;
  }
}

//获取节点值
/* Get entry pointed to by 'p' and store in either '*sstr' or 'sval' depending
 * on the encoding of the entry. '*sstr' is always set to NULL to be able
 * to find out whether the string pointer or the integer value was set.
 * Return 0 if 'p' points to the end of the ziplist, 1 otherwise. */
unsigned int ziplistGet(unsigned char *p, unsigned char **sstr, unsigned int *slen, long long *sval)
{
  zlentry entry;
  if (p == NULL || p[0] == ZIP_END)
    return 0;
  if (sstr)
    *sstr = NULL;

  //解析节点
  entry = zipEntry(p);
  if (ZIP_IS_STR(entry.encoding)) //字节数组
  {
    if (sstr)
    {
      *slen = entry.len;
      *sstr = p + entry.headersize;
    }
  }
  else //整数
  {
    if (sval)
    {
      *sval = zipLoadInteger(p + entry.headersize, entry.encoding);
    }
  }
  return 1;
}

//插入节点到p后
/* Insert an entry at "p". */
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen)
{
  return __ziplistInsert(zl, p, s, slen);
}

//删除节点
/* Delete a single entry from the ziplist, pointed to by *p.
 * Also update *p in place, to be able to iterate over the
 * ziplist, while deleting entries. */
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p)
{
  size_t offset = *p - zl;
  zl = __ziplistDelete(zl, *p, 1);

  /* Store pointer to current element in p, because ziplistDelete will
   * do a realloc which might result in a different "zl"-pointer.
   * When the delete direction is back to front, we might delete the last
   * entry and end up with "p" pointing to ZIP_END, so check this. */
  *p = zl + offset;
  return zl;
}

//删除区间节点
/* Delete a range of entries from the ziplist. */
unsigned char *ziplistDeleteRange(unsigned char *zl, unsigned int index, unsigned int num)
{
  unsigned char *p = ziplistIndex(zl, index);
  return (p == NULL) ? zl : __ziplistDelete(zl, p, num);
}

//比较节点
/* Compare entry pointer to by 'p' with 'sstr' of length 'slen'. */
/* Return 1 if equal. */
unsigned int ziplistCompare(unsigned char *p, unsigned char *sstr, unsigned int slen)
{
  zlentry entry;
  unsigned char sencoding;
  long long zval, sval;
  if (p[0] == ZIP_END)
    return 0;

  entry = zipEntry(p);
  if (ZIP_IS_STR(entry.encoding))
  {
    /* Raw compare */
    if (entry.len == slen)
    {
      return memcmp(p + entry.headersize, sstr, slen) == 0;
    }
    else
    {
      return 0;
    }
  }
  else
  {
    /* Try to compare encoded values. Don't compare encoding because
     * different implementations may encoded integers differently. */
    if (zipTryEncoding(sstr, slen, &sval, &sencoding))
    {
      zval = zipLoadInteger(p + entry.headersize, entry.encoding);
      return zval == sval;
    }
  }
  return 0;
}

//查找节点
/* Find pointer to the entry equal to the specified entry. Skip 'skip' entries
 * between every comparison. Returns NULL when the field could not be found. */
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip)
{
  int skipcnt = 0;
  unsigned char vencoding = 0;
  long long vll = 0;

  while (p[0] != ZIP_END)
  {
    unsigned int prevlensize, encoding, lensize, len;
    unsigned char *q;

    //解析节点
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);
    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
    //后继节点
    q = p + prevlensize + lensize;

    if (skipcnt == 0)
    {
      //比较
      /* Compare current entry with specified entry */
      if (ZIP_IS_STR(encoding))
      {
        if (len == vlen && memcmp(q, vstr, vlen) == 0)
        {
          return p;
        }
      }
      else
      {
        /* Find out if the searched field can be encoded. Note that
         * we do it only the first time, once done vencoding is set
         * to non-zero and vll is set to the integer value. */
        if (vencoding == 0)
        {
          if (!zipTryEncoding(vstr, vlen, &vll, &vencoding))
          {
            /* If the entry can't be encoded we set it to
             * UCHAR_MAX so that we don't retry again the next
             * time. */
            vencoding = UCHAR_MAX;
          }
          /* Must be non-zero by now */
          assert(vencoding);
        }

        /* Compare current entry with specified entry, do it only
         * if vencoding != UCHAR_MAX because if there is no encoding
         * possible for the field it can't be a valid integer. */
        if (vencoding != UCHAR_MAX)
        {
          long long ll = zipLoadInteger(q, encoding);
          if (ll == vll)
          {
            return p;
          }
        }
      }

      /* Reset skip count */
      skipcnt = skip;
    }
    else
    {
      /* Skip entry */
      skipcnt--;
    }

    /* Move to next entry */
    p = q + len;
  }

  return NULL;
}

//计算压缩链表长度
/* Return length of ziplist. */
unsigned int ziplistLen(unsigned char *zl)
{
  unsigned int len = 0;
  if (intrev16ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX) //小于16位
  {
    len = intrev16ifbe(ZIPLIST_LENGTH(zl)); //保存在len字段
  }
  else //大于16位
  {
    //遍历整个压缩链表
    unsigned char *p = zl + ZIPLIST_HEADER_SIZE;
    while (*p != ZIP_END)
    {
      p += zipRawEntryLength(p);
      len++;
    }

    /* Re-store length if small enough */
    if (len < UINT16_MAX)
      ZIPLIST_LENGTH(zl) = intrev16ifbe(len);
  }
  return len;
}

//整个压缩链表的空间大小
/* Return ziplist blob size in bytes. */
size_t ziplistBlobLen(unsigned char *zl)
{
  return intrev32ifbe(ZIPLIST_BYTES(zl));
}

void ziplistRepr(unsigned char *zl)
{
  unsigned char *p;
  int index = 0;
  zlentry entry;

  printf(
      "{total bytes %d} "
      "{length %u}\n"
      "{tail offset %u}\n",
      intrev32ifbe(ZIPLIST_BYTES(zl)),
      intrev16ifbe(ZIPLIST_LENGTH(zl)),
      intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)));
  p = ZIPLIST_ENTRY_HEAD(zl);
  while (*p != ZIP_END)
  {
    entry = zipEntry(p);
    printf(
        "{"
        "addr 0x%08lx, "
        "index %2d, "
        "offset %5ld, "
        "rl: %5u, "
        "hs %2u, "
        "pl: %5u, "
        "pls: %2u, "
        "payload %5u"
        "} ",
        (long unsigned)p,
        index,
        (unsigned long)(p - zl),
        entry.headersize + entry.len,
        entry.headersize,
        entry.prevrawlen,
        entry.prevrawlensize,
        entry.len);
    p += entry.headersize;
    if (ZIP_IS_STR(entry.encoding))
    {
      if (entry.len > 40)
      {
        if (fwrite(p, 40, 1, stdout) == 0)
          perror("fwrite");
        printf("...");
      }
      else
      {
        if (entry.len &&
            fwrite(p, entry.len, 1, stdout) == 0)
          perror("fwrite");
      }
    }
    else
    {
      printf("%lld", (long long)zipLoadInteger(p, entry.encoding));
    }
    printf("\n");
    p += entry.len;
    index++;
  }
  printf("{end}\n\n");
}

#ifdef ZIPLIST_TEST_MAIN
#include <sys/time.h>
#include "adlist.h"
#include "sds.h"

#define debug(f, ...)         \
  {                           \
    if (DEBUG)                \
      printf(f, __VA_ARGS__); \
  }

unsigned char *createList()
{
  unsigned char *zl = ziplistNew();
  zl = ziplistPush(zl, (unsigned char *)"foo", 3, ZIPLIST_TAIL);
  zl = ziplistPush(zl, (unsigned char *)"quux", 4, ZIPLIST_TAIL);
  zl = ziplistPush(zl, (unsigned char *)"hello", 5, ZIPLIST_HEAD);
  zl = ziplistPush(zl, (unsigned char *)"1024", 4, ZIPLIST_TAIL);
  return zl;
}

unsigned char *createIntList()
{
  unsigned char *zl = ziplistNew();
  char buf[32];

  sprintf(buf, "100");
  zl = ziplistPush(zl, (unsigned char *)buf, strlen(buf), ZIPLIST_TAIL);
  sprintf(buf, "128000");
  zl = ziplistPush(zl, (unsigned char *)buf, strlen(buf), ZIPLIST_TAIL);
  sprintf(buf, "-100");
  zl = ziplistPush(zl, (unsigned char *)buf, strlen(buf), ZIPLIST_HEAD);
  sprintf(buf, "4294967296");
  zl = ziplistPush(zl, (unsigned char *)buf, strlen(buf), ZIPLIST_HEAD);
  sprintf(buf, "non integer");
  zl = ziplistPush(zl, (unsigned char *)buf, strlen(buf), ZIPLIST_TAIL);
  sprintf(buf, "much much longer non integer");
  zl = ziplistPush(zl, (unsigned char *)buf, strlen(buf), ZIPLIST_TAIL);
  return zl;
}

long long usec(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (((long long)tv.tv_sec) * 1000000) + tv.tv_usec;
}

void stress(int pos, int num, int maxsize, int dnum)
{
  int i, j, k;
  unsigned char *zl;
  char posstr[2][5] = {"HEAD", "TAIL"};
  long long start;
  for (i = 0; i < maxsize; i += dnum)
  {
    zl = ziplistNew();
    for (j = 0; j < i; j++)
    {
      zl = ziplistPush(zl, (unsigned char *)"quux", 4, ZIPLIST_TAIL);
    }

    /* Do num times a push+pop from pos */
    start = usec();
    for (k = 0; k < num; k++)
    {
      zl = ziplistPush(zl, (unsigned char *)"quux", 4, pos);
      zl = ziplistDeleteRange(zl, 0, 1);
    }
    printf("List size: %8d, bytes: %8d, %dx push+pop (%s): %6lld usec\n",
           i, intrev32ifbe(ZIPLIST_BYTES(zl)), num, posstr[pos], usec() - start);
    zfree(zl);
  }
}

void pop(unsigned char *zl, int where)
{
  unsigned char *p, *vstr;
  unsigned int vlen;
  long long vlong;

  p = ziplistIndex(zl, where == ZIPLIST_HEAD ? 0 : -1);
  if (ziplistGet(p, &vstr, &vlen, &vlong))
  {
    if (where == ZIPLIST_HEAD)
      printf("Pop head: ");
    else
      printf("Pop tail: ");

    if (vstr)
      if (vlen && fwrite(vstr, vlen, 1, stdout) == 0)
        perror("fwrite");
      else
        printf("%lld", vlong);

    printf("\n");
    ziplistDeleteRange(zl, -1, 1);
  }
  else
  {
    printf("ERROR: Could not pop\n");
    exit(1);
  }
}

int randstring(char *target, unsigned int min, unsigned int max)
{
  int p = 0;
  int len = min + rand() % (max - min + 1);
  int minval, maxval;
  switch (rand() % 3)
  {
  case 0:
    minval = 0;
    maxval = 255;
    break;
  case 1:
    minval = 48;
    maxval = 122;
    break;
  case 2:
    minval = 48;
    maxval = 52;
    break;
  default:
    assert(NULL);
  }

  while (p < len)
    target[p++] = minval + rand() % (maxval - minval + 1);
  return len;
}

void verify(unsigned char *zl, zlentry *e)
{
  int i;
  int len = ziplistLen(zl);
  zlentry _e;

  for (i = 0; i < len; i++)
  {
    memset(&e[i], 0, sizeof(zlentry));
    e[i] = zipEntry(ziplistIndex(zl, i));

    memset(&_e, 0, sizeof(zlentry));
    _e = zipEntry(ziplistIndex(zl, -len + i));

    assert(memcmp(&e[i], &_e, sizeof(zlentry)) == 0);
  }
}

int main(int argc, char **argv)
{
  unsigned char *zl, *p;
  unsigned char *entry;
  unsigned int elen;
  long long value;

  /* If an argument is given, use it as the random seed. */
  if (argc == 2)
    srand(atoi(argv[1]));

  zl = createIntList();
  ziplistRepr(zl);

  zl = createList();
  ziplistRepr(zl);

  pop(zl, ZIPLIST_TAIL);
  ziplistRepr(zl);

  pop(zl, ZIPLIST_HEAD);
  ziplistRepr(zl);

  pop(zl, ZIPLIST_TAIL);
  ziplistRepr(zl);

  pop(zl, ZIPLIST_TAIL);
  ziplistRepr(zl);

  printf("Get element at index 3:\n");
  {
    zl = createList();
    p = ziplistIndex(zl, 3);
    if (!ziplistGet(p, &entry, &elen, &value))
    {
      printf("ERROR: Could not access index 3\n");
      return 1;
    }
    if (entry)
    {
      if (elen && fwrite(entry, elen, 1, stdout) == 0)
        perror("fwrite");
      printf("\n");
    }
    else
    {
      printf("%lld\n", value);
    }
    printf("\n");
  }

  printf("Get element at index 4 (out of range):\n");
  {
    zl = createList();
    p = ziplistIndex(zl, 4);
    if (p == NULL)
    {
      printf("No entry\n");
    }
    else
    {
      printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p - zl);
      return 1;
    }
    printf("\n");
  }

  printf("Get element at index -1 (last element):\n");
  {
    zl = createList();
    p = ziplistIndex(zl, -1);
    if (!ziplistGet(p, &entry, &elen, &value))
    {
      printf("ERROR: Could not access index -1\n");
      return 1;
    }
    if (entry)
    {
      if (elen && fwrite(entry, elen, 1, stdout) == 0)
        perror("fwrite");
      printf("\n");
    }
    else
    {
      printf("%lld\n", value);
    }
    printf("\n");
  }

  printf("Get element at index -4 (first element):\n");
  {
    zl = createList();
    p = ziplistIndex(zl, -4);
    if (!ziplistGet(p, &entry, &elen, &value))
    {
      printf("ERROR: Could not access index -4\n");
      return 1;
    }
    if (entry)
    {
      if (elen && fwrite(entry, elen, 1, stdout) == 0)
        perror("fwrite");
      printf("\n");
    }
    else
    {
      printf("%lld\n", value);
    }
    printf("\n");
  }

  printf("Get element at index -5 (reverse out of range):\n");
  {
    zl = createList();
    p = ziplistIndex(zl, -5);
    if (p == NULL)
    {
      printf("No entry\n");
    }
    else
    {
      printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p - zl);
      return 1;
    }
    printf("\n");
  }

  printf("Iterate list from 0 to end:\n");
  {
    zl = createList();
    p = ziplistIndex(zl, 0);
    while (ziplistGet(p, &entry, &elen, &value))
    {
      printf("Entry: ");
      if (entry)
      {
        if (elen && fwrite(entry, elen, 1, stdout) == 0)
          perror("fwrite");
      }
      else
      {
        printf("%lld", value);
      }
      p = ziplistNext(zl, p);
      printf("\n");
    }
    printf("\n");
  }

  printf("Iterate list from 1 to end:\n");
  {
    zl = createList();
    p = ziplistIndex(zl, 1);
    while (ziplistGet(p, &entry, &elen, &value))
    {
      printf("Entry: ");
      if (entry)
      {
        if (elen && fwrite(entry, elen, 1, stdout) == 0)
          perror("fwrite");
      }
      else
      {
        printf("%lld", value);
      }
      p = ziplistNext(zl, p);
      printf("\n");
    }
    printf("\n");
  }

  printf("Iterate list from 2 to end:\n");
  {
    zl = createList();
    p = ziplistIndex(zl, 2);
    while (ziplistGet(p, &entry, &elen, &value))
    {
      printf("Entry: ");
      if (entry)
      {
        if (elen && fwrite(entry, elen, 1, stdout) == 0)
          perror("fwrite");
      }
      else
      {
        printf("%lld", value);
      }
      p = ziplistNext(zl, p);
      printf("\n");
    }
    printf("\n");
  }

  printf("Iterate starting out of range:\n");
  {
    zl = createList();
    p = ziplistIndex(zl, 4);
    if (!ziplistGet(p, &entry, &elen, &value))
    {
      printf("No entry\n");
    }
    else
    {
      printf("ERROR\n");
    }
    printf("\n");
  }

  printf("Iterate from back to front:\n");
  {
    zl = createList();
    p = ziplistIndex(zl, -1);
    while (ziplistGet(p, &entry, &elen, &value))
    {
      printf("Entry: ");
      if (entry)
      {
        if (elen && fwrite(entry, elen, 1, stdout) == 0)
          perror("fwrite");
      }
      else
      {
        printf("%lld", value);
      }
      p = ziplistPrev(zl, p);
      printf("\n");
    }
    printf("\n");
  }

  printf("Iterate from back to front, deleting all items:\n");
  {
    zl = createList();
    p = ziplistIndex(zl, -1);
    while (ziplistGet(p, &entry, &elen, &value))
    {
      printf("Entry: ");
      if (entry)
      {
        if (elen && fwrite(entry, elen, 1, stdout) == 0)
          perror("fwrite");
      }
      else
      {
        printf("%lld", value);
      }
      zl = ziplistDelete(zl, &p);
      p = ziplistPrev(zl, p);
      printf("\n");
    }
    printf("\n");
  }

  printf("Delete inclusive range 0,0:\n");
  {
    zl = createList();
    zl = ziplistDeleteRange(zl, 0, 1);
    ziplistRepr(zl);
  }

  printf("Delete inclusive range 0,1:\n");
  {
    zl = createList();
    zl = ziplistDeleteRange(zl, 0, 2);
    ziplistRepr(zl);
  }

  printf("Delete inclusive range 1,2:\n");
  {
    zl = createList();
    zl = ziplistDeleteRange(zl, 1, 2);
    ziplistRepr(zl);
  }

  printf("Delete with start index out of range:\n");
  {
    zl = createList();
    zl = ziplistDeleteRange(zl, 5, 1);
    ziplistRepr(zl);
  }

  printf("Delete with num overflow:\n");
  {
    zl = createList();
    zl = ziplistDeleteRange(zl, 1, 5);
    ziplistRepr(zl);
  }

  printf("Delete foo while iterating:\n");
  {
    zl = createList();
    p = ziplistIndex(zl, 0);
    while (ziplistGet(p, &entry, &elen, &value))
    {
      if (entry && strncmp("foo", (char *)entry, elen) == 0)
      {
        printf("Delete foo\n");
        zl = ziplistDelete(zl, &p);
      }
      else
      {
        printf("Entry: ");
        if (entry)
        {
          if (elen && fwrite(entry, elen, 1, stdout) == 0)
            perror("fwrite");
        }
        else
        {
          printf("%lld", value);
        }
        p = ziplistNext(zl, p);
        printf("\n");
      }
    }
    printf("\n");
    ziplistRepr(zl);
  }

  printf("Regression test for >255 byte strings:\n");
  {
    char v1[257], v2[257];
    memset(v1, 'x', 256);
    memset(v2, 'y', 256);
    zl = ziplistNew();
    zl = ziplistPush(zl, (unsigned char *)v1, strlen(v1), ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char *)v2, strlen(v2), ZIPLIST_TAIL);

    /* Pop values again and compare their value. */
    p = ziplistIndex(zl, 0);
    assert(ziplistGet(p, &entry, &elen, &value));
    assert(strncmp(v1, (char *)entry, elen) == 0);
    p = ziplistIndex(zl, 1);
    assert(ziplistGet(p, &entry, &elen, &value));
    assert(strncmp(v2, (char *)entry, elen) == 0);
    printf("SUCCESS\n\n");
  }

  printf("Regression test deleting next to last entries:\n");
  {
    char v[3][257];
    zlentry e[3];
    int i;

    for (i = 0; i < (sizeof(v) / sizeof(v[0])); i++)
    {
      memset(v[i], 'a' + i, sizeof(v[0]));
    }

    v[0][256] = '\0';
    v[1][1] = '\0';
    v[2][256] = '\0';

    zl = ziplistNew();
    for (i = 0; i < (sizeof(v) / sizeof(v[0])); i++)
    {
      zl = ziplistPush(zl, (unsigned char *)v[i], strlen(v[i]), ZIPLIST_TAIL);
    }

    verify(zl, e);

    assert(e[0].prevrawlensize == 1);
    assert(e[1].prevrawlensize == 5);
    assert(e[2].prevrawlensize == 1);

    /* Deleting entry 1 will increase `prevrawlensize` for entry 2 */
    unsigned char *p = e[1].p;
    zl = ziplistDelete(zl, &p);

    verify(zl, e);

    assert(e[0].prevrawlensize == 1);
    assert(e[1].prevrawlensize == 5);

    printf("SUCCESS\n\n");
  }

  printf("Create long list and check indices:\n");
  {
    zl = ziplistNew();
    char buf[32];
    int i, len;
    for (i = 0; i < 1000; i++)
    {
      len = sprintf(buf, "%d", i);
      zl = ziplistPush(zl, (unsigned char *)buf, len, ZIPLIST_TAIL);
    }
    for (i = 0; i < 1000; i++)
    {
      p = ziplistIndex(zl, i);
      assert(ziplistGet(p, NULL, NULL, &value));
      assert(i == value);

      p = ziplistIndex(zl, -i - 1);
      assert(ziplistGet(p, NULL, NULL, &value));
      assert(999 - i == value);
    }
    printf("SUCCESS\n\n");
  }

  printf("Compare strings with ziplist entries:\n");
  {
    zl = createList();
    p = ziplistIndex(zl, 0);
    if (!ziplistCompare(p, (unsigned char *)"hello", 5))
    {
      printf("ERROR: not \"hello\"\n");
      return 1;
    }
    if (ziplistCompare(p, (unsigned char *)"hella", 5))
    {
      printf("ERROR: \"hella\"\n");
      return 1;
    }

    p = ziplistIndex(zl, 3);
    if (!ziplistCompare(p, (unsigned char *)"1024", 4))
    {
      printf("ERROR: not \"1024\"\n");
      return 1;
    }
    if (ziplistCompare(p, (unsigned char *)"1025", 4))
    {
      printf("ERROR: \"1025\"\n");
      return 1;
    }
    printf("SUCCESS\n\n");
  }

  printf("Stress with random payloads of different encoding:\n");
  {
    int i, j, len, where;
    unsigned char *p;
    char buf[1024];
    int buflen;
    list *ref;
    listNode *refnode;

    /* Hold temp vars from ziplist */
    unsigned char *sstr;
    unsigned int slen;
    long long sval;

    for (i = 0; i < 20000; i++)
    {
      zl = ziplistNew();
      ref = listCreate();
      listSetFreeMethod(ref, sdsfree);
      len = rand() % 256;

      /* Create lists */
      for (j = 0; j < len; j++)
      {
        where = (rand() & 1) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
        if (rand() % 2)
        {
          buflen = randstring(buf, 1, sizeof(buf) - 1);
        }
        else
        {
          switch (rand() % 3)
          {
          case 0:
            buflen = sprintf(buf, "%lld", (0LL + rand()) >> 20);
            break;
          case 1:
            buflen = sprintf(buf, "%lld", (0LL + rand()));
            break;
          case 2:
            buflen = sprintf(buf, "%lld", (0LL + rand()) << 20);
            break;
          default:
            assert(NULL);
          }
        }

        /* Add to ziplist */
        zl = ziplistPush(zl, (unsigned char *)buf, buflen, where);

        /* Add to reference list */
        if (where == ZIPLIST_HEAD)
        {
          listAddNodeHead(ref, sdsnewlen(buf, buflen));
        }
        else if (where == ZIPLIST_TAIL)
        {
          listAddNodeTail(ref, sdsnewlen(buf, buflen));
        }
        else
        {
          assert(NULL);
        }
      }

      assert(listLength(ref) == ziplistLen(zl));
      for (j = 0; j < len; j++)
      {
        /* Naive way to get elements, but similar to the stresser
         * executed from the Tcl test suite. */
        p = ziplistIndex(zl, j);
        refnode = listIndex(ref, j);

        assert(ziplistGet(p, &sstr, &slen, &sval));
        if (sstr == NULL)
        {
          buflen = sprintf(buf, "%lld", sval);
        }
        else
        {
          buflen = slen;
          memcpy(buf, sstr, buflen);
          buf[buflen] = '\0';
        }
        assert(memcmp(buf, listNodeValue(refnode), buflen) == 0);
      }
      zfree(zl);
      listRelease(ref);
    }
    printf("SUCCESS\n\n");
  }

  printf("Stress with variable ziplist size:\n");
  {
    stress(ZIPLIST_HEAD, 100000, 16384, 256);
    stress(ZIPLIST_TAIL, 100000, 16384, 256);
  }

  return 0;
}

#endif
