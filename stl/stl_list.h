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
 *
 * Copyright (c) 1996,1997
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

#ifndef __SGI_STL_INTERNAL_LIST_H
#define __SGI_STL_INTERNAL_LIST_H

#include <bits/concept_checks.h>

__STL_BEGIN_NAMESPACE

#if defined(__sgi) && !defined(__GNUC__) && (_MIPS_SIM != _MIPS_SIM_ABI32)
#pragma set woff 1174
#pragma set woff 1375
#endif

//链表节点基类
struct _List_node_base
{
  _List_node_base *_M_next; //指向下一个节点
  _List_node_base *_M_prev; //指向上一个节点
};

//链表节点
template <class _Tp>
struct _List_node : public _List_node_base
{
  _Tp _M_data; //元素
};

//链表迭代器基类
struct _List_iterator_base
{
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef bidirectional_iterator_tag iterator_category; //可双向访问

  _List_node_base *_M_node; //节点指针

  _List_iterator_base(_List_node_base *__x) : _M_node(__x) {}
  _List_iterator_base() {}

  void _M_incr() { _M_node = _M_node->_M_next; } //++
  void _M_decr() { _M_node = _M_node->_M_prev; } //--

  bool operator==(const _List_iterator_base &__x) const
  {
    return _M_node == __x._M_node;
  }
  bool operator!=(const _List_iterator_base &__x) const
  {
    return _M_node != __x._M_node;
  }
};

//链表迭代器
template <class _Tp, class _Ref, class _Ptr>
struct _List_iterator : public _List_iterator_base
{
  typedef _List_iterator<_Tp, _Tp &, _Tp *> iterator;
  typedef _List_iterator<_Tp, const _Tp &, const _Tp *> const_iterator;
  typedef _List_iterator<_Tp, _Ref, _Ptr> _Self;

  typedef _Tp value_type;
  typedef _Ptr pointer;
  typedef _Ref reference;
  typedef _List_node<_Tp> _Node;

  _List_iterator(_Node *__x) : _List_iterator_base(__x) {}
  _List_iterator() {}
  _List_iterator(const iterator &__x) : _List_iterator_base(__x._M_node) {}

  //迭代器解引用返回元素的数据字段
  reference operator*() const { return ((_Node *)_M_node)->_M_data; }

#ifndef __SGI_STL_NO_ARROW_OPERATOR
  //同上，迭代器->运算符转为对元素的数据字段的->操作
  pointer operator->() const
  {
    return &(operator*());
  }
#endif /* __SGI_STL_NO_ARROW_OPERATOR */

  //自增自减操作
  _Self &operator++()
  {
    this->_M_incr();
    return *this;
  }
  _Self operator++(int)
  {
    _Self __tmp = *this;
    this->_M_incr();
    return __tmp;
  }
  _Self &operator--()
  {
    this->_M_decr();
    return *this;
  }
  _Self operator--(int)
  {
    _Self __tmp = *this;
    this->_M_decr();
    return __tmp;
  }
};

#ifndef __STL_CLASS_PARTIAL_SPECIALIZATION

inline bidirectional_iterator_tag
iterator_category(const _List_iterator_base &)
{
  return bidirectional_iterator_tag();
}

template <class _Tp, class _Ref, class _Ptr>
inline _Tp *
value_type(const _List_iterator<_Tp, _Ref, _Ptr> &)
{
  return 0;
}

inline ptrdiff_t *
distance_type(const _List_iterator_base &)
{
  return 0;
}

#endif /* __STL_CLASS_PARTIAL_SPECIALIZATION */

// Base class that encapsulates details of allocators.  Three cases:
// an ordinary standard-conforming allocator, a standard-conforming
// allocator with no non-static data, and an SGI-style allocator.
// This complexity is necessary only because we're worrying about backward
// compatibility and because we want to avoid wasting storage on an
// allocator instance if it isn't necessary.

#ifdef __STL_USE_STD_ALLOCATORS

