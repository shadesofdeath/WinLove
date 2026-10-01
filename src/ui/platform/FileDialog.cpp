#include "ui/platform/FileDialog.h"

#include <shobjidl.h>
#include <wrl/client.h>

namespace wl::ui {

namespace {

using Microsoft::WRL::ComPtr;

// Title, options and file types every picker sets the same way.
void prepare(IFileDialog* dialog, const std::wstring& title, FILEOPENDIALOGOPTIONS add, const std::vector<FileFilter>& filters) {
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | add);
    dialog->SetTitle(title.c_str());
    std::vector<COMDLG_FILTERSPEC> specs;
    for (const auto& f : filters) {
        specs.push_back({f.label.c_str(), f.pattern.c_str()});
    }
    if (!specs.empty()) {
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }
}

std::optional<std::filesystem::path> fileSystemPath(IShellItem* item) {
    PWSTR path = nullptr;
    if (!item || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        return std::nullopt;
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

std::optional<std::filesystem::path> showOpen(HWND owner, const std::wstring& title, const std::vector<FileFilter>& filters,
                                              bool folders) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    prepare(dialog.Get(), title, FOS_PATHMUSTEXIST | (folders ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST), folders ? std::vector<FileFilter>{} : filters);
    ComPtr<IShellItem> item;
    if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResult(&item))) {
        return std::nullopt; // cancelled
    }
    return fileSystemPath(item.Get());
}

} // namespace

std::optional<std::filesystem::path> pickFile(HWND owner, const std::wstring& title, const std::vector<FileFilter>& filters) {
    return showOpen(owner, title, filters, false);
}

std::vector<std::filesystem::path> pickFiles(HWND owner, const std::wstring& title, const std::vector<FileFilter>& filters) {
    std::vector<std::filesystem::path> result;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return result;
    }
    prepare(dialog.Get(), title, FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST, filters);
    ComPtr<IShellItemArray> items;
    if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResults(&items))) {
        return result;
    }
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(items->GetItemAt(i, &item))) {
            if (auto path = fileSystemPath(item.Get())) {
                result.push_back(std::move(*path));
            }
        }
    }
    return result;
}

std::optional<std::filesystem::path> pickFolder(HWND owner, const std::wstring& title) {
    return showOpen(owner, title, {}, true);
}

std::optional<std::filesystem::path> pickSaveFile(HWND owner, const std::wstring& title, const std::vector<FileFilter>& filters,
                                                  const std::wstring& defaultName, const std::wstring& extension) {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    prepare(dialog.Get(), title, FOS_OVERWRITEPROMPT, filters);
    dialog->SetFileName(defaultName.c_str());
    dialog->SetDefaultExtension(extension.c_str());
    ComPtr<IShellItem> item;
    if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResult(&item))) {
        return std::nullopt;
    }
    return fileSystemPath(item.Get());
}

} // namespace wl::ui
