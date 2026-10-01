# wl_base — error model, UTF-8 helpers, log. The only library both core and ui may depend on.
add_library(wl_base STATIC
    Log.cpp
    Result.cpp
    Text.cpp
    File.cpp
    Utf8.cpp)
wl_configure_target(wl_base)
target_link_libraries(wl_base PRIVATE shell32 ole32)
