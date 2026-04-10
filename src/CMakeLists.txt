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
pkg_get_variable(SQLITE3_PREFIX sqlite3 prefix)

find_file(SQLITE3_RUNTIME_DLL
    NAMES libsqlite3-0.dll
    PATHS
        "${SQLITE3_PREFIX}/bin"
        "/mingw64/bin"
)

find_file(MINGW_WINPTHREAD_DLL
    NAMES libwinpthread-1.dll
    PATHS
        "/mingw64/bin"
)

if(DEFINED ENV{GUROBI_HOME})
    file(TO_CMAKE_PATH "$ENV{GUROBI_HOME}" GUROBI_HOME_CMAKE)
    set(GUROBI_INCLUDE_DIR "${GUROBI_HOME_CMAKE}/include")
    set(GUROBI_LIBRARY_DIR "${GUROBI_HOME_CMAKE}/lib")
    set(GUROBI_BIN_DIR "${GUROBI_HOME_CMAKE}/bin")
endif()

find_file(GUROBI_RUNTIME_DLL
    NAMES gurobi130.dll
    PATHS
        "${GUROBI_BIN_DIR}"
)

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

set(RUNTIME_STAGE_COMMANDS)

macro(stage_runtime_file runtime_file)
    if(${runtime_file})
        list(APPEND RUNTIME_STAGE_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${${runtime_file}}"
                "${CMAKE_CURRENT_BINARY_DIR}"
        )
    endif()
endmacro()

# ============================================================
# 1. Data Preprocessing (common/)
# ============================================================

add_subdirectory(common)

add_custom_target(prepare_routing_data
    COMMAND ${CMAKE_MAKE_PROGRAM} run_prepare_routing_data
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/common
    COMMENT "Prepare routing data: country bootstrap, station import, distance build, waypoint pruning"
)

add_custom_target(survey
    COMMAND historical_survey ${CMAKE_CURRENT_SOURCE_DIR}/../dat/survey2023spring.dat ${CMAKE_CURRENT_SOURCE_DIR}/../dat/gsp.db ${CMAKE_CURRENT_SOURCE_DIR}/../sol/survey
    DEPENDS historical_survey
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/common
    COMMENT "Export historical survey JSON directly from DAT + database"
)

# ============================================================
# 2. Unified GSP Solver (both init and sweep modes)
# ============================================================

add_executable(gsp
    gsp.c
    init/gsp_init_mode.c
    init/local_postopt.c
    init/nearest_neighbor.c
    init/greedy_insertion.c
    init/cheapest_insertion.c
    common/feasibility.c
    common/json_utils.c
    common/mip_report.c
    common/sql_utils.c
    sweep/gsp_sweep_mode.c
)

target_link_libraries(gsp
    ${SQLITE3_LIBRARIES}
    m
)

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    add_executable(gsp_gurobi
        gsp.c
        init/gsp_init_mode.c
        init/local_postopt.c
        init/nearest_neighbor.c
        init/greedy_insertion.c
        init/cheapest_insertion.c
        common/feasibility.c
        common/json_utils.c
        common/mip_report.c
        common/sql_utils.c
        sweep/gsp_sweep_mode.c
        mip/endpaired_tsp.c
        mip/mip_common.c
        mip/mip_paired_tour.c
    )

    target_link_libraries(gsp_gurobi
        ${SQLITE3_LIBRARIES}
        m
    )

    target_include_directories(gsp_gurobi PRIVATE
        ${GUROBI_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mip/include
    )
    target_compile_definitions(gsp_gurobi PRIVATE HAVE_GUROBI=1)
    target_link_libraries(gsp_gurobi
        -L${GUROBI_LIBRARY_DIR}
        gurobi130
    )
endif()

add_executable(gsp_init_from_order
    init/init_from_order.c
    init/local_postopt.c
    common/feasibility.c
    common/json_utils.c
    common/mip_report.c
    common/sql_utils.c
)

target_link_libraries(gsp_init_from_order
    ${SQLITE3_LIBRARIES}
    m
)

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    target_include_directories(gsp_init_from_order PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/mip/include
        ${GUROBI_INCLUDE_DIR}
    )
    target_compile_definitions(gsp_init_from_order PRIVATE HAVE_GUROBI=1)
    target_sources(gsp_init_from_order PRIVATE
        mip/endpaired_tsp.c
        mip/mip_common.c
        mip/mip_paired_tour.c
    )
    target_link_libraries(gsp_init_from_order
        -L${GUROBI_LIBRARY_DIR}
        gurobi130
    )
endif()

# ============================================================
# Run targets for Phase 0 strategies
# ============================================================

set(CONFIG_YAML ${CMAKE_CURRENT_SOURCE_DIR}/../config/gsp_solver.yaml)
set(DATABASE ${CMAKE_CURRENT_SOURCE_DIR}/../dat/gsp.db)
set(SOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../sol)
set(SOL_NN_DIR ${SOL_DIR}/nn)
set(SOL_GE_DIR ${SOL_DIR}/ge)
set(SOL_CI_DIR ${SOL_DIR}/ci)
set(SOL_NOPORT_DIR ${SOL_DIR}/noport)
set(SOL_FIXEDPORT_DIR ${SOL_DIR}/fixedport)

file(MAKE_DIRECTORY ${SOL_DIR})
file(MAKE_DIRECTORY ${SOL_NN_DIR})
file(MAKE_DIRECTORY ${SOL_GE_DIR})
file(MAKE_DIRECTORY ${SOL_CI_DIR})
file(MAKE_DIRECTORY ${SOL_NOPORT_DIR})
file(MAKE_DIRECTORY ${SOL_FIXEDPORT_DIR})

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    add_custom_target(run_init_nn
        COMMAND gsp_gurobi --mode init --strategy nn --database ${DATABASE} --output ${SOL_NN_DIR}/init.json --config ${CONFIG_YAML}
        DEPENDS gsp_gurobi
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Phase 0: NN (Nearest Neighbor)"
    )

    add_custom_target(run_init_ge
        COMMAND gsp_gurobi --mode init --strategy ge --database ${DATABASE} --output ${SOL_GE_DIR}/init.json --config ${CONFIG_YAML}
        DEPENDS gsp_gurobi
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Phase 0: GE (Greedy Edge)"
    )

    add_custom_target(run_init_ci
        COMMAND gsp_gurobi --mode init --strategy ci --database ${DATABASE} --output ${SOL_CI_DIR}/init.json --config ${CONFIG_YAML}
        DEPENDS gsp_gurobi
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Phase 0: CI (Cheapest Insertion)"
    )
else()
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
        COMMENT "Phase 0: CI (Cheapest Insertion)"
    )
