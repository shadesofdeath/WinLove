# Vendored dependencies (see docs/DECISIONS.md D-006). Included as SYSTEM so their warnings
# never trip /WX in first-party code.

add_library(wl_doctest INTERFACE)
target_include_directories(wl_doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/doctest)

add_library(wl_json INTERFACE)
target_include_directories(wl_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann)

add_library(wl_pugixml STATIC pugixml/pugixml.cpp)
target_include_directories(wl_pugixml SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/pugixml)
target_compile_options(wl_pugixml PRIVATE /W0 /utf-8)

# LZMA SDK 26.02 (Igor Pavlov, public domain): LZMA2 + x86 branch filter for WLM (D-057).
# Only the files WLM needs; LICENSE-lzma-sdk.txt is the SDK's own notice.
add_library(wl_lzma STATIC
    lzma/Alloc.c lzma/Bra86.c lzma/CpuArch.c lzma/LzFind.c lzma/LzFindMt.c lzma/LzFindOpt.c
    lzma/Lzma2Dec.c lzma/Lzma2Enc.c lzma/LzmaDec.c lzma/LzmaEnc.c lzma/MtCoder.c lzma/MtDec.c lzma/Threads.c
    lzma/7zStream.c)
target_include_directories(wl_lzma SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lzma)
target_compile_options(wl_lzma PRIVATE /W0)
