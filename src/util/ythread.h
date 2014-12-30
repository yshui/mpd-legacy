/* -*- mode: c; tab-width: 2; indent-tabs-mode: nil; -*-
Copyright (c) 2012 Marcus Geelnard
Copyright (c) 2013-2014 Evan Nemerson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "util/ydef.h"
#include "compiler.h"

/* Platform specific includes */
#if defined(_Y_POSIX_)
 #include <signal.h>
 #include <sched.h>
 #include <unistd.h>
 #include <sys/time.h>
 #include <errno.h>
 #include <pthread.h>
#elif defined(_Y_WIN32_)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #define __UNDEF_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <process.h>
 #include <sys/timeb.h>
#endif

/* Activate some POSIX functionality (e.g. clock_gettime and recursive mutexes) */
#if defined(_Y_POSIX_)
 #undef _FEATURES_H
 #if !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
 #endif
 #if !defined(_POSIX_C_SOURCE) || ((_POSIX_C_SOURCE - 0) < 199309L)
  #undef _POSIX_C_SOURCE
  #define _POSIX_C_SOURCE 199309L
 #endif
 #if !defined(_XOPEN_SOURCE) || ((_XOPEN_SOURCE - 0) < 500)
  #undef _XOPEN_SOURCE
  #define _XOPEN_SOURCE 500
 #endif
#endif

#ifndef TIME_UTC
#define TIME_UTC 1
#define _Y_EMULATE_TIMESPEC_GET_

#if defined(_Y_WIN32_)
struct _ythread_timespec {
  time_t tv_sec;
  long   tv_nsec;
};
#define timespec _ythread_timespec
#endif

static inline int
_ythread_timespec_get(struct timespec *ts, int base) {
#if defined(_Y_WIN32_)
	struct _timeb tb;
#else
	struct timeval tv;
#endif

	if (base != TIME_UTC)
		return 0;

#if defined(_Y_WIN32_)
	_ftime(&tb);
	ts->tv_sec = (time_t)tb.time;
	ts->tv_nsec = 1000000L * (long)tb.millitm;
#elif defined(CLOCK_REALTIME)
	return (clock_gettime(CLOCK_REALTIME, ts) == 0) ? base : 0;
#else
	gettimeofday(&tv, NULL);
	ts->tv_sec = (time_t)tv.tv_sec;
	ts->tv_nsec = 1000L * (long)tv.tv_usec;
#endif

	return base;
}
#define timespec_get _ythread_timespec_get
#endif

/**
* @def _Thread_local
* Thread local storage keyword.
* A variable that is declared with the @c _Thread_local keyword makes the
* value of the variable local to each thread (known as thread-local storage,
* or TLS). Example usage:
* @code
* // This variable is local to each thread.
* _Thread_local int variable;
* @endcode
* @note The @c _Thread_local keyword is a macro that maps to the corresponding
* compiler directive (e.g. @c __declspec(thread)).
* @note This directive is currently not supported on Mac OS X (it will give
* a compiler error), since compile-time TLS is not supported in the Mac OS X
* executable format. Also, some older versions of MinGW (before GCC 4.x) do
* not support this directive, nor does the Tiny C Compiler.
* @hideinitializer
*/

#if !(defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201102L)) && !defined(_Thread_local)
 #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
  #define _Thread_local __thread
 #else
  #define _Thread_local __declspec(thread)
 #endif
#elif defined(__GNUC__) && defined(__GNUC_MINOR__) && (((__GNUC__ << 8) | __GNUC_MINOR__) < ((4 << 8) | 9))
 #define _Thread_local __thread
#endif

/* Macros */
#if defined(_Y_WIN32_)
 #define TSS_DTOR_ITERATIONS (4)
#else
 #define TSS_DTOR_ITERATIONS PTHREAD_DESTRUCTOR_ITERATIONS
#endif

/* Mutex */
#if defined(_Y_WIN32_)
typedef struct {
	union {
		CRITICAL_SECTION cs;      /* Critical section handle (used for non-timed mutexes) */
		HANDLE mut;               /* Mutex handle (used for timed mutex) */
	} mHandle;                  /* Mutex handle */
	int mLockedBy;         /* TRUE if the mutex is already locked */
	int mRecursive;             /* TRUE if the mutex is recursive */
	int mTimed;                 /* TRUE if the mutex is timed */
} mtx_t;
Y_CTASSERT_GLOBAL(WAIT_OBJECT_0 == 0, "WAIT_OBJECT_0 != 0");
#else
typedef pthread_mutex_t mtx_t;
#endif

