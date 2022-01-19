/*
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * Copyright (c) 1997
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

/* NOTE: This is an internal header file, included by other STL headers.
 *   You should not attempt to use it directly.
 */

#ifndef __SGI_STL_INTERNAL_HEAP_H
#define __SGI_STL_INTERNAL_HEAP_H

__STL_BEGIN_NAMESPACE

#if defined(__sgi) && !defined(__GNUC__) && (_MIPS_SIM != _MIPS_SIM_ABI32)
#pragma set woff 1209
#endif

//堆 数组模拟完全二叉树实现
//如果一个结点的位置为k,则它的父节点的位置为k/2,左子节点位置2k,右子结点2k+1

template <class RandomAccessIterator, class Distance, class T>
void __push_heap(RandomAccessIterator first, Distance holeIndex, //洞点即插入点
                 Distance topIndex, T value)
{
  //计算插入点(当前处理点)的父节点
  Distance parent = (holeIndex - 1) / 2;
  while (holeIndex > topIndex && *(first + parent) < value) //大点上浮，可见这是最大堆
  {
    //交换当前处理点和父节点
    *(first + holeIndex) = *(first + parent);
    //递进上浮
    holeIndex = parent;
    parent = (holeIndex - 1) / 2;
  }
  //插入赋值
  *(first + holeIndex) = value;
}
template <class RandomAccessIterator, class Distance, class T>
inline void __push_heap_aux(RandomAccessIterator first,
                            RandomAccessIterator last, Distance *, T *)
{
  __push_heap(first, Distance((last - first) - 1), Distance(0),
              T(*(last - 1)));
}
//插入堆对外接口
template <class RandomAccessIterator>
inline void push_heap(RandomAccessIterator first, RandomAccessIterator last)
{
  __push_heap_aux(first, last, distance_type(first), value_type(first));
}

template <class RandomAccessIterator, class Distance, class T, class Compare>
void __push_heap(RandomAccessIterator first, Distance holeIndex,
                 Distance topIndex, T value, Compare comp)
{
  Distance parent = (holeIndex - 1) / 2;
  while (holeIndex > topIndex && comp(*(first + parent), value))
  {
    *(first + holeIndex) = *(first + parent);
    holeIndex = parent;
    parent = (holeIndex - 1) / 2;
  }
  *(first + holeIndex) = value;
}
template <class RandomAccessIterator, class Compare, class Distance, class T>
inline void __push_heap_aux(RandomAccessIterator first,
                            RandomAccessIterator last, Compare comp,
                            Distance *, T *)
{
  __push_heap(first, Distance((last - first) - 1), Distance(0),
              T(*(last - 1)), comp);
}
template <class RandomAccessIterator, class Compare>
inline void push_heap(RandomAccessIterator first, RandomAccessIterator last,
                      Compare comp)
{
  __push_heap_aux(first, last, comp, distance_type(first), value_type(first));
}

//弹出堆顶并调整堆
template <class RandomAccessIterator, class Distance, class T>
void __adjust_heap(RandomAccessIterator first, Distance holeIndex,
                   Distance len, T value)
{
  //从洞点开始处理
  Distance topIndex = holeIndex;
  //计算右子节点
  Distance secondChild = 2 * holeIndex + 2;
  while (secondChild < len) //右子节点有效
  {
    //取左右子节点的大值
    if (*(first + secondChild) < *(first + (secondChild - 1)))
      secondChild--;
    //左右子节点的大值填入处理点
    *(first + holeIndex) = *(first + secondChild);
    //处理点下沉
    holeIndex = secondChild;
    secondChild = 2 * (secondChild + 1);
  }
  if (secondChild == len) //右子节点无效，但是左子节点有效
  {
    *(first + holeIndex) = *(first + (secondChild - 1));
    holeIndex = secondChild - 1;
  }
  //插入value,value其实是原堆的最后元素
  __push_heap(first, holeIndex, topIndex, value);
}
//弹出堆顶
template <class RandomAccessIterator, class T, class Distance>
inline void __pop_heap(RandomAccessIterator first, RandomAccessIterator last,
                       RandomAccessIterator result, T value, Distance *)
{
  *result = *first;
  __adjust_heap(first, Distance(0), Distance(last - first), value);
}
//弹出堆顶 结果放在last-1
template <class RandomAccessIterator, class T>
inline void __pop_heap_aux(RandomAccessIterator first,
                           RandomAccessIterator last, T *)
{
  __pop_heap(first, last - 1, last - 1, T(*(last - 1)), distance_type(first));
}
//弹出堆顶 对外接口
template <class RandomAccessIterator>
inline void pop_heap(RandomAccessIterator first, RandomAccessIterator last)
{
  __pop_heap_aux(first, last, value_type(first));
}

