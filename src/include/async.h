#pragma once

#include "either.h"
#include "function_traits.h"

#include <functional>
#include <memory>
#include <mutex>
#include <ostream>
#include <utility>

//------------------------------------------------------------------------------
// The async monad

//------------------------------------------------------------------------------
// Simple type representing an asynchronous value which can be retrieved by
// passing a continuation to receive it.

template <typename T>
struct Continuation
{
  using type = std::function<void (typename std::decay<T>::type)>;
};

template <>
struct Continuation<void>
{
  using type = std::function<void ()>;
};

template <typename T>
using Async = std::function<void (typename Continuation<T>::type)>;

namespace async
{

  template <typename T>
  struct FromAsync
  {
  };

  template <typename T>
  struct FromAsync<T&> : public FromAsync<T>
  {
  };

  template <typename T>
  struct FromAsync<std::function<void (std::function<void (T)>)>>
  {
    using type = T;
  };

  template <>
  struct FromAsync<std::function<void (std::function<void ()>)>>
  {
    using type = void;
  };

  // Lift a value into an async context: just call the continuation with the
  // captured value.
  // a -> m a
  template <typename A>
  inline Async<A> pure(A&& a)
  {
    return [a1 = std::forward<A>(a)]
      (typename Continuation<A>::type cont) mutable
    {
      cont(std::move(a1));
    };
  }

  // Fmap a function into an async context: the new async will pass the existing
  // async a continuation that calls the new continuation with the result of
  // calling the function.
  // (a -> b) -> m a -> m b
  template <typename F,
            typename AA = typename Async<
              typename function_traits<F>::template Arg<0>::bareType>
            ::type>
  inline Async<typename function_traits<F>::appliedType> fmap(
      F&& f, AA&& aa)
  {
    using A = typename function_traits<F>::template Arg<0>::bareType;
    using B = typename function_traits<F>::appliedType;
    using C = typename Continuation<B>::type;

    return [f1 = std::forward<F>(f), aa1 = std::forward<AA>(aa)]
      (C&& cont)
    {
      aa1([c = std::forward<C>(cont), f2 = std::move(f1)] (A&& a) {
          c(function_traits<F>::apply(std::move(f2), std::forward<A>(a)));
        });
    };
  }

  // Apply an async function to an async argument: this is more involved. We
  // need to call each async, passing a continuation that stores its argument if
  // the other one isn't present, otherwise applies the function and calls the
  // new continuation with the result.
  // m (a -> b) -> m a -> m b
  template <typename AF,
            typename AA = typename Async<
              typename function_traits<
                typename FromAsync<AF>::type>
              ::template Arg<0>::bareType>
            ::type,
            typename B = typename function_traits<
              typename FromAsync<AF>::type>
            ::appliedType>
  inline Async<B> apply(AF&& af, AA&& aa)
  {
    using A = typename FromAsync<AA>::type;
    using F = typename FromAsync<AF>::type;
    using C = typename Continuation<B>::type;

    struct Data
    {
      std::unique_ptr<F> pf;
      std::unique_ptr<A> pa;
      std::mutex m;
    };
    std::shared_ptr<Data> pData = std::make_shared<Data>();

    return [pData, af1 = std::forward<AF>(af), aa1 = std::forward<AA>(aa)]
      (C&& cont)
    {
      af1([pData, cont] (F&& f) {
          bool have_a = false;
          {
            // if we don't have a already, store f
            std::lock_guard<std::mutex> g(pData->m);
            have_a = static_cast<bool>(pData->pa);
            if (!have_a)
              pData->pf = std::make_unique<F>(std::forward<F>(f));
          }
          // if we have both sides, call the continuation and we're done
          if (have_a)
            cont(function_traits<F>::apply(std::forward<F>(f), std::move(*pData->pa)));
        });

      aa1([pData, c = std::forward<C>(cont)] (A&& a) {
          bool have_f = false;
          {
            // if we don't have f already, store a
            std::lock_guard<std::mutex> g(pData->m);
            have_f = static_cast<bool>(pData->pf);
            if (!have_f)
              pData->pa = std::make_unique<A>(std::forward<A>(a));
          }
          // if we have both sides, call the continuation and we're done
          if (have_f)
            c(function_traits<F>::apply(std::move(*pData->pf), std::forward<A>(a)));
        });
    };
  }

