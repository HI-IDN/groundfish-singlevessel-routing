cmake_minimum_required(VERSION 3.10)
project(GSP_Solver C)

# Fix Windows/MSYS2 CMake dependency tracking issue
# The compiler_depend.ts file generation is broken on MinGW
# Solution: Use explicit dependency files instead of scanning
if(POLICY CMP0160)
    cmake_policy(SET CMP0160 OLD)  # Use old depend_file generation
endif()

# Set policies
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)  # Normalize install() DESTINATION paths
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# For Windows/MSYS2: disable compiler-based dependency scanning
# Instead use CMake's built-in dependency tracking
set(CMAKE_DEPENDS_USE_COMPILER FALSE)
set(CMAKE_DEPEND_USE_MODULES FALSE)
set(CMAKE_C_DEPENDS_USE_COMPILER FALSE)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Find SQLite3 using pkg-config (avoids MSYS2 path issues)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(GEOS REQUIRED geos)

if(DEFINED ENV{GUROBI_HOME})
    file(TO_CMAKE_PATH "$ENV{GUROBI_HOME}" GUROBI_HOME_CMAKE)
    set(GUROBI_INCLUDE_DIR "${GUROBI_HOME_CMAKE}/include")
    set(GUROBI_LIBRARY_DIR "${GUROBI_HOME_CMAKE}/lib")
    set(GUROBI_BIN_DIR "${GUROBI_HOME_CMAKE}/bin")
endif()

if(GUROBI_INCLUDE_DIR AND EXISTS "${GUROBI_INCLUDE_DIR}/gurobi_c.h" AND
   GUROBI_LIBRARY_DIR AND EXISTS "${GUROBI_LIBRARY_DIR}")
    message(STATUS "Found Gurobi include: ${GUROBI_INCLUDE_DIR}")
    message(STATUS "Found Gurobi library dir: ${GUROBI_LIBRARY_DIR}")
    message(STATUS "Found Gurobi bin dir: ${GUROBI_BIN_DIR}")
else()
    message(STATUS "Gurobi not found. GUROBI_HOME='${GUROBI_HOME_CMAKE}'")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SQLITE3_INCLUDE_DIRS}
    ${GEOS_INCLUDE_DIRS}
)

# ============================================================
# 1. Data Preprocessing (common/)
# ============================================================

add_subdirectory(common)

# Historical survey station import target
add_custom_target(stations
    COMMAND ${CMAKE_MAKE_PROGRAM} run_stations
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/common
    COMMENT "Step 2: Import historical survey stations"
)

# Distance recomputation target
add_custom_target(distance
    COMMAND ${CMAKE_MAKE_PROGRAM} run_distance
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/common
    COMMENT "Step 3: Recompute all location-pair distances"
)

# Compatibility target for combined step
add_custom_target(stations_with_distance
    COMMAND ${CMAKE_MAKE_PROGRAM} run_stations_with_distance
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/common
    COMMENT "Import stations and compute distances"
)

add_custom_target(survey
    COMMAND ${CMAKE_MAKE_PROGRAM} run_survey_import
    COMMAND historical_survey ${CMAKE_CURRENT_SOURCE_DIR}/../dat/gsp_data.db ${CMAKE_CURRENT_SOURCE_DIR}/../sol/survey
    DEPENDS historical_survey
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/common
    COMMENT "Step 4: Import historical survey assignments and export JSON"
)

# ============================================================
# 2. Unified GSP Solver (both init and sweep modes)
# ============================================================

add_executable(gsp
    gsp.c
    init/gsp_init_mode.c
    init/nearest_neighbor.c
    init/greedy_insertion.c
    init/cheapest_insertion.c
    common/feasibility.c
    sweep/gsp_sweep_mode.c
)

target_link_libraries(gsp
    ${SQLITE3_LIBRARIES}
    m
)

add_executable(gsp_init_opt
    init/opt_init_from_noport.c
    common/feasibility.c
)

target_link_libraries(gsp_init_opt
    ${SQLITE3_LIBRARIES}
    m
)

# ============================================================
# Run targets for Phase 0 strategies
# ============================================================

