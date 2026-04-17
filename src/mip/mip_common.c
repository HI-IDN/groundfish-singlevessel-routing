#include "include/mip_common.h"

#include <stdio.h>
#include <stdlib.h>

const char *mip_gurobi_status_name(int status) {
    switch (status) {
        case GRB_OPTIMAL: return "OPTIMAL";
        case GRB_SUBOPTIMAL: return "SUBOPTIMAL";
        case GRB_TIME_LIMIT: return "TIME_LIMIT";
        case GRB_INTERRUPTED: return "INTERRUPTED";
        case GRB_NODE_LIMIT: return "NODE_LIMIT";
        case GRB_ITERATION_LIMIT: return "ITERATION_LIMIT";
        case GRB_WORK_LIMIT: return "WORK_LIMIT";
        case GRB_INFEASIBLE: return "INFEASIBLE";
        case GRB_INF_OR_UNBD: return "INF_OR_UNBD";
        case GRB_UNBOUNDED: return "UNBOUNDED";
        default: return "OTHER";
    }
}

int mip_status_allows_incumbent(int status) {
    return status == GRB_OPTIMAL ||
           status == GRB_SUBOPTIMAL ||
           status == GRB_TIME_LIMIT ||
           status == GRB_INTERRUPTED ||
           status == GRB_NODE_LIMIT ||
           status == GRB_ITERATION_LIMIT ||
           status == GRB_WORK_LIMIT;
}

void mip_apply_common_params(GRBmodel *model, const mip_params_t *params) {
    GRBenv *model_env;

    if (!model) return;
    model_env = GRBgetenv(model);
    if (!model_env) return;

    if (params && params->time_limit_seconds > 0.0) {
        GRBsetdblparam(model_env, "TimeLimit", params->time_limit_seconds);
    }
    if (params && params->thread_count > 0) {
        GRBsetintparam(model_env, "Threads", params->thread_count);
    }
    if (params && params->mip_gap > 0.0) {
        GRBsetdblparam(model_env, "MIPGap", params->mip_gap);
    }
    GRBsetintparam(model_env, "OutputFlag", (params && params->verbose) ? 1 : 0);
    GRBsetintparam(model_env, "LogToConsole", (params && params->verbose) ? 1 : 0);
}

void *mip_xmalloc(size_t nbytes) {
    void *ptr = malloc(nbytes);
    if (!ptr) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return ptr;
}

void *mip_xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (!ptr) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return ptr;
}
