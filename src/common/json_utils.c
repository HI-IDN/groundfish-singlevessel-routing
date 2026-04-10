#include "../include/json_utils.h"

void gsp_write_distance_nm_json(FILE *fp,
                                const char *indent,
                                const gsp_distance_breakdown_t *segment_breakdowns,
                                int segment_count,
                                const gsp_distance_breakdown_t *grand_total,
                                int trailing_comma) {
    const char *base = indent ? indent : "";
    const gsp_distance_breakdown_t zero = {0.0, 0.0, 0.0};
    const gsp_distance_breakdown_t *total = grand_total ? grand_total : &zero;

    if (!fp) return;

    fprintf(fp, "%s\"distance_nm\": {\n", base);
    fprintf(fp, "%s  \"segment\": {\n", base);

    fprintf(fp, "%s    \"transit\": [", base);
    for (int s = 0; s < segment_count; s++) {
        if (s) fprintf(fp, ", ");
        fprintf(fp, "%.2f", segment_breakdowns ? segment_breakdowns[s].transit_distance_nm : 0.0);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "%s    \"haul\": [", base);
    for (int s = 0; s < segment_count; s++) {
        if (s) fprintf(fp, ", ");
        fprintf(fp, "%.2f", segment_breakdowns ? segment_breakdowns[s].haul_distance_nm : 0.0);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "%s    \"total\": [", base);
    for (int s = 0; s < segment_count; s++) {
        if (s) fprintf(fp, ", ");
        fprintf(fp, "%.2f", segment_breakdowns ? segment_breakdowns[s].total_distance_nm : 0.0);
    }
    fprintf(fp, "]\n");

    fprintf(fp, "%s  },\n", base);
    fprintf(fp, "%s  \"grand_total\": {\n", base);
    fprintf(fp, "%s    \"transit\": %.2f,\n", base, total->transit_distance_nm);
    fprintf(fp, "%s    \"haul\": %.2f,\n", base, total->haul_distance_nm);
    fprintf(fp, "%s    \"total\": %.2f\n", base, total->total_distance_nm);
    fprintf(fp, "%s  }\n", base);
    fprintf(fp, "%s}%s\n", base, trailing_comma ? "," : "");
}
