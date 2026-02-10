#ifndef GSP_MATH_H
#define GSP_MATH_H

double degmin_to_decimal(int degmin_value);
double decimal_to_radians(double degrees);
double haversine_distance(double lat1, double lon1, double lat2, double lon2);
double euclidean_distance(double x1, double y1, double x2, double y2);

#endif

