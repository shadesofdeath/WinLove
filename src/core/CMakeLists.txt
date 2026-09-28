# wl_core — image engine: sources, sessions, backends, change sets, tasks. Knows nothing about UI.
add_library(wl_core STATIC
    image/ImageFormat.cpp
    image/Source.cpp
    image/UdfImage.cpp
    image/WimFile.cpp
    image/dism/Dism.cpp
    io/ByteSource.cpp
    system/Privileges.cpp
    tasks/Task.cpp
    tasks/TaskRunner.cpp)
wl_configure_target(wl_core)
target_link_libraries(wl_core PUBLIC wl_base PRIVATE wl_pugixml advapi32 shell32 ole32)
