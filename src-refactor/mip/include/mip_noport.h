#ifndef GSP_MIP_NOPORT_H
#define GSP_MIP_NOPORT_H

#include "mip_capacity_aware.h"

int solve_mip_noport(const mip_instance_t *instance, const mip_params_t *params, mip_solution_t *solution);

#endif

