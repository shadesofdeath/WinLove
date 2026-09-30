# Unit tests (label: unit) â€” no admin, no images. Integration tests (label: integration) are added
# in Faz 2.6 and need an elevated shell plus build\lab (docs/TESTING.md).
add_executable(wl_tests
    main.cpp
    support/TestGraphics.cpp
    base/ResultTests.cpp
    base/ScriptTests.cpp
    core/AppxRemovalTests.cpp
    core/ChangeSetTests.cpp
    core/DriverTests.cpp
    core/EngineTests.cpp
    core/RegistryTests.cpp
    core/RegistryReadTests.cpp
    core/SystemComponentTests.cpp
    core/ImageEditionTests.cpp
    core/ImageFilesTests.cpp
    core/ImageFormatTests.cpp
    core/IsoTests.cpp
    core/PostSetupTests.cpp
    core/UnattendTests.cpp
    core/UpdateTests.cpp
    core/UpdateCatalogTests.cpp
    core/UsbMediaTests.cpp
    core/ImageExtrasTests.cpp
    core/WimEditTests.cpp
    core/WimVerifyTests.cpp
    ui/TokensTests.cpp
    ui/IconsTests.cpp
    ui/RenderTests.cpp
    ui/WidgetTests.cpp
    app/AnswerStoreTests.cpp
    app/ApplyTests.cpp
    app/ComponentTests.cpp
    app/EditionTests.cpp
    app/FeatureTests.cpp
    app/ImageSettingsTests.cpp
    app/ImageValuesTests.cpp
    app/PostSetupControllerTests.cpp
    app/PreloadTests.cpp
    app/PresetTests.cpp
    app/RegistryImportTests.cpp
    app/ServiceTests.cpp
    app/SettingsTests.cpp
    app/LocalizationTests.cpp
    app/PaletteTests.cpp
    app/SourceTests.cpp
    app/UnattendControllerTests.cpp
    app/UpdateCatalogControllerTests.cpp)
wl_configure_target(wl_tests)
target_include_directories(wl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(wl_tests PRIVATE WL_SOURCE_DIR=L"${PROJECT_SOURCE_DIR}")
target_link_libraries(wl_tests PRIVATE wl_app wl_doctest wl_json ole32)

add_test(NAME unit COMMAND wl_tests)
set_tests_properties(unit PROPERTIES LABELS unit)

# DISM ABI check against the official header when the ADK is installed (D-017: never required).
set(WL_ADK_DISMAPI "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Assessment and Deployment Kit/Deployment Tools/SDKs/DismApi/Include")
if(EXISTS "${WL_ADK_DISMAPI}/dismapi.h")
    target_sources(wl_tests PRIVATE core/DismAbiTests.cpp)
    target_include_directories(wl_tests SYSTEM PRIVATE "${WL_ADK_DISMAPI}")
    message(STATUS "DISM ABI test: ${WL_ADK_DISMAPI}")
endif()
