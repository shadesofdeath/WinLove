#include "ui/platform/FileDialog.h"

#include <shobjidl.h>
#include <wrl/client.h>

namespace wl::ui {

namespace {

using Microsoft::WRL::ComPtr;

std::optional<std::filesystem::path> show(HWND owner, const std::wstring& title, const std::vector<FileFilter>& filters,
                                          bool folders) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folders ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST);
    dialog->SetOptions(options);
    dialog->SetTitle(title.c_str());
    std::vector<COMDLG_FILTERSPEC> specs;
    for (const auto& f : filters) {
        specs.push_back({f.label.c_str(), f.pattern.c_str()});
    }
    if (!folders && !specs.empty()) {
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt; // cancelled
    }
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        return std::nullopt;
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

} // namespace

std::optional<std::filesystem::path> pickFile(HWND owner, const std::wstring& title, const std::vector<FileFilter>& filters) {
    return show(owner, title, filters, false);
}

std::optional<std::filesystem::path> pickFolder(HWND owner, const std::wstring& title) {
    return show(owner, title, {}, true);
}

std::optional<std::filesystem::path> pickSaveFile(HWND owner, const std::wstring& title, const std::vector<FileFilter>& filters,
                                                  const std::wstring& defaultName, const std::wstring& extension) {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    dialog->SetTitle(title.c_str());
    std::vector<COMDLG_FILTERSPEC> specs;
    for (const auto& f : filters) {
        specs.push_back({f.label.c_str(), f.pattern.c_str()});
    }
    if (!specs.empty()) {
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }
    dialog->SetFileName(defaultName.c_str());
    dialog->SetDefaultExtension(extension.c_str());
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        return std::nullopt;
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

} // namespace wl::ui
