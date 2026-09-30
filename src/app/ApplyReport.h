#pragma once
// The change report of an "Uygula" run (P05): one self-contained HTML page — which image, when,
// how much smaller it got, and every step with its result (skipped steps with their reason).
// "Raporu kaydet" on the finished Apply page writes it where the user says.
#include "app/Localization.h"
#include "app/state/AppState.h"

#include <chrono>
#include <string>

namespace wl::app {

// Shared with the Apply page: the group a step belongs to and why a step was skipped.
[[nodiscard]] std::wstring applyPhaseName(const Localization& strings, core::ops::Phase phase);
[[nodiscard]] std::wstring applySkipReason(const Localization& strings, const Error& error);

// UTF-8. `when`: the time printed on the report (local time).
[[nodiscard]] std::string applyReportHtml(const AppState& state, const AppState::ApplyRun& run, const Localization& strings,
                                          Language language, std::chrono::system_clock::time_point when);
// "WinLove-rapor-20260930-2015.html"
[[nodiscard]] std::wstring applyReportFileName(std::chrono::system_clock::time_point when);

} // namespace wl::app
