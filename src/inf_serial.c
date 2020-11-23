/**
 * @file inf_serial.h
 * @brief Serial API.
 * @platform Pax Prolin
 * @date 2020-11-19
 * 
 * @copyright Copyright (c) 2020 CloudWalk, Inc.
 * 
 */

#include "mruby/ext/infinite.h"

#include <fcntl.h>
#include <osal.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>

/********************/
/* Global variables */
/********************/

static char current_attr[255 + 1] = { 0 };

static pthread_mutex_t *inf_serial_mutex = NULL;

/*********************/
/* Private functions */
/*********************/

static int
set_attr(int channel, char *attr)
{
  switch (channel)
  {
  case PORT_USBDEV:
    memset(current_attr, 0, sizeof(current_attr));
    break;

  default:
    return INF_EN_NOT_SUPPORTED;
  }

  return INF_EN_OK;
}

static int
validate_channel(int input, int *output)
{
  if (!output)
  {
    return INF_EN_INVALID_ARGUMENT;
  }

  switch (input)
  {
  case INF_SERIAL_CHANNEL_COM:
    *output = PORT_COM1;
    break;

  case INF_SERIAL_CHANNEL_USB:
    *output = PORT_USBDEV;
    break;

  default:
    return INF_EN_INVALID_ARGUMENT;
  }

  return INF_EN_OK;
}

static int
serial_close(int channel)
{
  if (validate_channel(channel, &channel))
  {
    return INF_EN_INVALID_ARGUMENT;
  }

  OsPortReset(channel);

  OsPortClose(channel);

  return INF_EN_OK;
}

static int
serial_open(int channel, char *attr)
{
  if (validate_channel(channel, &channel))
  {
    return INF_EN_INVALID_ARGUMENT;
  }

  if (set_attr(channel, attr))
  {
    return INF_EN_INVALID_ARGUMENT;
  }

  return (!OsPortOpen(channel, attr)) ? INF_EN_OK : INF_EN_FAILURE;
}

static int
serial_recv(int channel, void *buffer, unsigned int length, unsigned int timeout)
{
  int return_value;

  if (validate_channel(channel, &channel))
  {
    return INF_EN_INVALID_ARGUMENT;
  }

  if (!buffer || !length || timeout > 25500)
  {
    return INF_EN_INVALID_ARGUMENT;
  }

  return_value = OsPortRecv(channel, buffer, length, timeout);

  return (return_value <= 0) ? ((return_value != ERR_TIME_OUT) ? INF_EN_FAILURE : INF_EN_TIMEOUT) : return_value;
}

static int
serial_send(int channel, const void *buffer, unsigned int length)
{
  int return_value;

  if (validate_channel(channel, &channel))
  {
    return INF_EN_INVALID_ARGUMENT;
  }

  if (!buffer || !length)
  {
    return INF_EN_INVALID_ARGUMENT;
  }

  return_value = OsPortSend(channel, buffer, length);

  while (OsPortCheckTx(channel) > 0);

  return (!return_value) ? INF_EN_OK : INF_EN_FAILURE;
}

/********************/
/* Public functions */
/********************/

extern int
inf_serial_close(int channel)
{
  int return_value;

  pthread_mutex_lock(inf_serial_mutex);

  return_value = serial_close(channel);

  pthread_mutex_unlock(inf_serial_mutex);

  return return_value;
}

extern int
inf_serial_init(void)
{
  if (!inf_serial_mutex)
  {
    if (OsRegSetValue("persist.sys.xcb.enable", "0"))
    {
      return INF_EN_FAILURE;
    }

    inf_serial_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));

    if (!inf_serial_mutex)
    {
      return INF_EN_MEMORY_FAILURE;
    }

    if (pthread_mutex_init(inf_serial_mutex, NULL))
    {
      exit(EXIT_FAILURE);
    }
  }

  return INF_EN_OK;
}

extern int
inf_serial_open(int channel, char *attr)
{
  int return_value;

  pthread_mutex_lock(inf_serial_mutex);

  return_value = serial_open(channel, attr);

  pthread_mutex_unlock(inf_serial_mutex);

  return return_value;
}

extern int
inf_serial_recv(int channel, char *buffer, size_t length, unsigned int timeout)
{
  int return_value;

  pthread_mutex_lock(inf_serial_mutex);

  return_value = serial_recv(channel, (void *)buffer, (unsigned int)length, timeout);

  pthread_mutex_unlock(inf_serial_mutex);

  return return_value;
}

extern int
inf_serial_send(int channel, char *buffer, size_t length)
{
  int return_value;

  pthread_mutex_lock(inf_serial_mutex);

  return_value = serial_send(channel, (const void *)buffer, (unsigned int)length);

  pthread_mutex_unlock(inf_serial_mutex);

  return return_value;
}