// Base for general standard-conforming allocators.
template <class _Tp, class _Allocator, bool _IsStatic>
class _List_alloc_base
{
public:
  typedef typename _Alloc_traits<_Tp, _Allocator>::allocator_type
      allocator_type;
  allocator_type get_allocator() const { return _Node_allocator; }

  _List_alloc_base(const allocator_type &__a) : _Node_allocator(__a) {}

protected:
  _List_node<_Tp> *_M_get_node()
  {
    return _Node_allocator.allocate(1);
  }
  void _M_put_node(_List_node<_Tp> *__p)
  {
    _Node_allocator.deallocate(__p, 1);
  }

protected:
  typename _Alloc_traits<_List_node<_Tp>, _Allocator>::allocator_type
      _Node_allocator;
  _List_node<_Tp> *_M_node;
};

// Specialization for instanceless allocators.

template <class _Tp, class _Allocator>
class _List_alloc_base<_Tp, _Allocator, true>
{
public:
  typedef typename _Alloc_traits<_Tp, _Allocator>::allocator_type
      allocator_type;
  allocator_type get_allocator() const { return allocator_type(); }

  _List_alloc_base(const allocator_type &) {}

protected:
  typedef typename _Alloc_traits<_List_node<_Tp>, _Allocator>::_Alloc_type
      _Alloc_type;
  _List_node<_Tp> *_M_get_node() { return _Alloc_type::allocate(1); }
  void _M_put_node(_List_node<_Tp> *__p) { _Alloc_type::deallocate(__p, 1); }

protected:
  _List_node<_Tp> *_M_node;
};

template <class _Tp, class _Alloc>
class _List_base
    : public _List_alloc_base<_Tp, _Alloc,
                              _Alloc_traits<_Tp, _Alloc>::_S_instanceless>
{
public:
  typedef _List_alloc_base<_Tp, _Alloc,
                           _Alloc_traits<_Tp, _Alloc>::_S_instanceless>
      _Base;
  typedef typename _Base::allocator_type allocator_type;

  _List_base(const allocator_type &__a) : _Base(__a)
  {
    _M_node = _M_get_node();
    _M_node->_M_next = _M_node;
    _M_node->_M_prev = _M_node;
  }
  ~_List_base()
  {
    clear();
    _M_put_node(_M_node);
  }

  void clear();
};

#else /* __STL_USE_STD_ALLOCATORS */

//链表基类
template <class _Tp, class _Alloc>
class _List_base
{
public:
  typedef _Alloc allocator_type;
  allocator_type get_allocator() const { return allocator_type(); }

  _List_base(const allocator_type &)
  {
    _M_node = _M_get_node();    //分配一个节点作为哨兵，作为“🈳️”节点
    _M_node->_M_next = _M_node; //指向“🈳️”节点
    _M_node->_M_prev = _M_node; //指向“🈳️”节点
  }
  ~_List_base()
  {
    //清空链表
    clear();
    //销毁空节点
    _M_put_node(_M_node);
  }

  void clear();

protected:
  typedef simple_alloc<_List_node<_Tp>, _Alloc> _Alloc_type;
  //分配一个节点的内存空间
  _List_node<_Tp> *_M_get_node() { return _Alloc_type::allocate(1); }
  //销毁节点的内存空间
  void _M_put_node(_List_node<_Tp> *__p) { _Alloc_type::deallocate(__p, 1); }

protected:
  _List_node<_Tp> *_M_node; //哨兵节点
};

#endif /* __STL_USE_STD_ALLOCATORS */

//清空链表
template <class _Tp, class _Alloc>
void _List_base<_Tp, _Alloc>::clear()
{
  //从哨兵节点的下一个节点开始处理
  _List_node<_Tp> *__cur = (_List_node<_Tp> *)_M_node->_M_next;
  while (__cur != _M_node)
  {
    _List_node<_Tp> *__tmp = __cur;
    __cur = (_List_node<_Tp> *)__cur->_M_next;
    //调用析构操作
    _Destroy(&__tmp->_M_data);
    //销毁节点内存
    _M_put_node(__tmp);
  }
  //指向哨兵，回归“🈳️”链表状态
  _M_node->_M_next = _M_node;
  _M_node->_M_prev = _M_node;
}