set(CONFIG_YAML ${CMAKE_CURRENT_SOURCE_DIR}/../config/gsp_solver.yaml)
set(DATABASE ${CMAKE_CURRENT_SOURCE_DIR}/../dat/gsp_data.db)
set(SOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../sol)
set(SOL_NN_DIR ${SOL_DIR}/nn)
set(SOL_GE_DIR ${SOL_DIR}/ge)
set(SOL_CI_DIR ${SOL_DIR}/ci)
set(SOL_OPT_DIR ${SOL_DIR}/opt)

file(MAKE_DIRECTORY ${SOL_DIR})
file(MAKE_DIRECTORY ${SOL_NN_DIR})
file(MAKE_DIRECTORY ${SOL_GE_DIR})
file(MAKE_DIRECTORY ${SOL_CI_DIR})
file(MAKE_DIRECTORY ${SOL_OPT_DIR})

add_custom_target(run_init_nn
    COMMAND gsp --mode init --strategy nn --database ${DATABASE} --output ${SOL_NN_DIR}/init.json --config ${CONFIG_YAML}
    DEPENDS gsp
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Phase 0: NN (Nearest Neighbor)"
)

add_custom_target(run_init_ge
    COMMAND gsp --mode init --strategy ge --database ${DATABASE} --output ${SOL_GE_DIR}/init.json --config ${CONFIG_YAML}
    DEPENDS gsp
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Phase 0: GE (Greedy Edge)"
)

add_custom_target(run_init_ci
    COMMAND gsp --mode init --strategy ci --database ${DATABASE} --output ${SOL_CI_DIR}/init.json --config ${CONFIG_YAML}
    DEPENDS gsp
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Phase 0: CI (Cheapest Insertion) - NOT YET IMPLEMENTED"
)

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    add_executable(gsp_noport_opt
        mip/noport.c
        mip/noport_opt.c
        common/feasibility.c
    )

    target_include_directories(gsp_noport_opt PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/mip/include
        ${SQLITE3_INCLUDE_DIRS}
        ${GUROBI_INCLUDE_DIR}
    )

    target_link_libraries(gsp_noport_opt
        ${SQLITE3_LIBRARIES}
        -L${GUROBI_LIBRARY_DIR}
        gurobi130
        m
    )

    add_custom_target(run_noport_opt
        COMMAND $<TARGET_FILE:gsp_noport_opt> --database ${DATABASE} --config ${CONFIG_YAML} --output ${SOL_OPT_DIR}/noport.json
        DEPENDS gsp_noport_opt
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Preprocessing: no-port optimal station ordering"
    )
else()
    add_custom_target(run_noport_opt
        COMMAND ${CMAKE_COMMAND} -E echo "No-port OPT unavailable: Gurobi headers/library not found during CMake configure"
        COMMAND ${CMAKE_COMMAND} -E false
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Preprocessing: no-port optimal station ordering"
    )
endif()

add_custom_target(run_init_opt
    COMMAND $<TARGET_FILE:gsp_init_opt> --database ${DATABASE} --config ${CONFIG_YAML} --input ${SOL_OPT_DIR}/noport.json --output ${SOL_OPT_DIR}/init.json
    DEPENDS gsp_init_opt
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Phase 0: OPT initialization from no-port ordering"
)

# ============================================================
# Tools
# ============================================================

# Export historical survey to JSON for plotting
add_executable(historical_survey
    tools/export_survey_json.c
    common/feasibility.c
)

target_link_libraries(historical_survey
    ${SQLITE3_LIBRARIES}
)

target_include_directories(historical_survey PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SQLITE3_INCLUDE_DIRS}
)

add_executable(gsp_country
    tools/infer_waypoints_from_coastline.c
    common/dat_parser.c
    common/coastline_db.c
)

target_link_libraries(gsp_country
    ${SQLITE3_LIBRARIES}
    ${GEOS_LIBRARIES}
    m
)

target_include_directories(gsp_country PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SQLITE3_INCLUDE_DIRS}
    ${GEOS_INCLUDE_DIRS}
)

# ============================================================
# Install
# ============================================================

install(TARGETS gsp historical_survey gsp_country gsp_stations gsp_distances gsp_historical_survey
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin
)

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    install(TARGETS gsp_noport_opt
        RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin
    )
endif()

install(TARGETS gsp_init_opt
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin
)
