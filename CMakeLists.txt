cmake_minimum_required(VERSION 3.16)
project(untitled)

set(CMAKE_CXX_STANDARD 20)
project(untitled C)
add_executable(untitled main.c mqtt_decode.c cJSON.c mqtt_topics.c vehicle_state.c display.c)
target_link_libraries(untitled mosquitto)