# Common settings for every first-party WinLove target.
# Usage: wl_configure_target(<target>)
function(wl_configure_target target)
    target_compile_options(${target} PRIVATE
        /W4 /WX /permissive- /utf-8 /EHsc
        /Zc:__cplusplus /Zc:preprocessor /Zc:inline
        /external:W0 /diagnostics:caret)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000010
        WL_VERSION_STRING="${PROJECT_VERSION}"
        WL_VERSION_LABEL="${WL_VERSION_LABEL}")
    # Headers are included relative to src/: #include "core/base/Result.h"
    target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR}/src)
endfunction()