endif()

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    set(GSP_MIP_SHARED_SOURCES
        mip/mip_common.c
        mip/mip_paired_tour.c
    )

    add_executable(gsp_noport
        mip/noport.c
        mip/endpaired_tsp.c
        ${GSP_MIP_SHARED_SOURCES}
        init/noport_order_main.c
        common/feasibility.c
        common/json_utils.c
        common/mip_report.c
        common/sql_utils.c
    )

    target_include_directories(gsp_noport PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/mip/include
        ${SQLITE3_INCLUDE_DIRS}
        ${GUROBI_INCLUDE_DIR}
    )

    target_link_libraries(gsp_noport
        ${SQLITE3_LIBRARIES}
        -L${GUROBI_LIBRARY_DIR}
        gurobi130
        m
    )

    add_custom_target(run_noport
        COMMAND $<TARGET_FILE:gsp_noport> --database ${DATABASE} --config ${CONFIG_YAML} --output ${SOL_NOPORT_DIR}/noport.json
        DEPENDS gsp_noport
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Preprocessing: no-port optimal station ordering"
    )
else()
    add_custom_target(run_noport
        COMMAND ${CMAKE_COMMAND} -E echo "No-port unavailable: Gurobi headers/library not found during CMake configure"
        COMMAND ${CMAKE_COMMAND} -E false
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Preprocessing: no-port optimal station ordering"
    )
endif()

add_custom_target(run_init_from_order
    COMMAND $<TARGET_FILE:gsp_init_from_order> --database ${DATABASE} --config ${CONFIG_YAML} --input ${SOL_NOPORT_DIR}/noport.json --output ${SOL_NOPORT_DIR}/init.json
    DEPENDS gsp_init_from_order
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Phase 0: initialization from ordered input"
)

add_custom_target(run_init_noport
    DEPENDS run_noport run_init_from_order
    COMMENT "Phase 0: no-port initialization"
)

# ============================================================
# Tools
# ============================================================

# Export historical survey to JSON for plotting
add_executable(historical_survey
    tools/export_survey_json.c
    common/dat_parser.c
    common/feasibility.c
    common/json_utils.c
    common/sql_utils.c
)

target_link_libraries(historical_survey
    ${SQLITE3_LIBRARIES}
)

target_include_directories(historical_survey PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SQLITE3_INCLUDE_DIRS}
)

add_executable(gsp_country
    tools/country_bootstrap_main.c
    common/dat_parser.c
    common/sql_utils.c
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

install(TARGETS gsp historical_survey gsp_country gsp_stations gsp_distances gsp_prepare_routing_data
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin
)

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    install(TARGETS gsp_noport
        RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin
    )
    install(TARGETS gsp_gurobi
        RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin
    )
endif()

stage_runtime_file(SQLITE3_RUNTIME_DLL)
stage_runtime_file(MINGW_WINPTHREAD_DLL)
stage_runtime_file(GUROBI_RUNTIME_DLL)

if(RUNTIME_STAGE_COMMANDS)
    add_custom_target(stage_runtime_deps ALL
        ${RUNTIME_STAGE_COMMANDS}
        COMMENT "Stage runtime DLLs into build directory"
    )
endif()

install(TARGETS gsp_init_from_order
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin
)
