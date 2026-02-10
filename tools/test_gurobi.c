#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gurobi_c.h"

int main(void) {
    GRBenv *env = NULL;
    GRBmodel *model = NULL;
    double start = (double)time(NULL);

    if (GRBloadenv(&env, NULL) != 0) {
        fprintf(stderr, "GUROBI_TEST: FAIL - GRBloadenv failed\n");
        return 2;
    }

    if (GRBnewmodel(env, &model, "test", 0, NULL, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "GUROBI_TEST: FAIL - GRBnewmodel failed\n");
        GRBfreeenv(env);
        return 3;
    }

    // add a single variable x with bounds [0, 10] and objective coefficient 1.0
    if (GRBaddvar(model, 0, NULL, NULL, 1.0, 0.0, 10.0, GRB_CONTINUOUS, "x") != 0) {
        fprintf(stderr, "GUROBI_TEST: FAIL - GRBaddvar failed\n");
        GRBfreemodel(model);
        GRBfreeenv(env);
        return 4;
    }

    if (GRBupdatemodel(model) != 0) {
        fprintf(stderr, "GUROBI_TEST: FAIL - GRBupdatemodel failed\n");
        GRBfreemodel(model);
        GRBfreeenv(env);
        return 5;
    }

    if (GRBsetintattr(model, GRB_INT_ATTR_MODELSENSE, GRB_MINIMIZE) != 0) {
        // Some environments or versions may not expose the int attribute; warn and continue.
        fprintf(stderr, "GUROBI_TEST: WARN - could not set model sense via GRBsetintattr (continuing)\n");
    }

    if (GRBoptimize(model) != 0) {
        fprintf(stderr, "GUROBI_TEST: FAIL - GRBoptimize failed\n");
        GRBfreemodel(model);
        GRBfreeenv(env);
        return 6;
    }

    int status = 0;
    if (GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status) != 0) {
        fprintf(stderr, "GUROBI_TEST: FAIL - cannot get status\n");
        GRBfreemodel(model);
        GRBfreeenv(env);
        return 7;
    }

    double end = (double)time(NULL);
    double elapsed = end - start;

    printf("GUROBI_TEST: PASS - status=%d elapsed=%.0f s\n", status, elapsed);

    GRBfreemodel(model);
    GRBfreeenv(env);
    return 0;
}

