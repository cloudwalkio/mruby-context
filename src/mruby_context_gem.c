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
  mrb_value code, ret, mrb_ret = mrb_nil_value();

  mrb_get_args(mrb, "S", &code);

  mrb2 = mrb_open();
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

char *variables;

static mrb_value
mrb_posxml_variables(mrb_state *mrb, mrb_value self)
{
  if (variables == NULL)
    return mrb_nil_value();
  else
    return mrb_str_new_cstr(mrb, variables);
}

static mrb_value
mrb_posxml_set_variables(mrb_state *mrb, mrb_value self)
{
  mrb_value var;
  mrb_get_args(mrb, "S", &var);

  if (variables != NULL){
    free(variables);
  }

  variables = malloc(RSTRING_LEN(var) + 1);
  memset(variables, 0, RSTRING_LEN(var) + 1);
  strncpy(variables, RSTRING_PTR(var), RSTRING_LEN(var));
  return mrb_true_value();
}

void
mrb_mruby_context_gem_init(mrb_state* mrb)
{
  struct RClass *krn, *posxml;

  krn = mrb->kernel_module;
  posxml = mrb_define_module(mrb, "Posxml");

  mrb_define_method(mrb       , krn    , "mrb_eval"          , mrb_mrb_eval             , MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb , posxml , "posxml_variables"  , mrb_posxml_variables     , MRB_ARGS_NONE());
  mrb_define_class_method(mrb , posxml , "posxml_variables=" , mrb_posxml_set_variables , MRB_ARGS_REQ(1));

  DONE;
}

void
mrb_mruby_context_gem_final(mrb_state* mrb)
{
}