//链表
template <class _Tp, class _Alloc = allocator<_Tp>>
class list : protected _List_base<_Tp, _Alloc>
{
  // requirements:

  __STL_CLASS_REQUIRES(_Tp, _Assignable);

  typedef _List_base<_Tp, _Alloc> _Base;

protected:
  typedef void *_Void_pointer;

public:
  typedef _Tp value_type;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;
  typedef value_type &reference;
  typedef const value_type &const_reference;
  typedef _List_node<_Tp> _Node;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;

  typedef typename _Base::allocator_type allocator_type;
  allocator_type get_allocator() const { return _Base::get_allocator(); }

public:
  typedef _List_iterator<_Tp, _Tp &, _Tp *> iterator;
  typedef _List_iterator<_Tp, const _Tp &, const _Tp *> const_iterator;

#ifdef __STL_CLASS_PARTIAL_SPECIALIZATION
  typedef reverse_iterator<const_iterator> const_reverse_iterator;
  typedef reverse_iterator<iterator> reverse_iterator;
#else  /* __STL_CLASS_PARTIAL_SPECIALIZATION */
  typedef reverse_bidirectional_iterator<const_iterator, value_type,
                                         const_reference, difference_type>
      const_reverse_iterator;
  typedef reverse_bidirectional_iterator<iterator, value_type, reference,
                                         difference_type>
      reverse_iterator;
#endif /* __STL_CLASS_PARTIAL_SPECIALIZATION */

protected:
#ifdef __STL_HAS_NAMESPACES
  using _Base::_M_get_node;
  using _Base::_M_node;
  using _Base::_M_put_node;
#endif /* __STL_HAS_NAMESPACES */

protected:
  //创建新节点
  _Node *_M_create_node(const _Tp &__x)
  {
    //获取新节点内存
    _Node *__p = _M_get_node();
    __STL_TRY
    {
      //调用构造
      _Construct(&__p->_M_data, __x);
    }
    // catch
    __STL_UNWIND(_M_put_node(__p));
    return __p;
  }

  //创建新节点，使用默认值版本
  _Node *_M_create_node()
  {
    _Node *__p = _M_get_node();
    __STL_TRY
    {
      _Construct(&__p->_M_data);
    }
    __STL_UNWIND(_M_put_node(__p));
    return __p;
  }

public:
  explicit list(const allocator_type &__a = allocator_type()) : _Base(__a) {}

  //第一个元素是哨兵的下一个元素
  //最后一个元素是哨兵
  //下面的函数定义简单明了

  iterator begin() { return (_Node *)(_M_node->_M_next); }
  const_iterator begin() const { return (_Node *)(_M_node->_M_next); }

  iterator end() { return _M_node; }
  const_iterator end() const { return _M_node; }

  reverse_iterator rbegin()
  {
    return reverse_iterator(end());
  }
  const_reverse_iterator rbegin() const
  {
    return const_reverse_iterator(end());
  }

  reverse_iterator rend()
  {
    // reverse_iterator的operator*是(--*node)的，所以用begin()初始化没有问题
    return reverse_iterator(begin());
  }
  const_reverse_iterator rend() const
  {
    // const_reverse_iterator的operator*是(--*node)的，所以用begin()初始化没有问题
    return const_reverse_iterator(begin());
  }

  //判断第一个逻辑节点是否指向哨兵作为链表空的依据
  bool empty() const { return _M_node->_M_next == _M_node; }
  size_type size() const
  {
    size_type __result = 0;
    //非可随机访问类别迭代器的distance实际上遍历了一遍所有元素！
    distance(begin(), end(), __result);
    return __result;
  }
  size_type max_size() const { return size_type(-1); }

  //第一个元素是哨兵的下一个元素
  //最后一个元素是哨兵
  //下面的函数定义简单明了

  reference front() { return *begin(); }
  const_reference front() const { return *begin(); }
  reference back() { return *(--end()); }
  const_reference back() const { return *(--end()); }

