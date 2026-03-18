cmake_minimum_required(VERSION 3.10)
project(GSP_Common C)

set(CMAKE_C_STANDARD 11)

# Disable automatic dependency tracking to avoid Windows path issues
set(CMAKE_DEPENDS_USE_COMPILER FALSE)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Find SQLite3 using pkg-config (avoids MSYS2 path issues)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(GEOS REQUIRED geos)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${SQLITE3_INCLUDE_DIRS}
    ${GEOS_INCLUDE_DIRS}
)

# Shared source files
set(COMMON_SOURCES
    dat_parser.c
    distance.c
    logging.c
    sql_utils.c
    coastline_db.c
)

# Station import using DataSet
add_executable(gsp_stations
    ../tools/station_import.c
    dat_parser.c
    logging.c
    sql_utils.c
)

target_link_libraries(gsp_stations
    ${SQLITE3_LIBRARIES}
    m
)

# Distance rebuild from existing DB
add_executable(gsp_distances
    ../tools/distance_builder.c
    distance.c
    coastline_db.c
    logging.c
    sql_utils.c
)

target_link_libraries(gsp_distances
    ${SQLITE3_LIBRARIES}
    ${GEOS_LIBRARIES}
    m
)

add_executable(gsp_prepare_routing_data
    ../tools/prepare_routing_data_main.c
    ../tools/country_bootstrap_main.c
    ../tools/station_import.c
    ../tools/distance_builder.c
    dat_parser.c
    distance.c
    logging.c
    sql_utils.c
    coastline_db.c
)

target_compile_definitions(gsp_prepare_routing_data PRIVATE GSP_LIBRARY_ONLY=1)

target_link_libraries(gsp_prepare_routing_data
    ${SQLITE3_LIBRARIES}
    ${GEOS_LIBRARIES}
    m
)


# Data files
set(STATIONS_DAT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/stations.dat)
set(DB_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/gsp.db)

# Custom target: import stations
add_custom_target(run_stations
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "=== Importing Stations ==="
    COMMAND ${CMAKE_COMMAND} -E echo "  Input: ${STATIONS_DAT_FILE}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Database: ${DB_FILE}"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND gsp_stations ${STATIONS_DAT_FILE} ${DB_FILE}
    DEPENDS gsp_stations
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Importing stations from dat/stations.dat"
)

# Custom target: recompute distances from existing database
add_custom_target(run_distance
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "=== Recomputing Distances ==="
    COMMAND ${CMAKE_COMMAND} -E echo "  Database: ${DB_FILE}"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND gsp_distances --db ${DB_FILE}
    DEPENDS gsp_distances
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Recomputing distance matrix from existing database"
)

add_custom_target(run_prepare_routing_data
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "=== Preparing Routing Data ==="
    COMMAND ${CMAKE_COMMAND} -E echo "  Database: ${DB_FILE}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Coastline: ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/island.tsv"
    COMMAND ${CMAKE_COMMAND} -E echo "  Waypoints: ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/waypoints.dat"
    COMMAND ${CMAKE_COMMAND} -E echo "  Ports: ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/ports.dat"
    COMMAND ${CMAKE_COMMAND} -E echo "  Boats: ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/boats.dat"
    COMMAND ${CMAKE_COMMAND} -E echo "  Stations: ${STATIONS_DAT_FILE}"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND gsp_prepare_routing_data --db ${DB_FILE} --coastline-file ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/island.tsv --waypoint-file ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/waypoints.dat --port-file ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/ports.dat --boat-file ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/boats.dat --seed-hints-only --stations-file ${STATIONS_DAT_FILE}
    DEPENDS gsp_prepare_routing_data
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Preparing routing data in one pass"
)
