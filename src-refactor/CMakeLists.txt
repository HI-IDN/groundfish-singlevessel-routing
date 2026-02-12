cmake_minimum_required(VERSION 3.10)
project(GSP_Solver C)

# Set policies
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)  # Normalize install() DESTINATION paths
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Find SQLite3 using pkg-config (avoids MSYS2 path issues)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SQLITE3_INCLUDE_DIRS}
)

# ============================================================
# 1. Data Preprocessing (common/)
# ============================================================

add_subdirectory(common)

# Preprocessing target delegates to common/run_preprocessing
add_custom_target(preprocess
    COMMAND ${CMAKE_MAKE_PROGRAM} run_preprocessing
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/common
    COMMENT "Step 1: Data Preparation - Parse .dat and build database"
)

# ============================================================
# 2. Phase 0: Initialization (init/)
# ============================================================

add_executable(gsp_init
    init/gsp_init.c
)

target_link_libraries(gsp_init
    ${SQLITE3_LIBRARIES}
    m
)

# ============================================================
# Run targets for Phase 0 strategies
# ============================================================

set(CONFIG_YAML ${CMAKE_CURRENT_SOURCE_DIR}/../config/gsp_solver.yaml)
set(DATABASE ${CMAKE_CURRENT_SOURCE_DIR}/../dat/gsp_data.db)
set(SOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../sol)

file(MAKE_DIRECTORY ${SOL_DIR})

add_custom_target(run_init_nn
    COMMAND gsp_init nn ${DATABASE} ${SOL_DIR}/init_nn.json ${CONFIG_YAML}
    DEPENDS gsp_init
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Phase 0: NN (Nearest Neighbor)"
)

add_custom_target(run_init_ge
    COMMAND gsp_init ge ${DATABASE} ${SOL_DIR}/init_ge.json ${CONFIG_YAML}
    DEPENDS gsp_init
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Phase 0: GE (Greedy Edge) - NOT YET IMPLEMENTED"
)

add_custom_target(run_init_ci
    COMMAND gsp_init ci ${DATABASE} ${SOL_DIR}/init_ci.json ${CONFIG_YAML}
    DEPENDS gsp_init
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Phase 0: CI (Cheapest Insertion) - NOT YET IMPLEMENTED"
)

add_custom_target(run_init_opt
    COMMAND gsp_init opt ${DATABASE} ${SOL_DIR}/init_opt.json ${CONFIG_YAML}
    DEPENDS gsp_init
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Phase 0: OPT (Optimal NP-MIP) - NOT YET IMPLEMENTED"
)

# ============================================================
# Install
# ============================================================

install(TARGETS gsp_init
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin
)

