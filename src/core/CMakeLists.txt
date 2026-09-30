# wl_core — image engine: sources, sessions, backends, change sets, tasks. Knows nothing about UI.
add_library(wl_core STATIC
    image/DriverInf.cpp
    image/OfflineHive.cpp
    image/RegistryEdit.cpp
    image/Services.cpp
    image/ImageFormat.cpp
    image/Source.cpp
    image/SystemComponents.cpp
    image/UdfImage.cpp
    image/UpdatePackage.cpp
    image/WimFile.cpp
    image/WindowsRelease.cpp
    image/dism/Appx.cpp
    image/dism/Dism.cpp
    image/dism/DismErrors.cpp
    image/dism/DismExe.cpp
    image/dism/Edition.cpp
    image/dism/MountHealth.cpp
    image/dism/OptionalFeatures.cpp
    image/dism/StoreCleanup.cpp
    image/wim/Lzx.cpp
    image/wim/WimGapi.cpp
    image/wim/WimVerify.cpp
    io/ByteSource.cpp
    iso/IsoBuilder.cpp
    ops/ApplyJob.cpp
    ops/Applier.cpp
    ops/ChangeSet.cpp
    ops/Planner.cpp
    postsetup/PostSetup.cpp
    system/FileLocks.cpp
    system/Privileges.cpp
    tasks/Task.cpp
    tasks/TaskRunner.cpp
    unattend/Unattend.cpp)
wl_configure_target(wl_core)
target_link_libraries(wl_core PUBLIC wl_base PRIVATE wl_pugixml wl_json advapi32 shell32 ole32 oleaut32 shlwapi rstrtmgr bcrypt)
