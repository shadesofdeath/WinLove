# wl_core — image engine: sources, sessions, backends, change sets, tasks. Knows nothing about UI.
add_library(wl_core STATIC
    image/ImageFormat.cpp
    image/Source.cpp
    image/UdfImage.cpp
    image/WimFile.cpp
    image/WindowsRelease.cpp
    image/dism/Dism.cpp
    image/dism/DismErrors.cpp
    image/dism/MountHealth.cpp
    image/dism/OptionalFeatures.cpp
    image/wim/WimGapi.cpp
    io/ByteSource.cpp
    iso/IsoBuilder.cpp
    ops/ApplyJob.cpp
    ops/Applier.cpp
    ops/ChangeSet.cpp
    ops/Planner.cpp
    system/FileLocks.cpp
    system/Privileges.cpp
    tasks/Task.cpp
    tasks/TaskRunner.cpp)
wl_configure_target(wl_core)
target_link_libraries(wl_core PUBLIC wl_base PRIVATE wl_pugixml wl_json advapi32 shell32 ole32 oleaut32 shlwapi rstrtmgr bcrypt)
