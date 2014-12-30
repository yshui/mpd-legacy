#pragma once

/* We only support for compiling with a compiler that
 * supports cleanup attribute, because `automatic'
 * reference count rely on it.
 */
#define Y_CLEANUP(x) __attribute__((cleanup(x)))

#if __STDC_VERSION__ >= 201112L
 #define __C11__
 #define Y_NORETURN _Noreturn
 #define Y_CTASSERT(x, y) static_assert(x, y)
 #define Y_CTASSERT_GLOBAL(x, y) static_assert(x, y)
#elif defined(__GNUC__)
 #define Y_NORETURN __attribute__((__noreturn__))
 #define Y_CTASSERT(x, y) static_assert(x, y)
 #define Y_CTASSERT_GLOBAL(x, y) static_assert(x, y)
#else
 #define Y_NORETURN
 #define __CTASSERT(x, y) \
	 typedef char __attribute__((unused)) \
	 __compile_time_assertion__ ## y[(x) ? 1 : -1]
 #define Y_CTASSERT(x, y) {__CTASSERT(x ,y);}
 #define Y_CTASSERT_GLOBAL(x, y) __CTASSERT(x, y)
#endif

#if defined(__GNUC__) || defined(__clang__)
# define Y_PURE __attribute__((__pure__))
# define Y_MALLOC __attribute__((__malloc__))
# define Y_CONST __attribute__((__const__))

# define likely(expr) (__builtin_expect (!!(expr), 1))
# define unlikely(expr) (__builtin_expect (!!(expr), 0))
#else
# define Y_PURE
# define Y_MALLOC
# define Y_CONST
# define likely(expr) (expr)
# define unlikely(expr) (expr)
#endif

#define GCC_CHECK_VERSION(major, minor) \
       (defined(__GNUC__) && \
        (__GNUC__ > (major) || \
         (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor))))

#ifndef __has_feature
# define __has_feature(x) 0
#endif
#ifndef __has_include
# define __has_include(x) 0
#endif
