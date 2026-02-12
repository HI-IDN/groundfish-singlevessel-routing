#ifndef GSP_GEO_UTILS_H
#define GSP_GEO_UTILS_H

#include <math.h>
#include "constants.h"

/**
 * Convert radians to degrees
 *
 * @param rad Angle in radians
 * @return Angle in degrees
 */
static inline double rad_to_deg(double rad) {
    return 180.0 * rad / PI;
}

/**
 * Convert degrees to radians
 *
 * @param deg Angle in degrees
 * @return Angle in radians
 */
static inline double deg_to_rad(double deg) {
    return deg * PI / 180.0;
}

/**
 * Convert degmin integer format to decimal degrees
 *
 * degmin format: Integer representing DDMM.mm * 100
 * Example: 663381 = 6633.81 = 66 degrees 33.81 minutes = 66 + 33.81/60 = 66.5635 degrees
 * For negative values (western longitudes): -DDMM.mm * 100
 *
 * @param degmin_int Degmin value as integer (e.g., 663381 for 66°33.81')
 * @return Decimal degrees (e.g., 66.5635)
 */
static inline double degmin_to_deg(int degmin_int) {
    int sign = (degmin_int < 0) ? -1 : 1;
    int abs_degmin = (degmin_int < 0) ? -degmin_int : degmin_int;

    /* Convert to decimal degmin (e.g., 663381 -> 6633.81) */
    double degmin_decimal = abs_degmin / 100.0;
    int degrees = (int)(degmin_decimal / 100);
    double minutes = degmin_decimal - (degrees * 100);

    return sign * (degrees + (minutes / 60.0));
}

/**
 * Convert degmin integer format to decimal degrees for LONGITUDE (Iceland convention)
 *
 * In Iceland survey data files, longitude values are stored as POSITIVE integers,
 * but represent WESTERN (negative) longitudes. This function applies the negation.
 *
 * degmin format: Integer representing DDMM.mm * 100 (positive for west)
 * Example: 224128 = 2241.28 = 22 degrees 41.28 minutes west = -22.688 degrees
 *
 * @param degmin_int Degmin value as integer (e.g., 224128 for 22°41.28'W)
 * @return Decimal degrees (e.g., -22.688 for western longitude)
 */
static inline double degmin_to_deg_lon(int degmin_int) {
    /* Apply Iceland convention: negate for western hemisphere */
    return -degmin_to_deg(degmin_int);
}

/**
 * Convert degmin integer format to radians
 *
 * degmin format: Integer representing DDMM.mm * 100
 * Example: 663381 = 6633.81 = 66 degrees 33.81 minutes = 66.5635 degrees = 1.161 radians
 *
 * @param degmin_int Degmin value as integer (e.g., 663381 for 66°33.81')
 * @return Radians
 */
static inline double degmin_to_rad(int degmin_int) {
    return deg_to_rad(degmin_to_deg(degmin_int));
}

/**
 * Convert degmin integer format to radians for LONGITUDE (Iceland convention)
 *
 * @param degmin_int Degmin value as integer (positive for west)
 * @return Radians (negative for western hemisphere)
 */
static inline double degmin_to_rad_lon(int degmin_int) {
    return deg_to_rad(degmin_to_deg_lon(degmin_int));
}

/**
 * Mercator projection for Iceland (~65°N, ~18°W)
 *
 * Projects lat/lon coordinates to 2D Mercator coordinates for accurate
 * geometric operations at high latitudes. Critical for Iceland where
 * longitude lines converge significantly.
 *
 * @param x Output array for projected x coordinates
 * @param y Output array for projected y coordinates
 * @param Lat Input latitude array (decimal degrees)
 * @param Lon Input longitude array (decimal degrees, negative for west)
 * @param length Number of points to project
 * @param MINLAT Map minimum latitude
 * @param MAXLAT Map maximum latitude
 */
static inline void mercator_project(double *x, double *y, const double *Lat, const double *Lon,
                                     int length, double MINLAT, double MAXLAT) {
    double lat65, x65, M, M65, Diff, lat, scale, lon;

    /* Reference point at 65°N, 18°W (Iceland's approximate center) */
    lat65 = 65.0 * PI / 180.0;
    x65 = (111415.13*cos(lat65) - 94.55*cos(3*lat65) + 0.12*cos(5*lat65)) / 60.0;
    M65 = 7915.704456*log10(tan(PI/4 + lat65/2))
          - sin(lat65)*(23.110771 + 0.052051*sin(lat65)*sin(lat65));

    /* Automatic scaling based on map bounds */
    lat = MAXLAT * PI / 180.0;
    M = 7915.704456*log10(tan(PI/4 + lat/2))
        - sin(lat)*(23.110771 + 0.052051*sin(lat)*sin(lat));
    Diff = M - M65;

    lat = MINLAT * PI / 180.0;
    M = 7915.704456*log10(tan(PI/4 + lat/2))
        - sin(lat)*(23.110771 + 0.052051*sin(lat)*sin(lat));
    Diff = Diff + M65 - M;

    scale = 1000000.0 / (Diff * x65);
    x65 = scale * x65;

    /* Project each point */
    for (int i = 0; i < length; i++) {
        lon = Lon[i];
        lat = Lat[i] * PI / 180.0;
        M = 7915.704456*log10(tan(PI/4 + lat/2))
            - sin(lat)*(23.110771 + 0.052051*sin(lat)*sin(lat));
        Diff = M65 - M;
        y[i] = (int)(Diff * x65);
        x[i] = (int)((lon + 18.0) * x65 * 60.0);  /* Offset by 18° for Iceland */
    }
}

/**
 * Segment intersection test in projected coordinates
 *
 * Tests if two line segments intersect using cross products.
 * Works in Mercator-projected coordinate space.
 *
 * @param s0x X coordinates of first segment [start, end]
 * @param s0y Y coordinates of first segment [start, end]
 * @param s1x X coordinates of second segment [start, end]
 * @param s1y Y coordinates of second segment [start, end]
 * @return 1 if segments intersect, 0 otherwise
 */
static inline int segments_intersect_mercator(double s0x[2], double s0y[2],
                                              double s1x[2], double s1y[2]) {
    double dx0 = s0x[1] - s0x[0];
    double dx1 = s1x[1] - s1x[0];
    double dy0 = s0y[1] - s0y[0];
    double dy1 = s1y[1] - s1y[0];
    double p0 = dy1 * (s1x[1] - s0x[0]) - dx1 * (s1y[1] - s0y[0]);
    double p1 = dy1 * (s1x[1] - s0x[1]) - dx1 * (s1y[1] - s0y[1]);
    double p2 = dy0 * (s0x[1] - s1x[0]) - dx0 * (s0y[1] - s1y[0]);
    double p3 = dy0 * (s0x[1] - s1x[1]) - dx0 * (s0y[1] - s1y[1]);
    return ((p0 * p1 <= 0) && (p2 * p3 <= 0));
}

#endif /* GSP_GEO_UTILS_H */
