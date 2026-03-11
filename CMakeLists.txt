cmake_minimum_required(VERSION 4.1)
project(Code C)

set(CMAKE_C_STANDARD 11)

include_directories(src-refactor/include)
include_directories(src-refactor/mip/include)

add_executable(Code
        src-refactor/common/dat_parser.c
        src-refactor/common/distance.c
        src-refactor/common/logging.c
        src-refactor/common/prep_data.c
        src-refactor/common/sql_utils.c
        src-refactor/include/constants.h
        src-refactor/include/dat_parser.h
        src-refactor/include/data_types.h
        src-refactor/include/db_helpers.h
        src-refactor/include/db_schema.h
        src-refactor/include/db_writer.h
        src-refactor/include/distance.h
        src-refactor/include/logging.h
        src-refactor/include/output.h
        src-refactor/include/sql_utils.h
        src-refactor/init/init.c
        src-refactor/mip/include/mip_capacity_aware.h
        src-refactor/mip/include/mip_endpaired_tsp.h
        src-refactor/mip/include/mip_noport.h
        src-refactor/mip/capacity_aware.c
        src-refactor/mip/endpaired_tsp.c
        src-refactor/mip/noport.c
        src-refactor/sweep/sweep.c
        tools/test_gurobi.c
        tools/test_sqlite.c)
