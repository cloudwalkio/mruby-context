/**
 * @file inf_debug.h
 * @brief API definition.
 * @platform Pax Prolin
 * @date 2020-11-21
 * 
 * @copyright Copyright (c) 2020 CloudWalk, Inc.
 * 
 */

#ifndef _INF_DEBUG_H_INCLUDED_
#define _INF_DEBUG_H_INCLUDED_

#include <string.h>

/**********/
/* Macros */
/**********/

#define INF_SERIAL 0
#define INF_LOGCAT 1

#ifdef _INF_DEBUG_
#ifndef INF_TRACE
#define INF_TRACE(...) inf_debug_send(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#endif /* #ifndef INF_TRACE */

#ifndef INF_TRACE_FUNCTION
#define INF_TRACE_FUNCTION() INF_TRACE(NULL)
#endif /* #ifndef INF_TRACE_FUNCTION */

#ifndef INF_TRACE_INIT
#define INF_TRACE_INIT(...) inf_debug_init(__VA_ARGS__)
#endif /* #ifndef INF_TRACE_INIT */
#else
#ifndef INF_TRACE
#define INF_TRACE(...)
#endif /* #ifndef INF_TRACE */

#ifndef INF_TRACE_FUNCTION
#define INF_TRACE_FUNCTION()
#endif /* #ifndef INF_TRACE_FUNCTION */

#ifndef INF_TRACE_INIT
#define INF_TRACE_INIT(...)
#endif /* #ifndef INF_TRACE_INIT */
#endif /* #ifdef _INF_DEBUG_ */

/********************/
/* Public functions */
/********************/

extern void
inf_debug_init(void);

extern void
inf_debug_send(const char *file, const int line, const char *function, const char *format, ...);

#endif /* #ifndef _INF_DEBUG_H_INCLUDED_ */
