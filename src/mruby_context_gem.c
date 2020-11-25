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
#include "mruby/ext/context.h"
#include "mruby/ext/context_log.h"
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

static struct instance *instances[20];

/***********************/
/* Function prototypes */
/***********************/

extern void context_memprof_init(mrb_allocf *, void **);

extern void mrb_thread_scheduler_init(mrb_state *mrb);

/*********************/
/* Private functions */
/*********************/

static void *
context_memprof_allocf(struct mrb_state *mrb, void *ptr, size_t size, void *ud0)
{
  struct memprof_userdata *ud = ud0;
  struct memheader *mptr;
  size_t oldsize = 0;

  if (ptr != NULL) {
    mptr = (struct memheader *)((char *)ptr - offsetof(struct memheader, obj));
  } else {
    mptr = NULL;
  }

  if (size == 0) {
    /* free(ptr) */
    ud->free_cnt++;
    if (mptr != NULL) {
      ud->current_objcnt--;
      ud->current_size -= mptr->len;
      mptr->len = SIZE_MAX;
      free(mptr);
    } else {
      ud->freezero_cnt++;
    }
    return NULL;
  }
  else {
    /* Check memory leak size */
    if (size >= 1000000) return NULL;
    /* malloc(size) or realloc(ptr, size) */
    if (ptr == NULL) {
      ud->malloc_cnt++;
    } else {
      ud->realloc_cnt++;
      oldsize = mptr->len;
    }
    mptr = realloc(mptr, size + sizeof(*mptr) - sizeof(mptr->obj));
    if (mptr == NULL) {
      return NULL;
    }
    mptr->len = size;
    if (ptr == NULL) {
      ud->current_objcnt++;
    } else {
      ud->current_size -= oldsize;
    }
    ud->current_size += size;
    ud->total_size += size;
    return (void *) &mptr->obj;
  }
}

static instance *
mrb_alloc_instance(char *application_name, int application_size, mrb_state *mrb)
{
  int i = 0;
  void *ud;
  instance *current;
  mrb_allocf allocf;
  int instance_free_spot = -1;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&context_mutex); /* 2020-11-23: poor protection (shared
                                       * mem. address is externalized in return
                                       * statements) */

  while (i < 20) {
    if (instances[i] != NULL && strcmp(instances[i]->application, application_name) == 0) {
      INF_TRACE("return");

      pthread_mutex_unlock(&context_mutex);

      return instances[i];
    }
    if (instance_free_spot == -1 && instances[i] == NULL) instance_free_spot = i;
    i++;
  }

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

  INF_TRACE("return");

  pthread_mutex_unlock(&context_mutex);

  return current;
}

static void
mrb_free_instance(instance *current)
{
  mrbc_context_free(current->mrb, current->context);
  mrb_close(current->mrb);
  free(current);
}

static mrb_value
mrb_mrb_eval(mrb_state *mrb, mrb_value self)
{
  mrb_value code, ret, mrb_ret, application;
  instance *current;

  mrb_ret = mrb_nil_value();
  mrb_get_args(mrb, "S|S", &code, &application);

  current = mrb_alloc_instance(RSTRING_PTR(application), RSTRING_LEN(application), mrb);
  if (current->outdated) {
    if (strcmp(RSTRING_PTR(code), "Context.start") >= 0) {
      ret = mrb_true_value();
    } else {
      mrb_free_instance(current);
      mrb_funcall(mrb, self, "mrb_start", 1, application);
      current = mrb_alloc_instance(RSTRING_PTR(application), RSTRING_LEN(application), mrb);
      ret     = mrb_load_nstring_cxt(current->mrb, RSTRING_PTR(code), RSTRING_LEN(code), current->context);
    }
  } else {
    ret = mrb_load_nstring_cxt(current->mrb, RSTRING_PTR(code), RSTRING_LEN(code), current->context);
  }

  /* 2020-11-23: if starting new mruby contexts isn't natively async, it may be
   * a problem to do it as if it was. Matz said it does not intend to support
   * multithread environments (so the 'thread bus approach' with multiple mruby
   * contexts may need to be replaced by another approach based on a single
   * mruby context) */

  if (mrb_undef_p(ret))
    mrb_ret = mrb_nil_value();
  else if (mrb_string_p(ret))
    mrb_ret = mrb_str_new_cstr(mrb, RSTRING_PTR(ret));
  else
    mrb_ret = ret;

  return mrb_ret;
}

static mrb_value
mrb_mrb_stop(mrb_state *mrb, mrb_value self)
{
  mrb_value application;
  int i = 0;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&context_mutex);

  mrb_get_args(mrb, "S", &application);

  while (i < 20) {
    if (instances[i] != NULL && strcmp(instances[i]->application, RSTRING_PTR(application)) == 0) {
      if (instances[i]->mrb == mrb) {
        instances[i]->outdated = TRUE;
      } else {
        mrb_free_instance(instances[i]);
        instances[i] = NULL;
      }
      break;
    }
    i++;
  }

  INF_TRACE("return");

  pthread_mutex_unlock(&context_mutex);

  return mrb_nil_value();
}

