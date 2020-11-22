/**
 * @file inf_serial.h
 * @brief API definition.
 * @platform Pax Prolin
 * @date 2020-11-19
 * 
 * @copyright Copyright (c) 2020 CloudWalk, Inc.
 * 
 */

#ifndef _INF_SERIAL_H_INCLUDED_
#define _INF_SERIAL_H_INCLUDED_

/**********/
/* Macros */
/**********/

#define INF_SERIAL_CHANNEL_USB 1
#define INF_SERIAL_CHANNEL_COM 0

/********************/
/* Public functions */
/********************/

extern int
inf_serial_close(int channel);

extern int
inf_serial_init(void);

extern int
inf_serial_open(int channel, char *attr);

extern int
inf_serial_recv(int channel, char *buffer, size_t length, unsigned int timeout);

extern int
inf_serial_send(int channel, char *buffer, size_t length);

#endif /* #ifndef _INF_SERIAL_H_INCLUDED_ */
