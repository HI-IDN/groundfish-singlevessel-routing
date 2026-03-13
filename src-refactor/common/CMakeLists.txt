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

# Legacy combined preprocessing utility kept as backup
add_executable(gsp_stations_with_distance
    preprocessing.c
    ${COMMON_SOURCES}
)

target_link_libraries(gsp_stations_with_distance
    ${SQLITE3_LIBRARIES}
    ${GEOS_LIBRARIES}
    m
)

# Station import using DataSet
add_executable(gsp_stations
    station_import.c
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
    distance_builder.c
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


# Data files
set(STATIONS_DAT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/stations.dat)
set(DB_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/gsp_data.db)

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

# Compatibility target: keep combined step available
add_custom_target(run_stations_with_distance
    COMMAND ${CMAKE_MAKE_PROGRAM} run_stations
    COMMAND ${CMAKE_MAKE_PROGRAM} run_distance
    DEPENDS gsp_stations gsp_distances
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Importing stations and recomputing distances"
)

