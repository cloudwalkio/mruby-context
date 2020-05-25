#include <stdlib.h>
#include <stdio.h>
#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/value.h"
#include "mruby/array.h"
#include "mruby/string.h"
#include "mruby/hash.h"

#include <time.h>
#ifdef _WIN32
    #include <windows.h>
    #define sleep(x) Sleep(x * 1000)
    #define usleep(x) Sleep(((x)<1000) ? 1 : ((x)/1000))
#else
    #include <unistd.h>
    #include <sys/time.h>
#endif

#define THREAD_STATUS_BAR 0
#define THREAD_COMMUNICATION 1

#define THREAD_STATUS_DEAD 0
#define THREAD_STATUS_ALIVE 1
#define THREAD_STATUS_COMMAND 2
#define THREAD_STATUS_RESPONSE 3
#define THREAD_STATUS_PAUSE 4
#define THREAD_STATUS_BLOCK 5

#define THREAD_BLOCK 0
#define THREAD_FREE 1

int connTaskId = 0;
int connIsHandshake = 0;

typedef struct thread {
  int id;
  int status;
  char command[256];
  char response[256];
  int sem;
} thread;

typedef struct message {
  int id;
  int len;
  char data[51200];
  struct message *front;
  struct message *rear;
} message;

typedef struct queueMessage {
  struct message *first;
  struct message *last;
  int sem;
  int size;
} queueMessage;

static struct thread *StatusBarThread           = NULL;
static struct thread *CommunicationThread       = NULL;

static struct queueMessage *connThreadQueueRecv = NULL;
static struct queueMessage *connThreadQueueSend = NULL;

static struct queueMessage *connThreadEvents[10];

/*
 *TODO:
 *  1. Semaphore functions
 *    Context thread and channel message semaphore implementation are beign
 *    developed together, but it may be equal.
 *  2. Unify channel buffers
 *    Functions thread_channel_enqueue_send
 *  3. Unify queues in a generic interface
 *
 */

thread *context_thread_new(int id, int status)
{
  thread *threadControl = (thread*) malloc(sizeof (thread));

  threadControl->id = id;
  threadControl->sem = THREAD_BLOCK;
  threadControl->status = status;
  memset(threadControl->response, 0, sizeof(threadControl->response));
  memset(threadControl->command, 0, sizeof (threadControl->command));

  return threadControl;
}

void context_sem_push(thread *threadControl)
{
  if (threadControl) threadControl->sem = THREAD_FREE;
}

int context_sem_wait(thread *threadControl, int timeout_msec)
{
  int attempts = 6000; // 6000 * 50 ms = 5 minutes
  int i = 1;

  if (timeout_msec > 0) attempts = timeout_msec / 50;

  if (threadControl) {
    while(threadControl->sem == THREAD_BLOCK){
      usleep(50000);
      i++;
      if (i >= attempts) return -1;
    }
    threadControl->sem = THREAD_BLOCK;
  }
  return 1;
}

int context_thread_pause(thread *threadControl)
{
  int attempt = 1, attempts = 10, ret = 0;

  if (threadControl != NULL && threadControl->status != THREAD_STATUS_DEAD) {
    while(threadControl->status == THREAD_STATUS_ALIVE && attempt <= attempts) {
      usleep(10000);
      attempt++;
    }

    context_sem_wait(threadControl, 0);
    if (threadControl->status == THREAD_STATUS_ALIVE) {
      threadControl->status = THREAD_STATUS_PAUSE;
      ret = 1;
    }
    context_sem_push(threadControl);
  }

  return ret;
}

int context_thread_continue(thread *threadControl)
{
  if (threadControl != NULL && threadControl->status == THREAD_STATUS_PAUSE) {
    context_sem_wait(threadControl, 0);
    threadControl->status = THREAD_STATUS_ALIVE;
    context_sem_push(threadControl);
    return 1;
  } else {
    return 0;
  }
}

queueMessage *context_channel_new(void)
{
  queueMessage *queue = (queueMessage*) malloc(sizeof (queueMessage));

  queue->size = 0;
  queue->first = NULL;
  queue->last = NULL;
  queue->sem = THREAD_BLOCK;
  return queue;
}

void context_channel_sem_wait(queueMessage *threadControl)
{
  if (threadControl) {
    while(threadControl->sem == THREAD_BLOCK) usleep(50000);
    threadControl->sem = THREAD_BLOCK;
  }
}

void context_channel_sem_push(queueMessage *threadControl)
{
  if (threadControl) threadControl->sem = THREAD_FREE;
}

void thread_channel_debug(queueMessage *queue)
{
  struct message *local;

  local = queue->first;
  while (local != NULL) {
    /*memcpy(buf, local->data, local->len);*/
    ContextLogFile("0[%d]len:[%d]\n", local, local->len);
    /*ContextLogFile("1[%d]len:[%d]buf:[%s]\n", local, local->len, buf);*/
    ContextLogFile("[%d]front:[%d]rear:[%d]\n", local, local->front, local->rear);
    local = local->rear;
  }
}

int thread_channel_dequeue(queueMessage *queue, int *id, char *buf)
{
  int len=0;
  struct message *local;

  /*thread_channel_debug(queue);*/

  if (queue != NULL && queue->size > 0) {
    context_channel_sem_wait(queue);
    local = queue->first;

    while (local != NULL) {
      if ((id == NULL || *id == 0 || local->id == *id) && local->len > 0) {
        memcpy(buf, local->data, local->len);
        len = local->len;

        if (local->front == NULL) {
          queue->first = local->rear;
        } else {
          if (local->rear != NULL) local->rear->front = NULL;
        }

        if (local->rear  == NULL) {
          queue->last = local->front;
        } else {
          if (local->front != NULL) local->front->rear = NULL;
        }

        queue->size--;
        if (id != NULL) (*id) = local->id;
        free(local);

        break;
      }
      local = local->rear;
    }

    context_channel_sem_push(queue);
  }
  return len;
}

int thread_channel_enqueue(struct queueMessage *queue, int id, char *buf, int len)
{
  struct message *newMessage;

  if (len > 0 && len < 5000) {
    context_channel_sem_wait(queue);
    newMessage = (message *)malloc(sizeof(message));

    /*Copy message*/
    memset(newMessage->data, 0, sizeof(newMessage->data));
    memcpy(newMessage->data, buf, len);
    newMessage->len = len;
    newMessage->id = id;
    newMessage->front = NULL;
    newMessage->rear = NULL;

    if (queue->size == 0) {
    }

    if (queue->size == 0) {
      queue->first = newMessage;
      queue->last = newMessage;
      queue->size = 1;
    } else {
      /*populate last, queue and node*/
      queue->last->rear = newMessage;
      newMessage->front = queue->last;
      queue->last = newMessage;
      queue->size++;
    }

    context_channel_sem_push(queue);
  }

  return len;
}

void thread_channel_clean(struct queueMessage *queue)
{
  char trash[51200] = {0x00};
  int len = 1, id = 0;

  while(len != 0) {
    id = 0;
    memset(trash, 0, sizeof(trash));
    len = thread_channel_dequeue(queue, &id, trash);
  }
}

static mrb_value
mrb_thread_scheduler_s__check(mrb_state *mrb, mrb_value self)
{
  mrb_int status = 0, id = 0, ret = 1, timeout = 0;

  mrb_get_args(mrb, "ii", &id, &timeout);

  if (id == THREAD_STATUS_BAR && StatusBarThread) {
    ret = context_sem_wait(StatusBarThread, timeout);
    if (ret == 1) {
      status = StatusBarThread->status;
      context_sem_push(StatusBarThread);
    }
  } else if (id == THREAD_COMMUNICATION && CommunicationThread) {
    ret = context_sem_wait(CommunicationThread, timeout);
    if (ret == 1) {
      status = CommunicationThread->status;
      context_sem_push(CommunicationThread);
    }
  } else {
    status = THREAD_STATUS_DEAD;
  }

  if (ret == -1)
    return mrb_fixnum_value(THREAD_STATUS_BLOCK);
  else
    return mrb_fixnum_value(status);
}

static mrb_value
mrb_thread_scheduler_s__start(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0;
  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR) {
    if (StatusBarThread) free(StatusBarThread);
    StatusBarThread = context_thread_new(id, THREAD_FREE);
    context_sem_push(StatusBarThread);
  } else if (id == THREAD_COMMUNICATION) {

    if (CommunicationThread) {
      context_sem_wait(CommunicationThread, 0);
      free(CommunicationThread);
      CommunicationThread = NULL;
    }
    if (connThreadQueueRecv) {
      thread_channel_clean(connThreadQueueRecv);
      free(connThreadQueueRecv);
      connThreadQueueRecv = NULL;
    }
    if (connThreadQueueSend) {
      thread_channel_clean(connThreadQueueSend);
      free(connThreadQueueSend);
      connThreadQueueSend = NULL;
    }

    CommunicationThread = context_thread_new(id, THREAD_FREE);
    connThreadQueueRecv = context_channel_new();
    connThreadQueueSend = context_channel_new();

    context_sem_push(CommunicationThread);
    context_channel_sem_push(connThreadQueueRecv);
    context_channel_sem_push(connThreadQueueSend);
  } else {
    return mrb_false_value();
  }

  return mrb_true_value();
}

static mrb_value
mrb_thread_scheduler_s__stop(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0;

  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR && StatusBarThread) {
    context_sem_wait(StatusBarThread, 0);
    StatusBarThread->status = THREAD_STATUS_DEAD;
    context_sem_push(StatusBarThread);

  } else if (id == THREAD_COMMUNICATION && CommunicationThread) {
    context_sem_wait(CommunicationThread, 0);
    CommunicationThread->status = THREAD_STATUS_DEAD;

    if (connThreadQueueRecv) {
      thread_channel_clean(connThreadQueueRecv);
      free(connThreadQueueRecv);
      connThreadQueueRecv = NULL;
    }
    if (connThreadQueueSend) {
      thread_channel_clean(connThreadQueueSend);
      free(connThreadQueueSend);
      connThreadQueueSend = NULL;
    }

    context_sem_push(CommunicationThread);
  }

  return mrb_true_value();
}

static mrb_value
mrb_thread_scheduler_s__pause(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, pause = 0;

  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR)
    pause = context_thread_pause(StatusBarThread);
  else if (id == THREAD_COMMUNICATION)
    pause = context_thread_pause(CommunicationThread);

  if (pause == 1)
    return mrb_true_value();
  else
    return mrb_false_value();
}

static mrb_value
mrb_thread_scheduler_s__continue(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, ret = 0;

  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR) {
    ret = context_thread_continue(StatusBarThread);
  } else if (id == THREAD_COMMUNICATION) {
    ret = context_thread_continue(CommunicationThread);
  }

  if (ret == 1)
    return mrb_true_value();
  else
    return mrb_false_value();
}

static mrb_value
mrb_thread_channel_s__write(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, channel = 0, eventId = 0, len = 0;
  mrb_value value;

  mrb_get_args(mrb, "iiiS", &id, &channel, &eventId, &value);

  if (channel == 0)
    len = thread_channel_enqueue(connThreadQueueSend, eventId, RSTRING_PTR(value), RSTRING_LEN(value));
  else
    len = thread_channel_enqueue(connThreadQueueRecv, eventId, RSTRING_PTR(value), RSTRING_LEN(value));

  return mrb_fixnum_value(len);
}

static mrb_value
mrb_thread_channel_s__read(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, len = 0, channel = 0, eventId = 0;
  char buf[51200] = {0x00};
  mrb_value array;

  mrb_get_args(mrb, "iii", &id, &channel, &eventId);

  if (channel == 0) {
    len = thread_channel_dequeue(connThreadQueueSend, &eventId, buf);
  } else {
    len = thread_channel_dequeue(connThreadQueueRecv, &eventId, buf);
  }


  array = mrb_ary_new(mrb);
  mrb_ary_push(mrb, array, mrb_fixnum_value(eventId));
  if (len > 0) {
    mrb_ary_push(mrb, array, mrb_str_new(mrb, buf, len));
  }

  return array;
}

static int
schedule_command(int id, int tries, int timeout_micro) {
  mrb_int try = 1;

  if (CommunicationThread && CommunicationThread->status != THREAD_STATUS_DEAD) {
    /*Wait for thread that call command collect the response*/
    while(CommunicationThread->status == THREAD_STATUS_RESPONSE && try <= tries) {
      usleep(timeout_micro);
      try++;
    }

    if (CommunicationThread->status == THREAD_STATUS_RESPONSE) {
      context_sem_wait(CommunicationThread, 0);
      if (CommunicationThread->status == THREAD_STATUS_RESPONSE) CommunicationThread->status = THREAD_STATUS_ALIVE;
      context_sem_push(CommunicationThread);
    }

    try = 1;
    if (id == THREAD_COMMUNICATION && CommunicationThread) {
      while(CommunicationThread->status != THREAD_STATUS_ALIVE && try <= tries) {
        usleep(timeout_micro);
        try++;
      }
      if (CommunicationThread->status == THREAD_STATUS_ALIVE) return 0;
    }
  }
  return -1;
}

static mrb_value
mrb_thread_scheduler_s__command(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, try = 1;
  mrb_value command;
  char response[256];

  memset(response, 0, sizeof(response));

  mrb_get_args(mrb, "iS", &id, &command);

  if (schedule_command(id, 20, 10000) == 0) {
    context_sem_wait(CommunicationThread, 0);
    memset(CommunicationThread->command, 0, sizeof(CommunicationThread->command));
    memcpy(CommunicationThread->command, RSTRING_PTR(command), RSTRING_LEN(command));
    CommunicationThread->status = THREAD_STATUS_COMMAND;
    context_sem_push(CommunicationThread);

    while(CommunicationThread->status == THREAD_STATUS_COMMAND && try <= 20) {
      usleep(10000);
      try++;
    }

    context_sem_wait(CommunicationThread, 0);
    if (CommunicationThread->status == THREAD_STATUS_RESPONSE) {
      memcpy(response, CommunicationThread->response, strlen(CommunicationThread->response));
    } else {
      strcpy(response, "cache");
    }
    if (CommunicationThread->status != THREAD_STATUS_DEAD && (CommunicationThread->status == THREAD_STATUS_COMMAND || CommunicationThread->status == THREAD_STATUS_RESPONSE)) {
      CommunicationThread->status = THREAD_STATUS_ALIVE;
    }
    memset(CommunicationThread->response, 0, sizeof(CommunicationThread->response));
    memset(CommunicationThread->command, 0, sizeof(CommunicationThread->command));
    context_sem_push(CommunicationThread);

    return mrb_str_new(mrb, response, strlen(response));
  }

  return mrb_str_new(mrb, "cache", 5);
}

static mrb_value
mrb_thread_scheduler_s__execute(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0;
  mrb_value block, response_object;

  mrb_get_args(mrb, "i&", &id, &block);

  if (mrb_nil_p(block)) {
    return mrb_false_value();
  }

  if (id == THREAD_COMMUNICATION && CommunicationThread && CommunicationThread->status == THREAD_STATUS_COMMAND) {
    /*TODO Scalone check gc arena sabe approach lib/mruby/mrbgems/mruby-string-ext/src/string.c:592*/
    response_object = mrb_yield(mrb, block, mrb_str_new_cstr(mrb, CommunicationThread->command));

    context_sem_wait(CommunicationThread, 0);
    if (CommunicationThread->status == THREAD_STATUS_COMMAND) {
      if (mrb_string_p(response_object)) {
        memcpy(CommunicationThread->response, RSTRING_PTR(response_object), RSTRING_LEN(response_object));
      }
      CommunicationThread->status = THREAD_STATUS_RESPONSE;
    }
    context_sem_push(CommunicationThread);

    return mrb_true_value();
  }

  return mrb_false_value();
}

int subscribe(void)
{
  int id = 0;
  while (connThreadEvents[id] != NULL) { id++; };
  connThreadEvents[id] = context_channel_new();
  context_channel_sem_push(connThreadEvents[id]);
  return id;
}

int pubsub_publish(char *buf, int len)
{
  int id = 0, ret = 0;

  while(connThreadEvents[id] != NULL) {
    ret = thread_channel_enqueue(connThreadEvents[id], 0, buf, len);
    id++;
  };
  return ret;
}

int pubsub_listen(int id, char *buf)
{
  int eventId = 0;
  if (connThreadEvents[id] != NULL)
    return thread_channel_dequeue(connThreadEvents[id], &eventId, buf);
  else
    return 0;
}

static mrb_value
mrb_thread_pub_sub_s_subscribe(mrb_state *mrb, mrb_value self)
{
  return mrb_fixnum_value(subscribe());
}

static mrb_value
mrb_thread_pub_sub_s_listen(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, len = 0;
  char buf[51200] = {0x00};

  mrb_get_args(mrb, "i", &id);

  len = pubsub_listen(id, buf);

  if (len > 0)
    return mrb_str_new(mrb, buf, len);
  else
    return mrb_nil_value();
}

static mrb_value
mrb_thread_pub_sub_s_publish(mrb_state *mrb, mrb_value self)
{
  mrb_int len = 0;
  mrb_value buf;

  mrb_get_args(mrb, "S", &buf);

  if (mrb_string_p(buf)) {
    len = pubsub_publish(RSTRING_PTR(buf), RSTRING_LEN(buf));
    if (len > 0) return mrb_true_value();
  }

  return mrb_false_value();
}

void
mrb_thread_scheduler_init(mrb_state* mrb)
{
  struct RClass *thread_scheduler;
  struct RClass *thread_channel;
  struct RClass *thread_pub_sub;
  struct RClass *context;

  context          = mrb_define_class(mrb , "Context"   , mrb->object_class);
  thread_scheduler = mrb_define_class(mrb , "ThreadScheduler" , mrb->object_class);
  thread_channel   = mrb_define_class_under(mrb, context, "ThreadChannel", mrb->object_class);
  thread_pub_sub   = mrb_define_class_under(mrb, context, "ThreadPubSub", mrb->object_class);

  mrb_define_class_method(mrb , thread_scheduler , "_check"    , mrb_thread_scheduler_s__check    , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_scheduler , "_start"    , mrb_thread_scheduler_s__start    , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_scheduler , "_stop"     , mrb_thread_scheduler_s__stop     , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_scheduler , "_pause"    , mrb_thread_scheduler_s__pause    , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_scheduler , "_continue" , mrb_thread_scheduler_s__continue , MRB_ARGS_REQ(1));

  mrb_define_class_method(mrb , thread_scheduler , "_command"  , mrb_thread_scheduler_s__command  , MRB_ARGS_REQ(2));
  mrb_define_class_method(mrb , thread_scheduler , "_execute"  , mrb_thread_scheduler_s__execute  , MRB_ARGS_REQ(2));

  mrb_define_class_method(mrb , thread_channel   , "_write"     , mrb_thread_channel_s__write     , MRB_ARGS_REQ(4));
  mrb_define_class_method(mrb , thread_channel   , "_read"      , mrb_thread_channel_s__read      , MRB_ARGS_REQ(3));

  mrb_define_class_method(mrb , thread_pub_sub   , "subscribe" , mrb_thread_pub_sub_s_subscribe   , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , thread_pub_sub   , "listen"    , mrb_thread_pub_sub_s_listen      , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_pub_sub   , "publish"   , mrb_thread_pub_sub_s_publish     , MRB_ARGS_REQ(1));
}