enum {
	thrd_success = 0,
	thrd_interrupt = -1,
	thrd_nomem = -2,
	thrd_timedout = -3,
	thrd_busy = -4,
	thrd_error = -5
};

#if defined(_Y_WIN32_)
static inline int _ythread_err_map(int err) {
	switch(err) {
		case WAIT_OBJECT_0:
			return thrd_success;
		case WAIT_TIMEOUT:
			return thrd_timedout;
		case WAIT_ABANDONED:
		case WAIT_FAILED:
		default:
			return thrd_error;
	}
}

static inline void
_ythread_mtx_deadlock(mtx_t *mtx) {
	//Emulate a deadlock
	while(mtx->mLockedBy == GetCurrentThreadId())
		Sleep(1);
}

#else
static inline int _ythread_err_map(int err) {
	switch(err) {
		case 0:
			return thrd_success;
		case ETIMEDOUT:
			return thrd_timedout;
		case EBUSY:
			return thrd_busy;
		case ENOMEM:
			return thrd_nomem;
		case EINTR:
			return thrd_interrupt;
		case EOWNERDEAD:
		default:
			return thrd_error;
	}
}
#endif

/* Mutex types */
#define mtx_plain     0
#define mtx_timed     1
#define mtx_recursive 2

static inline int
mtx_init(mtx_t *mtx, int type) {
#if defined(_Y_WIN32_)
	mtx->mAlreadyLocked = false;
	mtx->mRecursive = type & mtx_recursive;
	mtx->mTimed = type & mtx_timed;
	if (!mtx->mTimed) {
		InitializeCriticalSection(&(mtx->mHandle.cs));
	} else {
		mtx->mHandle.mut = CreateMutex(NULL, FALSE, NULL);
		if (mtx->mHandle.mut == NULL)
			return thrd_nomem;
	}
	return thrd_success;
#else
	int ret;
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	if (type & mtx_recursive)
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	ret = pthread_mutex_init(mtx, &attr);
	pthread_mutexattr_destroy(&attr);
	return _ythread_err_map(ret);
#endif
}

static inline void
mtx_destroy(mtx_t *mtx) {
#if defined(_Y_WIN32_)
	if (!mtx->mTimed)
		DeleteCriticalSection(&(mtx->mHandle.cs));
	else
		CloseHandle(mtx->mHandle.mut);
#else
	pthread_mutex_destroy(mtx);
#endif
}

static inline int
mtx_lock(mtx_t *mtx) {
#if defined(_Y_WIN32_)
	int ret = 0;
	if (!mtx->mRecursive && mtx->mLockedBy == GetCurrentThreadId())
		_ythread_mtx_deadlock(mtx);
	if (!mtx->mTimed)
		EnterCriticalSection(&(mtx->mHandle.cs));
	else
		ret = WaitForSingleObject(mtx->mHandle.mut, INFINITE));
	if (!ret)
		mtx->mLockedBy = GetCurrentThreadId();
	return _ythread_err_map(ret);
#else
	return _ythread_err_map(pthread_mutex_lock(mtx));
#endif
}

