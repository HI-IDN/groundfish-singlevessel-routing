/*
 * Smoke-test: verify GEOS is installed and the reentrant C API works.
 * Creates a simple triangle polygon, checks it is valid, then tears down.
 */
#include <stdio.h>
#include <geos_c.h>

static void geos_msg(const char *fmt, ...) { (void)fmt; }

int main(void) {
    GEOSContextHandle_t ctx = GEOS_init_r();
    if (!ctx) {
        fprintf(stderr, "GEOS_TEST: FAIL - GEOS_init_r() returned NULL\n");
        return 1;
    }
    GEOSContext_setNoticeHandler_r(ctx, geos_msg);
    GEOSContext_setErrorHandler_r(ctx,  geos_msg);

    /* Build a simple triangle as a coordinate sequence */
    GEOSCoordSequence *seq = GEOSCoordSeq_create_r(ctx, 4, 2);
    if (!seq) {
        fprintf(stderr, "GEOS_TEST: FAIL - GEOSCoordSeq_create_r failed\n");
        GEOS_finish_r(ctx);
        return 2;
    }
    GEOSCoordSeq_setXY_r(ctx, seq, 0, 0.0, 0.0);
    GEOSCoordSeq_setXY_r(ctx, seq, 1, 1.0, 0.0);
    GEOSCoordSeq_setXY_r(ctx, seq, 2, 0.5, 1.0);
    GEOSCoordSeq_setXY_r(ctx, seq, 3, 0.0, 0.0);  /* close ring */

    GEOSGeometry *ring = GEOSGeom_createLinearRing_r(ctx, seq);
    if (!ring) {
        fprintf(stderr, "GEOS_TEST: FAIL - GEOSGeom_createLinearRing_r failed\n");
        GEOS_finish_r(ctx);
        return 3;
    }

    GEOSGeometry *poly = GEOSGeom_createPolygon_r(ctx, ring, NULL, 0);
    if (!poly) {
        fprintf(stderr, "GEOS_TEST: FAIL - GEOSGeom_createPolygon_r failed\n");
        GEOSGeom_destroy_r(ctx, ring);
        GEOS_finish_r(ctx);
        return 4;
    }

    int valid = GEOSisValid_r(ctx, poly);
    GEOSGeom_destroy_r(ctx, poly);
    GEOS_finish_r(ctx);

    if (valid == 1) {
        printf("GEOS_TEST: PASS (version %s)\n", GEOSversion());
        return 0;
    } else {
        fprintf(stderr, "GEOS_TEST: FAIL - polygon not valid (ret=%d)\n", valid);
        return 5;
    }
}

