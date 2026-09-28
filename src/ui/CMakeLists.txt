# wl_ui — custom-draw UI framework (D2D/DirectWrite). Knows nothing about Windows images.
add_library(wl_ui STATIC
    anim/Tween.cpp
    icons/IconCache.cpp
    platform/Clipboard.cpp
    platform/DropTarget.cpp
    platform/FileDialog.cpp
    platform/Window.cpp
    render/Canvas.cpp
    render/Graphics.cpp
    render/OffscreenTarget.cpp
    render/RenderDevice.cpp
    render/SvgPath.cpp
    render/SwapChainTarget.cpp
    text/FontLibrary.cpp
    text/TextStyles.cpp
    theme/Palette.cpp
    widget/Host.cpp
    widget/Stack.cpp
    widget/Widget.cpp
    widgets/Button.cpp
    widgets/Checkbox.cpp
    widgets/Dialog.cpp
    widgets/DropZone.cpp
    widgets/Dropdown.cpp
    widgets/EmptyState.cpp
    widgets/InfoBar.cpp
    widgets/Kbd.cpp
    widgets/Label.cpp
    widgets/LogConsole.cpp
    widgets/ScrollBar.cpp
    widgets/SearchBox.cpp
    widgets/Splitter.cpp
    widgets/Toast.cpp
    widgets/Toggle.cpp)
wl_configure_target(wl_ui)
target_link_libraries(wl_ui
    PUBLIC wl_base d2d1 dwrite d3d11 dxgi windowscodecs dxguid
    PRIVATE dwmapi ole32 shell32)
