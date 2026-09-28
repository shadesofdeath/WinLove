# wl_app — shell, pages, app state, localization. Joins ui and core. Library so tests can link it.
add_library(wl_app STATIC
    App.cpp
    catalog/AppxCatalog.cpp
    controllers/ApplyController.cpp
    controllers/ComponentController.cpp
    controllers/FeatureController.cpp
    controllers/ImageController.cpp
    controllers/IsoController.cpp
    Format.cpp
    Localization.cpp
    Resources.cpp
    pages/GalleryPage.cpp
    pages/apply/RiskConfirm.cpp
    pages/apply/StatStrip.cpp
    pages/components/ComponentInspector.cpp
    pages/ApplyPage.cpp
    pages/ComponentsPage.cpp
    pages/FeaturesPage.cpp
    pages/ImagesPage.cpp
    pages/IsoPage.cpp
    pages/LogsPage.cpp
    pages/images/EditionTable.cpp
    pages/images/ImageInspector.cpp
    pages/images/OperationStrip.cpp
    pages/PageInfo.cpp
    pages/SourcePage.cpp
    pages/source/RecentList.cpp
    shell/NavRail.cpp
    shell/PageView.cpp
    shell/Shell.cpp
    shell/StatusBar.cpp
    shell/TitleBar.cpp
    state/AppSettings.cpp
    state/AppState.cpp
    state/RecentSources.cpp)
wl_configure_target(wl_app)
target_link_libraries(wl_app PUBLIC wl_core wl_ui PRIVATE wl_json)

add_executable(WinLove WIN32
    main.cpp
    WinLove.rc)
# rc.exe embeds these files (RCDATA/ICON/manifest) but the dependency scanner does not see them:
# without this, editing resources/strings/*.json would leave stale text inside WinLove.exe.
set(WL_EMBEDDED_RESOURCES
    ${PROJECT_SOURCE_DIR}/resources/WinLove.manifest
    ${PROJECT_SOURCE_DIR}/resources/brand/WinLove.ico
    ${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexSans-Regular.ttf
    ${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexSans-Medium.ttf
    ${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexSans-SemiBold.ttf
    ${PROJECT_SOURCE_DIR}/resources/fonts/JetBrainsMono-Regular.ttf
    ${PROJECT_SOURCE_DIR}/resources/catalog/appx.json
    ${PROJECT_SOURCE_DIR}/resources/strings/tr.json
    ${PROJECT_SOURCE_DIR}/resources/strings/en.json)
set_source_files_properties(WinLove.rc PROPERTIES OBJECT_DEPENDS "${WL_EMBEDDED_RESOURCES}")
wl_configure_target(WinLove)
target_link_libraries(WinLove PRIVATE wl_app shell32 ole32)
# The manifest is embedded through WinLove.rc; stop the linker from generating a second one.
target_link_options(WinLove PRIVATE /MANIFEST:NO)