static inline int
mtx_timedlock(mtx_t *mtx, const struct timespec *ts) {
	int ret;
#if defined(_Y_WIN32_)
	struct timespec current_ts;
	DWORD timeoutMs;

	if (!mtx->mTimed)
		return thrd_error;
	if (!mtx->mRecursive && mtx->mLockedBy == GetCurrentThreadId())
		_ythread_mtx_deadlock(mtx);

	timespec_get(&current_ts, TIME_UTC);

	if ((current_ts.tv_sec > ts->tv_sec) ||
	    ((current_ts.tv_sec == ts->tv_sec) &&
	    (current_ts.tv_nsec >= ts->tv_nsec)))
		timeoutMs = 0;
	else {
		timeoutMs  = (ts->tv_sec  - current_ts.tv_sec)  * 1000;
		timeoutMs += (ts->tv_nsec - current_ts.tv_nsec) / 1000000;
		timeoutMs += 1;
	}

	/* TODO: the timeout for WaitForSingleObject doesn't include time
	   while the computer is asleep. */
	ret = WaitForSingleObject(mtx->mHandle.mut, timeoutMs);
	if (!ret)
		mtx->mLockedBy = GetCurrentThreadId();
	return _ythread_err_map(ret);
#elif defined(_POSIX_TIMEOUTS) && (_POSIX_TIMEOUTS >= 200112L) && defined(_POSIX_THREADS) && (_POSIX_THREADS >= 200112L)
	return _ythread_err_map(pthread_mutex_timedlock(mtx, ts));
#else
	struct timespec cur, dur;

	/* Try to acquire the lock and, if we fail, sleep for 5ms. */
	while ((ret = pthread_mutex_trylock (mtx)) == EBUSY) {
		timespec_get(&cur, TIME_UTC);

		if ((cur.tv_sec > ts->tv_sec) || ((cur.tv_sec == ts->tv_sec) &&
		    (cur.tv_nsec >= ts->tv_nsec)))
			return thrd_timedout;

		dur.tv_sec = ts->tv_sec - cur.tv_sec;
		dur.tv_nsec = ts->tv_nsec - cur.tv_nsec;
		if (dur.tv_nsec < 0) {
			dur.tv_sec--;
			dur.tv_nsec += 1000000000;
		}

		if ((dur.tv_sec != 0) || (dur.tv_nsec > 5000000)) {
			dur.tv_sec = 0;
			dur.tv_nsec = 5000000;
		}

		nanosleep(&dur, NULL);
	}

	return _ythread_err_map(ret);
#endif
}

static inline int
mtx_trylock(mtx_t *mtx)
{
#if defined(_Y_WIN32_)
	int ret;

	if (!mtx->mRecursive && mtx->mLockedBy == GetCurrentThreadId())
		return thrd_busy;

	if (!mtx->mTimed)
		ret = TryEnterCriticalSection(&(mtx->mHandle.cs));
	else
		ret = WaitForSingleObject(mtx->mHandle.mut, 0);

	if (!ret) {
		mtx->mLockedBy = GetCurrentThreadId();
		return thrd_success;
	}
	if (!mtx->mTimed)
		return thrd_busy;
	else
		return _ythread_err_map(ret);
#else
	return _ythread_err_map(pthread_mutex_trylock(mtx));
#endif
}

static inline int
mtx_unlock(mtx_t *mtx) {
#if defined(_Y_WIN32_)
	mtx->mLockedBy = 0;
	if (!mtx->mTimed)
		LeaveCriticalSection(&(mtx->mHandle.cs));
	else
		if (!ReleaseMutex(mtx->mHandle.mut))
			return thrd_error;
	return thrd_success;
#else
	return _ythread_err_map(pthread_mutex_unlock(mtx));
#endif
}

#if defined(_Y_WIN32_)
#define _CONDITION_EVENT_ONE 0
#define _CONDITION_EVENT_ALL 1
#endif

/* Condition variable */
#if defined(_Y_WIN32_)
typedef struct {
	HANDLE mEvents[2];                  /* Signal and broadcast event HANDLEs. */
	unsigned int mWaitersCount;         /* Count of the number of waiters. */
	CRITICAL_SECTION mWaitersCountLock; /* Serialize access to mWaitersCount. */
} cnd_t;
#else
typedef pthread_cond_t cnd_t;
#endif

static inline int
cnd_init(cnd_t *cond) {
#if defined(_Y_WIN32_)
	cond->mWaitersCount = 0;

	/* Init critical section */
	InitializeCriticalSection(&cond->mWaitersCountLock);

	/* Init events */
	cond->mEvents[_CONDITION_EVENT_ONE] = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (cond->mEvents[_CONDITION_EVENT_ONE] == NULL) {
		cond->mEvents[_CONDITION_EVENT_ALL] = NULL;
		return thrd_nomem;
	}
	cond->mEvents[_CONDITION_EVENT_ALL] = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (cond->mEvents[_CONDITION_EVENT_ALL] == NULL) {
		CloseHandle(cond->mEvents[_CONDITION_EVENT_ONE]);
		cond->mEvents[_CONDITION_EVENT_ONE] = NULL;
		return thrd_nomem;
	}

	return thrd_success;
#else
	return _ythread_err_map(pthread_cond_init(cond, NULL));
#endif
}

static inline void
cnd_destroy(cnd_t *cond) {
#if defined(_Y_WIN32_)
	if (cond->mEvents[_CONDITION_EVENT_ONE] != NULL)
		CloseHandle(cond->mEvents[_CONDITION_EVENT_ONE]);
	if (cond->mEvents[_CONDITION_EVENT_ALL] != NULL)
		CloseHandle(cond->mEvents[_CONDITION_EVENT_ALL]);
	DeleteCriticalSection(&cond->mWaitersCountLock);
#else
	pthread_cond_destroy(cond);
#endif
}

