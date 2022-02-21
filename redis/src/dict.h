/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

#define DICT_OK 0
#define DICT_ERR 1

//消除参数未使用的警告
/* Unused arguments generate annoying warnings... */
#define DICT_NOTUSED(V) ((void)V)

//字典节点
typedef struct dictEntry
{
    void *key; //键
    union
    {
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;                    //值
    struct dictEntry *next; //后继
} dictEntry;

//字典方法集合
typedef struct dictType
{
    unsigned int (*hashFunction)(const void *key);                         //哈希方法
    void *(*keyDup)(void *privdata, const void *key);                      //键复制
    void *(*valDup)(void *privdata, const void *obj);                      //值复制
    int (*keyCompare)(void *privdata, const void *key1, const void *key2); //键比较
    void (*keyDestructor)(void *privdata, void *key);                      //键销毁
    void (*valDestructor)(void *privdata, void *obj);                      //值销毁
} dictType;

//哈希表
/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */
typedef struct dictht
{
    dictEntry **table;      //指向节点数组(拉链法)
    unsigned long size;     //桶数，哈希表长度
    unsigned long sizemask; //掩码，用来计算哈希值
    unsigned long used;     //节点数
} dictht;

//字典
typedef struct dict
{
    dictType *type; //操作方法集合
    void *privdata; //私有数据
    dictht ht[2];   //两个哈希表
    // rehash进行中的ht[0].table数组索引
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */
    //迭代器
    int iterators; /* number of iterators currently running */
} dict;

//字典迭代器
/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */
typedef struct dictIterator
{
    dict *d;    //指向字典
    long index; //迭代器当前所指向的哈希表索引位置
    int table /* 正在被迭代的哈希表 */, safe /* 标识这个迭代器是否安全 */;
    dictEntry *entry /* 当前节点 */, *nextEntry /* 下一个节点 */;
    /* unsafe iterator fingerprint for misuse detection. */
    long long fingerprint;
} dictIterator;

typedef void(dictScanFunction)(void *privdata, const dictEntry *de);

/* This is the initial size of every hash table */
#define DICT_HT_INITIAL_SIZE 4

/* ------------------------------- Macros ------------------------------------*/
//销毁节点值
#define dictFreeVal(d, entry)     \
    if ((d)->type->valDestructor) \
    (d)->type->valDestructor((d)->privdata, (entry)->v.val)

//设置节点值
#define dictSetVal(d, entry, _val_)                                 \
    do                                                              \
    {                                                               \
        if ((d)->type->valDup)                                      \
            entry->v.val = (d)->type->valDup((d)->privdata, _val_); \
        else                                                        \
            entry->v.val = (_val_);                                 \
    } while (0)

//设置节点值为有符号数
#define dictSetSignedIntegerVal(entry, _val_) \
    do                                        \
    {                                         \
        entry->v.s64 = _val_;                 \
    } while (0)

//设置节点值为无符号数
#define dictSetUnsignedIntegerVal(entry, _val_) \
    do                                          \
    {                                           \
        entry->v.u64 = _val_;                   \
    } while (0)

//设置节点值为浮点数
#define dictSetDoubleVal(entry, _val_) \
    do                                 \
    {                                  \
        entry->v.d = _val_;            \
    } while (0)

//销毁节点键
#define dictFreeKey(d, entry)     \
    if ((d)->type->keyDestructor) \
    (d)->type->keyDestructor((d)->privdata, (entry)->key)

//设置节点键
#define dictSetKey(d, entry, _key_)                               \
    do                                                            \
    {                                                             \
        if ((d)->type->keyDup)                                    \
            entry->key = (d)->type->keyDup((d)->privdata, _key_); \
        else                                                      \
            entry->key = (_key_);                                 \
    } while (0)

//比较键
#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? (d)->type->keyCompare((d)->privdata, key1, key2) : (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) ((d)->ht[0].size + (d)->ht[1].size)
#define dictSize(d) ((d)->ht[0].used + (d)->ht[1].used)
#define dictIsRehashing(d) ((d)->rehashidx != -1)

/* API */
dict *dictCreate(dictType *type, void *privDataPtr);
int dictExpand(dict *d, unsigned long size);
int dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key);
int dictReplace(dict *d, void *key, void *val);
dictEntry *dictReplaceRaw(dict *d, void *key);
int dictDelete(dict *d, const void *key);
int dictDeleteNoFree(dict *d, const void *key);
void dictRelease(dict *d);
dictEntry *dictFind(dict *d, const void *key);
void *dictFetchValue(dict *d, const void *key);
int dictResize(dict *d);
dictIterator *dictGetIterator(dict *d);
dictIterator *dictGetSafeIterator(dict *d);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);
void dictPrintStats(dict *d);
unsigned int dictGenHashFunction(const void *key, int len);
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);
void dictEmpty(dict *d, void(callback)(void *));
void dictEnableResize(void);
void dictDisableResize(void);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(unsigned int initval);
unsigned int dictGetHashFunctionSeed(void);
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
