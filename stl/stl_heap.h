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

#ifndef _CPP_BITS_STL_HEAP_H
#define _CPP_BITS_STL_HEAP_H 1

__STL_BEGIN_NAMESPACE

#if defined(__sgi) && !defined(__GNUC__) && (_MIPS_SIM != _MIPS_SIM_ABI32)
#pragma set woff 1209
#endif

// Heap-manipulation functions: push_heap, pop_heap, make_heap, sort_heap.

//堆 数组模拟完全二叉树实现
//如果一个结点的位置为k,则它的父节点的位置为k/2,左子节点位置2k,右子结点2k+1

template <class _RandomAccessIterator, class _Distance, class _Tp>
void __push_heap(_RandomAccessIterator __first, //起点
                 _Distance __holeIndex,         //终点，插入点
                 _Distance __topIndex,          //根节点 0
                 _Tp __value)
{
  //计算插入点的父节点
  _Distance __parent = (__holeIndex - 1) / 2;
  //“🕳️”节点在根节点后 并且 父节点值小于插入值 代表需要交换节点
  while (__holeIndex > __topIndex && *(__first + __parent) < __value)
  {
    //“🕳️”值放入父节点的值
    *(__first + __holeIndex) = *(__first + __parent);
    //“🕳️”位移动到父节点
    __holeIndex = __parent;
    //父节点更新
    __parent = (__holeIndex - 1) / 2;
  }
  //”🕳️“值插入
  *(__first + __holeIndex) = __value;
}

template <class _RandomAccessIterator, class _Distance, class _Tp>
inline void
__push_heap_aux(_RandomAccessIterator __first,
                _RandomAccessIterator __last, _Distance *, _Tp *)
{
  //在堆[first, last-1)中插入*(last-1)
  __push_heap(__first, _Distance((__last - __first) - 1), _Distance(0),
              _Tp(*(__last - 1)));
}

//在堆[first, last-1)中插入*(last-1)
template <class _RandomAccessIterator>
inline void
push_heap(_RandomAccessIterator __first, _RandomAccessIterator __last)
{
  __STL_REQUIRES(_RandomAccessIterator, _Mutable_RandomAccessIterator);
  __STL_REQUIRES(typename iterator_traits<_RandomAccessIterator>::value_type,
                 _LessThanComparable);
  __push_heap_aux(__first, __last,
                  __DISTANCE_TYPE(__first), __VALUE_TYPE(__first));
}

//在堆[first, last-1)中插入*(last-1) 自定义比较方法版
template <class _RandomAccessIterator, class _Distance, class _Tp,
          class _Compare>
void __push_heap(_RandomAccessIterator __first, _Distance __holeIndex,
                 _Distance __topIndex, _Tp __value, _Compare __comp)
{
  _Distance __parent = (__holeIndex - 1) / 2;
  while (__holeIndex > __topIndex && __comp(*(__first + __parent), __value))
  {
    *(__first + __holeIndex) = *(__first + __parent);
    __holeIndex = __parent;
    __parent = (__holeIndex - 1) / 2;
  }
  *(__first + __holeIndex) = __value;
}

template <class _RandomAccessIterator, class _Compare,
          class _Distance, class _Tp>
inline void
__push_heap_aux(_RandomAccessIterator __first,
                _RandomAccessIterator __last, _Compare __comp,
                _Distance *, _Tp *)
{
  __push_heap(__first, _Distance((__last - __first) - 1), _Distance(0),
              _Tp(*(__last - 1)), __comp);
}

template <class _RandomAccessIterator, class _Compare>
inline void
push_heap(_RandomAccessIterator __first, _RandomAccessIterator __last,
          _Compare __comp)
{
  __STL_REQUIRES(_RandomAccessIterator, _Mutable_RandomAccessIterator);
  __push_heap_aux(__first, __last, __comp,
                  __DISTANCE_TYPE(__first), __VALUE_TYPE(__first));
}

