cmake_minimum_required(VERSION 3.22)
project(mnrdonw LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(APP_SHELL_APP_NAME "mnrdonw" CACHE STRING
    "Base name for the log file and the Android logcat tag.")
set(APP_SHELL_WIN_CLASS "MnrdonwMain" CACHE STRING
    "Win32 window class name for the main window.")
set(APP_SHELL_WINDOW_TITLE "mnrdonw" CACHE STRING
    "Window title shown to the user.")
set(APP_SHELL_APP_ID "io.nava.mnrdonw" CACHE STRING
    "Wayland app_id / desktop-file base name.")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(editor_test
        tests/editor_test.cc
        editor/utf8.cc
        editor/document.cc
        editor/parse.cc
        editor/inline.cc
        editor/layout.cc
        editor/save.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/firstparty/framework/vk_canvas/core/text_buffer.cc
    )
    target_include_directories(editor_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/editor
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/firstparty/framework/vk_canvas/core
    )
endif()

# ui_metrics_test, inside app_shell, depends on generate_ui_min_text_size at
# configure time. The generator has to exist before that subdirectory.
set(VK_CANVAS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/firstparty/framework/vk_canvas CACHE PATH
    "Where vk_canvas lives; app_shell needs its core headers and one backend")
include(${CMAKE_CURRENT_SOURCE_DIR}/libs/firstparty/framework/app_shell/cmake/AppShellMinTextSize.cmake)
app_shell_generate_min_text_size(
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/newcomputermodern/NewCM10-Regular.otf
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/newcomputermodern/NewCM10-Bold.otf
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/newcomputermodern/NewCM10-Italic.otf)

add_subdirectory(libs/firstparty/framework/app_shell)
add_dependencies(app_shell generate_ui_min_text_size)

add_subdirectory(${VK_CANVAS_DIR}/core ${CMAKE_BINARY_DIR}/vk_canvas_core)
add_subdirectory(${VK_CANVAS_DIR}/platform/linux ${CMAKE_BINARY_DIR}/vk_canvas_wayland)

include(${VK_CANVAS_DIR}/cmake/VceShaders.cmake)
vce_compile_slang(mnrdonw_shaders
    ${CMAKE_BINARY_DIR}/assets/shaders
    ${VK_CANVAS_DIR}/shaders_src
    overlay_vert
    overlay_frag
    image_vert
    image_frag
    shape_vert
    shape_frag)
vce_compile_slang(mnrdonw_font_shaders
    ${CMAKE_BINARY_DIR}/assets/shaders
    ${VK_CANVAS_DIR}/first_party/vulkan_font_engine/shaders_src
    composite_vert
    composite_frag
    tiling
    coverage
    msdf_vert
    msdf_frag)

set(MNRDONW_FONT_DIR ${CMAKE_BINARY_DIR}/assets/fonts/newcomputermodern)
set(MNRDONW_FONT_OUTPUTS)
foreach(face NewCM10-Regular.otf NewCM10-Bold.otf NewCM10-Italic.otf)
    add_custom_command(
        OUTPUT ${MNRDONW_FONT_DIR}/${face}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MNRDONW_FONT_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/newcomputermodern/${face}
            ${MNRDONW_FONT_DIR}/${face}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/newcomputermodern/${face}
        COMMENT "Copying ${face}")
    list(APPEND MNRDONW_FONT_OUTPUTS ${MNRDONW_FONT_DIR}/${face})
endforeach()
add_custom_target(mnrdonw_fonts DEPENDS ${MNRDONW_FONT_OUTPUTS})

add_executable(mnrdonw
    app/main.cc
    app/editor_app.cc
    editor/utf8.cc
    editor/document.cc
    editor/parse.cc
    editor/inline.cc
    editor/layout.cc
    editor/save.cc)
set_target_properties(mnrdonw PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(mnrdonw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mnrdonw PRIVATE app_shell app_shell_wayland vk_canvas_wayland)
add_dependencies(mnrdonw generate_ui_min_text_size mnrdonw_shaders mnrdonw_font_shaders mnrdonw_fonts)
