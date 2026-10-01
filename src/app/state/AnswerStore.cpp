#include "app/state/AnswerStore.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <windows.h>

#include <dpapi.h>

namespace wl::app {

namespace {

using Json = nlohmann::json;

// DPAPI with the current user's key; empty on failure.
std::string protect(std::string_view plain) {
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"WinLove answers", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    std::string bytes(reinterpret_cast<const char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return bytes;
}

std::optional<std::string> unprotect(std::string_view stored) {
    DATA_BLOB in{static_cast<DWORD>(stored.size()), reinterpret_cast<BYTE*>(const_cast<char*>(stored.data()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return std::nullopt;
    }
    std::string bytes(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return bytes;
}

} // namespace

std::filesystem::path defaultAnswersFile() {
    return log::defaultDirectory().parent_path() / L"answers.dat";
}

std::string answersToJson(const StoredAnswers& answers) {
    return Json{{"version", 1},
                {"includeInIso", answers.includeInIso},
                {"xml", utf8::fromWide(core::buildUnattendXml(answers.options))}}
        .dump();
}

std::optional<StoredAnswers> answersFromJson(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return std::nullopt;
    }
    try {
        auto options = core::parseUnattendXml(doc.value("xml", std::string{}));
        if (!options) {
            return std::nullopt;
        }
        return StoredAnswers{std::move(*options), doc.value("includeInIso", false)};
    } catch (const Json::exception&) {
        return std::nullopt;
    }
}

std::optional<StoredAnswers> loadAnswers(const std::filesystem::path& file) {
    const auto stored = readFileBytes(file);
    if (!stored) {
        return std::nullopt;
    }
    const auto plain = unprotect(*stored);
    if (!plain) {
        log::warn("app", L"saved answers cannot be read (another user's file?): " + file.wstring());
        return std::nullopt;
    }
    auto answers = answersFromJson(*plain);
    if (answers && answers->blank()) {
        return std::nullopt;
    }
    return answers;
}

void saveAnswers(const std::filesystem::path& file, const StoredAnswers& answers) {
    std::error_code ec;
    if (answers.blank()) {
        std::filesystem::remove(file, ec);
        return;
    }
    const std::string stored = protect(answersToJson(answers));
    if (stored.empty()) {
        return; // no protection, no file: the password must not reach the disk in the clear
    }
    std::filesystem::create_directories(file.parent_path(), ec);
    // Through a temporary file: a crash mid-write must not cost the answers saved before.
    if (auto written = writeFileAtomic(file, stored); !written) {
        log::warn("app", L"answers not saved: " + describe(written.error()));
    }
}

} // namespace wl::app
