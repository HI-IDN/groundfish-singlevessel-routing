#ifndef GSP_LOGGING_H
#define GSP_LOGGING_H

#include "constants.h"

typedef int log_level_t;

void log_init(const char *module_name, log_level_t level);
void log_set_level(log_level_t level);
log_level_t log_get_level(void);
void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_close(void);

#endif

