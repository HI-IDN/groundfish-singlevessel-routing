#include "../include/logging.h"
#include "../include/data_types.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_init("SWEEP", LOG_INFO);
    log_info("Placeholder sweep entry point. Implement sweep matheuristic here.");
    log_close();
    return 0;
}

