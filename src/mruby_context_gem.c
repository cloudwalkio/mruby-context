#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/string.h"
#include "mruby/variable.h"
#include "mruby/array.h"
#include "mruby/dump.h"

#include "mruby/error.h"

struct memheader {
  size_t len; /* size of obj (not including len) */
  union {     /* union for alignment */
    void *ptr;
    long long l;
    double d;
  } obj;
};

struct memprof_userdata {
  unsigned int malloc_cnt;
  unsigned int realloc_cnt;
  unsigned int free_cnt;
  unsigned int freezero_cnt;
  unsigned long long total_size;

  unsigned int current_objcnt;
  unsigned long long current_size;
};

extern void context_memprof_init(mrb_allocf *, void **);

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
    /*Check memory leak size*/
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
    return (void *)&mptr->obj;
  }
}

void
context_memprof_init(mrb_allocf *funp, void **udp)
{
  struct memprof_userdata *ud;
  ud = calloc(1, sizeof(*ud));
  if (ud == NULL) {
    abort(); /* fatal! */
  }

  *funp = context_memprof_allocf;
  *udp  = ud;
}

static mrb_value
mrb_mrb_eval(mrb_state *mrb, mrb_value self)
{
  mrb_state *mrb2=NULL;
  mrbc_context *c;
  mrb_allocf allocf;
  mrb_value code, ret, mrb_ret;
  void *ud;
    
  mrb_ret = mrb_nil_value();

  mrb_get_args(mrb, "S", &code);

  context_memprof_init(&allocf, &ud);

  mrb2 = mrb_open_allocf(allocf, ud);
  c = mrbc_context_new(mrb2);

  ret = mrb_load_string_cxt(mrb2, RSTRING_PTR(code), c);

  if (mrb_undef_p(ret))
    mrb_ret = mrb_nil_value();
  else if (mrb_string_p(ret))
    mrb_ret = mrb_str_new_cstr(mrb, RSTRING_PTR(ret));
  else
    mrb_ret = ret;

  mrbc_context_free(mrb2, c);
  mrb_close(mrb2);

  return mrb_ret;
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

void
mrb_mruby_context_gem_init(mrb_state* mrb)
{
  struct RClass *krn, *vm;

  krn = mrb->kernel_module;
  vm  = mrb_define_module(mrb, "Vm");

  mrb_define_method(mrb       , krn , "mrb_eval"       , mrb_mrb_eval            , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , vm  , "mallocs"        , mrb_vm_s_mallocs        , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "reallocs"       , mrb_vm_s_reallocs       , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "frees"          , mrb_vm_s_frees          , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "free_not_null"  , mrb_vm_s_free_not_null  , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "free_null"      , mrb_vm_s_free_null      , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "total_memory"   , mrb_vm_s_total_memory   , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "objects"        , mrb_vm_s_objects        , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , vm  , "current_memory" , mrb_vm_s_current_memory , MRB_ARGS_NONE());
}

void
mrb_mruby_context_gem_final(mrb_state* mrb)
{
}

