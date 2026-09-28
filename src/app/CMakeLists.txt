# wl_app — shell, pages, app state, localization. Joins ui and core. Library so tests can link it.
add_library(wl_app STATIC
    App.cpp
    Localization.cpp
    Resources.cpp
    shell/TitleBar.cpp)
wl_configure_target(wl_app)
target_link_libraries(wl_app PUBLIC wl_core wl_ui PRIVATE wl_json)

add_executable(WinLove WIN32
    main.cpp
    WinLove.rc)
wl_configure_target(WinLove)
target_link_libraries(WinLove PRIVATE wl_app shell32 ole32)
# The manifest is embedded through WinLove.rc; stop the linker from generating a second one.
target_link_options(WinLove PRIVATE /MANIFEST:NO)
