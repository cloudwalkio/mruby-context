/**
 * @file context.h
 * @brief Internal definitions.
 * @platform Pax Prolin
 * @date 2020-11-22
 *
 * @copyright Copyright (c) 2020 CloudWalk, Inc.
 *
 */

#ifndef _CONTEXT_H_INCLUDED_
#define _CONTEXT_H_INCLUDED_

/** (mutex) When enabled, adds mutex protection to mruby-context. */
#define _INF_MULTI_VM_

#ifndef _INF_MULTI_VM_
#ifndef PTHREAD_MUTEX_INIT
#define PTHREAD_MUTEX_INIT(...)
#endif /* #ifndef PTHREAD_MUTEX_INIT */

#ifndef PTHREAD_MUTEX_LOCK
#define PTHREAD_MUTEX_LOCK(...)
#endif /* #ifndef PTHREAD_MUTEX_LOCK */

#ifndef PTHREAD_MUTEX_UNLK
#define PTHREAD_MUTEX_UNLK(...)
#endif /* #ifndef PTHREAD_MUTEX_UNLK */
#else
#ifndef PTHREAD_MUTEX_INIT
#define PTHREAD_MUTEX_INIT(...) pthread_mutex_init(__VA_ARGS__)
#endif /* #ifndef PTHREAD_MUTEX_INIT */

#ifndef PTHREAD_MUTEX_LOCK
#define PTHREAD_MUTEX_LOCK(...) pthread_mutex_lock(__VA_ARGS__)
#endif /* #ifndef PTHREAD_MUTEX_LOCK */

#ifndef PTHREAD_MUTEX_UNLK
#define PTHREAD_MUTEX_UNLK(...) pthread_mutex_unlock(__VA_ARGS__)
#endif /* #ifndef PTHREAD_MUTEX_UNLK */
#endif /* #ifndef _INF_MULTI_VM_ */

#ifndef INF_TRACE_INIT
#define INF_TRACE_INIT(...)
#endif /* #ifndef INF_TRACE_INIT */

#ifndef INF_TRACE
#define INF_TRACE(...)
#endif /* #ifndef INF_TRACE */

#endif /* _CONTEXT_H_INCLUDED_ */
