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

#ifdef _ENG_DEBUG_ /* TODO: remove?! */
#include "libeng/engine.h"
#endif /* #ifdef _ENG_DEBUG_ */

#if defined ENG_TRACE && !defined TRACE
#define TRACE(...) ENG_TRACE(__VA_ARGS__)
#else
#ifndef TRACE
#define TRACE(...)
#endif /* #ifndef TRACE */
#endif /* #if defined ENG_TRACE && !defined TRACE */

#if defined ENG_TRACE_FUNCTION && !defined TRACE_FUNCTION
#define TRACE_FUNCTION(...) ENG_TRACE_FUNCTION(__VA_ARGS__)
#else
#ifndef TRACE_FUNCTION
#define TRACE_FUNCTION(...)
#endif /* #ifndef TRACE_FUNCTION */
#endif /* #if defined ENG_TRACE_FUNCTION && !defined TRACE_FUNCTION */

#if defined ENG_TRACE_INIT && !defined TRACE_INIT
#define TRACE_INIT(...) ENG_TRACE_INIT(__VA_ARGS__)
#else
#ifndef TRACE_INIT
#define TRACE_INIT(...)
#endif /* #ifndef TRACE_INIT */
#endif /* #if defined ENG_TRACE_INIT && !defined TRACE_INIT */

#endif /* _CONTEXT_H_INCLUDED_ */
