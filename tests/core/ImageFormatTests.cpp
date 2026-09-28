#include "core/image/ImageFormat.h"

#include <doctest.h>

using wl::core::ImageFormat;
using wl::core::formatFromPath;

TEST_CASE("formatFromPath classifies by extension, case-insensitively") {
    CHECK(formatFromPath(L"C:\\Users\\x\\Downloads\\Win11_25H2_Turkish_x64_v2.iso") == ImageFormat::Iso);
    CHECK(formatFromPath(L"install.WIM") == ImageFormat::Wim);
    CHECK(formatFromPath(L"install.esd") == ImageFormat::Esd);
    CHECK(formatFromPath(L"install2.swm") == ImageFormat::Swm);
    CHECK(formatFromPath(L"disk.vhd") == ImageFormat::Vhd);
    CHECK(formatFromPath(L"disk.VHDX") == ImageFormat::Vhdx);
}

TEST_CASE("formatFromPath rejects unknown or partial extensions") {
    CHECK(formatFromPath(L"") == ImageFormat::Unknown);
    CHECK(formatFromPath(L"readme.txt") == ImageFormat::Unknown);
    CHECK(formatFromPath(L"wim") == ImageFormat::Unknown);
    CHECK(formatFromPath(L"archive.iso.bak") == ImageFormat::Unknown);
}
