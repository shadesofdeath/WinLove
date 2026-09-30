#pragma once
// The answer file being edited (P13), kept between runs (D-037): twice a user filled the form in,
// closed the app and had to start over. One file next to settings.json, written on every change,
// read at startup. It holds the account password, so the whole file is protected with the Windows
// user's own key (DPAPI): another account on the PC, or a copy of the file, cannot read it.
#include "core/unattend/Unattend.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace wl::app {

struct StoredAnswers {
    core::UnattendOptions options;
    bool includeInIso = false;

    // Nothing answered and nothing asked for: not worth a file.
    [[nodiscard]] bool blank() const { return !includeInIso && options == core::UnattendOptions{}; }
};

// %LOCALAPPDATA%\WinLove\answers.dat
[[nodiscard]] std::filesystem::path defaultAnswersFile();

// The plain document: {"version":1,"includeInIso":…,"xml":"<unattend…>"} — the same XML a preset
// carries (options WinLove does not know are not in it).
[[nodiscard]] std::string answersToJson(const StoredAnswers& answers);
[[nodiscard]] std::optional<StoredAnswers> answersFromJson(std::string_view json);

// Missing, unreadable (another user's, damaged) or blank: nothing. Never an error: the form
// simply starts empty.
[[nodiscard]] std::optional<StoredAnswers> loadAnswers(const std::filesystem::path& file);
// Blank answers remove the file.
void saveAnswers(const std::filesystem::path& file, const StoredAnswers& answers);

} // namespace wl::app
