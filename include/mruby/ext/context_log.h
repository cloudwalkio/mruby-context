
#ifndef MRUBY_CONTEXT_LOG_H
#define MRUBY_CONTEXT_LOG_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "context.h"

void ContextLog(mrb_state *mrb, int severity_level, const char *format, ...);
void ContextLogFile(const char *format, ...);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif /* MRUBY_CONTEXT_LOG_H */