  // Bind an async value to a function returning async. We need to call the
  // async, passing a continuation that calls the function on the argument, then
  // passes the new continuation to that async value. A can't be void here; use
  // sequence instead for that case.
  // m a -> (a -> m b) - > m b
  template <typename F, typename AA,
            typename AB = typename function_traits<F>::returnType>
  inline AB bind(AA&& aa, F&& f)
  {
    using A = typename async::FromAsync<AA>::type;
    using C = typename function_traits<AB>::template Arg<0>::bareType;
    return [f1 = std::forward<F>(f), aa1 = std::forward<AA>(aa)]
      (C&& cont)
    {
      aa1([c = std::forward<C>(cont), f2 = std::move(f1)] (A&& a) {
          f2(std::forward<A>(a))(c); });
    };
  }

  // Sequence is like bind, but it drops the result of the first async. We need
  // to deal separately with Async<void> and Async<T>.
  // m a -> m b -> m b
  template <typename F, typename AA, typename A>
  struct sequence
  {
    using AB = typename function_traits<F>::returnType;
    inline AB operator()(AA&& aa, F&& f)
    {
      using C = typename function_traits<AB>::template Arg<0>::bareType;
      return [f1 = std::forward<F>(f), aa1 = std::forward<Async<A>>(aa)]
        (C&& cont)
      {
        aa1([c = std::forward<C>(cont), f2 = std::move(f1)] (A&&) {
            f2()(c); });
      };
    }
  };

  template <typename F, typename AA>
  struct sequence<F, AA, void>
  {
    using AB = typename function_traits<F>::returnType;
    inline AB operator()(AA&& aa, F&& f)
    {
      using C = typename function_traits<AB>::template Arg<0>::bareType;
      return [f1 = std::forward<F>(f), aa1 = std::forward<Async<void>>(aa)]
        (C&& cont)
      {
        aa1([c = std::forward<C>(cont), f2 = std::move(f1)] () {
            f2()(c); });
      };
    }
  };

  // For use in zero, pair and Either.
  struct Void {};
  std::ostream& operator<<(std::ostream& s, const Void&)
  {
    return s << "(void)";
  }

  // Convert an Async<void> to an Async<Void>. Useful for using Async<void> with
  // && and || operators.
  template <typename T>
  struct IgnoreVoid
  {
    using type = T;
  };

  template <>
  struct IgnoreVoid<void>
  {
    using type = Void;
  };

  template <typename AT>
  inline Async<Void> ignore(AT&& at)
  {
    using C = typename Continuation<Void>::type;
    return [at1 = std::forward<AT>(at)]
      (C&& cont)
    {
      at1([c = std::forward<C>(cont)] () {
          c(Void()); });
    };
  }

  template <typename F,
            typename AA = typename Async<
              typename function_traits<F>::template Arg<0>::bareType>
            ::type,
            typename AB = typename Async<
              typename function_traits<F>::template Arg<1>::bareType>
            ::type,
            typename C = typename function_traits<F>::returnType>
  inline Async<C> concurrently(AA&& aa, AB&& ab, F&& f)
  {
    return apply(fmap(std::forward<F>(f), std::forward<AA>(aa)), std::forward<AB>(ab));
  }

  template <typename F, typename AA, typename AB, typename A, typename B>
  struct runConcurrently
  {
    using C = typename function_traits<F>::returnType;
    inline Async<C> operator()(AA&& aa, AB&& ab, F&& f)
    {
      return concurrently<F>(std::forward<AA>(aa),
                             std::forward<AB>(ab),
                             std::forward<F>(f));
    }
  };

  template <typename F, typename AA, typename AB, typename A>
  struct runConcurrently<F, AA, AB, A, void>
  {
    using C = typename function_traits<F>::returnType;
    inline Async<C> operator()(AA&& aa, AB&& ab, F&& f)
    {
      return concurrently<F>(std::forward<AA>(aa),
                             ignore(std::forward<AB>(ab)),
                             std::forward<F>(f));
    }
  };

  template <typename F, typename AA, typename AB, typename B>
  struct runConcurrently<F, AA, AB, void, B>
  {
    using C = typename function_traits<F>::returnType;
    inline Async<C> operator()(AA&& aa, AB&& ab, F&& f)
    {
      return concurrently<F>(ignore(std::forward<AA>(aa)),
                             std::forward<AB>(ab),
                             std::forward<F>(f));
    }
  };

  template <typename F, typename AA, typename AB>
  struct runConcurrently<F, AA, AB, void, void>
  {
    using C = typename function_traits<F>::returnType;
    inline Async<C> operator()(AA&& aa, AB&& ab, F&& f)
    {
      return concurrently<F>(ignore(std::forward<AA>(aa)),
                             ignore(std::forward<AB>(ab)),
                             std::forward<F>(f));
    }
  };

  // The zero element of the Async monoid. It never calls its continuation.
  template <typename T = Void>
  inline Async<T> zero()
  {
    return [] (typename Continuation<T>::type) {};
  }

