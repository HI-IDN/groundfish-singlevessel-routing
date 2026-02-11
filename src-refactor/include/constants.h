#ifndef GSP_CONSTANTS_H
#define GSP_CONSTANTS_H

enum {
    NODE_TYPE_BOAT = 0,
	NODE_TYPE_STATION = 1,
    NODE_TYPE_PORT = 2,
    NODE_TYPE_WAYPOINT = 3
};

enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

// Gurobi MIP status codes (https://www.gurobi.com/documentation/9.5/refman/mip_status_codes.html)
enum {
	MIP_STATUS_OPTIMAL = 2,
    MIP_STATUS_SUBOPTIMAL = 9,
    MIP_STATUS_TIME_LIMIT = 9,
    MIP_STATUS_INFEASIBLE = 3
};

#define MAX_NODES 10000
#define MAX_STATIONS 5000
#define MAX_WAYPOINTS 1000
#define MAX_LOG_LINES 10000
#define MAX_TOUR_LENGTH 10000

#define DEFAULT_MIP_TIME_LIMIT 3600
#define DEFAULT_L2SEG_MIN 60
#define DEFAULT_L2SEG_MAX 480
#define DEFAULT_L2SEG_STEP 60
#define DEFAULT_SEGMENT_STRIDE 30

/* Mathematical constants */
#define PI 3.14159265358979323846

/* Distance computation constants */
#define INFEASIBLE_LINK_PENALTY 100000.0  /* Penalty added to infeasible links that cross land */
#define DIJKSTRA_INFINITY 10000000000.0    /* Infinity value for Dijkstra algorithm */

#endif

