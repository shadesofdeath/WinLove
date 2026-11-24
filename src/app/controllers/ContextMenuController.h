#pragma once
// D-155 Sağ tık menüsü (docs/pages/24-context-menu.md): reads the mounted image's right-click menu
// entries once per mount (engine thread, offreg) and turns the page's edits into SetContextMenu
// operations: "entry\<id>" hides an entry (or shows one the image hides), "add\<id>" is our own item.
// Choosing what the image already has takes the operation out.
#include "app/state/AppState.h"
#include "core/image/ContextMenu.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

class ContextMenuController {
public:
    ContextMenuController(AppState& state, std::function<void(std::function<void()>)> postToUi);
    ~ContextMenuController();

    void load(bool force = false);

    // After Uygula: hidden (queued, or the image's own state).
    [[nodiscard]] bool hidden(const core::ContextMenuEntry& entry) const;
    [[nodiscard]] bool changed(const core::ContextMenuEntry& entry) const;
    void setHidden(const core::ContextMenuEntry& entry, bool hidden);

    // Our own items in the queue, in the order added.
    [[nodiscard]] std::vector<core::ContextMenuItem> items() const;
    // An error (no name / command / place) queues nothing. The id is made from the name when empty,
    // and made unique against the queued items.
    Result<void> addItem(core::ContextMenuItem item);
    void removeItem(const std::wstring& id);

    void resetChanges();
    [[nodiscard]] int changedCount() const;

    // Pure mappings, unit-tested.
    [[nodiscard]] static core::ops::Operation operationFor(const core::ContextMenuEntry& entry, bool hidden);
    [[nodiscard]] static Result<core::ops::Operation> operationFor(const core::ContextMenuItem& item);
    [[nodiscard]] static std::wstring entryTarget(const core::ContextMenuEntry& entry);

private:
    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
