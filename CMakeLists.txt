cmake_minimum_required(VERSION 3.22)
project(mnrdonw CXX)

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
