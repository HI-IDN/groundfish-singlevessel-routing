#ifndef GSP_GEO_UTILS_H
#define GSP_GEO_UTILS_H

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


#endif /* GSP_GEO_UTILS_H */



