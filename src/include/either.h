#pragma once

#include "function_traits.h"
#include <ostream>

//------------------------------------------------------------------------------
// The either monad

template <typename Left, typename Right>
struct Either
{
  typedef Left L;
  typedef Right R;

  explicit Either(const R& r)
    : m_tag(Tag::RIGHT)
    , m_right(r)
  {}

  Either(const L& l, bool)
    : m_tag(Tag::LEFT)
    , m_left(l)
  {}

  Either(const Either& other)
    : m_tag(other.m_tag)
  {
    if (other.isRight())
      new (&m_right) R(other.m_right);
    else
      new (&m_left) L(other.m_left);
  }

  Either& operator=(const Either& other)
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
// normal C++ things: output, equality

template <typename L, typename R>
std::ostream& operator<<(std::ostream& s, const Either<L, R>& e)
{
  if (e.isRight())
    s << "Right:" << e.m_right;
  else
    s << "Left:" << e.m_left;
  return s;
}

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
  Either<A, typename function_traits<F>::returnType> fmap(
      const F& f,
      const Either<A, typename function_traits<F>::template Arg<0>::bareType>& e)
  {
    typedef typename function_traits<F>::returnType C;

    if (!e.isRight())
      return Either<A, C>(e.m_left, true);
    return Either<A, C>(f(e.m_right));
  }

  template <typename A, typename B>
  Either<A, B> mjoin(const Either<A, Either<A, B>>& e)
  {
    if (!e.isRight())
      return Either<A, B>(e.m_left, true);
    return e.m_right;
  }

  template <typename A, typename F>
  typename function_traits<F>::returnType mbind(
      const F& f,
      const Either<A, typename function_traits<F>::template Arg<0>::bareType>& e)
  {
    return mjoin(fmap(f, e));
  }

  template <typename A, typename B>
  Either<A, B> mreturn(B b)
  {
    return Either<A, B>(b);
  }
}

//------------------------------------------------------------------------------
// sugar operators

// unfortunately in c++, >>= is right associative, so in order to chain binds
// without parens, we need an alternative operator

template <typename A, typename F>
typename function_traits<F>::returnType operator>=(
    const Either<A, typename function_traits<F>::template Arg<0>::bareType>& e,
    const F& f)
{
  return either::mbind(f, e);
}

template <typename A, typename B, typename F>
typename function_traits<F>::returnType operator>(
    const Either<A,B>&, const F& f)
{
  return f();
}
