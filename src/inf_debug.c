/**
 * @file inf_debug.h
 * @brief Debug API.
 * @platform Pax Prolin
 * @date 2020-11-21
 * 
 * @copyright Copyright (c) 2020 CloudWalk, Inc.
 * 
 */

#include "mruby/ext/infinite.h"

#include <osal.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/**********/
/* Macros */
/**********/

#ifndef INF_DEBUG_TYPE
#define INF_DEBUG_CHANNEL INF_SERIAL_CHANNEL_USB
#define INF_DEBUG_TYPE INF_SERIAL
#endif /* #ifndef INF_DEBUG_TYPE */

#if INF_DEBUG_TYPE == INF_SERIAL
#warning INF_DEBUG_TYPE defined as INF_SERIAL. INF_LOGCAT may be preferable.
#endif

#ifndef INF_DEBUG_CHANNEL
#error INF_DEBUG_CHANNEL not defined.
#endif /* #ifndef INF_DEBUG_CHANNEL */

/********************/
/* Global variables */
/********************/

static pthread_mutex_t *inf_debug_mutex = NULL;

/*********************/
/* Private functions */
/*********************/

static void
debug_close(void)
{
    switch (INF_DEBUG_TYPE)
    {
    case INF_LOGCAT:
        break;

    default:
        inf_serial_close(INF_SERIAL_CHANNEL_USB);
        break;
    }
}

static void
debug_open(void)
{
    switch (INF_DEBUG_TYPE)
    {
    case INF_LOGCAT:
        break;

    default:
        inf_serial_open(INF_SERIAL_CHANNEL_USB, NULL);
        break;
    }
}

/********************/
/* Public functions */
/********************/

extern void
inf_debug_init(void)
{
    if (!inf_debug_mutex)
    {
        switch (INF_DEBUG_TYPE)
        {
        case INF_LOGCAT:
            OsLogSetTag("INF_DEBUG");
            break;

        default:
            inf_serial_init();
            break;
        }

        inf_debug_mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));

        if (!inf_debug_mutex)
        {
            return; /* INF_EN_MEMORY_FAILURE */
        }

        if (pthread_mutex_init(inf_debug_mutex, NULL))
        {
            exit(EXIT_FAILURE);
        }
    }
}

extern void
inf_debug_send(const char *format, ...)
{
    char buffer[1024];
    int return_value;
    va_list args;

    if (!inf_debug_mutex)
    {
        return;
    }

    if (!format)
    {
        return; /* INF_EN_INVALID_ARGUMENT */
    }

    pthread_mutex_lock(inf_debug_mutex);

    debug_open();

    va_start(args, format);

    return_value = vsnprintf(buffer, sizeof(buffer), format, args);

    if (return_value)
    {
        switch (INF_DEBUG_TYPE)
        {
        case INF_LOGCAT:
            OsLog(LOG_DEBUG, buffer);
            break;

        default:
            inf_serial_send(INF_DEBUG_CHANNEL, buffer, return_value);
            break;
        }
    }

    va_end(args);

    debug_close();

    pthread_mutex_unlock(inf_debug_mutex);
}