  // Race two Asyncs: call the continuation with the result of the first one
  // that completes. Problem: how to cancel the other one, how to clean up in
  // case of ORing with zero.
  template <typename AA, typename AB,
            typename A = typename FromAsync<AA>::type,
            typename B = typename FromAsync<AB>::type>
  inline Async<Either<A,B>> race(AA&& aa, AB&& ab)
  {
    struct Data
    {
      Data() : done(false) {}
      bool done;
      std::mutex m;
    };
    std::shared_ptr<Data> pData = std::make_shared<Data>();

    using C = typename Continuation<Either<A,B>>::type;
    return [pData, aa1 = std::forward<AA>(aa), ab1 = std::forward<AB>(ab)]
      (C&& cont)
    {
      aa1([pData, c = std::forward<C>(cont)] (A&& a) {
          bool done = false;
          {
            std::lock_guard<std::mutex> g(pData->m);
            done = pData->done;
            pData->done = true;
          }
          if (!done)
            c(Either<A,B>(std::forward<A>(a), true));
        });

      ab1([pData, c = std::forward<C>(cont)] (B&& b) {
          bool done = false;
          {
            std::lock_guard<std::mutex> g(pData->m);
            done = pData->done;
            pData->done = true;
          }
          if (!done)
            c(Either<A,B>(std::forward<B>(b)));
        });
    };
  }

  template <typename AA, typename AB, typename A, typename B>
  struct runRace
  {
    inline Async<Either<A,B>> operator()(AA&& aa, AB&& ab)
    {
      return race<AA, AB>(
          std::forward<AA>(aa), std::forward<AB>(ab));
    }
  };

  template <typename AA, typename AB, typename A>
  struct runRace<AA, AB, A, void>
  {
    inline Async<Either<A,Void>> operator()(AA&& aa, AB&& ab)
    {
      return race<AA, Async<Void>>(
          std::forward<AA>(aa), ignore(std::forward<AB>(ab)));
    }
  };

  template <typename AA, typename AB, typename B>
  struct runRace<AA, AB, void, B>
  {
    inline Async<Either<Void, B>> operator()(AA&& aa, AB&& ab)
    {
      return race<Async<Void>, AB>(
          ignore(std::forward<AA>(aa)), std::forward<AB>(ab));
    }
  };

  template <typename AA, typename AB>
  struct runRace<AA, AB, void, void>
  {
    inline Async<Either<Void, Void>> operator()(AA&& aa, AB&& ab)
    {
      return race<Async<Void>, Async<Void>>(
          ignore(std::forward<AA>(aa)), ignore(std::forward<AB>(ab)));
    }
  };
}

// Syntactic sugar: >= is Haskell's >>=, and > is Haskell's >>.

template <typename F, typename AA,
          typename A = typename async::FromAsync<AA>::type,
          typename AB = typename function_traits<F>::returnType>
inline AB operator>=(AA&& a, F&& f)
{
  return async::bind<F,AA>(std::forward<AA>(a),
                           std::forward<F>(f));
}

template <typename F, typename AA,
          typename A = typename async::FromAsync<AA>::type,
          typename AB = typename function_traits<F>::returnType>
inline AB operator>(AA&& a, F&& f)
{
  return async::sequence<F,AA,A>()(std::forward<AA>(a),
                                   std::forward<F>(f));
}

// The behaviour of && is to return the pair of results. If one of the operands
// is Async<void>, it is converted to Async<Void> for the purposes of making a
// pair.

template <typename AA, typename AB,
          typename A = typename async::IgnoreVoid<typename async::FromAsync<AA>::type>::type,
          typename B = typename async::IgnoreVoid<typename async::FromAsync<AB>::type>::type>
inline Async<std::pair<A,B>> operator&&(AA&& a, AB&& b)
{
  using F = decltype(std::make_pair<A,B>);
  using RA = typename async::FromAsync<AA>::type;
  using RB = typename async::FromAsync<AB>::type;
  return async::runConcurrently<F,AA,AB,RA,RB>()(
      std::forward<AA>(a), std::forward<AB>(b), std::make_pair<A,B>);
}

// The behaviour of || is to return the result of whichever Async completed
// first.

template <typename AA, typename AB,
          typename A = typename async::IgnoreVoid<typename async::FromAsync<AA>::type>::type,
          typename B = typename async::IgnoreVoid<typename async::FromAsync<AB>::type>::type>
inline Async<Either<A,B>> operator||(AA&& a, AB&& b)
{
  using RA = typename async::FromAsync<AA>::type;
  using RB = typename async::FromAsync<AB>::type;
  return async::runRace<AA,AB,RA,RB>()(std::forward<AA>(a),
                                       std::forward<AB>(b));
}
