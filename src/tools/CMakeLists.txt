add_executable(historical_survey
    export_survey_json.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/dat_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/feasibility.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/json_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/sql_utils.c
)

set_target_properties(historical_survey PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_link_libraries(historical_survey
    ${SQLITE3_LIBRARIES}
)

target_include_directories(historical_survey PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${SQLITE3_INCLUDE_DIRS}
)

add_executable(survey_fixedport_candidates
    export_fixedport_candidates.c
)

set_target_properties(survey_fixedport_candidates PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_link_libraries(survey_fixedport_candidates
    ${SQLITE3_LIBRARIES}
    m
)

target_include_directories(survey_fixedport_candidates PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_custom_target(fixedport_candidates
    COMMAND survey_fixedport_candidates ${CMAKE_CURRENT_SOURCE_DIR}/../../config/gsp_solver.yaml ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/gsp.db ${CMAKE_CURRENT_SOURCE_DIR}/../../dat/candidate_ports.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sol/survey/boat1.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sol/survey/boat2.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sol/survey/boat3.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sol/survey/boat4.json
    DEPENDS survey_fixedport_candidates
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Aggregate fixed-port candidate sequence from survey boat JSONs"
)

add_executable(gsp_country
    country_bootstrap_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/dat_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/sql_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/coastline_db.c
)

set_target_properties(gsp_country PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_link_libraries(gsp_country
    ${SQLITE3_LIBRARIES}
    ${GEOS_LIBRARIES}
    m
)

target_include_directories(gsp_country PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${SQLITE3_INCLUDE_DIRS}
    ${GEOS_INCLUDE_DIRS}
)
