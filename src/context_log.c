#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "mruby.h"
#include "mruby/value.h"
#include "mruby/compile.h"
#include "mruby/variable.h"
#include "mruby/string.h"

void ContextLog(mrb_state *mrb, int severity_level, const char *format, ...)
{
  char dest[1024];
  va_list argptr;

  va_start(argptr, format);
  vsprintf(dest, format, argptr);
  va_end(argptr);

  mrb_value msg, context;
  context = mrb_const_get(mrb, mrb_obj_value(mrb->object_class), mrb_intern_lit(mrb, "ContextLog"));
  msg = mrb_funcall(mrb, mrb_str_new(mrb, dest, strlen(dest)), "inspect", 0);
  if (severity_level == 3) /*Error*/
    mrb_funcall(mrb, context, "error", 1, msg);
  else
    mrb_funcall(mrb, context, "info", 1, msg);
}

