#include <stdlib.h>
#include <string.h>

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/string.h"
#include "mruby/variable.h"

#if MRUBY_RELEASE_NO < 10000
  #include "error.h"
#else
  #include "mruby/error.h"
#endif

#define DONE mrb_gc_arena_restore(mrb, 0)

static mrb_value
mrb_mrb_eval(mrb_state *mrb, mrb_value self)
{
  mrb_state *mrb2=NULL;
  mrbc_context *c;
  mrb_value code, ret;

  mrb_get_args(mrb, "S", &code);

  mrb2 = mrb_open();
  c = mrbc_context_new(mrb2);

  ret = mrb_load_string_cxt(mrb2, RSTRING_PTR(code), c);
  if (mrb_undef_p(ret)) return mrb_false_value();

  mrbc_context_free(mrb2, c);
  mrb_close(mrb2);

  return ret;
}

void
mrb_mruby_context_gem_init(mrb_state* mrb)
{
  struct RClass *krn;

  krn = mrb->kernel_module;

  mrb_define_method(mrb, krn, "mrb_eval", mrb_mrb_eval, MRB_ARGS_REQ(1));

  DONE;
}

void
mrb_mruby_context_gem_final(mrb_state* mrb)
{
}