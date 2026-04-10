#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdio.h>

#include "init_types.h"

void gsp_write_distance_nm_json(FILE *fp,
                                const char *indent,
                                const gsp_distance_breakdown_t *segment_breakdowns,
                                int segment_count,
                                const gsp_distance_breakdown_t *grand_total,
                                int trailing_comma);

#endif
