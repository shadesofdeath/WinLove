# Vendored dependencies (see docs/DECISIONS.md D-006). Included as SYSTEM so their warnings
# never trip /WX in first-party code.

add_library(wl_doctest INTERFACE)
target_include_directories(wl_doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/doctest)

add_library(wl_json INTERFACE)
target_include_directories(wl_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann)

add_library(wl_pugixml STATIC pugixml/pugixml.cpp)
target_include_directories(wl_pugixml SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/pugixml)
target_compile_options(wl_pugixml PRIVATE /W0 /utf-8)
