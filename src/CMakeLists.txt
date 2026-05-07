cmake_minimum_required(VERSION 3.10)
project(GSP_Solver C)

# Fix Windows/MSYS2 CMake dependency tracking issue
if(POLICY CMP0160)
    cmake_policy(SET CMP0160 OLD)
endif()

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_DEPENDS_USE_COMPILER FALSE)
set(CMAKE_DEPEND_USE_MODULES FALSE)
set(CMAKE_C_DEPENDS_USE_COMPILER FALSE)
option(GSP_ENABLE_GUROBI "Build Gurobi-backed routing executables" ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

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

if(GSP_ENABLE_GUROBI AND DEFINED ENV{GUROBI_HOME})
    set(GUROBI_HOME_RAW "$ENV{GUROBI_HOME}")
    string(REGEX REPLACE "/$" "" GUROBI_HOME_RAW "${GUROBI_HOME_RAW}")

    if(GUROBI_HOME_RAW MATCHES "^[A-Za-z]:[\\\\/].*")
        string(SUBSTRING "${GUROBI_HOME_RAW}" 0 1 GUROBI_DRIVE)
        string(TOLOWER "${GUROBI_DRIVE}" GUROBI_DRIVE)
        string(REGEX REPLACE "^[A-Za-z]:[\\\\/]?" "" GUROBI_PATH_TAIL "${GUROBI_HOME_RAW}")
        string(REPLACE "\\" "/" GUROBI_PATH_TAIL "${GUROBI_PATH_TAIL}")
        set(GUROBI_HOME_CMAKE "/${GUROBI_DRIVE}/${GUROBI_PATH_TAIL}")
    elseif(GUROBI_HOME_RAW MATCHES "^/([A-Za-z])/(.*)")
        set(GUROBI_HOME_CMAKE "${GUROBI_HOME_RAW}")
    else()
        file(TO_CMAKE_PATH "${GUROBI_HOME_RAW}" GUROBI_HOME_CMAKE)
    endif()

    set(GUROBI_INCLUDE_DIR "${GUROBI_HOME_CMAKE}/include")
    set(GUROBI_LIBRARY_DIR "${GUROBI_HOME_CMAKE}/lib")
    set(GUROBI_BIN_DIR "${GUROBI_HOME_CMAKE}/bin")
    unset(GUROBI_RUNTIME_DLL CACHE)
    find_file(GUROBI_RUNTIME_DLL
        NAMES gurobi130.dll
        PATHS
            "${GUROBI_BIN_DIR}"
    )

    unset(GUROBI_C_LIBRARY CACHE)
    find_file(GUROBI_C_LIBRARY
        NAMES gurobi130.lib libgurobi130.dll.a
        PATHS "${GUROBI_LIBRARY_DIR}"
        NO_DEFAULT_PATH
    )

    if(GUROBI_INCLUDE_DIR AND EXISTS "${GUROBI_INCLUDE_DIR}/gurobi_c.h" AND
       GUROBI_LIBRARY_DIR AND EXISTS "${GUROBI_LIBRARY_DIR}" AND
       GUROBI_C_LIBRARY)
        message(STATUS "Found Gurobi include: ${GUROBI_INCLUDE_DIR}")
        message(STATUS "Found Gurobi library dir: ${GUROBI_LIBRARY_DIR}")
        message(STATUS "Found Gurobi bin dir: ${GUROBI_BIN_DIR}")
        message(STATUS "Found Gurobi library: ${GUROBI_C_LIBRARY}")
    else()
        message(STATUS "Gurobi not found. GUROBI_HOME raw='$ENV{GUROBI_HOME}', normalized='${GUROBI_HOME_CMAKE}'")
        unset(GUROBI_INCLUDE_DIR)
        unset(GUROBI_LIBRARY_DIR)
        unset(GUROBI_BIN_DIR)
        unset(GUROBI_C_LIBRARY)
    endif()
else()
    unset(GUROBI_INCLUDE_DIR)
    unset(GUROBI_LIBRARY_DIR)
    unset(GUROBI_BIN_DIR)
    unset(GUROBI_C_LIBRARY)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SQLITE3_INCLUDE_DIRS}
    ${GEOS_INCLUDE_DIRS}
)

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

add_subdirectory(common)
add_subdirectory(mip)
add_subdirectory(tools)
add_subdirectory(sweep)
add_subdirectory(init)

add_custom_target(prepare_routing_data
    COMMAND ${CMAKE_MAKE_PROGRAM} run_prepare_routing_data
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/common
    COMMENT "Prepare routing data: country bootstrap, station import, distance build, waypoint pruning"
)

add_custom_target(survey
    COMMAND historical_survey
            ${CMAKE_CURRENT_SOURCE_DIR}/../dat/survey2023spring.dat
            ${CMAKE_CURRENT_SOURCE_DIR}/../dat/gsp.db
            ${CMAKE_CURRENT_SOURCE_DIR}/../sol/survey/multivessel.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../config/gsp_solver.yaml
    DEPENDS historical_survey
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Export historical survey to sol/survey/multivessel.json"
)

install(TARGETS
    gsp
    historical_survey
    survey_fixedport_candidates
    gsp_country
    gsp_stations
    gsp_distances
    gsp_prepare_routing_data
    gsp_segment_from_construction
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../bin
)

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    install(TARGETS
        gsp_noport
        gsp_fixedport
        gsp_gurobi
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
