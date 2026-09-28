# Unit tests (label: unit) — no admin, no images. Integration tests (label: integration) are added
# in Faz 2.6 and need an elevated shell plus C:\WinLoveLab (docs/TESTING.md).
add_executable(wl_tests
    main.cpp
    base/ResultTests.cpp
    core/ImageFormatTests.cpp
    ui/TokensTests.cpp
    ui/IconsTests.cpp
    ui/RenderTests.cpp
    app/LocalizationTests.cpp)
wl_configure_target(wl_tests)
target_compile_definitions(wl_tests PRIVATE WL_SOURCE_DIR=L"${PROJECT_SOURCE_DIR}")
target_link_libraries(wl_tests PRIVATE wl_app wl_doctest ole32)

add_test(NAME unit COMMAND wl_tests)
set_tests_properties(unit PROPERTIES LABELS unit)
