cmake_minimum_required(VERSION 3.16)
project(uosm_dashboard C)

set(CMAKE_C_STANDARD 11)

# Platform-independent modules (see docs/DASHBOARD.md, section 2)
set(CORE_SOURCES
    src/mqtt_topics.c
    src/mqtt_decode.c
    src/vehicle_state.c
    src/display.c
    lib/cjson/cJSON.c
)

# The dashboard (Linux, libmosquitto)
add_executable(uosm_dashboard src/main.c ${CORE_SOURCES})
target_include_directories(uosm_dashboard PRIVATE src lib/cjson)
target_link_libraries(uosm_dashboard PRIVATE mosquitto)

# Decoder tests, no broker needed: ./test_decode
add_executable(test_decode tests/test_decode.c src/mqtt_topics.c src/mqtt_decode.c lib/cjson/cJSON.c)
target_include_directories(test_decode PRIVATE src lib/cjson)
target_link_libraries(test_decode PRIVATE m)
