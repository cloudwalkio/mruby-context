/**
 * @file thread_scheduler.c
 * @brief mruby-context thread scheduler.
 * @platform Pax Prolin
 * @date 2018-10-01
 *
 * @copyright Copyright (c) 2018 CloudWalk, Inc.
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif /* #ifdef _WIN32 */

#include "mruby.h"
#include "mruby/array.h"
#include "mruby/compile.h"
#include "mruby/ext/context.h"
#include "mruby/ext/context_log.h"
#include "mruby/hash.h"
#include "mruby/string.h"
#include "mruby/value.h"
#include "mruby/variable.h"

/**********/
/* Macros */
/**********/

#define INF_CHANNEL_MAX_SIZE 102400
#define INF_QUEUE_MAX_SIZE 1024 /* TODO: manage "memory leaking" (forgotten nodes) from (user) aborted operations!? (this could be way smaller) (~8) */
#define INF_PUB_SUB_MAX_SLOT 10
#define THREAD_BLOCK 0
#define THREAD_COMMAND_MAX_MSG_SIZE 102400
#define THREAD_COMMUNICATION 1
#define THREAD_FREE 1
#define THREAD_STATUS_ALIVE 1
#define THREAD_STATUS_BAR 0
#define THREAD_STATUS_BLOCK 5
#define THREAD_STATUS_DEAD 0
#define THREAD_STATUS_PAUSE 4

/********************/
/* Type definitions */
/********************/

typedef struct thread
{
  char command[256];
  char response[256];
  int id;
  int sem;
  int status;
} thread;

typedef struct
{
  char data[51200];
  int id;
  int len;
} message;

typedef struct executionMessage
{
  char *command;
  char *response;
  int commandLen;
  int executed;
  int id;
  int responseLen;
  struct executionMessage *front;
  struct executionMessage *rear;
} executionMessage;

typedef struct threadExecutionQueue
{
  int size;
  struct executionMessage *first;
  struct executionMessage *last;
} threadExecutionQueue;

/********************/
/* Global variables */
/********************/

/**
 * @brief (deprecated) Marker for indexes in @link conn_thread_events @endlink.
 * It says if a given index is ready to be used (1) or not (0). <br>
 * <em> Kept to facilitate refactoring efforts (2020, Nov) (disposable) </em>
 */
static int conn_thread_events_marker[INF_PUB_SUB_MAX_SLOT] = { 0 };

static pthread_mutex_t command_exchange_mutex;

static pthread_mutex_t message_exchange_mutex;

static message *conn_thread_events[INF_PUB_SUB_MAX_SLOT][INF_QUEUE_MAX_SIZE] = { { NULL }, { NULL }, { NULL }, { NULL }, { NULL }, { NULL }, { NULL }, { NULL }, { NULL }, { NULL } };

static message *message_recv_queue[INF_QUEUE_MAX_SIZE] = { NULL };

static message *message_send_queue[INF_QUEUE_MAX_SIZE] = { NULL };

static thread *CommunicationThread = NULL;

static thread *StatusBarThread = NULL;

static threadExecutionQueue *executionQueue = NULL;

/*********************/
/* Private functions */
/*********************/

/**
 * @brief Searches for a node in a given queue and retrieves its data: <br>
 * - When node ID is NULL, dequeues the most recent one;
 * - When node ID is ZERO, dequeues the most recent one and updates the ID.
 *
 * @param queue given message queue
 * @param id node ID
 * @param buf dequeued node content
 *
 * @return dequeued node content length or 0, otherwise
 */
static int
thread_channel_dequeue(message *queue[], int *id, char *buf)
{
  int i = INF_QUEUE_MAX_SIZE;
  int length;

  INF_TRACE_FUNCTION();

  if (!queue || !buf)
  {
    INF_TRACE("return [0]");

    return 0;
  }

  while (!queue[--i]); /* 2020-11-25: would be better to know the current size
                        * beforehand, but its not time consuming */

  while (queue[i] && i >= 0)
  {
    INF_TRACE("*id [%d], queue[%d]->id [%d]", (!id) ? 0 : *id, i, queue[i]->id);

    if (id == NULL || *id == 0 || queue[i]->id == *id)
    {
      if (!(queue[i]->len))
      {
        continue;
      }

      if (id != NULL) (*id) = queue[i]->id;

      length = queue[i]->len;

      memcpy(buf, queue[i]->data, length);

      INF_TRACE("%*.*s", length, length, buf);

      free(queue[i]);

      while (i++ < (INF_QUEUE_MAX_SIZE - 1)) 
      {
        queue[i - 1] = queue[i];

        if (!queue[i]) break; /* 2020-11-24: no need to go through the entire
                               * queue (sorted) */
      }

      queue[--i] = NULL;

      INF_TRACE("return [%d]", length);

      return length;
    }

    i--;
  }

  INF_TRACE("return [0]");

  return 0;
}