//下沉根节点
template <class _RandomAccessIterator, class _Distance, class _Tp>
void __adjust_heap(_RandomAccessIterator __first, // 0
                   _Distance __holeIndex,         // 0
                   _Distance __len, _Tp __value)
{
  //根节点
  _Distance __topIndex = __holeIndex;
  //“🕳️”点的右子节点
  _Distance __secondChild = 2 * __holeIndex + 2;
  while (__secondChild < __len) //右子节点有效
  {
    //取左右子节点的大值
    if (*(__first + __secondChild) < *(__first + (__secondChild - 1)))
      __secondChild--;
    //“🕳️”点取子节点的大值
    *(__first + __holeIndex) = *(__first + __secondChild);
    //“🕳️”点下沉
    __holeIndex = __secondChild;
    //更新子节点
    __secondChild = 2 * (__secondChild + 1);
  }
  if (__secondChild == __len) // fix 节点数为len-1
  {
    //“🕳️”点交换左子节点
    *(__first + __holeIndex) = *(__first + (__secondChild - 1));
    __holeIndex = __secondChild - 1;
  }
  //插入value，调整堆
  __push_heap(__first, __holeIndex, __topIndex, __value);
}

template <class _RandomAccessIterator, class _Tp, class _Distance>
inline void
__pop_heap(_RandomAccessIterator __first, _RandomAccessIterator __last,
           _RandomAccessIterator __result, _Tp __value, _Distance *)
{
  //取根
  *__result = *__first;
  //下沉根节点
  __adjust_heap(__first, _Distance(0), _Distance(__last - __first), __value);
}

template <class _RandomAccessIterator, class _Tp>
inline void
__pop_heap_aux(_RandomAccessIterator __first, _RandomAccessIterator __last,
               _Tp *)
{
  // pop根，实际移动到last-1并且重新调整堆
  __pop_heap(__first, __last - 1, __last - 1,
             _Tp(*(__last - 1)), __DISTANCE_TYPE(__first));
}

// pop根
template <class _RandomAccessIterator>
inline void pop_heap(_RandomAccessIterator __first,
                     _RandomAccessIterator __last)
{
  __STL_REQUIRES(_RandomAccessIterator, _Mutable_RandomAccessIterator);
  __STL_REQUIRES(typename iterator_traits<_RandomAccessIterator>::value_type,
                 _LessThanComparable);
  __pop_heap_aux(__first, __last, __VALUE_TYPE(__first));
}

template <class _RandomAccessIterator, class _Distance,
          class _Tp, class _Compare>
void __adjust_heap(_RandomAccessIterator __first, _Distance __holeIndex,
                   _Distance __len, _Tp __value, _Compare __comp)
{
  _Distance __topIndex = __holeIndex;
  _Distance __secondChild = 2 * __holeIndex + 2;
  while (__secondChild < __len)
  {
    if (__comp(*(__first + __secondChild), *(__first + (__secondChild - 1))))
      __secondChild--;
    *(__first + __holeIndex) = *(__first + __secondChild);
    __holeIndex = __secondChild;
    __secondChild = 2 * (__secondChild + 1);
  }
  if (__secondChild == __len)
  {
    *(__first + __holeIndex) = *(__first + (__secondChild - 1));
    __holeIndex = __secondChild - 1;
  }
  __push_heap(__first, __holeIndex, __topIndex, __value, __comp);
}

template <class _RandomAccessIterator, class _Tp, class _Compare,
          class _Distance>
inline void
__pop_heap(_RandomAccessIterator __first, _RandomAccessIterator __last,
           _RandomAccessIterator __result, _Tp __value, _Compare __comp,
           _Distance *)
{
  *__result = *__first;
  __adjust_heap(__first, _Distance(0), _Distance(__last - __first),
                __value, __comp);
}

