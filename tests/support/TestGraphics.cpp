#include "support/TestGraphics.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace wl::test {

namespace {

std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

ui::Graphics& graphics() {
    static std::vector<std::vector<char>> files = [] {
        const auto dir = std::filesystem::path(WL_SOURCE_DIR) / L"resources" / L"fonts";
        std::vector<std::vector<char>> data;
        for (const auto* name : {L"IBMPlexSans-Regular.ttf", L"IBMPlexSans-Medium.ttf", L"IBMPlexSans-SemiBold.ttf",
                                 L"JetBrainsMono-Regular.ttf"}) {
            data.push_back(readFile(dir / name));
        }
        return data;
    }();
    static std::unique_ptr<ui::Graphics> instance = [] {
        std::vector<ui::FontBytes> bytes;
        for (const auto& f : files) {
            bytes.push_back({f.data(), f.size()});
        }
        auto g = ui::Graphics::create(bytes);
        REQUIRE_MESSAGE(g.has_value(), describe(g.error()).c_str());
        return std::move(*g);
    }();
    return *instance;
}

} // namespace wl::test