  //交换哨兵相当于交换了链表！
  void swap(list<_Tp, _Alloc> &__x) { __STD::swap(_M_node, __x._M_node); }

  //插入新元素
  iterator insert(iterator __position, const _Tp &__x)
  {
    //构造一个新节点
    _Node *__tmp = _M_create_node(__x);
    //头插法
    __tmp->_M_next = __position._M_node;
    __tmp->_M_prev = __position._M_node->_M_prev;
    __position._M_node->_M_prev->_M_next = __tmp;
    __position._M_node->_M_prev = __tmp;
    //返回新节点，同时它也是position的元素。
    return __tmp;
  }
  //插入新元素，默认版本
  iterator insert(iterator __position) { return insert(__position, _Tp()); }
#ifdef __STL_MEMBER_TEMPLATES
  // Check whether it's an integral type.  If so, it's not an iterator.

  template <class _Integer>
  void _M_insert_dispatch(iterator __pos, _Integer __n, _Integer __x,
                          __true_type)
  {
    _M_fill_insert(__pos, (size_type)__n, (_Tp)__x);
  }

  template <class _InputIterator>
  void _M_insert_dispatch(iterator __pos,
                          _InputIterator __first, _InputIterator __last,
                          __false_type);

  template <class _InputIterator>
  void insert(iterator __pos, _InputIterator __first, _InputIterator __last)
  {
    typedef typename _Is_integer<_InputIterator>::_Integral _Integral;
    _M_insert_dispatch(__pos, __first, __last, _Integral());
  }

#else  /* __STL_MEMBER_TEMPLATES */
  void insert(iterator __position, const _Tp *__first, const _Tp *__last);
  void insert(iterator __position,
              const_iterator __first, const_iterator __last);
#endif /* __STL_MEMBER_TEMPLATES */

  //插入多个新元素
  void insert(iterator __pos, size_type __n, const _Tp &__x)
  {
    _M_fill_insert(__pos, __n, __x);
  }
  void _M_fill_insert(iterator __pos, size_type __n, const _Tp &__x);

  //在最前面插入
  void push_front(const _Tp &__x) { insert(begin(), __x); }
  void push_front() { insert(begin()); }
  //在尾部插入
  void push_back(const _Tp &__x) { insert(end(), __x); }
  void push_back() { insert(end()); }

  //删除该位置的元素
  iterator erase(iterator __position)
  {
    //保存删除位元素的前后指向
    _List_node_base *__next_node = __position._M_node->_M_next;
    _List_node_base *__prev_node = __position._M_node->_M_prev;
    //获取删除位元素
    _Node *__n = (_Node *)__position._M_node;
    //连接获取删除位元素的前后元素
    __prev_node->_M_next = __next_node;
    __next_node->_M_prev = __prev_node;
    //析构
    _Destroy(&__n->_M_data);
    //销毁内存
    _M_put_node(__n);
    //返回原下一个元素，此时它就在position位！
    return iterator((_Node *)__next_node);
  }
  iterator erase(iterator __first, iterator __last);
  void clear() { _Base::clear(); }

  void resize(size_type __new_size, const _Tp &__x);
  void resize(size_type __new_size) { this->resize(__new_size, _Tp()); }

  //删除首元素
  void pop_front() { erase(begin()); }
  //删除尾元素，非哨兵！
  void pop_back()
  {
    //哨兵
    iterator __tmp = end();
    //删除哨兵前一个元素
    erase(--__tmp);
  }
  //构造，n个元素，值都为value
  list(size_type __n, const _Tp &__value,
       const allocator_type &__a = allocator_type())
      : _Base(__a)
  {
    insert(begin(), __n, __value);
  }
  //构造，n个元素，值都为默认值
  explicit list(size_type __n)
      : _Base(allocator_type())
  {
    insert(begin(), __n, _Tp());
  }

#ifdef __STL_MEMBER_TEMPLATES

