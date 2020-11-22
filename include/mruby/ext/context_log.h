
#ifndef MRUBY_CONTEXT_LOG_H
#define MRUBY_CONTEXT_LOG_H

#define _MULTI_VM_

#ifdef _MULTI_VM_
#define PTHREAD_MUTEX_INIT(...) pthread_mutex_init(__VA_ARGS__)
#define PTHREAD_MUTEX_LOCK(...) pthread_mutex_lock(__VA_ARGS__)
#define PTHREAD_MUTEX_UNLOCK(...) pthread_mutex_unlock(__VA_ARGS__)
#else
#define PTHREAD_MUTEX_INIT(...)
#define PTHREAD_MUTEX_LOCK(...)
#define PTHREAD_MUTEX_UNLOCK(...)
#endif /* #ifdef _MULTI_VM_ */

#endif /* MRUBY_CONTEXT_LOG_H */
