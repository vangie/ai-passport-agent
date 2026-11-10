cmake_minimum_required(VERSION 3.16)
project(passport_ui_preview C)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO}/managed_components/lvgl__lvgl" lvgl)
add_executable(preview
    "${REPO}/tests/test_passport_screen.c"
    "${REPO}/app/passport_ui.c" "${REPO}/app/passport_store.c" "${REPO}/app/passport_proxy.c"
    "${REPO}/app/passport_network.c"
    "${REPO}/assets/fonts/passport_font_16.c" "${REPO}/assets/fonts/passport_font_20.c"
    "${REPO}/assets/images/passport_lobster_22.c")
target_include_directories(preview PRIVATE "${REPO}/app")
target_link_libraries(preview PRIVATE lvgl m)
enable_testing()
add_test(NAME screen COMMAND preview)