template <class RandomAccessIterator, class Distance, class T, class Compare>
void __adjust_heap(RandomAccessIterator first, Distance holeIndex,
                   Distance len, T value, Compare comp)
{
  Distance topIndex = holeIndex;
  Distance secondChild = 2 * holeIndex + 2;
  while (secondChild < len)
  {
    if (comp(*(first + secondChild), *(first + (secondChild - 1))))
      secondChild--;
    *(first + holeIndex) = *(first + secondChild);
    holeIndex = secondChild;
    secondChild = 2 * (secondChild + 1);
  }
  if (secondChild == len)
  {
    *(first + holeIndex) = *(first + (secondChild - 1));
    holeIndex = secondChild - 1;
  }
  __push_heap(first, holeIndex, topIndex, value, comp);
}
template <class RandomAccessIterator, class T, class Compare, class Distance>
inline void __pop_heap(RandomAccessIterator first, RandomAccessIterator last,
                       RandomAccessIterator result, T value, Compare comp,
                       Distance *)
{
  *result = *first;
  __adjust_heap(first, Distance(0), Distance(last - first), value, comp);
}
template <class RandomAccessIterator, class T, class Compare>
inline void __pop_heap_aux(RandomAccessIterator first,
                           RandomAccessIterator last, T *, Compare comp)
{
  __pop_heap(first, last - 1, last - 1, T(*(last - 1)), comp,
             distance_type(first));
}
template <class RandomAccessIterator, class Compare>
inline void pop_heap(RandomAccessIterator first, RandomAccessIterator last,
                     Compare comp)
{
  __pop_heap_aux(first, last, value_type(first), comp);
}

//构建堆
template <class RandomAccessIterator, class T, class Distance>
void __make_heap(RandomAccessIterator first, RandomAccessIterator last, T *,
                 Distance *)
{
  if (last - first < 2)
    return;
  Distance len = last - first;
  //从最底下的满的一层开始处理!
  Distance parent = (len - 2) / 2;
  while (true)
  {
    //将处理点(parent)弹出(使以处理点为根节点的树上浮最大值),再重新插入堆。这样这层的最大值完成上浮
    //如果__adjust_heap设计成只比较parent和它的子层(而不是遍历所有子层)，是否更能提升效率？
    __adjust_heap(first, parent, len, T(*(first + parent)));
    if (parent == 0)
      return;
    parent--;
  }
}
//构建堆
template <class RandomAccessIterator>
inline void make_heap(RandomAccessIterator first, RandomAccessIterator last)
{
  __make_heap(first, last, value_type(first), distance_type(first));
}

template <class RandomAccessIterator, class Compare, class T, class Distance>
void __make_heap(RandomAccessIterator first, RandomAccessIterator last,
                 Compare comp, T *, Distance *)
{
  if (last - first < 2)
    return;
  Distance len = last - first;
  Distance parent = (len - 2) / 2;

  while (true)
  {
    __adjust_heap(first, parent, len, T(*(first + parent)), comp);
    if (parent == 0)
      return;
    parent--;
  }
}
template <class RandomAccessIterator, class Compare>
inline void make_heap(RandomAccessIterator first, RandomAccessIterator last,
                      Compare comp)
{
  __make_heap(first, last, comp, value_type(first), distance_type(first));
}

//排序堆
template <class RandomAccessIterator>
void sort_heap(RandomAccessIterator first, RandomAccessIterator last)
{
  while (last - first > 1)
    pop_heap(first, last--); //循环pop取出最大值并放置到后面
}
template <class RandomAccessIterator, class Compare>
void sort_heap(RandomAccessIterator first, RandomAccessIterator last,
               Compare comp)
{
  while (last - first > 1)
    pop_heap(first, last--, comp);
}

#if defined(__sgi) && !defined(__GNUC__) && (_MIPS_SIM != _MIPS_SIM_ABI32)
#pragma reset woff 1209
#endif

__STL_END_NAMESPACE

#endif /* __SGI_STL_INTERNAL_HEAP_H */

// Local Variables:
// mode:C++
// End:
