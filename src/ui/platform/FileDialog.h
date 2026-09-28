#pragma once
// System file / folder pickers (IFileOpenDialog). Modal to `owner`; nullopt when cancelled.
// One of the few places WinLove uses stock Windows UI (D-001: system dialogs are allowed).
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wl::ui {

struct FileFilter {
    std::wstring label;   // "Windows images"
    std::wstring pattern; // "*.iso;*.wim;*.esd;*.swm"
};

[[nodiscard]] std::optional<std::filesystem::path> pickFile(HWND owner, const std::wstring& title,
                                                            const std::vector<FileFilter>& filters);
[[nodiscard]] std::optional<std::filesystem::path> pickFolder(HWND owner, const std::wstring& title);
// Save As: `defaultName` prefilled, `extension` ("wim") appended when the user types none.
[[nodiscard]] std::optional<std::filesystem::path> pickSaveFile(HWND owner, const std::wstring& title,
                                                                const std::vector<FileFilter>& filters,
                                                                const std::wstring& defaultName,
                                                                const std::wstring& extension);

} // namespace wl::ui
