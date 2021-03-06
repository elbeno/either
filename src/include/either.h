#pragma once

#include "function_traits.h"

//------------------------------------------------------------------------------
// The either monad

template <typename Left, typename Right>
struct Either
{
  using L = Left;
  using R = Right;

  // copy construct from a right value
  explicit Either(const R& r)
    noexcept(std::is_nothrow_copy_constructible<R>())
    : m_tag(Tag::RIGHT)
    , m_right(r)
  {}

  // move construct from a right value
  explicit Either(R&& r)
    noexcept(std::is_nothrow_move_constructible<R>())
    : m_tag(Tag::RIGHT)
    , m_right(std::move(r))
  {}

  // copy construct from a left value
  Either(const L& l, bool)
    noexcept(std::is_nothrow_copy_constructible<L>())
    : m_tag(Tag::LEFT)
    , m_left(l)
  {}

  // move construct from a left value
  Either(L&& l, bool)
    noexcept(std::is_nothrow_move_constructible<L>())
    : m_tag(Tag::LEFT)
    , m_left(std::move(l))
  {}

  // copy constructor
  Either(const Either& other)
    noexcept(std::is_nothrow_copy_constructible<L>() &&
             std::is_nothrow_copy_constructible<R>())
    : m_tag(other.m_tag)
  {
    if (other.isRight())
      new (&m_right) R(other.m_right);
    else
      new (&m_left) L(other.m_left);
  }

  // move constructor
  Either(const Either&& other)
    noexcept(std::is_nothrow_move_constructible<L>() &&
             std::is_nothrow_move_constructible<R>())
    : m_tag(other.m_tag)
  {
    if (other.isRight())
      new (&m_right) R(std::move(other.m_right));
    else
      new (&m_left) L(std::move(other.m_left));
  }

  // copy assignment
  Either& operator=(const Either& other)
    noexcept(std::is_nothrow_copy_assignable<L>() &&
             std::is_nothrow_copy_assignable<R>() &&
             std::is_nothrow_copy_constructible<L>() &&
             std::is_nothrow_copy_constructible<R>())
  {
    // if the tags match, a plain copy of the data member
    if (isRight() == other.isRight())
    {
      if (isRight())
        m_right = other.m_right;
      else
        m_left = other.m_left;
      return *this;
    }

    // explicit deletion
    if (isRight())
      m_right.~R();
    else
      m_left.~L();

    // placement new
    m_tag = other.m_tag;
    if (isRight())
      new (&m_right) R(other.m_right);
    else
      new (&m_left) L(other.m_left);
    return *this;
  }

  // move assignment
  Either& operator=(Either&& other)
    noexcept(std::is_nothrow_move_assignable<L>() &&
             std::is_nothrow_move_assignable<R>() &&
             std::is_nothrow_move_constructible<L>() &&
             std::is_nothrow_move_constructible<R>())
  {
    // if the tags match, a plain copy of the data member
    if (isRight() == other.isRight())
    {
      if (isRight())
        m_right = std::move(other.m_right);
      else
        m_left = std::move(other.m_left);
      return *this;
    }

    // explicit deletion
    if (isRight())
      m_right.~R();
    else
      m_left.~L();

    // placement new
    m_tag = other.m_tag;
    if (isRight())
      new (&m_right) R(std::move(other.m_right));
    else
      new (&m_left) L(std::move(other.m_left));
    return *this;
  }

  ~Either()
  {
    if (isRight())
      m_right.~R();
    else
      m_left.~L();
  }

  enum class Tag { LEFT, RIGHT } m_tag;
  union
  {
    L m_left;
    R m_right;
  };

  bool isRight() const { return m_tag == Tag::RIGHT; }
};

//------------------------------------------------------------------------------
// equality

template<typename L, typename R>
bool operator==(const Either<L, R>& a, const Either<L, R>& b)
{
  return a.isRight() == b.isRight()
    && (a.isRight()
        ? a.m_right == b.m_right
        : a.m_left == b.m_left);
}

template<typename L, typename R>
bool operator!=(const Either<L, R>& a, const Either<L, R>& b)
{
  return !(a == b);
}

//------------------------------------------------------------------------------
// functor and monad functions

namespace either
{
  template <typename A, typename F>
  inline Either<A, typename function_traits<F>::returnType> fmap(
      const F& f,
      const Either<A, typename function_traits<F>::template Arg<0>::bareType>& e)
  {
    using C = typename function_traits<F>::returnType;

    if (!e.isRight())
      return Either<A, C>(e.m_left, true);
    return Either<A, C>(f(e.m_right));
  }

  template <typename A, typename F>
  inline typename function_traits<F>::returnType bind(
      const F& f,
      const Either<A, typename function_traits<F>::template Arg<0>::bareType>& e)
  {
    using C = typename function_traits<F>::returnType;

    if (!e.isRight())
      return Either<A, C>(e.m_left, true);
    return f(e.m_right);
  }

  template <typename A, typename B>
  inline Either<A, B> pure(B&& b)
  {
    return Either<A, B>(std::forward<B>(b));
  }
}

//------------------------------------------------------------------------------
// sugar operators

template <typename A, typename F>
inline typename function_traits<F>::returnType operator>=(
    Either<A, typename function_traits<F>::template Arg<0>::bareType>&& e,
    F&& f)
{
  return either::bind(
      std::forward<F>(f),
      std::forward<Either<A, typename function_traits<F>::template Arg<0>::bareType>>(e));
}

template <typename A, typename B, typename F>
inline typename function_traits<F>::returnType operator>(
    Either<A,B>&& e, const F& f)
{
  using C = typename function_traits<F>::returnType;

  if (!e.isRight())
    return Either<A, C>(std::forward<A>(e.m_left), true);
  return f();
}
