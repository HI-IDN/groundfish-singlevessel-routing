add_executable(gsp
    ${CMAKE_CURRENT_SOURCE_DIR}/../gsp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../init/gsp_init_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../init/segment_postopt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../init/construction_nn.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../init/construction_ge.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../init/construction_ci.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/feasibility.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/json_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/mip_report.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/sql_utils.c
    gsp_sweep_mode.c
    gsp_sweep_fallback.c
)

set_target_properties(gsp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_link_libraries(gsp
    ${SQLITE3_LIBRARIES}
    m
)

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    add_executable(gsp_gurobi
        ${CMAKE_CURRENT_SOURCE_DIR}/../gsp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../init/gsp_init_mode.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../init/segment_postopt.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../init/construction_nn.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../init/construction_ge.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../init/construction_ci.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/feasibility.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/json_utils.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/mip_report.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/sql_utils.c
        gsp_sweep_mode.c
        gsp_sweep_fallback.c
        ${GSP_MIP_FIXEDPORT_SOURCE}
        ${GSP_MIP_ENDPAIRED_SOURCE}
        ${GSP_MIP_SHARED_SOURCES}
    )

    set_target_properties(gsp_gurobi PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    target_link_libraries(gsp_gurobi
        ${SQLITE3_LIBRARIES}
        m
    )

    target_include_directories(gsp_gurobi PRIVATE
        ${GUROBI_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../mip/include
    )
    target_compile_definitions(gsp_gurobi PRIVATE HAVE_GUROBI=1)
    target_link_libraries(gsp_gurobi
        ${GUROBI_C_LIBRARY}
    )
endif()
