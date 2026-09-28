# wl_base — error model, UTF-8 helpers. The only library both core and ui may depend on.
add_library(wl_base STATIC
    Result.cpp
    Utf8.cpp)
wl_configure_target(wl_base)
