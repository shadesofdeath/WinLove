cmake_minimum_required(VERSION 3.28)
project(WinLove VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "WinLove builds only with MSVC on Windows.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: WinLove.exe / wlcli.exe run from a USB stick or WinPE without redistributables.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/WinLoveTarget.cmake)

add_subdirectory(third_party)
add_subdirectory(src/base)
add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/app)
add_subdirectory(src/cli)

enable_testing()
add_subdirectory(tests)
