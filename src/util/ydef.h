#pragma once

/* Platform macros, ported from tinycthread */

#if !defined(_Y_PLATFORM_DEFINED_)
  #if defined(_WIN32) || defined(__WIN32__) || defined(__WINDOWS__)
    #define _Y_WIN32_
  #else
    #define _Y_POSIX_
  #endif
  #define _Y_PLATFORM_DEFINED_
#endif

/* Some common util macros */

#define talloc(nmemb, type) ((type *)calloc(nmemb, sizeof(type)))

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
