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
#include "mruby/ext/context_log.h"
#include "mruby/hash.h"
#include "mruby/string.h"
#include "mruby/value.h"
#include "mruby/variable.h"

/**********/
/* Macros */
/**********/

#ifdef _WIN32
#define sleep(x) Sleep(x * 1000)
#define usleep(x) Sleep((x < 1000) ? 1 : (x / 1000))
#endif /* #ifdef _WIN32 */

#define CHANNEL_MAX_MSG_SIZE 100001
#define PUBSUB_SLOTS 10
#define THREAD_BLOCK 0
#define THREAD_COMMAND_MAX_MSG_SIZE 10001
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
    int id;
    int status;
    char command[256];
    char response[256];
    int sem;
} thread;

typedef struct message
{
    int id;
    int len;
    char data[51200];
    struct message *front;
    struct message *rear;
} message;

typedef struct queueMessage
{
    struct message *first;
    struct message *last;
    int sem;
    int size;
} queueMessage;

typedef struct executionMessage
{
    int id;
    int sem;
    int executed;
    char *command;
    int commandLen;
    char *response;
    int responseLen;
    struct executionMessage *front;
    struct executionMessage *rear;
} executionMessage;

typedef struct threadExecutionQueue
{
    struct executionMessage *first;
    struct executionMessage *last;
    int sem;
    int size;
} threadExecutionQueue;

/********************/
/* Global variables */
/********************/

static struct queueMessage *connThreadEvents[PUBSUB_SLOTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static struct queueMessage *connThreadQueueRecv = NULL;

static struct queueMessage *connThreadQueueSend = NULL;

static struct thread *CommunicationThread = NULL; /* TODO: */

static struct thread *StatusBarThread = NULL; /* TODO: */

static struct threadExecutionQueue *executionQueue = NULL; /* TODO: */

static pthread_mutex_t thread_scheduler_mutex;

/***********************/
/* Function prototypes */
/***********************/

thread *context_thread_new(int id, int status)
{
    thread *threadControl = (thread *)malloc(sizeof(thread));

    threadControl->id = id;
    threadControl->sem = THREAD_BLOCK;
    threadControl->status = status;
    memset(threadControl->response, 0, sizeof(threadControl->response));
    memset(threadControl->command, 0, sizeof(threadControl->command));

    return threadControl;
}

/**
 * @brief 
 */
void context_thread_sem_push(thread *threadControl)
{
    if (threadControl)
        threadControl->sem = THREAD_FREE;
}

/**
 * @brief 
 */
int context_thread_sem_wait(thread *threadControl, int timeout_msec)
{
    int attempts = 6000; // 6000 * 50 ms = 5 minutes
    int i = 1;

    if (timeout_msec > 0)
        attempts = timeout_msec / 50;

    if (threadControl)
    {
        while (threadControl->sem == THREAD_BLOCK)
        {
            usleep(50000);
            i++;
            if (i >= attempts)
                return -1;
        }
        threadControl->sem = THREAD_BLOCK;
    }
    return 1;
}

/**
 * @brief 
 */
int context_thread_pause(thread *threadControl)
{
    int attempt = 1, attempts = 10, ret = 0;

    if (threadControl != NULL && threadControl->status != THREAD_STATUS_DEAD)
    {
        while (threadControl->status == THREAD_STATUS_ALIVE && attempt <= attempts)
        {
            usleep(10000);
            attempt++;
        }

        context_thread_sem_wait(threadControl, 0);
        if (threadControl->status == THREAD_STATUS_ALIVE)
        {
            threadControl->status = THREAD_STATUS_PAUSE;
            ret = 1;
        }
        context_thread_sem_push(threadControl);
    }

    return ret;
}

/**
 * @brief 
 */
int context_thread_continue(thread *threadControl)
{
    if (threadControl != NULL && threadControl->status == THREAD_STATUS_PAUSE)
    {
        context_thread_sem_wait(threadControl, 0);
        threadControl->status = THREAD_STATUS_ALIVE;
        context_thread_sem_push(threadControl);
        return 1;
    }
    else
    {
        return 0;
    }
}

/**
 * @brief 
 */
queueMessage *context_channel_new(void)
{
    queueMessage *queue = (queueMessage *)malloc(sizeof(queueMessage));

    queue->size = 0;
    queue->first = NULL;
    queue->last = NULL;
    queue->sem = THREAD_BLOCK;
    return queue;
}

/**
 * @brief 
 */
void context_channel_sem_wait(queueMessage *threadControl)
{
    if (threadControl != NULL)
    {
        while (threadControl->sem == THREAD_BLOCK)
            usleep(50000);
        threadControl->sem = THREAD_BLOCK;
    }
}

/**
 * @brief 
 */
int context_channel_is_thread_block(queueMessage *threadControl)
{
    if (threadControl->sem == THREAD_BLOCK)
        return TRUE;
    else
        return FALSE;
}

/**
 * @brief 
 */
void context_channel_sem_push(queueMessage *threadControl)
{
    if (threadControl)
        threadControl->sem = THREAD_FREE;
}

/**
 * @brief 
 */
void thread_channel_debug(queueMessage *queue)
{
    struct message *local = NULL;

    local = queue->first;
    while (local != NULL)
    {
        local = local->rear;
    }
}

/**
 * @brief 
 */
int thread_channel_dequeue(queueMessage *queue, int *id, char *buf)
{
    int len = 0;
    struct message *local = NULL;

    if (queue != NULL && queue->size > 0 && queue->size < 1000 && !context_channel_is_thread_block(queue))
    {
        context_channel_sem_wait(queue);
        local = queue->first;

        while (local != NULL)
        {
            if ((id == NULL || *id == 0 || local->id == *id) && local->len > 0)
            {
                if (local->len < CHANNEL_MAX_MSG_SIZE)
                {
                    memcpy(buf, local->data, local->len);
                    len = local->len;
                }
                else
                {
                    len = 0;
                }

                if (local->front == NULL)
                {
                    queue->first = local->rear;
                }
                else
                {
                    if (local->rear != NULL)
                        local->rear->front = NULL;
                }

                if (local->rear == NULL)
                {
                    queue->last = local->front;
                }
                else
                {
                    if (local->front != NULL)
                        local->front->rear = NULL;
                }

                queue->size--;
                if (id != NULL)
                    (*id) = local->id;
                free(local);

                break;
            }
            local = local->rear;
        }

        context_channel_sem_push(queue);
    }
    return len;
}

/**
 * @brief 
 */
int thread_channel_enqueue(struct queueMessage *queue, int id, char *buf, int len)
{
    struct message *newMessage = NULL;

    if (len > 0 && len < CHANNEL_MAX_MSG_SIZE)
    {
        context_channel_sem_wait(queue);
        newMessage = (message *)malloc(sizeof(message));

        /* Copy message */
        memset(newMessage->data, 0, sizeof(newMessage->data));
        memcpy(newMessage->data, buf, len);
        newMessage->len = len;
        newMessage->id = id;
        newMessage->front = NULL;
        newMessage->rear = NULL;

        if (queue->size == 0)
        {
            queue->first = newMessage;
            queue->last = newMessage;
            queue->size = 1;
        }
        else
        {
            /* Populate last, queue and node */
            queue->last->rear = newMessage;
            newMessage->front = queue->last;
            queue->last = newMessage;
            queue->size++;
        }

        context_channel_sem_push(queue);
    }

    return len;
}

/**
 * @brief 
 */
void thread_channel_clean(struct queueMessage *queue)
{
    char trash[CHANNEL_MAX_MSG_SIZE] = {0x00};
    int len = 1, id = 0;

    while (len != 0)
    {
        id = 0;
        memset(trash, 0, sizeof(trash));
        len = thread_channel_dequeue(queue, &id, trash);
    }
}

/**
 * @brief 
 */
threadExecutionQueue *thread_execution_new(void)
{
    threadExecutionQueue *queue = (threadExecutionQueue *)malloc(sizeof(threadExecutionQueue));

    queue->size = 0;
    queue->first = NULL;
    queue->last = NULL;
    queue->sem = THREAD_BLOCK;
    return queue;
}

/**
 * @brief 
 */
void thread_execution_sem_wait(threadExecutionQueue *threadControl)
{
    if (threadControl)
    {
        while (threadControl->sem == THREAD_BLOCK)
            usleep(50000);
        threadControl->sem = THREAD_BLOCK;
    }
}

/**
 * @brief 
 */
void thread_execution_sem_push(threadExecutionQueue *threadControl)
{
    if (threadControl)
        threadControl->sem = THREAD_FREE;
}

/**
 * @brief 
 */
executionMessage *thread_execution_message_new(int id)
{
    executionMessage *message = (executionMessage *)malloc(sizeof(executionMessage));

    message->id = id;
    message->sem = THREAD_BLOCK;
    message->rear = NULL;
    message->front = NULL;
    message->command = NULL;
    message->commandLen = 0;
    message->response = NULL;
    message->responseLen = 0;
    message->executed = 0;

    return message;
}

/**
 * @brief 
 */
void thread_execution_message_sem_wait(executionMessage *threadControl)
{
    if (threadControl)
    {
        while (threadControl->sem == THREAD_BLOCK)
            usleep(50000);
        threadControl->sem = THREAD_BLOCK;
    }
}

/**
 * @brief 
 */
void thread_execution_message_sem_push(executionMessage *threadControl)
{
    if (threadControl)
        threadControl->sem = THREAD_FREE;
}

/**
 * @brief 
 */
int thread_execution_enqueue(struct threadExecutionQueue *queue, int id, int command, char *buf, int len)
{
    struct executionMessage *message = NULL;
    struct executionMessage *local = NULL;

    if (queue == NULL)
        return 0;

    /* Search for already schedule execution */
    if (queue && queue->size > 0)
    {
        local = queue->first;
        while (local != NULL)
        {
            if (local->id == id)
            {
                message = local;
                break;
            }
            local = local->rear;
        }
    }

    if (message != NULL)
    {
        thread_execution_message_sem_wait(message);
    }
    else
    {
        thread_execution_sem_wait(queue);
        message = thread_execution_message_new(id);
        if (queue->size == 0)
        {
            queue->first = message;
            queue->last = message;
            queue->size = 1;
        }
        else
        {
            queue->last->rear = message;
            message->front = queue->last;
            queue->last = message;
            queue->size++;
        }
        thread_execution_sem_push(queue);
    }

    /* Copy command/response to message */
    if (command == 0)
    {
        message->command = (char *)realloc(message->command, len);
        memcpy(message->command, buf, len);
        message->commandLen = len;
        message->executed = 0;
    }
    else
    {
        message->response = (char *)realloc(message->response, len);
        memcpy(message->response, buf, len);
        message->responseLen = len;
        message->executed = 1;
    }

    thread_execution_message_sem_push(message);

    return len;
}

/**
 * @brief 
 */
int thread_execution_get(struct threadExecutionQueue *queue, int id, int command, char *buf)
{
    int len = 0;
    struct executionMessage *message = NULL;
    struct executionMessage *local = NULL;

    /* Search for already schedule execution */
    if (queue && queue->size > 0)
    {
        local = queue->first;
        while (local != NULL)
        {
            if (local->id == id)
            {
                message = local;
                break;
            }
            local = local->rear;
        }
    }
    if (message == NULL)
        return len;

    /* Get information inside of struct synchronizing */
    if (command == 0 && message->commandLen >= 0 && message->executed == 0)
    {
        if (message->commandLen > 0)
        {
            memcpy(buf, message->command, message->commandLen);
            len = message->commandLen;
            thread_execution_message_sem_wait(message);
            message->executed = 0;
            thread_execution_message_sem_push(message);
        }
    }
    else if (command == 1 && message->responseLen >= 0 && message->executed == 1)
    {
        if (message->responseLen > 0)
        {
            thread_execution_message_sem_wait(message);
            memcpy(buf, message->response, message->responseLen);
            len = message->responseLen;
            message->executed = 1;
            thread_execution_message_sem_push(message);
        }
    }

    return len;
}

/**
 * @brief 
 */
int thread_execution_dequeue(struct threadExecutionQueue *queue, int id, int command, char *buf)
{
    int len = 0;
    struct executionMessage *message = NULL;
    struct executionMessage *local = NULL;

    /* Search for already schedule execution */
    if (queue && queue->size > 0)
    {
        local = queue->first;
        while (local != NULL)
        {
            if (local->id == id)
            {
                message = local;
                break;
            }
            local = local->rear;
        }
    }
    if (message == NULL)
        return len;

    /* Get information inside of struct synchronizing */
    if (command == 0 && message && message->commandLen > 0 && message->executed == 0 && message->command)
    {
        memcpy(buf, message->command, message->commandLen);
        len = message->commandLen;

        thread_execution_message_sem_wait(message);
        free(message->command);
        message->executed = 0;
        message->command = NULL;
        message->commandLen = 0;
        thread_execution_message_sem_push(message);
    }
    else if (command == 1 && message && message->responseLen > 0 && message->executed == 1 && message->response)
    {
        thread_execution_message_sem_wait(message);
        memcpy(buf, message->response, message->responseLen);
        len = message->responseLen;
        message->executed = 1;
        free(message->response);
        message->response = NULL;
        message->responseLen = 0;
        thread_execution_message_sem_push(message);
    }

    if (message && message->command == NULL && message->response == NULL)
    {
        thread_execution_sem_wait(queue);

        if (message->front == NULL)
            queue->first = message->rear;
        else if (message->rear != NULL)
            message->rear->front = NULL;

        if (message->rear == NULL)
            queue->last = message->front;
        else if (message->front != NULL)
            message->front->rear = NULL;

        queue->size--;
        free(message);

        thread_execution_sem_push(queue);
    }

    return len;
}

/**
 * @brief 
 */
void thread_execution_clean(struct threadExecutionQueue *queue)
{
    char trash[CHANNEL_MAX_MSG_SIZE] = {0x00};
    int len = 1, id = 0;

    while (len != 0)
    {
        id = 0;
        memset(trash, 0, sizeof(trash));
        len = thread_execution_dequeue(queue, id, 0, trash);
        memset(trash, 0, sizeof(trash));
        if (len > 0)
            thread_execution_dequeue(queue, id, 1, trash);
    }
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_scheduler_s__check(mrb_state *mrb, mrb_value self)
{
    mrb_int status = 0, id = 0, ret = 1, timeout = 0;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    mrb_get_args(mrb, "ii", &id, &timeout);

    if (id == THREAD_STATUS_BAR && StatusBarThread)
    {
        ret = context_thread_sem_wait(StatusBarThread, timeout);
        if (ret == 1)
        {
            status = StatusBarThread->status;
            context_thread_sem_push(StatusBarThread);
        }
    }
    else if (id == THREAD_COMMUNICATION && CommunicationThread)
    {
        ret = context_thread_sem_wait(CommunicationThread, timeout);
        if (ret == 1)
        {
            status = CommunicationThread->status;
            context_thread_sem_push(CommunicationThread);
        }
    }
    else
    {
        status = THREAD_STATUS_DEAD;
    }

    if (ret == -1)
        return_value = mrb_fixnum_value(THREAD_STATUS_BLOCK);
    else
        return_value = mrb_fixnum_value(status);

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_scheduler_s__start(mrb_state *mrb, mrb_value self)
{
    mrb_int id = 0;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    mrb_get_args(mrb, "i", &id);

    return_value = mrb_true_value();

    if (id == THREAD_STATUS_BAR)
    {
        if (StatusBarThread)
            free(StatusBarThread);
        StatusBarThread = context_thread_new(id, THREAD_FREE);
        context_thread_sem_push(StatusBarThread);
    }
    else if (id == THREAD_COMMUNICATION)
    {
        if (CommunicationThread)
        {
            context_thread_sem_wait(CommunicationThread, 0);
            free(CommunicationThread);
            CommunicationThread = NULL;
        }

        if (connThreadQueueRecv)
        {
            thread_channel_clean(connThreadQueueRecv);
            free(connThreadQueueRecv);
            connThreadQueueRecv = NULL;
        }

        if (connThreadQueueSend)
        {
            thread_channel_clean(connThreadQueueSend);
            free(connThreadQueueSend);
            connThreadQueueSend = NULL;
        }

        if (executionQueue)
        {
            thread_execution_clean(executionQueue);
            free(executionQueue);
            executionQueue = NULL;
        }

        CommunicationThread = context_thread_new(id, THREAD_FREE);

        connThreadQueueRecv = context_channel_new();

        connThreadQueueSend = context_channel_new();

        executionQueue = thread_execution_new();

        context_thread_sem_push(CommunicationThread);

        context_channel_sem_push(connThreadQueueRecv);

        context_channel_sem_push(connThreadQueueSend);

        thread_execution_sem_push(executionQueue);
    }
    else
    {
        return_value = mrb_false_value();
    }

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_scheduler_s__stop(mrb_state *mrb, mrb_value self)
{
    mrb_int eventId = 0, id = 0;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    mrb_get_args(mrb, "i", &id);

    if (id == THREAD_STATUS_BAR && StatusBarThread)
    {
        context_thread_sem_wait(StatusBarThread, 0);
        StatusBarThread->status = THREAD_STATUS_DEAD;
        context_thread_sem_push(StatusBarThread);
    }
    else if (id == THREAD_COMMUNICATION && CommunicationThread)
    {
        context_thread_sem_wait(CommunicationThread, 0);
        CommunicationThread->status = THREAD_STATUS_DEAD;

        if (connThreadQueueRecv)
        {
            thread_channel_clean(connThreadQueueRecv);
            free(connThreadQueueRecv);
            connThreadQueueRecv = NULL;
        }

        if (connThreadQueueSend)
        {
            thread_channel_clean(connThreadQueueSend);
            free(connThreadQueueSend);
            connThreadQueueSend = NULL;
        }

        while (connThreadEvents[eventId] != NULL)
        {
            thread_channel_clean(connThreadEvents[eventId]);
            free(connThreadEvents[eventId]);
            connThreadEvents[eventId] = NULL;
            eventId++;
        };

        context_thread_sem_push(CommunicationThread);
    }

    return_value = mrb_true_value();

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_scheduler_s__pause(mrb_state *mrb, mrb_value self)
{
    mrb_int id = 0, pause = 0;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    mrb_get_args(mrb, "i", &id);

    if (id == THREAD_STATUS_BAR)
        pause = context_thread_pause(StatusBarThread);
    else if (id == THREAD_COMMUNICATION)
        pause = context_thread_pause(CommunicationThread);

    if (pause == 1)
        return_value = mrb_true_value();
    else
        return_value = mrb_false_value();

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_scheduler_s__continue(mrb_state *mrb, mrb_value self)
{
    mrb_int id = 0, ret = 0;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

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

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_channel_s__write(mrb_state *mrb, mrb_value self)
{
    mrb_int id = 0, channel = 0, eventId = 0, len = 0;
    mrb_value value;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    mrb_get_args(mrb, "iiiS", &id, &channel, &eventId, &value);

    if (channel == 0)
    {
        len = thread_channel_enqueue(connThreadQueueSend, eventId, RSTRING_PTR(value), RSTRING_LEN(value));
    }
    else
    {
        len = thread_channel_enqueue(connThreadQueueRecv, eventId, RSTRING_PTR(value), RSTRING_LEN(value));
    }

    return_value = mrb_fixnum_value(len);

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_channel_s__read(mrb_state *mrb, mrb_value self)
{
    mrb_int id = 0, len = 0, channel = 0, eventId = 0;
    char buf[CHANNEL_MAX_MSG_SIZE] = {0x00};
    mrb_value array;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    mrb_get_args(mrb, "iii", &id, &channel, &eventId);

    if (channel == 0)
    {
        len = thread_channel_dequeue(connThreadQueueSend, &eventId, buf);
    }
    else
    {
        len = thread_channel_dequeue(connThreadQueueRecv, &eventId, buf);
    }

    array = mrb_ary_new(mrb);
    mrb_ary_push(mrb, array, mrb_fixnum_value(eventId));
    if (len > 0)
    {
        mrb_ary_push(mrb, array, mrb_str_new(mrb, buf, len));
    }

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return array;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_scheduler_s__command(mrb_state *mrb, mrb_value self)
{
    mrb_value command;
    char response[THREAD_COMMAND_MAX_MSG_SIZE] = {0x00};
    mrb_int id = 0, len = 0;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    memset(response, 0, sizeof(response));

    mrb_get_args(mrb, "iS", &id, &command);

    len = thread_execution_get(executionQueue, id, 1, response);
    thread_execution_enqueue(executionQueue, id, 0, RSTRING_PTR(command), RSTRING_LEN(command));

    if (len > 0)
    {
        return_value = mrb_str_new(mrb, response, len);
    }
    else
    {
        return_value = mrb_str_new(mrb, "cache", 5);
    }

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_scheduler_s__command_once(mrb_state *mrb, mrb_value self)
{
    mrb_value command;
    char response[THREAD_COMMAND_MAX_MSG_SIZE] = {0x00};
    char trash[THREAD_COMMAND_MAX_MSG_SIZE] = {0x00};
    mrb_int id = 0, len = 0;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    memset(response, 0, sizeof(response));

    mrb_get_args(mrb, "iS", &id, &command);

    len = thread_execution_dequeue(executionQueue, id, 1, response);
    if (len == 0)
    {
        thread_execution_enqueue(executionQueue, id, 0, RSTRING_PTR(command), RSTRING_LEN(command));
    }
    else
    {
        thread_execution_dequeue(executionQueue, id, 0, trash);
    }

    if (len > 0)
    {
        return_value = mrb_str_new(mrb, response, len);
    }
    else
    {
        return_value = mrb_str_new(mrb, "cache", 5);
    }

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_scheduler_s__execute(mrb_state *mrb, mrb_value self)
{
    mrb_int id = 0, len = 0;
    mrb_value block, obj;
    char command[THREAD_COMMAND_MAX_MSG_SIZE] = {0x00};
    struct executionMessage *local = NULL;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    mrb_get_args(mrb, "i&", &id, &block);

    if (mrb_nil_p(block) && executionQueue != NULL)
    {
        PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

        return mrb_false_value();
    }

    if (executionQueue && executionQueue->size > 0)
    {
        local = executionQueue->first;
        while (local != NULL)
        {
            if (local->executed == 0 && local->commandLen > 0 && (id == 0 || local->id == id))
            {
                len = thread_execution_get(executionQueue, local->id, 0, command);
                if (len > 0)
                {
                    /* maybe free this obj */ /* <- ??? */
                    obj = mrb_yield(mrb, block, mrb_str_new(mrb, command, len));
                    if (mrb_string_p(obj))
                    {
                        thread_execution_enqueue(executionQueue, local->id, 1, RSTRING_PTR(obj), RSTRING_LEN(obj));
                    }
                }
            }
            local = local->rear;
        }
    }
    else
    {
        PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

        return mrb_false_value();
    }

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return mrb_true_value();
}

/**
 * @brief 
 */
int subscribe(void)
{
    int id = 0;

    while (connThreadEvents[id] != NULL)
    {
        id++;
        if (id >= 10)
        {
            id = -1;
            break;
        }
    }

    if (id >= 0)
    {
        connThreadEvents[id] = context_channel_new();

        context_channel_sem_push(connThreadEvents[id]);
    }

    return id;
}

/**
 * @brief 
 */
int pubsub_publish(char *buf, int len, int avoid_id)
{
    int id = 0, ret = 0;

    while (connThreadEvents[id] != NULL && id < 10)
    {
        if (id != avoid_id)
        {
            ret = thread_channel_enqueue(connThreadEvents[id], 0, buf, len);
        }
        id++;
    }

    return ret;
}

/**
 * @brief 
 */
int pubsub_listen(int id, char *buf)
{
    int eventId = 0;
    int ret = 0;

    if (connThreadEvents[id] != NULL)
    {
        ret = thread_channel_dequeue(connThreadEvents[id], &eventId, buf);
    }

    return ret;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_pub_sub_s__subscribe(mrb_state *mrb, mrb_value self)
{
    mrb_int i;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    i = subscribe();

    return_value = mrb_fixnum_value(i);

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_pub_sub_s__listen(mrb_state *mrb, mrb_value self)
{
    mrb_int id = 0, len = 0;
    char buf[CHANNEL_MAX_MSG_SIZE] = {0x00};
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    mrb_get_args(mrb, "i", &id);

    memset(buf, 0, sizeof(buf));

    len = pubsub_listen(id, buf);

    if (len > 0)
        return_value = mrb_str_new(mrb, buf, len);
    else
        return_value = mrb_nil_value();

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
static mrb_value
mrb_thread_pub_sub_s__publish(mrb_state *mrb, mrb_value self)
{
    mrb_int len = 0;
    mrb_value avoid_id;
    mrb_value buf;
    mrb_value return_value;

    PTHREAD_MUTEX_LOCK(&thread_scheduler_mutex);

    mrb_get_args(mrb, "So", &buf, &avoid_id);

    if (mrb_string_p(buf))
    {
        if (mrb_fixnum_p(avoid_id))
        {
            len = pubsub_publish(RSTRING_PTR(buf), RSTRING_LEN(buf), mrb_fixnum(avoid_id));
        }
        else
        {
            len = pubsub_publish(RSTRING_PTR(buf), RSTRING_LEN(buf), -1);
        }
        if (len > 0)
        {
            return_value = mrb_true_value();
        }
    }

    return_value = mrb_false_value();

    PTHREAD_MUTEX_UNLOCK(&thread_scheduler_mutex);

    return return_value;
}

/**
 * @brief 
 */
void mrb_thread_scheduler_init(mrb_state *mrb)
{
    static int mutex_init = 0;

    struct RClass *thread_scheduler;
    struct RClass *thread_channel;
    struct RClass *thread_pub_sub;
    struct RClass *context;

    if (!mutex_init)
    {
        PTHREAD_MUTEX_INIT(&thread_scheduler_mutex, NULL);

        mutex_init = 1;
    }

    context = mrb_define_class(mrb, "Context", mrb->object_class);
    thread_scheduler = mrb_define_class(mrb, "ThreadScheduler", mrb->object_class);
    thread_channel = mrb_define_class_under(mrb, context, "ThreadChannel", mrb->object_class);
    thread_pub_sub = mrb_define_class_under(mrb, context, "ThreadPubSub", mrb->object_class);

    mrb_define_class_method(mrb, thread_scheduler, "_check", mrb_thread_scheduler_s__check, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, thread_scheduler, "_start", mrb_thread_scheduler_s__start, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, thread_scheduler, "_stop", mrb_thread_scheduler_s__stop, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, thread_scheduler, "_pause", mrb_thread_scheduler_s__pause, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, thread_scheduler, "_continue", mrb_thread_scheduler_s__continue, MRB_ARGS_REQ(1));

    mrb_define_class_method(mrb, thread_scheduler, "_command", mrb_thread_scheduler_s__command, MRB_ARGS_REQ(2));
    mrb_define_class_method(mrb, thread_scheduler, "_command_once", mrb_thread_scheduler_s__command_once, MRB_ARGS_REQ(2));
    mrb_define_class_method(mrb, thread_scheduler, "_execute", mrb_thread_scheduler_s__execute, MRB_ARGS_REQ(2));

    mrb_define_class_method(mrb, thread_channel, "_write", mrb_thread_channel_s__write, MRB_ARGS_REQ(4));
    mrb_define_class_method(mrb, thread_channel, "_read", mrb_thread_channel_s__read, MRB_ARGS_REQ(3));

    mrb_define_class_method(mrb, thread_pub_sub, "_subscribe", mrb_thread_pub_sub_s__subscribe, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, thread_pub_sub, "_listen", mrb_thread_pub_sub_s__listen, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, thread_pub_sub, "_publish", mrb_thread_pub_sub_s__publish, MRB_ARGS_REQ(2));
}