static inline int
cnd_signal(cnd_t *cond) {
#if defined(_Y_WIN32_)
	int haveWaiters;

	/* Are there any waiters? */
	EnterCriticalSection(&cond->mWaitersCountLock);
	haveWaiters = (cond->mWaitersCount > 0);
	LeaveCriticalSection(&cond->mWaitersCountLock);

	/* If we have any waiting threads, send them a signal */
	if(haveWaiters)
		if (SetEvent(cond->mEvents[_CONDITION_EVENT_ONE]) == 0)
			return thrd_error;

	return thrd_success;
#else
	return _ythread_err_map(pthread_cond_signal(cond));
#endif
}

static inline int
cnd_broadcast(cnd_t *cond) {
#if defined(_Y_WIN32_)
	int haveWaiters;

	/* Are there any waiters? */
	EnterCriticalSection(&cond->mWaitersCountLock);
	haveWaiters = (cond->mWaitersCount > 0);
	LeaveCriticalSection(&cond->mWaitersCountLock);

	/* If we have any waiting threads, send them a signal */
	if(haveWaiters)
		if (SetEvent(cond->mEvents[_CONDITION_EVENT_ALL]) == 0)
			return thrd_error;

	return thrd_success;
#else
	return _ythread_err_map(pthread_cond_signal(cond));
#endif
}

#if defined(_Y_WIN32_)
static inline
int _cnd_timedwait_win32(cnd_t *cond, mtx_t *mtx, DWORD timeout)
{
	int result, lastWaiter;

	/* Increment number of waiters */
	EnterCriticalSection(&cond->mWaitersCountLock);
	++ cond->mWaitersCount;
	LeaveCriticalSection(&cond->mWaitersCountLock);

	/* Release the mutex while waiting for the condition (will decrease
	   the number of waiters when done)... */
	mtx_unlock(mtx);

	/* Wait for either event to become signaled due to cnd_signal() or
	   cnd_broadcast() being called */
	result = WaitForMultipleObjects(2, cond->mEvents, FALSE, timeout);
	if (result == WAIT_TIMEOUT)
		return thrd_timedout;
	else if (result == WAIT_FAILED)
		return thrd_error;

	/* Check if we are the last waiter */
	EnterCriticalSection(&cond->mWaitersCountLock);
	-- cond->mWaitersCount;
	lastWaiter = (result == (WAIT_OBJECT_0 + _CONDITION_EVENT_ALL)) &&
		(cond->mWaitersCount == 0);
	LeaveCriticalSection(&cond->mWaitersCountLock);

	/* If we are the last waiter to be notified to stop waiting, reset the event */
	if (lastWaiter)
		if (ResetEvent(cond->mEvents[_CONDITION_EVENT_ALL]) == 0)
			return thrd_error;

	/* Re-acquire the mutex */
	mtx_lock(mtx);

	return thrd_success;
}
#endif

static inline int
cnd_wait(cnd_t *cond, mtx_t *mtx) {
#if defined(_Y_WIN32_)
	return _cnd_timedwait_win32(cond, mtx, INFINITE);
#else
	return _ythread_err_map(pthread_cond_wait(cond, mtx));
#endif
}

int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts)
{
#if defined(_Y_WIN32_)
	struct timespec now;
	if (timespec_get(&now, TIME_UTC) == 0) {
		DWORD delta = (ts->tv_sec - now.tv_sec) * 1000 +
			(ts->tv_nsec - now.tv_nsec + 500000) / 1000000;
		return _cnd_timedwait_win32(cond, mtx, delta);
	}
	else
		return thrd_error;
#else
	return _ythread_err_map(pthread_cond_timedwait(cond, mtx, ts));
#endif
}

#if defined(_Y_WIN32_)
struct YThreadTSSData {
	void* value;
	tss_t key;
	struct YThreadTSSData* next;
};
extern tss_dtor_t _ythread_tss_dtors[1088];
extern _Thread_local struct YThreadTSSData* _ythread_tss_head = NULL;
extern _Thread_local struct YThreadTSSData* _ythread_tss_tail = NULL;
#endif /* defined(_Y_WIN32_) */

/* Thread */
#if defined(_Y_WIN32_)
typedef HANDLE thrd_t;
#else
typedef pthread_t thrd_t;
#endif