/**
 * @brief Enqueues a node in a given queue. Queue is kept sorted from the
 * oldest to the newest node to be enqueued.
 * 
 * @param queue given message queue
 * @param id node ID
 * @param buf node content
 * @param len node content length
 * 
 * @return int enqueued node content length or 0, otherwise
 */
static int
thread_channel_enqueue(message *queue[], int id, char *buf, int len)
{
  int i;
  message *node;

  INF_TRACE_FUNCTION();

  if (len <= 0 || len >= INF_CHANNEL_MAX_SIZE)
  {
    INF_TRACE("return [%d]", len);

    return 0;
  }

  if (!queue || !buf)
  {
    INF_TRACE("return [%d]", len);

    return 0;
  }

  node = (message *) calloc(1, sizeof(message));

  if (!node)
  {
    INF_TRACE("return [%d]", len);

    return 0;
  }

  node->id = id;

  memcpy(node->data, buf, len);

  INF_TRACE("%*.*s", len, len, node->data);

  node->len = len;

  i = -1;

  while (queue[++i] && i < INF_QUEUE_MAX_SIZE); /* 2020-11-24: will enqueue at
                                                 * the back */

  if (i >= INF_QUEUE_MAX_SIZE)
  {
    free(node);

    INF_TRACE("return [0]");

    return 0;
  }

  queue[i] = (void *) node;

  INF_TRACE("queue[%d]->id [%d], return [%d]", i, queue[i]->id, len);

  return len;
}

static thread *
context_thread_new(int id, int status)
{
  thread *threadControl = (thread *) malloc(sizeof(thread));

  threadControl->id = id;
  threadControl->sem = THREAD_BLOCK;
  threadControl->status = status;
  memset(threadControl->response, 0, sizeof(threadControl->response));
  memset(threadControl->command, 0, sizeof(threadControl->command));

  return threadControl;
}

static void
context_thread_sem_push(thread *threadControl)
{
  if (threadControl) threadControl->sem = THREAD_FREE;
}

static int
context_thread_sem_wait(thread *threadControl, int timeout_msec)
{
  (void) timeout_msec;

  if (threadControl) {
    if (threadControl->sem == THREAD_BLOCK) {
      return -1;
    }

    threadControl->sem = THREAD_BLOCK;
  }

  return 1;
}

static int
context_thread_continue(thread *threadControl)
{
  if (threadControl != NULL && threadControl->status == THREAD_STATUS_PAUSE) {
    context_thread_sem_wait(threadControl, 0);
    threadControl->status = THREAD_STATUS_ALIVE;
    context_thread_sem_push(threadControl);
    return 1;
  } else {
    return 0;
  }
}

static int
context_thread_pause(thread *threadControl)
{
  int ret = 0;

  if (threadControl != NULL && threadControl->status != THREAD_STATUS_DEAD) {
    context_thread_sem_wait(threadControl, 0);
    if (threadControl->status == THREAD_STATUS_ALIVE) {
      threadControl->status = THREAD_STATUS_PAUSE;
      ret = 1;
    }
    context_thread_sem_push(threadControl);
  }

  return ret;
}

static void
thread_channel_clean(message *queue)
{
  char trash[INF_CHANNEL_MAX_SIZE] = { 0x00 };
  int id;
  int len = 1;

  while (len != 0)
  {
    id = 0;

    memset(trash, 0, sizeof(trash));

    len = thread_channel_dequeue(&queue, &id, trash);
  }
}

static int
subscribe(void)
{
  int i = 0;

  while (conn_thread_events_marker[i++])
  {
    if (i >= 10)
    {
      i = -1;

      break;
    }
  }

  if (i >= 0)
  {
    conn_thread_events_marker[i] = 1;
  }

  return i;
}

static int
pubsub_publish(char *buf, int len, int avoid_id)
{
  int id = 0, ret = 0;

  while (id < 10 && conn_thread_events_marker[id])
  {
    if (id != avoid_id)
    {
      ret = thread_channel_enqueue(conn_thread_events[id], 0, buf, len);
    }
    id++;
  }

  return ret;
}

