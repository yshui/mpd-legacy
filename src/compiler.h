#pragma once

// Use 'auto' keyword for type inference, because we
// are not using it anyway
#define auto __auto_type

#ifndef __has_feature
# define __has_feature(x) 0
#endif

#if defined(__GNUC__) || defined(__clang__)
# define MPD_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
# define MPD_PURE __attribute__((__pure__))
# define MPD_MALLOC __attribute__((__malloc__))
# define MPD_CONST __attribute__((__const__))
# if defined(__GNUC__)
#  define MPD_FORCE
# else
#  define MPD_FORCE
# endif

# define likely(expr) (__builtin_expect (!!(expr), 1))
# define unlikely(expr) (__builtin_expect (!!(expr), 0))
#else
# define MPD_PURE
# define MPD_MALLOC
# define MPD_CONST
#endif

#define GCC_CHECK_VERSION(major, minor) \
       (defined(__GNUC__) && \
        (__GNUC__ > (major) || \
         (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor))))

#if GCC_CHECK_VERSION(2, 8)
#  define GNUC_EXT __extension__
#else
#  define GNUC_EXT
#endif

