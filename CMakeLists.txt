cmake_minimum_required(VERSION 3.16)
project(uosm_dashboard C)

set(CMAKE_C_STANDARD 11)

# Platform-independent modules (see docs/DASHBOARD.md, section 2)
set(CORE_SOURCES
    src/mqtt_topics.c
    src/mqtt_decode.c
    src/vehicle_state.c
    src/display.c
    src/dashboard_state.c
    lib/cjson/cJSON.c
)

# PC simulator of the screen: cmake -B build-sim -G Ninja -DUOSM_SIM=ON
option(UOSM_SIM "Build the screen simulator for a PC (LVGL + SDL2) instead of the dashboard" OFF)
set(SDL2_ROOT "${CMAKE_SOURCE_DIR}/../deps/SDL2-2.32.10/x86_64-w64-mingw32" CACHE PATH "SDL2 development package (MinGW)")

# LVGL 9.2.2 (git submodule), configured by src/lv_conf.h
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS lib/lvgl/src/*.c)
add_library(lvgl STATIC EXCLUDE_FROM_ALL ${LVGL_SOURCES})   # only built when a target links it
target_include_directories(lvgl PUBLIC lib/lvgl src)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

if(UOSM_SIM)
    target_compile_definitions(lvgl PUBLIC UOSM_SIM)
    target_include_directories(lvgl PUBLIC ${SDL2_ROOT}/include)
    target_link_libraries(lvgl PUBLIC ${SDL2_ROOT}/lib/libSDL2.dll.a)

    add_executable(ui_sim sim/main_sim.c src/ui.c src/ui_assets.c)
    target_link_libraries(ui_sim PRIVATE lvgl m)
    target_compile_definitions(ui_sim PRIVATE SDL_MAIN_HANDLED)   # our own main(), not SDL's
    add_custom_command(TARGET ui_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL2_ROOT}/bin/SDL2.dll $<TARGET_FILE_DIR:ui_sim>)
else()
    # The dashboard (Linux, libmosquitto), with the screen on /dev/fb0
    add_executable(uosm_dashboard src/main.c ${CORE_SOURCES} src/screen.c src/ui.c src/ui_assets.c)
    target_include_directories(uosm_dashboard PRIVATE src lib/cjson)
    target_link_libraries(uosm_dashboard PRIVATE mosquitto lvgl m)
endif()

# Decoder tests, no broker needed: ./test_decode
add_executable(test_decode tests/test_decode.c src/mqtt_topics.c src/mqtt_decode.c lib/cjson/cJSON.c)
target_include_directories(test_decode PRIVATE src lib/cjson)
target_link_libraries(test_decode PRIVATE m)

# dashboard_state tests, no screen needed: ./test_dashboard_state
add_executable(test_dashboard_state tests/test_dashboard_state.c src/dashboard_state.c src/vehicle_state.c)
target_include_directories(test_dashboard_state PRIVATE src lib/cjson)
target_link_libraries(test_dashboard_state PRIVATE m)
