add_executable(gsp_segment_from_construction
    segment_from_order.c
    segment_postopt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/feasibility.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/json_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/mip_report.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/sql_utils.c
)

set_target_properties(gsp_segment_from_construction PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_link_libraries(gsp_segment_from_construction
    ${SQLITE3_LIBRARIES}
    m
)

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    target_include_directories(gsp_segment_from_construction PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../mip/include
        ${GUROBI_INCLUDE_DIR}
    )
    target_compile_definitions(gsp_segment_from_construction PRIVATE HAVE_GUROBI=1)
    target_sources(gsp_segment_from_construction PRIVATE
        ${GSP_MIP_ENDPAIRED_SOURCE}
        ${GSP_MIP_SHARED_SOURCES}
    )
    target_link_libraries(gsp_segment_from_construction
        ${GUROBI_C_LIBRARY}
    )
endif()

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    add_executable(gsp_noport
        ${GSP_MIP_NOPORT_SOURCE}
        ${GSP_MIP_ENDPAIRED_SOURCE}
        ${GSP_MIP_SHARED_SOURCES}
        noport_order_main.c
        segment_postopt.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/feasibility.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/json_utils.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/mip_report.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/sql_utils.c
    )

    set_target_properties(gsp_noport PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    target_include_directories(gsp_noport PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../mip/include
        ${SQLITE3_INCLUDE_DIRS}
        ${GUROBI_INCLUDE_DIR}
    )

    target_link_libraries(gsp_noport
        ${SQLITE3_LIBRARIES}
        ${GUROBI_C_LIBRARY}
        m
    )

    add_custom_target(run_noport
        COMMAND $<TARGET_FILE:gsp_noport> --database ${DATABASE} --config ${CONFIG_YAML} --output ${SOL_NOPORT_DIR}/noport.json
        DEPENDS gsp_noport
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Preprocessing: no-port optimal station ordering"
    )

    add_executable(gsp_fixedport
        ${GSP_MIP_FIXEDPORT_SOURCE}
        ${GSP_MIP_ENDPAIRED_SOURCE}
        ${GSP_MIP_SHARED_SOURCES}
        fixedport_order_main.c
        segment_postopt.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/json_utils.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/mip_report.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/sql_utils.c
    )

    set_target_properties(gsp_fixedport PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    target_include_directories(gsp_fixedport PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../mip/include
        ${SQLITE3_INCLUDE_DIRS}
        ${GUROBI_INCLUDE_DIR}
    )

    target_link_libraries(gsp_fixedport
        ${SQLITE3_LIBRARIES}
        ${GUROBI_C_LIBRARY}
        m
    )

    add_custom_target(run_fixedport
        COMMAND $<TARGET_FILE:gsp_fixedport> --database ${DATABASE} --config ${CONFIG_YAML} --candidates ${CMAKE_CURRENT_SOURCE_DIR}/../../sol/fixedport/candidate_ports.json --output ${SOL_FIXEDPORT_DIR}/fixedport.json
        DEPENDS gsp_fixedport
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Preprocessing: fixed-port debug optimization"
    )
else()
    add_custom_target(run_noport
        COMMAND ${CMAKE_COMMAND} -E echo "No-port unavailable: Gurobi headers/library not found during CMake configure"
        COMMAND ${CMAKE_COMMAND} -E false
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Preprocessing: no-port optimal station ordering"
    )

    add_custom_target(run_fixedport
        COMMAND ${CMAKE_COMMAND} -E echo "Fixed-port unavailable: Gurobi headers/library not found during CMake configure"
        COMMAND ${CMAKE_COMMAND} -E false
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Preprocessing: fixed-port debug optimization"
    )
endif()

if(GUROBI_INCLUDE_DIR AND GUROBI_LIBRARY_DIR)
    add_custom_target(run_construction_nn
        COMMAND gsp_gurobi --mode construction --strategy nn --database ${DATABASE} --output ${SOL_NN_DIR}/construction.json --config ${CONFIG_YAML}
        DEPENDS gsp_gurobi
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Construction: NN (Nearest Neighbor)"
    )

    add_custom_target(run_construction_ge
        COMMAND gsp_gurobi --mode construction --strategy ge --database ${DATABASE} --output ${SOL_GE_DIR}/construction.json --config ${CONFIG_YAML}
        DEPENDS gsp_gurobi
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Construction: GE (Greedy Edge)"
    )

    add_custom_target(run_construction_ci
        COMMAND gsp_gurobi --mode construction --strategy ci --database ${DATABASE} --output ${SOL_CI_DIR}/construction.json --config ${CONFIG_YAML}
        DEPENDS gsp_gurobi
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Construction: CI (Cheapest Insertion)"
    )
else()
    add_custom_target(run_construction_nn
        COMMAND gsp --mode construction --strategy nn --database ${DATABASE} --output ${SOL_NN_DIR}/construction.json --config ${CONFIG_YAML}
        DEPENDS gsp
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Construction: NN (Nearest Neighbor)"
    )

    add_custom_target(run_construction_ge
        COMMAND gsp --mode construction --strategy ge --database ${DATABASE} --output ${SOL_GE_DIR}/construction.json --config ${CONFIG_YAML}
        DEPENDS gsp
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Construction: GE (Greedy Edge)"
    )

    add_custom_target(run_construction_ci
        COMMAND gsp --mode construction --strategy ci --database ${DATABASE} --output ${SOL_CI_DIR}/construction.json --config ${CONFIG_YAML}
        DEPENDS gsp
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Construction: CI (Cheapest Insertion)"
    )
endif()

add_custom_target(run_segment_from_construction
    COMMAND $<TARGET_FILE:gsp_segment_from_construction> --database ${DATABASE} --config ${CONFIG_YAML} --input ${SOL_NOPORT_DIR}/construction.json --output ${SOL_NOPORT_DIR}/segment.json
    DEPENDS gsp_segment_from_construction
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Segment: build a segmented baseline from construction input"
)

add_custom_target(run_segment_noport
    DEPENDS run_noport run_segment_from_construction
    COMMENT "Construction + segment: no-port route initialization"
)
