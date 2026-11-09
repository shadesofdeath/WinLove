cmake_minimum_required(VERSION 3.16)
project(WinLoveUI CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Kütüphane: projene bunu eklemen yeterli
add_library(winlove_ui STATIC src/Painter.cpp)
target_include_directories(winlove_ui PUBLIC include)
target_link_libraries(winlove_ui PUBLIC d2d1 dwrite)
target_compile_definitions(winlove_ui PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# Demo (isteğe bağlı)
option(WINLOVE_UI_DEMO "Demo uygulamasını derle" ON)
if(WINLOVE_UI_DEMO)
    add_executable(winlove_demo WIN32 demo/main.cpp)
    target_link_libraries(winlove_demo PRIVATE winlove_ui dwmapi)
    if(MSVC)
        target_compile_options(winlove_demo PRIVATE /utf-8)
        target_compile_options(winlove_ui PRIVATE /utf-8)
    endif()
endif()