static int
pubsub_listen(int id, char *buf)
{
  int event = 0;

  if (conn_thread_events_marker[id])
  {
    return thread_channel_dequeue(conn_thread_events[id], &event, buf);
  }

  return 0;
}

static threadExecutionQueue *
thread_execution_new(void)
{
  threadExecutionQueue *queue = (threadExecutionQueue *) malloc(sizeof(threadExecutionQueue));

  queue->size = 0;
  queue->first = NULL;
  queue->last = NULL;

  return queue;
}

static executionMessage *
thread_execution_message_new(int id)
{
  executionMessage *message = (executionMessage *) malloc(sizeof(executionMessage));

  message->command = NULL;
  message->response = NULL;
  message->commandLen = 0;
  message->executed = 0;
  message->id = id;
  message->responseLen = 0;
  message->front = NULL;
  message->rear = NULL;

  return message;
}

static int
thread_execution_get(threadExecutionQueue *queue, int id, int command, char *buf)
{
  int len = 0;
  executionMessage *message = NULL;
  executionMessage *local = NULL;

  /* Search for already schedule execution */
  if (queue && queue->size > 0) {
    local = queue->first;
    while (local != NULL) {
      if (local->id == id) {
        message = local;
        break;
      }
      local = local->rear;
    }
  }
  if (message == NULL) return len;

  /* Get information inside of struct synchronizing */
  if (command == 0 && message->commandLen >= 0 && message->executed == 0) {
    if (message->commandLen > 0) {
      memcpy(buf, message->command, message->commandLen);
      len = message->commandLen;
      message->executed = 0;
    }
  } else if (command == 1 && message->responseLen >= 0 && message->executed == 1) {
    if (message->responseLen > 0) {
      memcpy(buf, message->response, message->responseLen);
      len = message->responseLen;
      message->executed = 1;
    }
  }

  return len;
}

static int
thread_execution_enqueue(threadExecutionQueue *queue, int id, int command, char *buf, int len)
{
  executionMessage *message = NULL;
  executionMessage *local = NULL;

  if (queue == NULL) return 0;

  /* Search for already schedule execution */
  if (queue && queue->size > 0) {
    local = queue->first;
    while (local != NULL) {
      if (local->id == id) {
        message = local;
        break;
      }
      local = local->rear;
    }
  }

  if (message != NULL) {
  } else {
    message = thread_execution_message_new(id);
    if (queue->size == 0) {
      queue->first = message;
      queue->last = message;
      queue->size = 1;
    } else {
      queue->last->rear = message;
      message->front = queue->last;
      queue->last = message;
      queue->size++;
    }
  }

  /* Copy command/response to message */
  if (command == 0) {
    message->command = (char *) realloc(message->command, len);
    memcpy(message->command, buf, len);
    message->commandLen = len;
    message->executed = 0;
  } else {
    message->response = (char *) realloc(message->response, len);
    memcpy(message->response, buf, len);
    message->responseLen = len;
    message->executed = 1;
  }

  return len;
}

static int
thread_execution_dequeue(threadExecutionQueue *queue, int id, int command, char *buf)
{
  int len = 0;
  executionMessage *message = NULL;
  executionMessage *local = NULL;

  /* Search for already schedule execution */
  if (queue && queue->size > 0) {
    local = queue->first;
    while (local != NULL) {
      if (local->id == id) {
        message = local;
        break;
      }
      local = local->rear;
    }
  }
  if (message == NULL) return len;

  /* Get information inside of struct synchronizing */
  if (command == 0 && message && message->commandLen > 0 && message->executed == 0 && message->command) {
    memcpy(buf, message->command, message->commandLen);
    len = message->commandLen;

    free(message->command);
    message->executed = 0;
    message->command = NULL;
    message->commandLen = 0;
  } else if (command == 1 && message && message->responseLen > 0 && message->executed == 1 && message->response) {
    memcpy(buf, message->response, message->responseLen);
    len = message->responseLen;
    message->executed = 1;
    free(message->response);
    message->response = NULL;
    message->responseLen = 0;
  }

  if (message && message->command == NULL && message->response == NULL) {
    if (message->front == NULL)
      queue->first = message->rear;
    else
      if (message->rear != NULL) message->rear->front = NULL;

    if (message->rear == NULL)
      queue->last = message->front;
    else
      if (message->front != NULL) message->front->rear = NULL;

    queue->size--;
    free(message);
  }

  return len;
}