  // We don't need any dispatching tricks here, because insert does all of
  // that anyway.
  template <class _InputIterator>
  list(_InputIterator __first, _InputIterator __last,
       const allocator_type &__a = allocator_type())
      : _Base(__a)
  {
    insert(begin(), __first, __last);
  }

#else /* __STL_MEMBER_TEMPLATES */

  //构造，初始为[first, last)的值
  list(const _Tp *__first, const _Tp *__last,
       const allocator_type &__a = allocator_type())
      : _Base(__a)
  {
    this->insert(begin(), __first, __last);
  }
  list(const_iterator __first, const_iterator __last,
       const allocator_type &__a = allocator_type())
      : _Base(__a)
  {
    this->insert(begin(), __first, __last);
  }

#endif /* __STL_MEMBER_TEMPLATES */

  //拷贝构造
  list(const list<_Tp, _Alloc> &__x) : _Base(__x.get_allocator())
  {
    insert(begin(), __x.begin(), __x.end());
  }

  //链表基类负责内存管理了，链表类无需处理
  ~list() {}

  list<_Tp, _Alloc> &operator=(const list<_Tp, _Alloc> &__x);

public:
  // assign(), a generalized assignment member function.  Two
  // versions: one that takes a count, and one that takes a range.
  // The range version is a member template, so we dispatch on whether
  // or not the type is an integer.

  void assign(size_type __n, const _Tp &__val) { _M_fill_assign(__n, __val); }

  void _M_fill_assign(size_type __n, const _Tp &__val);

#ifdef __STL_MEMBER_TEMPLATES

  template <class _InputIterator>
  void assign(_InputIterator __first, _InputIterator __last)
  {
    typedef typename _Is_integer<_InputIterator>::_Integral _Integral;
    _M_assign_dispatch(__first, __last, _Integral());
  }

  template <class _Integer>
  void _M_assign_dispatch(_Integer __n, _Integer __val, __true_type)
  {
    _M_fill_assign((size_type)__n, (_Tp)__val);
  }

  template <class _InputIterator>
  void _M_assign_dispatch(_InputIterator __first, _InputIterator __last,
                          __false_type);

#endif /* __STL_MEMBER_TEMPLATES */

protected:
  // 移动[first, last)的元素到position前
  void transfer(iterator __position, iterator __first, iterator __last)
  {
    if (__position != __last)
    {
      // Remove [first, last) from its old position.

      // position节点成为last前节点的后继
      __last._M_node->_M_prev->_M_next = __position._M_node;
      // last节点成为first前节点的后继
      __first._M_node->_M_prev->_M_next = __last._M_node;
      // first节点成为position前节点的后继
      __position._M_node->_M_prev->_M_next = __first._M_node;

      // Splice [first, last) into its new position.

      _List_node_base *__tmp = __position._M_node->_M_prev;
      // last前节点成为position前节点
      __position._M_node->_M_prev = __last._M_node->_M_prev;
      // fist前节点成为last前节点
      __last._M_node->_M_prev = __first._M_node->_M_prev;
      // position前节点成为first前节点
      __first._M_node->_M_prev = __tmp;
    }
  }

public:
  //将链表x拼接到position前
  void splice(iterator __position, list &__x)
  {
    if (!__x.empty())
      this->transfer(__position, __x.begin(), __x.end());
  }
  //将i移动到position前
  void splice(iterator __position, list &, iterator __i)
  {
    iterator __j = __i;
    ++__j;
    if (__position == __i || __position == __j)
      return;
    this->transfer(__position, __i, __j);
  }
  //将[first, last)移动到position前
  void splice(iterator __position, list &, iterator __first, iterator __last)
  {
    if (__first != __last)
      this->transfer(__position, __first, __last);
  }
  void remove(const _Tp &__value);
  void unique();
  void merge(list &__x);
  void reverse();
  void sort();

#ifdef __STL_MEMBER_TEMPLATES
  template <class _Predicate>
  void remove_if(_Predicate);
  template <class _BinaryPredicate>
  void unique(_BinaryPredicate);
  template <class _StrictWeakOrdering>
  void merge(list &, _StrictWeakOrdering);
  template <class _StrictWeakOrdering>
  void sort(_StrictWeakOrdering);
#endif /* __STL_MEMBER_TEMPLATES */
};