static mrb_value
mrb_mrb_expire(mrb_state *mrb, mrb_value self)
{
  mrb_value application;
  int i = 0;

  INF_TRACE_FUNCTION();

  pthread_mutex_lock(&context_mutex);

  mrb_get_args(mrb, "S", &application);

  while (i < 20) {
    if (instances[i] != NULL && strcmp(instances[i]->application, RSTRING_PTR(application)) == 0) {
      instances[i]->outdated = TRUE;
      break;
    }
    i++;
  }

  INF_TRACE("return");

  pthread_mutex_unlock(&context_mutex);

  return mrb_nil_value();
}

static mrb_value
mrb_vm_s_mallocs(mrb_state *mrb, mrb_value self)
{
  struct memprof_userdata *ud = mrb->allocf_ud;
  return mrb_fixnum_value(ud->malloc_cnt);
}

static mrb_value
mrb_vm_s_reallocs(mrb_state *mrb, mrb_value self)
{
  struct memprof_userdata *ud = mrb->allocf_ud;
  return mrb_fixnum_value(ud->realloc_cnt);
}

static mrb_value
mrb_vm_s_frees(mrb_state *mrb, mrb_value self)
{
  struct memprof_userdata *ud = mrb->allocf_ud;
  return mrb_fixnum_value(ud->free_cnt);
}

static mrb_value
mrb_vm_s_free_not_null(mrb_state *mrb, mrb_value self)
{
  struct memprof_userdata *ud = mrb->allocf_ud;
  return mrb_fixnum_value(ud->free_cnt - ud->freezero_cnt);
}

static mrb_value
mrb_vm_s_free_null(mrb_state *mrb, mrb_value self)
{
  struct memprof_userdata *ud = mrb->allocf_ud;
  return mrb_fixnum_value(ud->freezero_cnt);
}

static mrb_value
mrb_vm_s_total_memory(mrb_state *mrb, mrb_value self)
{
  struct memprof_userdata *ud = mrb->allocf_ud;
  return mrb_fixnum_value(ud->total_size);
}

static mrb_value
mrb_vm_s_objects(mrb_state *mrb, mrb_value self)
{
  struct memprof_userdata *ud = mrb->allocf_ud;
  return mrb_fixnum_value(ud->current_objcnt);
}

static mrb_value
mrb_vm_s_current_memory(mrb_state *mrb, mrb_value self)
{
  const struct memprof_userdata *ud = mrb->allocf_ud;
  return mrb_fixnum_value(ud->current_size);
}

/********************/
/* Public functions */
/********************/

extern void
context_memprof_init(mrb_allocf *funp, void **udp)
{
  struct memprof_userdata *ud;

  INF_TRACE_FUNCTION();

  ud = calloc(1, sizeof(*ud));

  if (ud == NULL)
  {
    abort();
  }

  *funp = context_memprof_allocf;
  *udp  = ud;

  INF_TRACE("return");
}

extern void
mrb_mruby_context_gem_init(mrb_state *mrb)
{
  static int mutex_init = 0;

  struct RClass *krn;
  struct RClass *vm;

  INF_TRACE_FUNCTION();

  if (!mutex_init)
  {
    pthread_mutex_init(&context_mutex, NULL);

    mutex_init = 1;
  }

  krn = mrb->kernel_module;

  vm = mrb_define_module(mrb, "Vm");

  mrb_define_method(mrb       , krn , "mrb_eval"       , mrb_mrb_eval            , MRB_ARGS_REQ(1) | MRB_ARGS_OPT(1));
  mrb_define_method(mrb       , krn , "mrb_stop"       , mrb_mrb_stop            , MRB_ARGS_REQ(1));
  mrb_define_method(mrb       , krn , "mrb_expire"     , mrb_mrb_expire          , MRB_ARGS_REQ(1));

  mrb_define_class_method(mrb , vm  , "mallocs"        , mrb_vm_s_mallocs        , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "reallocs"       , mrb_vm_s_reallocs       , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "frees"          , mrb_vm_s_frees          , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "free_not_null"  , mrb_vm_s_free_not_null  , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "free_null"      , mrb_vm_s_free_null      , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "total_memory"   , mrb_vm_s_total_memory   , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "objects"        , mrb_vm_s_objects        , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "current_memory" , mrb_vm_s_current_memory , MRB_ARGS_NONE());

  DONE;

  mrb_thread_scheduler_init(mrb);

  DONE;

  INF_TRACE("return");
}

extern void
mrb_mruby_context_gem_final(mrb_state *mrb)
{
  INF_TRACE_FUNCTION();

  /* Nothing to do! */

  INF_TRACE("return");
}
