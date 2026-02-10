#include "../include/logging.h"

void log_init(const char *module_name, log_level_t level) {
    (void)module_name;
    (void)level;
}

void log_set_level(log_level_t level) {
    (void)level;
}

log_level_t log_get_level(void) {
    return LOG_INFO;
}

void log_debug(const char *fmt, ...) {
    (void)fmt;
}

void log_info(const char *fmt, ...) {
    (void)fmt;
}

void log_warn(const char *fmt, ...) {
    (void)fmt;
}

void log_error(const char *fmt, ...) {
    (void)fmt;
}

void log_close(void) {
}

