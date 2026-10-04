#pragma once
// D-066: "Mağazadan ekle" — a search box (Enter searches the Microsoft Store), the apps found
// (Uygulama · Yayıncı · Kimlik) and "İndir ve ekle" for the one selected. Results arrive later
// (the search runs on the network thread): the Shell hands them to setResults().
#include "app/Localization.h"
#include "core/store/MsStore.h"
#include "ui/widgets/Dialog.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

struct StoreDialogActions {
    std::function<void(std::wstring query)> search;
    std::function<void(core::StoreSearchResult app)> install; // called after `close`
    std::function<void()> close;
};

class StoreDialogHandle {
public:
    virtual ~StoreDialogHandle() = default;
    virtual void setResults(const std::wstring& query, std::vector<core::StoreSearchResult> results) = 0;
    virtual void setSearching(bool searching) = 0;
};

struct StoreDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr;  // the search box
    StoreDialogHandle* handle = nullptr; // lives as long as the dialog
};

[[nodiscard]] StoreDialog makeStoreDialog(const Localization& strings, std::wstring architecture, StoreDialogActions actions);

} // namespace wl::app
