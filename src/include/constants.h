#ifndef GSP_CONSTANTS_H
#define GSP_CONSTANTS_H

/* Node types used in survey/export routing tables. */
enum {
    NODE_TYPE_BOAT = 0,
    NODE_TYPE_STATION = 1,
    NODE_TYPE_PORT = 2,
    NODE_TYPE_WAYPOINT = 3
};

/* DAT record tags. */
#define GSP_DAT_TAG_BOAT "BOAT"
#define GSP_DAT_TAG_STAT "STAT"
#define GSP_DAT_TAG_WAYP "WAYP"
#define GSP_DAT_TAG_PORT "PORT"

/* Waypoint granularity levels stored in the waypoints.granularity INTEGER column.
 *   0 = coarse coastline approximation
 *   1 = routing coastline ring
 *   2 = buffered coastline support points
 */
enum {
    GSP_WAYPOINT_GRANULARITY_COARSE   = 0,
    GSP_WAYPOINT_GRANULARITY_ROUTING  = 1,
    GSP_WAYPOINT_GRANULARITY_BUFFERED = 2
};

enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

/* Gurobi MIP status codes. */
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
#define INFEASIBLE_LINK_PENALTY 100000.0
#define DIJKSTRA_INFINITY 10000000000.0

/* Buffered coastline support points:
 * 1 nautical mile is visibly offshore while still representing near-coast routing.
 * The GEOS buffer is applied in geographic degrees, so we use the latitude approximation
 * 1 nm = 1/60 degree here.
 */
#define BUFFERED_COASTLINE_OFFSET_NM 1.0
#define BUFFERED_COASTLINE_OFFSET_DEG (BUFFERED_COASTLINE_OFFSET_NM / 60.0)

#endif