typedef int (*thrd_start_t)(void *arg);

/** Information to pass to the new thread (what to run). */
typedef struct {
  thrd_start_t mFunction; /**< Pointer to the function to be executed. */
  void * mArg;            /**< Function argument for the thread function. */
} _thread_start_info;

/* Thread wrapper function. */
#if defined(_Y_WIN32_)
static DWORD WINAPI _thrd_wrapper_function(LPVOID aArg)
#elif defined(_Y_POSIX_)
static void * _thrd_wrapper_function(void * aArg)
#endif
{
	thrd_start_t fun;
	void *arg;
	int  res;
#if defined(_Y_POSIX_)
	void *pres;
#endif

	/* Get thread startup information */
	_thread_start_info *ti = (_thread_start_info *) aArg;
	fun = ti->mFunction;
	arg = ti->mArg;

	/* The thread is responsible for freeing the startup information */
	free((void *)ti);

	/* Call the actual client thread function */
	res = fun(arg);

#if defined(_Y_WIN32_)
	if (_ythread_tss_head != NULL)
		_ythread_tss_cleanup();

	return res;
#else
	pres = malloc(sizeof(int));
	if (pres != NULL)
		*(int*)pres = res;
	return pres;
#endif
}

static int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
	/* Fill out the thread startup information (passed to the thread wrapper,
	   which will eventually free it) */
	_thread_start_info* ti = (_thread_start_info*)malloc(sizeof(_thread_start_info));
	if (ti == NULL)
		return thrd_nomem;
	ti->mFunction = func;
	ti->mArg = arg;

	/* Create the thread */
#if defined(_Y_WIN32_)
	*thr = CreateThread(NULL, 0, _thrd_wrapper_function, (LPVOID) ti, 0, NULL);
#elif defined(_Y_POSIX_)
	if(pthread_create(thr, NULL, _thrd_wrapper_function, (void *)ti) != 0)
		*thr = 0;
#endif

	/* Did we fail to create the thread? */
	if(!*thr) {
		free(ti);
		return thrd_error;
	}
	return thrd_success;
}

static thrd_t thrd_current(void)
{
#if defined(_Y_WIN32_)
	return GetCurrentThread();
#else
	return pthread_self();
#endif
}

static int thrd_detach(thrd_t thr) {
#if defined(_Y_WIN32_)
	/* https://stackoverflow.com/questions/12744324/how-to-detach-a-thread-on-windows-c#answer-12746081 */
	return CloseHandle(thr) != 0 ? thrd_success : thrd_error;
#else
	return _ythread_err_map(pthread_detach(thr));
#endif
}

static int thrd_equal(thrd_t thr0, thrd_t thr1) {
#if defined(_Y_WIN32_)
	return thr0 == thr1;
#else
	return pthread_equal(thr0, thr1);
#endif
}

static void thrd_exit(int res) {
#if defined(_Y_WIN32_)
	if (_tinycthread_tss_head != NULL)
		_tinycthread_tss_cleanup();

	ExitThread(res);
#else
	void *pres = malloc(sizeof(int));
	if (pres != NULL)
		*(int*)pres = res;
	pthread_exit(pres);
#endif
}

static int thrd_join(thrd_t thr, int *res) {
	int ret;
#if defined(_Y_WIN32_)
	DWORD dwRes;

	if (WaitForSingleObject(thr, INFINITE) == WAIT_FAILED)
		return thrd_error;
	if (res != NULL) {
		GetExitCodeThread(thr, &dwRes);
		*res = dwRes;
	}
	CloseHandle(thr);
#elif defined(_Y_POSIX_)
	void *pres;
	int ires = 0;
	ret = pthread_join(thr, &pres);
	if (ret != 0)
		return _ythread_err_map(ret);
	if (pres != NULL) {
		ires = *(int*)pres;
		free(pres);
	}
	if (res != NULL) {
		*res = ires;
	}
#endif
	return thrd_success;
}