template <class _RandomAccessIterator, class _Tp, class _Compare>
inline void
__pop_heap_aux(_RandomAccessIterator __first,
               _RandomAccessIterator __last, _Tp *, _Compare __comp)
{
  __pop_heap(__first, __last - 1, __last - 1, _Tp(*(__last - 1)), __comp,
             __DISTANCE_TYPE(__first));
}

//下沉根节点
template <class _RandomAccessIterator, class _Compare>
inline void
pop_heap(_RandomAccessIterator __first,
         _RandomAccessIterator __last, _Compare __comp)
{
  __STL_REQUIRES(_RandomAccessIterator, _Mutable_RandomAccessIterator);
  __pop_heap_aux(__first, __last, __VALUE_TYPE(__first), __comp);
}

template <class _RandomAccessIterator, class _Tp, class _Distance>
void __make_heap(_RandomAccessIterator __first,
                 _RandomAccessIterator __last, _Tp *, _Distance *)
{
  if (__last - __first < 2)
    return;
  //堆长度
  _Distance __len = __last - __first;
  //父节点
  _Distance __parent = (__len - 2) / 2;

  while (true)
  {
    __adjust_heap(__first, __parent, __len, _Tp(*(__first + __parent)));
    if (__parent == 0)
      return;
    __parent--;
  }
}

//生成堆
template <class _RandomAccessIterator>
inline void
make_heap(_RandomAccessIterator __first, _RandomAccessIterator __last)
{
  __STL_REQUIRES(_RandomAccessIterator, _Mutable_RandomAccessIterator);
  __STL_REQUIRES(typename iterator_traits<_RandomAccessIterator>::value_type,
                 _LessThanComparable);
  __make_heap(__first, __last,
              __VALUE_TYPE(__first), __DISTANCE_TYPE(__first));
}

template <class _RandomAccessIterator, class _Compare,
          class _Tp, class _Distance>
void __make_heap(_RandomAccessIterator __first, _RandomAccessIterator __last,
                 _Compare __comp, _Tp *, _Distance *)
{
  if (__last - __first < 2)
    return;
  _Distance __len = __last - __first;
  _Distance __parent = (__len - 2) / 2;

  while (true)
  {
    __adjust_heap(__first, __parent, __len, _Tp(*(__first + __parent)),
                  __comp);
    if (__parent == 0)
      return;
    __parent--;
  }
}

template <class _RandomAccessIterator, class _Compare>
inline void
make_heap(_RandomAccessIterator __first,
          _RandomAccessIterator __last, _Compare __comp)
{
  __STL_REQUIRES(_RandomAccessIterator, _Mutable_RandomAccessIterator);
  __make_heap(__first, __last, __comp,
              __VALUE_TYPE(__first), __DISTANCE_TYPE(__first));
}

//堆排序，循环pop取出最大值并放置到后面实现
template <class _RandomAccessIterator>
void sort_heap(_RandomAccessIterator __first, _RandomAccessIterator __last)
{
  __STL_REQUIRES(_RandomAccessIterator, _Mutable_RandomAccessIterator);
  __STL_REQUIRES(typename iterator_traits<_RandomAccessIterator>::value_type,
                 _LessThanComparable);
  while (__last - __first > 1)
    pop_heap(__first, __last--);
}

template <class _RandomAccessIterator, class _Compare>
void sort_heap(_RandomAccessIterator __first,
               _RandomAccessIterator __last, _Compare __comp)
{
  __STL_REQUIRES(_RandomAccessIterator, _Mutable_RandomAccessIterator);
  while (__last - __first > 1)
    pop_heap(__first, __last--, __comp);
}

#if defined(__sgi) && !defined(__GNUC__) && (_MIPS_SIM != _MIPS_SIM_ABI32)
#pragma reset woff 1209
#endif

__STL_END_NAMESPACE

#endif /* _CPP_BITS_STL_HEAP_H */

// Local Variables:
// mode:C++
// End:
