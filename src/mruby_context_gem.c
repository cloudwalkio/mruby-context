/**
 * @file mruby_context_gem.c
 * @brief mruby-context entry point.
 * @platform Pax Prolin
 * @date 2014-12-04
 *
 * @copyright Copyright (c) 2014 CloudWalk, Inc.
 *
 */

#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "mruby.h"
#include "mruby/array.h"
#include "mruby/compile.h"
#include "mruby/dump.h"
#include "mruby/error.h"
#include "mruby/ext/context_log.h"
#include "mruby/ext/infinite.h"
#include "mruby/string.h"
#include "mruby/variable.h"

/**********/
/* Macros */
/**********/

#define DONE mrb_gc_arena_restore(mrb, 0);

/********************/
/* Type definitions */
/********************/

typedef struct instance
{
    char application[256];
    mrbc_context *context;
    mrb_state *mrb;
    int outdated;
} instance;

/* typedef */ struct memheader
{
    size_t len; /* size of obj (not including len) */
    union /* union for alignment */
    {
        void *ptr;
        long long l;
        double d;
    } obj;
} /* memheader */;

/* typedef */ struct memprof_userdata
{
    unsigned int malloc_cnt;
    unsigned int realloc_cnt;
    unsigned int free_cnt;
    unsigned int freezero_cnt;
    unsigned long long total_size;
    unsigned int current_objcnt;
    unsigned long long current_size;
} /* memprof_userdata */;

/********************/
/* Global variables */
/********************/

/* Static */

static pthread_mutex_t context_mutex;

static struct instance *instances[20]; /* Note: functions mentioning it should
                                        * be mutex protected! */

/***********************/
/* Function prototypes */
/***********************/

extern void context_memprof_init(mrb_allocf *, void **);

extern void mrb_thread_scheduler_init(mrb_state *mrb);

/*********************/
/* Private functions */
/*********************/

/**
 * @brief 
 */
static void *
context_memprof_allocf(struct mrb_state *mrb, void *ptr, size_t size, void *ud0)
{
    struct memprof_userdata *ud = ud0;
    struct memheader *mptr;
    size_t oldsize = 0;

    if (ptr != NULL)
    {
        mptr = (struct memheader *) ((char *) ptr - offsetof(struct memheader, obj));
    }
    else
    {
        mptr = NULL;
    }

    if (size == 0)
    {
        /* free(ptr) */
        ud->free_cnt++;

        if (mptr != NULL)
        {
            ud->current_objcnt--;
            ud->current_size -= mptr->len;
            mptr->len = SIZE_MAX;
            free(mptr);
        }
        else
        {
            ud->freezero_cnt++;
        }
        return NULL;
    }
    else
    {
        if (size >= 1000000) /* checking memory leak size */
        {
            return NULL;
        }

        if (ptr == NULL)
        {
            ud->malloc_cnt++;
        }
        else
        {
            ud->realloc_cnt++;
            oldsize = mptr->len;
        }

        mptr = realloc(mptr, size + sizeof(*mptr) - sizeof(mptr->obj));

        if (mptr == NULL)
        {
            return NULL;
        }

        mptr->len = size;

        if (ptr == NULL)
        {
            ud->current_objcnt++;
        }
        else
        {
            ud->current_size -= oldsize;
        }

        ud->current_size += size;
        ud->total_size += size;

        return (void *)&mptr->obj;
    }
}

/**
 * @brief 
 */
void context_memprof_init(mrb_allocf *funp, void **udp)
{
    struct memprof_userdata *ud;

    ud = calloc(1, sizeof(*ud));

    if (!ud)
    {
        abort();
    }

    *funp = context_memprof_allocf;
    *udp = ud;
}

/**
 * @brief 
 */
static instance *
mrb_alloc_instance(char *application_name, int application_size, mrb_state *mrb)
{
    int i = 0;
    void *ud;
    instance *current;
    mrb_allocf allocf;
    int instance_free_spot = -1;

    INF_TRACE("");

    PTHREAD_MUTEX_LOCK(&context_mutex);

    while (i < 20)
    {
        if (instances[i] != NULL && strcmp(instances[i]->application, application_name) == 0)
        {
            PTHREAD_MUTEX_UNLOCK(&context_mutex);

            return instances[i];
        }
        if (instance_free_spot == -1 && instances[i] == NULL)
            instance_free_spot = i;
        i++;
    }

    INF_TRACE("instance_free_spot [%d], i [%d]", instance_free_spot, i);

    current = (instance *) malloc(sizeof(instance));

    context_memprof_init(&allocf, &ud);
    current->mrb = mrb_open_allocf(allocf, ud);

    current->context = mrbc_context_new(current->mrb);
    current->context->capture_errors = TRUE;
    current->context->no_optimize = TRUE;
    current->outdated = FALSE;
    memset(current->application, 0, 256);
    strcpy(current->application, application_name);

    instances[instance_free_spot] = current;

    INF_TRACE("returning mrb_nil_value()");

    PTHREAD_MUTEX_UNLOCK(&context_mutex);

    return current;
}

/**
 * @brief 
 */
static void
mrb_free_instance(instance *current)
{
    mrbc_context_free(current->mrb, current->context);
    mrb_close(current->mrb);
    free(current);
}

/**
 * @brief 
 */
static mrb_value
mrb_mrb_eval(mrb_state *mrb, mrb_value self)
{
    mrb_value code, ret, mrb_ret, application;
    instance *current;

    mrb_ret = mrb_nil_value();
    mrb_get_args(mrb, "S|S", &code, &application);

    current = mrb_alloc_instance(RSTRING_PTR(application), RSTRING_LEN(application), mrb);

    if (current->outdated)
    {
        if (strcmp(RSTRING_PTR(code), "Context.start") >= 0)
        {
            ret = mrb_true_value();
        }
        else
        {
            mrb_free_instance(current);
            mrb_funcall(mrb, self, "mrb_start", 1, application);
            current = mrb_alloc_instance(RSTRING_PTR(application), RSTRING_LEN(application), mrb);
            ret = mrb_load_nstring_cxt(current->mrb, RSTRING_PTR(code), RSTRING_LEN(code), current->context);
        }
    }
    else
    {
        ret = mrb_load_nstring_cxt(current->mrb, RSTRING_PTR(code), RSTRING_LEN(code), current->context);
    }

    if (mrb_undef_p(ret))
        mrb_ret = mrb_nil_value();
    else if (mrb_string_p(ret))
        mrb_ret = mrb_str_new_cstr(mrb, RSTRING_PTR(ret));
    else
        mrb_ret = ret;

    return mrb_ret;
}

/**
 * @brief 
 */
static mrb_value
mrb_mrb_stop(mrb_state *mrb, mrb_value self)
{
    mrb_value application;
    int i = 0;

    INF_TRACE("");

    PTHREAD_MUTEX_LOCK(&context_mutex);

    mrb_get_args(mrb, "S", &application);

    while (i < 20)
    {
        if (instances[i] != NULL && strcmp(instances[i]->application, RSTRING_PTR(application)) == 0)
        {
            if (instances[i]->mrb == mrb)
            {
                instances[i]->outdated = TRUE;
            }
            else
            {
                mrb_free_instance(instances[i]);
                instances[i] = NULL;
            }
            break;
        }
        i++;
    }

    INF_TRACE("returning mrb_nil_value()");

    PTHREAD_MUTEX_UNLOCK(&context_mutex);

    return mrb_nil_value();
}

/**
 * @brief 
 */
static mrb_value
mrb_mrb_expire(mrb_state *mrb, mrb_value self)
{
    mrb_value application;
    int i = 0;

    INF_TRACE("");

    PTHREAD_MUTEX_LOCK(&context_mutex);

    mrb_get_args(mrb, "S", &application);

    while (i < 20)
    {
        if (instances[i] != NULL && strcmp(instances[i]->application, RSTRING_PTR(application)) == 0)
        {
            instances[i]->outdated = TRUE;
            break;
        }
        i++;
    }

    INF_TRACE("returning mrb_nil_value()");

    PTHREAD_MUTEX_UNLOCK(&context_mutex);

    return mrb_nil_value();
}

/**
 * @brief 
 */
static mrb_value
mrb_vm_s_mallocs(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(((struct memprof_userdata *) mrb->allocf_ud)->malloc_cnt);
}

/**
 * @brief 
 */
static mrb_value
mrb_vm_s_reallocs(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(((struct memprof_userdata *) mrb->allocf_ud)->realloc_cnt);
}

/**
 * @brief 
 */
static mrb_value
mrb_vm_s_frees(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(((struct memprof_userdata *) mrb->allocf_ud)->free_cnt);
}

/**
 * @brief 
 */
static mrb_value
mrb_vm_s_free_not_null(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(((struct memprof_userdata *) mrb->allocf_ud)->free_cnt - ((struct memprof_userdata *) mrb->allocf_ud)->freezero_cnt);
}

/**
 * @brief 
 */
static mrb_value
mrb_vm_s_free_null(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(((struct memprof_userdata *) mrb->allocf_ud)->freezero_cnt);
}

/**
 * @brief 
 */
static mrb_value
mrb_vm_s_total_memory(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(((struct memprof_userdata *) mrb->allocf_ud)->total_size);
}

/**
 * @brief 
 */
static mrb_value
mrb_vm_s_objects(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(((struct memprof_userdata *) mrb->allocf_ud)->current_objcnt);
}

/**
 * @brief 
 */
static mrb_value
mrb_vm_s_current_memory(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(((struct memprof_userdata *) mrb->allocf_ud)->current_size);
}

/**
 * @brief 
 */
void
mrb_mruby_context_gem_init(mrb_state *mrb)
{
    static int mutex_init = 0;

    struct RClass *krn;
    struct RClass *vm;

    inf_debug_init(); /* TODO: move to main()!? */

    INF_TRACE("");

    if (!mutex_init)
    {
        PTHREAD_MUTEX_INIT(&context_mutex, NULL);

        mutex_init = 1;
    }

    krn = mrb->kernel_module;

    vm = mrb_define_module(mrb, "Vm");

    mrb_define_method(mrb, krn, "mrb_eval", mrb_mrb_eval, MRB_ARGS_REQ(1) | MRB_ARGS_OPT(1));
    mrb_define_method(mrb, krn, "mrb_expire", mrb_mrb_expire, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, krn, "mrb_stop", mrb_mrb_stop, MRB_ARGS_REQ(1));

    mrb_define_class_method(mrb, vm, "current_memory", mrb_vm_s_current_memory, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, vm, "free_not_null", mrb_vm_s_free_not_null, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, vm, "free_null", mrb_vm_s_free_null, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, vm, "frees", mrb_vm_s_frees, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, vm, "mallocs", mrb_vm_s_mallocs, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, vm, "objects", mrb_vm_s_objects, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, vm, "reallocs", mrb_vm_s_reallocs, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, vm, "total_memory", mrb_vm_s_total_memory, MRB_ARGS_NONE());

    DONE;

    mrb_thread_scheduler_init(mrb);

    DONE;
}

/**
 * @brief 
 */
void
mrb_mruby_context_gem_final(mrb_state *mrb)
{
    INF_TRACE("");

    return; /* Nothing to do! */
}