template <class _Tp, class _Alloc>
inline bool
operator==(const list<_Tp, _Alloc> &__x, const list<_Tp, _Alloc> &__y)
{
  typedef typename list<_Tp, _Alloc>::const_iterator const_iterator;
  const_iterator __end1 = __x.end();
  const_iterator __end2 = __y.end();

  const_iterator __i1 = __x.begin();
  const_iterator __i2 = __y.begin();
  while (__i1 != __end1 && __i2 != __end2 && *__i1 == *__i2)
  {
    ++__i1;
    ++__i2;
  }
  return __i1 == __end1 && __i2 == __end2;
}

template <class _Tp, class _Alloc>
inline bool operator<(const list<_Tp, _Alloc> &__x,
                      const list<_Tp, _Alloc> &__y)
{
  return lexicographical_compare(__x.begin(), __x.end(),
                                 __y.begin(), __y.end());
}

#ifdef __STL_FUNCTION_TMPL_PARTIAL_ORDER

template <class _Tp, class _Alloc>
inline bool operator!=(const list<_Tp, _Alloc> &__x,
                       const list<_Tp, _Alloc> &__y)
{
  return !(__x == __y);
}

template <class _Tp, class _Alloc>
inline bool operator>(const list<_Tp, _Alloc> &__x,
                      const list<_Tp, _Alloc> &__y)
{
  return __y < __x;
}

template <class _Tp, class _Alloc>
inline bool operator<=(const list<_Tp, _Alloc> &__x,
                       const list<_Tp, _Alloc> &__y)
{
  return !(__y < __x);
}

template <class _Tp, class _Alloc>
inline bool operator>=(const list<_Tp, _Alloc> &__x,
                       const list<_Tp, _Alloc> &__y)
{
  return !(__x < __y);
}

template <class _Tp, class _Alloc>
inline void
swap(list<_Tp, _Alloc> &__x, list<_Tp, _Alloc> &__y)
{
  __x.swap(__y);
}

#endif /* __STL_FUNCTION_TMPL_PARTIAL_ORDER */

#ifdef __STL_MEMBER_TEMPLATES

template <class _Tp, class _Alloc>
template <class _InputIter>
void list<_Tp, _Alloc>::_M_insert_dispatch(iterator __position,
                                           _InputIter __first, _InputIter __last,
                                           __false_type)
{
  for (; __first != __last; ++__first)
    insert(__position, *__first);
}

#else /* __STL_MEMBER_TEMPLATES */

// position前插入[first, last)
template <class _Tp, class _Alloc>
void list<_Tp, _Alloc>::insert(iterator __position,
                               const _Tp *__first, const _Tp *__last)
{
  for (; __first != __last; ++__first)
    insert(__position, *__first);
}

// position前插入[first, last)
template <class _Tp, class _Alloc>
void list<_Tp, _Alloc>::insert(iterator __position,
                               const_iterator __first, const_iterator __last)
{
  for (; __first != __last; ++__first)
    insert(__position, *__first);
}

#endif /* __STL_MEMBER_TEMPLATES */

// position前插入n个x
template <class _Tp, class _Alloc>
void list<_Tp, _Alloc>::_M_fill_insert(iterator __position,
                                       size_type __n, const _Tp &__x)
{
  for (; __n > 0; --__n)
    insert(__position, __x);
}

// 删除[first, last)
template <class _Tp, class _Alloc>
typename list<_Tp, _Alloc>::iterator list<_Tp, _Alloc>::erase(iterator __first,
                                                              iterator __last)
{
  while (__first != __last)
    erase(__first++);
  return __last;
}

