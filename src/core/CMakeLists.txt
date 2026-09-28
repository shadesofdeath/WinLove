# wl_core — image engine: sources, sessions, backends, change sets, tasks. Knows nothing about UI.
add_library(wl_core STATIC
    image/ImageFormat.cpp)
wl_configure_target(wl_core)
target_link_libraries(wl_core PUBLIC wl_base)
