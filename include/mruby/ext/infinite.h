/**
 * @file infinite.h
 * @brief Infinite Engine's API definition.
 * @platform Pax Prolin
 * @date 2020-11-19
 * 
 * @copyright Copyright (c) 2020 CloudWalk, Inc.
 * 
 */

#ifndef _INFINITE_H_INCLUDED_
#define _INFINITE_H_INCLUDED_

#include <stdlib.h>

#ifndef __INF_FILE__
#define __INF_FILE__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__))
#endif /* #ifndef __FILENAME__ */

typedef enum EN_ERROR
{
    INF_EN_OK = 0,
    INF_EN_FAILURE = - EXIT_FAILURE - 99999,
    INF_EN_INVALID_ARGUMENT,
    INF_EN_NOT_SUPPORTED,
    INF_EN_MEMORY_FAILURE,
    INF_EN_TIMEOUT,
                                    /* <- new defs. can be added here! */
    INF_EN_MARKER
} INF_EN_ERROR;

#include "inf_debug.h"
/* #include "inf_display.h" */
#include "inf_serial.h"
/* ... */

#endif /* #ifndef _INFINITE_H_INCLUDED_ */