// 重新设置大小
template <class _Tp, class _Alloc>
void list<_Tp, _Alloc>::resize(size_type __new_size, const _Tp &__x)
{
  iterator __i = begin();
  size_type __len = 0;
  for (; __i != end() && __len < __new_size; ++__i, ++__len)
    ;
  if (__len == __new_size)
    erase(__i, end());                      //如果当前大小大于新大小，删除多余元素
  else                                      // __i == end()
    insert(end(), __new_size - __len, __x); //尾部补充缺少元素
}

// 链表拷贝
template <class _Tp, class _Alloc>
list<_Tp, _Alloc> &list<_Tp, _Alloc>::operator=(const list<_Tp, _Alloc> &__x)
{
  if (this != &__x)
  {
    iterator __first1 = begin();
    iterator __last1 = end();
    const_iterator __first2 = __x.begin();
    const_iterator __last2 = __x.end();
    // 元素值拷贝
    while (__first1 != __last1 && __first2 != __last2)
      *__first1++ = *__first2++;
    if (__first2 == __last2)
      erase(__first1, __last1); //删除多余元素
    else
      insert(__last1, __first2, __last2); //补充插入剩下的元素
  }
  return *this;
}

// 链表重新赋值为n个val
template <class _Tp, class _Alloc>
void list<_Tp, _Alloc>::_M_fill_assign(size_type __n, const _Tp &__val)
{
  iterator __i = begin();
  for (; __i != end() && __n > 0; ++__i, --__n)
    *__i = __val;
  if (__n > 0)
    insert(end(), __n, __val); //补充插入
  else
    erase(__i, end()); //删除多余
}

#ifdef __STL_MEMBER_TEMPLATES

template <class _Tp, class _Alloc>
template <class _InputIter>
void list<_Tp, _Alloc>::_M_assign_dispatch(_InputIter __first2, _InputIter __last2,
                                           __false_type)
{
  iterator __first1 = begin();
  iterator __last1 = end();
  for (; __first1 != __last1 && __first2 != __last2; ++__first1, ++__first2)
    *__first1 = *__first2;
  if (__first2 == __last2)
    erase(__first1, __last1);
  else
    insert(__last1, __first2, __last2);
}

#endif /* __STL_MEMBER_TEMPLATES */

//删除value元素
template <class _Tp, class _Alloc>
void list<_Tp, _Alloc>::remove(const _Tp &__value)
{
  iterator __first = begin();
  iterator __last = end();
  while (__first != __last)
  {
    //先获取下一个迭代器
    iterator __next = __first;
    ++__next;
    //等于value就删除
    if (*__first == __value)
      erase(__first);
    //赋值为next进行下一轮循环
    __first = __next;
  }
}

//删除多余重复元素
template <class _Tp, class _Alloc>
void list<_Tp, _Alloc>::unique()
{
  iterator __first = begin();
  iterator __last = end();
  if (__first == __last)
    return;
  iterator __next = __first;
  while (++__next != __last)
  {
    if (*__first == *__next)
      erase(__next); // *first==*lext，有重复，删除next
    else
      __first = __next;
    __next = __first;
  }
}

//合并链表
template <class _Tp, class _Alloc>
void list<_Tp, _Alloc>::merge(list<_Tp, _Alloc> &__x)
{
  iterator __first1 = begin();
  iterator __last1 = end();
  iterator __first2 = __x.begin();
  iterator __last2 = __x.end();
  while (__first1 != __last1 && __first2 != __last2)
    if (*__first2 < *__first1)
    {
      //插入first2
      iterator __next = __first2;
      transfer(__first1, __first2, ++__next);
      __first2 = __next;
    }
    else
      // first1滑动一个位置
      ++__first1;
  if (__first2 != __last2)
    //插入剩余节点
    transfer(__last1, __first2, __last2);
}

//反转链表
inline void __List_base_reverse(_List_node_base *__p)
{
  _List_node_base *__tmp = __p;
  do
  {
    //循环交换prev和next
    __STD::swap(__tmp->_M_next, __tmp->_M_prev);
    __tmp = __tmp->_M_prev; // Old next node is now prev.
  } while (__tmp != __p);
}

