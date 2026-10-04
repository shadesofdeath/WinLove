#pragma once
// D-061: "Dil ekle" — the languages Windows Update has for the mounted image's build, as a check
// list: Dil · Etiket · Özellikler · Boyut, with a search box above. Below, the optional parts
// (El yazısı, OCR, Metin okuma, Konuşma, Bileşen dilleri — the pack, Basic and the fonts a script
// needs always go) and "Arayüz dili yap" for a single language. Languages the image already has
// in full cannot be ticked. The primary button names the count and the size to download.
#include "app/Localization.h"
#include "app/controllers/LanguageController.h"
#include "app/controllers/LanguageFetchController.h"
#include "ui/widgets/Dialog.h"

#include <functional>
#include <memory>
#include <vector>

namespace wl::app {

struct LanguageAddDialog {
    std::unique_ptr<ui::Dialog> dialog;
    ui::Widget* initialFocus = nullptr; // the search box
};

struct LanguageAddActions {
    // Called after `close`: the files to download (install order) and the display language to set (or empty).
    std::function<void(std::vector<core::UupLanguageFile>, std::wstring uiLanguage)> download;
    std::function<void()> close;
};

[[nodiscard]] LanguageAddDialog makeLanguageAddDialog(const Localization& strings, Language language,
                                                      const LanguageController& controller, const LanguageTarget& target,
                                                      std::vector<core::UupLanguage> languages, LanguageAddActions actions);

// "Temel · El yazısı · OCR" — the features of a language file list (no pack, no components).
[[nodiscard]] std::wstring languagePartsText(const Localization& strings, const std::vector<core::LanguagePackFile::Kind>& kinds);

} // namespace wl::app