static int
thrd_sleep(const struct timespec *duration, struct timespec *remaining) {
#if defined(_Y_POSIX_)
	return _ythread_err_map(nanosleep(duration, remaining));
#else
	struct timespec start;
	DWORD t;

	timespec_get(&start, TIME_UTC);

	t = SleepEx(duration->tv_sec * 1000 +
			duration->tv_nsec / 1000000 +
			(((duration->tv_nsec % 1000000) == 0) ? 0 : 1),
			TRUE);

	if (t == 0)
		return thrd_success;
	else {
		if (remaining != NULL) {
			timespec_get(remaining, TIME_UTC);
			remaining->tv_sec -= start.tv_sec;
			remaining->tv_nsec -= start.tv_nsec;
			if (remaining->tv_nsec < 0) {
				remaining->tv_nsec += 1000000000;
				remaining->tv_sec -= 1;
			}
		}
		return thrd_interrupt;
	}
#endif
}

static void thrd_yield(void) {
#if defined(_Y_WIN32_)
	Sleep(0);
#else
	sched_yield();
#endif
}

/* Thread local storage */
#if defined(_Y_WIN32_)
typedef DWORD tss_t;
#else
typedef pthread_key_t tss_t;
#endif

/** Destructor function for a thread-specific storage.
* @param val The value of the destructed thread-specific storage.
*/
typedef void (*tss_dtor_t)(void *val);

static int tss_create(tss_t *key, tss_dtor_t dtor) {
#if defined(_Y_WIN32_)
	*key = TlsAlloc();
	if (*key == TLS_OUT_OF_INDEXES)
		return thrd_error;
	_ythread_tss_dtors[*key] = dtor;
#else
	return _ythread_err_map(
			pthread_key_create(key, dtor)
	       );
#endif
}

static void tss_delete(tss_t key) {
#if defined(_Y_WIN32_)
	struct YThreadTSSData* data = (struct YThreadTSSData*) TlsGetValue (key);
	struct YThreadTSSData* prev = NULL;
	if (data != NULL) {
		if (data == _tinycthread_tss_head)
			_ythread_tss_head = data->next;
		else {
			prev = _ythread_tss_head;
			while (prev->next != data)
				prev = prev->next;
		}

		if (data == _tinycthread_tss_tail)
			_tinycthread_tss_tail = prev;

		free(data);
	}
	_ythread_tss_dtors[key] = NULL;
	TlsFree(key);
#else
	pthread_key_delete(key);
#endif
}

static void *tss_get(tss_t key) {
#if defined(_Y_WIN32_)
	struct YThreadTSSData* data = (struct YThreadTSSData*)TlsGetValue(key);
	if (!data)
		return NULL;
	return data->value;
#else
	return pthread_getspecific(key);
#endif
}

static int tss_set(tss_t key, void *val) {
#if defined(_Y_WIN32_)
	struct YThreadTSSData* data = (struct YThreadTSSData*)TlsGetValue(key);
	if (!data) {
		data = (struct YThreadTSSData*)malloc(sizeof(struct YThreadTSSData));
		if (!data)
			return thrd_nomem;

		data->value = NULL;
		data->key = key;
		data->next = NULL;

		if (_ythread_tss_tail)
			_ythread_tss_tail->next = data;
		else
			_ythread_tss_tail = data;

		if (!_ythread_tss_head)
			_ythread_tss_head = data;

		if (!TlsSetValue(key, data)) {
			free(data);
			return thrd_error;
		}
	}
	data->value = val;
#else
	return _ythread_err_map(pthread_setspecific(key, val));
#endif
}

#if defined(_Y_WIN32_)
typedef struct {
	LONG volatile status;
	CRITICAL_SECTION lock;
} once_flag;
# define ONCE_FLAG_INIT {0,}
#else
# define once_flag pthread_once_t
# define ONCE_FLAG_INIT PTHREAD_ONCE_INIT
#endif

static void
call_once(once_flag *flag, void (*func)(void)) {
#if defined(_Y_WIN32_)
	/* The idea here is that we use a spin lock (via the
	   InterlockedCompareExchange function) to restrict access to the
	   critical section until we have initialized it, then we use the
	   critical section to block until the callback has completed
	   execution. */
	while (flag->status < 3) {
		switch (flag->status) {
			case 0:
				if (InterlockedCompareExchange (&(flag->status), 1, 0) == 0) {
					InitializeCriticalSection(&(flag->lock));
					EnterCriticalSection(&(flag->lock));
					flag->status = 2;
					func();
					flag->status = 3;
					LeaveCriticalSection(&(flag->lock));
					return;
				}
				break;
			case 1:
				break;
			case 2:
				EnterCriticalSection(&(flag->lock));
				LeaveCriticalSection(&(flag->lock));
				break;
		}
	}
#else
	pthread_once(flag, func);
#endif /* defined(_Y_WIN32_) */
}
