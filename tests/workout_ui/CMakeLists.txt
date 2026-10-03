cmake_minimum_required(VERSION 3.16)
project(workout_ui_host C CXX ASM)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${REPO_ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(workout_ui_host test_workout_ui.c
    "${REPO_ROOT}/main/workout_ui.c" "${REPO_ROOT}/main/workout_model.c" "${REPO_ROOT}/main/ai_quota.c"
    "${REPO_ROOT}/main/workout_profiles.c"
    "${REPO_ROOT}/main/passport_calendar.c"
    "${REPO_ROOT}/main/codex_monitor.c"
    "${REPO_ROOT}/components/bsp/src/bsp_display_rounding.c"
    "${REPO_ROOT}/assets/fonts/workout_font_12.c"
    "${REPO_ROOT}/assets/fonts/workout_font_16.c"
    "${REPO_ROOT}/assets/fonts/workout_font_20.c"
    "${REPO_ROOT}/assets/fonts/workout_digits_35.c"
    "${REPO_ROOT}/assets/fonts/workout_clock_50.c"
    "${REPO_ROOT}/assets/fonts/workout_network_font_16.c"
    "${REPO_ROOT}/assets/fonts/workout_monitor_font_12.c")
target_include_directories(workout_ui_host PRIVATE ../workout_stubs
    "${REPO_ROOT}/main" "${REPO_ROOT}/assets/fonts" "${REPO_ROOT}/components/bsp/src")
target_link_libraries(workout_ui_host PRIVATE lvgl m)
