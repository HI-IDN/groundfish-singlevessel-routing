cmake_minimum_required(VERSION 3.10)
project(GSP_Common C)

set(CMAKE_C_STANDARD 11)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Find SQLite3 using pkg-config (avoids MSYS2 path issues)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${SQLITE3_INCLUDE_DIRS}
)

# Source files for preprocessing utility
set(COMMON_SOURCES
    dat_parser.c
    distance.c
    logging.c
    sql_utils.c
    coastline_db.c
)

# Preprocessing executable (renamed to avoid conflict with custom target)
add_executable(gsp_preprocess
    preprocessing.c
    ${COMMON_SOURCES}
)

target_link_libraries(gsp_preprocess
    ${SQLITE3_LIBRARIES}
    m
)


# Data files
set(DAT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/data2023spring.dat)
set(DB_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/gsp_data.db)

# Custom target: run preprocessing
add_custom_target(run_preprocessing
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "=== Preparing Data ==="
    COMMAND ${CMAKE_COMMAND} -E echo "  Input: ${DAT_FILE}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Database: ${DB_FILE}"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND gsp_preprocess ${DAT_FILE} ${DB_FILE}
    DEPENDS gsp_preprocess
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Parsing .dat file and building SQLite database"
)