static void
thread_execution_clean(threadExecutionQueue *queue)
{
  char trash[INF_CHANNEL_MAX_SIZE] = {0x00};
  int len = 1, id = 0;

  while (len != 0) {
    id = 0;
    memset(trash, 0, sizeof(trash));
    len = thread_execution_dequeue(queue, id, 0, trash);
    memset(trash, 0, sizeof(trash));
    if (len > 0) thread_execution_dequeue(queue, id, 1, trash);
  }
}

/**************************/
/* Externalized functions */
/**************************/

static mrb_value
mrb_thread_channel_s__read(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, len = 0, channel = 0, event = 0;
  char buf[INF_CHANNEL_MAX_SIZE] = {0x00};
  mrb_value array;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  mrb_get_args(mrb, "iii", &id, &channel, &event);

  INF_TRACE("channel [%d], event [%d]", channel, event);

  if (channel == 0) {
    len = thread_channel_dequeue(message_send_queue, &event, buf);
  } else {
    len = thread_channel_dequeue(message_recv_queue, &event, buf);
  }

  array = mrb_ary_new(mrb);
  mrb_ary_push(mrb, array, mrb_fixnum_value(event));
  if (len > 0) {
    mrb_ary_push(mrb, array, mrb_str_new(mrb, buf, len));
  }

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return array;
}