//反转链表
template <class _Tp, class _Alloc>
inline void list<_Tp, _Alloc>::reverse()
{
  __List_base_reverse(this->_M_node);
}

//排序
template <class _Tp, class _Alloc>
void list<_Tp, _Alloc>::sort()
{
  // Do nothing if the list has length 0 or 1.
  if (_M_node->_M_next != _M_node && _M_node->_M_next->_M_next != _M_node)
  {
    list<_Tp, _Alloc> __carry;
    list<_Tp, _Alloc> __counter[64];
    int __fill = 0;
    while (!empty())
    {
      //移动第一个元素到临时链表开头
      __carry.splice(__carry.begin(), *this, begin());
      int __i = 0;
      while (__i < __fill && !__counter[__i].empty())
      {
        __counter[__i].merge(__carry);
        __carry.swap(__counter[__i++]);
      }
      __carry.swap(__counter[__i]);
      if (__i == __fill)
        ++__fill;
    }

    for (int __i = 1; __i < __fill; ++__i)
      __counter[__i].merge(__counter[__i - 1]);
    swap(__counter[__fill - 1]);
  }
}

#ifdef __STL_MEMBER_TEMPLATES

template <class _Tp, class _Alloc>
template <class _Predicate>
void list<_Tp, _Alloc>::remove_if(_Predicate __pred)
{
  iterator __first = begin();
  iterator __last = end();
  while (__first != __last)
  {
    iterator __next = __first;
    ++__next;
    if (__pred(*__first))
      erase(__first);
    __first = __next;
  }
}

template <class _Tp, class _Alloc>
template <class _BinaryPredicate>
void list<_Tp, _Alloc>::unique(_BinaryPredicate __binary_pred)
{
  iterator __first = begin();
  iterator __last = end();
  if (__first == __last)
    return;
  iterator __next = __first;
  while (++__next != __last)
  {
    if (__binary_pred(*__first, *__next))
      erase(__next);
    else
      __first = __next;
    __next = __first;
  }
}

template <class _Tp, class _Alloc>
template <class _StrictWeakOrdering>
void list<_Tp, _Alloc>::merge(list<_Tp, _Alloc> &__x,
                              _StrictWeakOrdering __comp)
{
  iterator __first1 = begin();
  iterator __last1 = end();
  iterator __first2 = __x.begin();
  iterator __last2 = __x.end();
  while (__first1 != __last1 && __first2 != __last2)
    if (__comp(*__first2, *__first1))
    {
      iterator __next = __first2;
      transfer(__first1, __first2, ++__next);
      __first2 = __next;
    }
    else
      ++__first1;
  if (__first2 != __last2)
    transfer(__last1, __first2, __last2);
}

template <class _Tp, class _Alloc>
template <class _StrictWeakOrdering>
void list<_Tp, _Alloc>::sort(_StrictWeakOrdering __comp)
{
  // Do nothing if the list has length 0 or 1.
  if (_M_node->_M_next != _M_node && _M_node->_M_next->_M_next != _M_node)
  {
    list<_Tp, _Alloc> __carry;
    list<_Tp, _Alloc> __counter[64];
    int __fill = 0;
    while (!empty())
    {
      __carry.splice(__carry.begin(), *this, begin());
      int __i = 0;
      while (__i < __fill && !__counter[__i].empty())
      {
        __counter[__i].merge(__carry, __comp);
        __carry.swap(__counter[__i++]);
      }
      __carry.swap(__counter[__i]);
      if (__i == __fill)
        ++__fill;
    }

    for (int __i = 1; __i < __fill; ++__i)
      __counter[__i].merge(__counter[__i - 1], __comp);
    swap(__counter[__fill - 1]);
  }
}

#endif /* __STL_MEMBER_TEMPLATES */

#if defined(__sgi) && !defined(__GNUC__) && (_MIPS_SIM != _MIPS_SIM_ABI32)
#pragma reset woff 1174
#pragma reset woff 1375
#endif

__STL_END_NAMESPACE

#endif /* __SGI_STL_INTERNAL_LIST_H */

// Local Variables:
// mode:C++
// End:
