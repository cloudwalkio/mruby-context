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
  int len;
  char data[4096];
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

void context_sem_wait(thread *threadControl)
{
  if (threadControl) {
    while(threadControl->sem == THREAD_BLOCK) usleep(50000);
    threadControl->sem = THREAD_BLOCK;
  }
}

queueMessage *context_channel_new(void)
{
  queueMessage *queue = (queueMessage*) malloc(sizeof (queueMessage));

  queue->size = 0;
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

int thread_channel_enqueue_send(char *buf, int len)
{
  struct message *newMessage;

  if (len > 0) {
    context_channel_sem_wait(connThreadQueueSend);
    newMessage = (message *)malloc(sizeof(message));

    /*Copy message*/
    memset(newMessage->data, 0, sizeof(newMessage->data));
    memcpy(newMessage->data, buf, len);
    newMessage->len = len;

    if (connThreadQueueSend->size == 0) {
      connThreadQueueSend->first = newMessage;
      connThreadQueueSend->last = newMessage;
      connThreadQueueSend->size = 1;
    } else {
      /*populate last, queue and node*/
      connThreadQueueSend->last->rear = newMessage;
      newMessage->front = connThreadQueueSend->last;
      connThreadQueueSend->last = newMessage;
      connThreadQueueSend->size++;
    }

    context_channel_sem_push(connThreadQueueSend);
  }

  return len;
}

int thread_channel_dequeue_send(char *buf)
{
  int len=0;
  struct message *first;
  struct message *local;

  if (connThreadQueueSend->size > 0) {
    context_channel_sem_wait(connThreadQueueSend);

    local = connThreadQueueSend->first;

    memcpy(buf, local->data, local->len);

    len   = local->len;
    first = local->rear;

    connThreadQueueSend->first = first;
    connThreadQueueSend->size--;
    free(local);
    context_channel_sem_push(connThreadQueueSend);
  }
  return len;
}

int thread_channel_enqueue_recv(char *buf, int len)
{
  struct message *newMessage;

  if (len > 0) {
    context_channel_sem_wait(connThreadQueueRecv);
    newMessage = (message *)malloc(sizeof(message));

    /*Copy message*/
    memset(newMessage->data, 0, sizeof(newMessage->data));
    strcpy(newMessage->data, buf);
    newMessage->len = len;

    if (connThreadQueueRecv->size == 0) {
      connThreadQueueRecv->first = newMessage;
      connThreadQueueRecv->last = newMessage;
      connThreadQueueRecv->size = 1;
    } else {
      /*populate last, queue and node*/
      connThreadQueueRecv->last->rear = newMessage;
      newMessage->front = connThreadQueueRecv->last;
      connThreadQueueRecv->last = newMessage;
      connThreadQueueRecv->size++;
    }

    context_channel_sem_push(connThreadQueueRecv);
  }

  return len;
}

int thread_channel_dequeue_recv(char *buf)
{
  int len=0;
  struct message *first;
  struct message *local;

  if (connThreadQueueRecv->size > 0) {
    context_channel_sem_wait(connThreadQueueRecv);

    local = connThreadQueueRecv->first;
    memcpy(buf, local->data, local->len);

    len   = local->len;
    first = local->rear;

    connThreadQueueRecv->first = first;
    connThreadQueueRecv->size--;
    free(local);

    context_channel_sem_push(connThreadQueueRecv);
  }
  return len;
}

static mrb_value
mrb_thread_scheduler_s__check(mrb_state *mrb, mrb_value self)
{
  mrb_int status = 0, id = 0;

  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR) {
    context_sem_wait(StatusBarThread);
    status = StatusBarThread->status;
    context_sem_push(StatusBarThread);
  } else if (id == THREAD_COMMUNICATION) {
    context_sem_wait(CommunicationThread);
    status = CommunicationThread->status;
    context_sem_push(CommunicationThread);
  } else {
    status = THREAD_STATUS_DEAD;
  }

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
    if (CommunicationThread) free(CommunicationThread);
    if (connThreadQueueRecv) free(connThreadQueueRecv);
    if (connThreadQueueSend) free(connThreadQueueSend);

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
    context_sem_wait(StatusBarThread);
    StatusBarThread->status = THREAD_STATUS_DEAD;
    context_sem_push(StatusBarThread);
  } else if (id == THREAD_COMMUNICATION && CommunicationThread) {
    context_sem_wait(CommunicationThread);
    CommunicationThread->status = THREAD_STATUS_DEAD;
    context_sem_push(CommunicationThread);
  }

  return mrb_true_value();
}

static mrb_value
mrb_thread_scheduler_s__pause(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0;

  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR && StatusBarThread) {
    context_sem_wait(StatusBarThread);
    StatusBarThread->status = THREAD_STATUS_PAUSE;
    context_sem_push(StatusBarThread);
  } else if (id == THREAD_COMMUNICATION && CommunicationThread) {
    context_sem_wait(CommunicationThread);
    CommunicationThread->status = THREAD_STATUS_PAUSE;
    context_sem_push(CommunicationThread);
  }

  return mrb_true_value();
}

static mrb_value
mrb_thread_scheduler_s__continue(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0;

  mrb_get_args(mrb, "i", &id);

  if (id == THREAD_STATUS_BAR && StatusBarThread) {
    context_sem_wait(StatusBarThread);
    StatusBarThread->status = THREAD_STATUS_ALIVE;
    context_sem_push(StatusBarThread);
  } else if (id == THREAD_COMMUNICATION && CommunicationThread) {
    context_sem_wait(CommunicationThread);
    CommunicationThread->status = THREAD_STATUS_ALIVE;
    context_sem_push(CommunicationThread);
  }

  return mrb_true_value();
}

static mrb_value
mrb_thread_channel_s_channel_write(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0;
  mrb_value value;

  mrb_get_args(mrb, "iS", &id, &value);

  thread_channel_enqueue_send(RSTRING_PTR(value), RSTRING_LEN(value));

  return mrb_fixnum_value(RSTRING_LEN(value));
}

static mrb_value
mrb_thread_channel_s_channel_read(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, len = 0;
  char buf[12000] = {0x00};

  mrb_get_args(mrb, "i", &id);

  len = thread_channel_dequeue_recv(buf);
  if (len > 0)
    return mrb_str_new(mrb, buf, len);
  else
    return mrb_nil_value();
}

static mrb_value
mrb_thread_channel_s_queue_write(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0;
  mrb_value value;

  mrb_get_args(mrb, "iS", &id, &value);

  thread_channel_enqueue_recv(RSTRING_PTR(value), RSTRING_LEN(value));

  return mrb_fixnum_value(RSTRING_LEN(value));
}

static mrb_value
mrb_thread_channel_s_queue_read(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, len = 0;
  char buf[12000]= {0x00};

  mrb_get_args(mrb, "i", &id);

  len = thread_channel_dequeue_send(buf);
  if (len > 0)
    return mrb_str_new(mrb, buf, len);
  else
    return mrb_nil_value();
}

static mrb_value
mrb_thread_scheduler_s__command(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, try = 1;
  mrb_value command;
  char response[256];

  memset(response, 0, sizeof(response));

  mrb_get_args(mrb, "iS", &id, &command);

  if (id == THREAD_COMMUNICATION && CommunicationThread && CommunicationThread->status == THREAD_STATUS_ALIVE) {
    context_sem_wait(CommunicationThread);
    memcpy(CommunicationThread->command, RSTRING_PTR(command), RSTRING_LEN(command));
    CommunicationThread->status = THREAD_STATUS_COMMAND;
    context_sem_push(CommunicationThread);

    while(CommunicationThread->status == THREAD_STATUS_COMMAND && try <= 20) {
      usleep(10000);
      try++;
    }

    context_sem_wait(CommunicationThread);
    if (CommunicationThread->status == THREAD_STATUS_RESPONSE) {
      memcpy(response, CommunicationThread->response, strlen(CommunicationThread->response));
    } else {
      strcpy(response, "cache");
    }
    if (CommunicationThread->status != THREAD_STATUS_DEAD)
      CommunicationThread->status = THREAD_STATUS_ALIVE;
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
    context_sem_wait(CommunicationThread);
    /*TODO Scalone check gc arena sabe approach lib/mruby/mrbgems/mruby-string-ext/src/string.c:592*/
    response_object = mrb_yield(mrb, block, mrb_str_new_cstr(mrb, CommunicationThread->command));

    if (mrb_string_p(response_object)) {
      memcpy(CommunicationThread->response, RSTRING_PTR(response_object), RSTRING_LEN(response_object));
    }
    CommunicationThread->status = THREAD_STATUS_RESPONSE;
    context_sem_push(CommunicationThread);

    return mrb_true_value();
  }

  return mrb_false_value();
}

int thread_channel_enqueue(struct queueMessage *queue, char *buf, int len)
{
  struct message *newMessage;

  if (len > 0) {
    context_channel_sem_wait(queue);
    newMessage = (message *)malloc(sizeof(message));

    /*Copy message*/
    memset(newMessage->data, 0, sizeof(newMessage->data));
    memcpy(newMessage->data, buf, len);
    newMessage->len = len;

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

int thread_channel_dequeue(queueMessage *queue, char *buf)
{
  int len=0;
  struct message *first;
  struct message *local;

  if (queue->size > 0) {
    context_channel_sem_wait(queue);

    local = queue->first;

    memcpy(buf, local->data, local->len);

    len   = local->len;
    first = local->rear;

    queue->first = first;
    queue->size--;
    free(local);
    context_channel_sem_push(queue);
  }
  return len;
}

int subscribe(void)
{
  int id = 0;
  while (connThreadEvents[id] != NULL) { id++; };
  if (connThreadEvents[id]) free(connThreadEvents[id]);
  connThreadEvents[id] = context_channel_new();
  context_channel_sem_push(connThreadEvents[id]);
  return id;
}

int publish(char *buf, int len)
{
  int id = 0, ret = 0;

  while(connThreadEvents[id] != NULL) {
    ret = thread_channel_enqueue(connThreadEvents[id], buf, len);
    id++;
  };
  return ret;
}

int listen(int id, char *buf)
{
  if (connThreadEvents[id] != NULL)
    return thread_channel_dequeue(connThreadEvents[id], buf);
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
  char buf[12000] = {0x00};

  mrb_get_args(mrb, "i", &id);

  len = listen(id, buf);

  if (len > 0)
    return mrb_str_new(mrb, buf, len);
  else
    return mrb_nil_value();
}

static mrb_value
mrb_thread_pub_sub_s_publish(mrb_state *mrb, mrb_value self)
{
  mrb_int id = 0, len = 0;
  mrb_value buf;

  mrb_get_args(mrb, "S", &buf);

  if (mrb_string_p(buf)) {
    len = publish(RSTRING_PTR(buf), RSTRING_LEN(buf));
    if (len > 0) mrb_true_value();
  }

  mrb_false_value();
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

  mrb_define_class_method(mrb , thread_channel , "channel_write" , mrb_thread_channel_s_channel_write , MRB_ARGS_REQ(2));
  mrb_define_class_method(mrb , thread_channel , "channel_read"  , mrb_thread_channel_s_channel_read  , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_channel , "queue_write"  , mrb_thread_channel_s_queue_write  , MRB_ARGS_REQ(2));
  mrb_define_class_method(mrb , thread_channel , "queue_read"   , mrb_thread_channel_s_queue_read   , MRB_ARGS_REQ(1));

  mrb_define_class_method(mrb , thread_pub_sub , "subscribe" , mrb_thread_pub_sub_s_subscribe , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , thread_pub_sub , "listen"    , mrb_thread_pub_sub_s_listen    , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , thread_pub_sub , "publish"   , mrb_thread_pub_sub_s_publish   , MRB_ARGS_REQ(1));
}
