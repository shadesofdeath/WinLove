# wlcli.exe — engine test driver. Links core only (never ui).
add_executable(wlcli
    main.cpp
    wlcli.rc)
wl_configure_target(wlcli)
target_link_libraries(wlcli PRIVATE wl_core wl_json)
target_link_options(wlcli PRIVATE /MANIFEST:NO)