static mrb_value
mrb_thread_channel_s__write(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, channel = 0, event = 0, len = 0;
  mrb_value value;
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  mrb_get_args(mrb, "iiiS", &id, &channel, &event, &value);

  INF_TRACE("channel [%d], event [%d]", channel, event);

  if (channel == 0)
    len = thread_channel_enqueue(message_send_queue, event, RSTRING_PTR(value), RSTRING_LEN(value));
  else
    len = thread_channel_enqueue(message_recv_queue, event, RSTRING_PTR(value), RSTRING_LEN(value));

  return_value = mrb_fixnum_value(len);

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_pub_sub_s__listen(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, len = 0;
  char buf[INF_CHANNEL_MAX_SIZE] = {0x00};
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  mrb_get_args(mrb, "i", &id);

  memset(buf, 0, sizeof(buf));

  len = pubsub_listen(id, buf);

  if (len > 0)
    return_value = mrb_str_new(mrb, buf, len);
  else
    return_value = mrb_nil_value();

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_pub_sub_s__publish(mrb_state *mrb, mrb_value self)
{
  mrb_int len = 0;
  mrb_value avoid_id;
  mrb_value buf;
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  mrb_get_args(mrb, "So", &buf, &avoid_id);

  if (mrb_string_p(buf)) {
    if (mrb_fixnum_p(avoid_id)) {
      len = pubsub_publish(RSTRING_PTR(buf), RSTRING_LEN(buf), mrb_fixnum(avoid_id));
    } else {
      len = pubsub_publish(RSTRING_PTR(buf), RSTRING_LEN(buf), -1);
    }
    if (len > 0) return_value = mrb_true_value();
  }

  return_value = mrb_false_value();

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_pub_sub_s__subscribe(mrb_state *mrb, mrb_value self)
{
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  return_value = mrb_fixnum_value(subscribe());

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_scheduler_s__check(mrb_state *mrb, mrb_value self)
{
  mrb_int status = 0, id = 0, ret = 1, timeout = 0;
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  mrb_get_args(mrb, "ii", &id, &timeout);

  if (id == THREAD_STATUS_BAR && StatusBarThread) {
    ret = context_thread_sem_wait(StatusBarThread, timeout);
    if (ret == 1) {
      status = StatusBarThread->status;
      context_thread_sem_push(StatusBarThread);
    }
  } else if (id == THREAD_COMMUNICATION && CommunicationThread) {
    ret = context_thread_sem_wait(CommunicationThread, timeout);
    if (ret == 1) {
      status = CommunicationThread->status;
      context_thread_sem_push(CommunicationThread);
    }
  } else {
    status = THREAD_STATUS_DEAD;
  }

  if (ret == -1)
    return_value = mrb_fixnum_value(THREAD_STATUS_BLOCK);
  else
    return_value = mrb_fixnum_value(status);

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_scheduler_s__continue(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, ret = 0;
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR)
  {
    ret = context_thread_continue(StatusBarThread);
  }
  else if (id == THREAD_COMMUNICATION)
  {
    ret = context_thread_continue(CommunicationThread);
  }

  if (ret == 1)
    return_value = mrb_true_value();
  else
    return_value = mrb_false_value();

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_scheduler_s__pause(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, pause = 0;
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR)
    pause = context_thread_pause(StatusBarThread);
  else if (id == THREAD_COMMUNICATION)
    pause = context_thread_pause(CommunicationThread);

  if (pause == 1)
    return_value = mrb_true_value();
  else
    return_value = mrb_false_value();

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_scheduler_s__start(mrb_state *mrb, mrb_value self)
{
  int i;
  mrb_int id = 0;
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  mrb_get_args(mrb, "i", &id);

  return_value = mrb_true_value();

  if (id == THREAD_STATUS_BAR) {
    if (StatusBarThread) free(StatusBarThread);
    StatusBarThread = context_thread_new(id, THREAD_FREE);
    context_thread_sem_push(StatusBarThread);
  } else if (id == THREAD_COMMUNICATION) {
    if (CommunicationThread) {
      context_thread_sem_wait(CommunicationThread, 0);
      free(CommunicationThread);
      CommunicationThread = NULL;
    }

    i = 0;

    while (i < INF_QUEUE_MAX_SIZE && message_send_queue[i])
    {
      free(message_send_queue[i]);

      message_send_queue[i++] = NULL;
    }

    i = 0;

    while (i < INF_QUEUE_MAX_SIZE && message_recv_queue[i])
    {
      free(message_recv_queue[i]);

      message_recv_queue[i++] = NULL;
    }

    pthread_mutex_lock(&command_exchange_mutex);

    if (executionQueue)
    {
      thread_execution_clean(executionQueue);

      free(executionQueue);

      executionQueue = NULL;
    }

    executionQueue = thread_execution_new();

    pthread_mutex_unlock(&command_exchange_mutex);

    CommunicationThread = context_thread_new(id, THREAD_FREE);

    context_thread_sem_push(CommunicationThread);
  } else {
    return_value = mrb_false_value();
  }

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_scheduler_s__stop(mrb_state *mrb, mrb_value self)
{
  mrb_int i = 0, id = 0;
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&message_exchange_mutex);

  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR && StatusBarThread) {
    context_thread_sem_wait(StatusBarThread, 0);
    StatusBarThread->status = THREAD_STATUS_DEAD;
    context_thread_sem_push(StatusBarThread);
  } else if (id == THREAD_COMMUNICATION && CommunicationThread) {
    context_thread_sem_wait(CommunicationThread, 0);
    CommunicationThread->status = THREAD_STATUS_DEAD;

    while (i < INF_QUEUE_MAX_SIZE && message_send_queue[i])
    {
      free(message_send_queue[i]);

      message_send_queue[i++] = NULL;
    }

    i = 0;

    while (i < INF_QUEUE_MAX_SIZE && message_recv_queue[i])
    {
      free(message_recv_queue[i]);

      message_recv_queue[i++] = NULL;
    }

    i = 0;

    while (conn_thread_events_marker[i])
    {
      thread_channel_clean(*conn_thread_events[i]);

      conn_thread_events_marker[i++] = 0;
    };

    context_thread_sem_push(CommunicationThread);
  }

  return_value = mrb_true_value();

  INF_TRACE("return");

  pthread_mutex_unlock(&message_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_scheduler_s__command(mrb_state *mrb, mrb_value self)
{
  mrb_value command;
  char response[THREAD_COMMAND_MAX_MSG_SIZE] = {0x00};
  mrb_int id = 0, len = 0;
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&command_exchange_mutex);

  memset(response, 0, sizeof(response));

  mrb_get_args(mrb, "iS", &id, &command);

  len = thread_execution_get(executionQueue, id, 1, response);
  thread_execution_enqueue(executionQueue, id, 0, RSTRING_PTR(command), RSTRING_LEN(command));

  if (len > 0) {
    return_value = mrb_str_new(mrb, response, len);
  } else {
    return_value = mrb_str_new(mrb, "cache", 5);
  }

  INF_TRACE("return");

  pthread_mutex_unlock(&command_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_scheduler_s__command_once(mrb_state *mrb, mrb_value self)
{
  mrb_value command;
  char response[THREAD_COMMAND_MAX_MSG_SIZE] = {0x00};
  char trash[THREAD_COMMAND_MAX_MSG_SIZE] = {0x00};
  mrb_int id = 0, len = 0;
  mrb_value return_value;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&command_exchange_mutex);

  memset(response, 0, sizeof(response));

  mrb_get_args(mrb, "iS", &id, &command);

  len = thread_execution_dequeue(executionQueue, id, 1, response);
  if (len == 0) {
    thread_execution_enqueue(executionQueue, id, 0, RSTRING_PTR(command), RSTRING_LEN(command));
  } else {
    thread_execution_dequeue(executionQueue, id, 0, trash);
  }

  if (len > 0) {
    return_value = mrb_str_new(mrb, response, len);
  } else {
    return_value = mrb_str_new(mrb, "cache", 5);
  }

  INF_TRACE("return");

  pthread_mutex_unlock(&command_exchange_mutex);

  return return_value;
}

static mrb_value
mrb_thread_scheduler_s__execute(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, len = 0;
  mrb_value block, obj;
  char command[THREAD_COMMAND_MAX_MSG_SIZE] = {0x00};
  executionMessage *local = NULL;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&command_exchange_mutex);

  mrb_get_args(mrb, "i&", &id, &block);

  if (mrb_nil_p(block) && executionQueue != NULL)
  {
    INF_TRACE("return");

    pthread_mutex_unlock(&command_exchange_mutex);

    return mrb_false_value();
  }

  if (executionQueue && executionQueue->size > 0) {
    local = executionQueue->first;
    while (local != NULL) {
      if (local->executed == 0 && local->commandLen > 0 && (id == 0 || local->id == id)) {
        len = thread_execution_get(executionQueue, local->id, 0, command);
        if (len > 0) {
          /* maybe free this obj */ /* <- ??? */
          obj = mrb_yield(mrb, block, mrb_str_new(mrb, command, len));
          if (mrb_string_p(obj)) {
            thread_execution_enqueue(executionQueue, local->id, 1, RSTRING_PTR(obj), RSTRING_LEN(obj));
          }
        }
      }
      local = local->rear;
    }
  } else {
    INF_TRACE("return");

    pthread_mutex_unlock(&command_exchange_mutex);

    return mrb_false_value();
  }

  INF_TRACE("return");

  pthread_mutex_unlock(&command_exchange_mutex);

  return mrb_true_value();
}

/********************/
/* Public functions */
/********************/

extern void
mrb_thread_scheduler_init(mrb_state *mrb)
{
  static int mutex_init = 0;

  struct RClass *thread_scheduler;
  struct RClass *thread_channel;
  struct RClass *thread_pub_sub;
  struct RClass *context;

  INF_TRACE_FUNCTION();

  if (!mutex_init)
  {
    pthread_mutex_init(&message_exchange_mutex, NULL);

    pthread_mutex_init(&command_exchange_mutex, NULL);

    mutex_init = 1;
  }

  context          = mrb_define_class(mrb , "Context"   , mrb->object_class);

  thread_channel   = mrb_define_class_under(mrb, context, "ThreadChannel", mrb->object_class);

  mrb_define_class_method(mrb , thread_channel   , "_read"      , mrb_thread_channel_s__read      , MRB_ARGS_REQ(3));
  mrb_define_class_method(mrb , thread_channel   , "_write"     , mrb_thread_channel_s__write     , MRB_ARGS_REQ(4));

  thread_pub_sub   = mrb_define_class_under(mrb, context, "ThreadPubSub", mrb->object_class);

  mrb_define_class_method(mrb , thread_pub_sub   , "_listen"    , mrb_thread_pub_sub_s__listen    , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_pub_sub   , "_publish"   , mrb_thread_pub_sub_s__publish   , MRB_ARGS_REQ(2));
  mrb_define_class_method(mrb , thread_pub_sub   , "_subscribe" , mrb_thread_pub_sub_s__subscribe , MRB_ARGS_NONE());

  thread_scheduler = mrb_define_class(mrb , "ThreadScheduler" , mrb->object_class);

  mrb_define_class_method(mrb , thread_scheduler , "_check"    , mrb_thread_scheduler_s__check    , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_scheduler , "_continue" , mrb_thread_scheduler_s__continue , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_scheduler , "_pause"    , mrb_thread_scheduler_s__pause    , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_scheduler , "_start"    , mrb_thread_scheduler_s__start    , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_scheduler , "_stop"     , mrb_thread_scheduler_s__stop     , MRB_ARGS_REQ(1));

  mrb_define_class_method(mrb , thread_scheduler , "_command"  , mrb_thread_scheduler_s__command  , MRB_ARGS_REQ(2));
  mrb_define_class_method(mrb , thread_scheduler , "_command_once" , mrb_thread_scheduler_s__command_once , MRB_ARGS_REQ(2));
  mrb_define_class_method(mrb , thread_scheduler , "_execute"  , mrb_thread_scheduler_s__execute  , MRB_ARGS_REQ(2));

  INF_TRACE("return");
}
