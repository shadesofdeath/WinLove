// Timings behind docs/PERF-BASELINE.md's "after" tables, for what cannot be measured from outside
// the process (it needs a mounted image or a long session). Skipped in the unit run; by hand, in
// Release:
//     build\x64-release\bin\wl_tests.exe -tc="perf*" --no-skip -s
// "before" is the old algorithm written out again here, run on the same data in the same binary —
// not a measurement of the old build.
#include "app/state/AppState.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/HostsFile.h"
#include "core/image/SystemComponents.h"
#include "core/ops/ChangeSet.h"
#include "core/ops/Compat.h"

#include <doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>

using namespace wl;
using namespace wl::core;
using namespace wl::core::ops;

namespace {

template <class F>
double ms(F&& work) {
    const auto started = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

void line(const char* what, double before, double after) {
    MESSAGE(std::format("{:<58} before {:>10.2f} ms   after {:>9.2f} ms   x{:.1f}", what, before, after,
                        after > 0 ? before / after : 0.0));
}

// A queue like the user's preset: 900 steps, package-like targets.
ChangeSet presetQueue(std::size_t steps) {
    ChangeSet set;
    std::vector<Operation> ops;
    for (std::size_t i = 0; i < steps; ++i) {
        const OpKind kind = i % 3 == 0 ? OpKind::RemoveComponent : i % 3 == 1 ? OpKind::RemoveAppx : OpKind::SetRegistryValue;
        ops.push_back({kind, std::format(L"Microsoft-Windows-Component-{:04}-Package~31bf3856ad364e35", i), L"value"});
    }
    set.addAll(std::move(ops));
    return set;
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"perf";
    std::filesystem::create_directories(dir);
    return dir / name;
}

} // namespace

TEST_CASE("perf: ChangeSet::find for every row of a page (1,300 rows, 900 queued)" * doctest::skip()) {
    const ChangeSet set = presetQueue(900);
    std::vector<std::wstring> rows;
    for (int i = 0; i < 1300; ++i) {
        rows.push_back(std::format(L"microsoft-windows-component-{:04}-package~31bf3856ad364e35", i));
    }
    std::size_t hitsOld = 0;
    std::size_t hitsNew = 0;
    const double before = ms([&] {
        for (const auto& row : rows) {
            const auto it = std::ranges::find_if(set.operations(), [&](const Operation& o) {
                return o.kind == OpKind::RemoveComponent && text::iequals(o.target, row);
            });
            hitsOld += it != set.operations().end() ? 1 : 0;
        }
    });
    const double after = ms([&] {
        for (const auto& row : rows) {
            hitsNew += set.find(OpKind::RemoveComponent, row) ? 1 : 0;
        }
    });
    CHECK(hitsOld == hitsNew);
    CHECK(hitsNew == 300);
    line("one pass of find() over 1,300 rows", before, after);
}

TEST_CASE("perf: taking 850 finished steps out of the queue (queue.json on disk)" * doctest::skip()) {
    using app::AppState;
    using app::MountedImage;
    const MountedImage mounted{LR"(C:\WinLove\mount)", LR"(D:\work\sources\install.wim)", 6, L"Windows 11 Pro"};
    std::vector<std::pair<OpKind, std::wstring>> ran;
    auto fill = [&](AppState& state) {
        state.setMounted(mounted);
        std::vector<Operation> ops = presetQueue(900).operations();
        ran.clear();
        for (std::size_t i = 0; i < 850; ++i) {
            ran.emplace_back(ops[i].kind, ops[i].target);
        }
        state.queueMany(std::move(ops));
    };
    double before = 0;
    double after = 0;
    {
        AppState state{scratch(L"recent.json"), scratch(L"settings.json"), scratch(L"answers-a.dat")};
        fill(state);
        before = ms([&] {
            for (const auto& [kind, target] : ran) {
                state.unqueue(kind, target); // as ApplyController did: a notification and a file per step
            }
        });
        CHECK(state.changes().size() == 50);
    }
    {
        AppState state{scratch(L"recent.json"), scratch(L"settings.json"), scratch(L"answers-b.dat")};
        fill(state);
        after = ms([&] { state.unqueueMany(ran); });
        CHECK(state.changes().size() == 50);
    }
    line("unqueue of 850 steps, no listeners", before, after);
}

TEST_CASE("perf: the guard question for 1,300 component rows" * doctest::skip()) {
    // 31 guards with services, as compat.json has them; recipes with a few services and store names.
    std::vector<CompatGuard> active;
    for (int g = 0; g < 31; ++g) {
        CompatGuard guard;
        guard.id = std::format(L"guard{}", g);
        for (int s = 0; s < 12; ++s) {
            guard.services.push_back(std::format(L"svc-{}-{}", g, s));
            guard.components.push_back(std::format(L"held-{}-{}", g, s));
        }
        guard.appx = {std::format(L"Vendor.App{}", g)};
        active.push_back(std::move(guard));
    }
    AppxNeeds needs;
    for (int a = 0; a < 60; ++a) {
        needs[std::format(L"Vendor.Store{}", a)] = {L"Microsoft.VCLibs.140.00", L"Microsoft.UI.Xaml.2.8"};
    }
    const ChangeSet queue = presetQueue(900);
    std::vector<ComponentRecipe> recipes;
    for (int i = 0; i < 1300; ++i) {
        ComponentRecipe recipe;
        recipe.title = std::format(L"Component {}", i);
        for (int n = 0; n < 12; ++n) {
            recipe.storeNames.push_back(std::format(L"microsoft-windows-part-{}-{}", i, n));
        }
        recipe.services = {std::format(L"own-{}", i), std::format(L"own2-{}", i)};
        recipes.push_back(std::move(recipe));
    }
    std::size_t heldOld = 0;
    std::size_t heldNew = 0;
    const double before = ms([&] {
        for (int i = 0; i < 1300; ++i) {
            // ComponentController::block: the recipe written out, the guards copied, the needs map
            // built, the queue walked — per row.
            Operation op{OpKind::RemoveComponent, std::format(L"row{}", i), utf8::toWide(componentRecipeToJson(recipes[static_cast<std::size_t>(i)]))};
            const std::vector<CompatGuard> guards = active;
            const AppxNeeds map = needs;
            heldOld += compatBlock(op, guards, map, queue).empty() ? 0 : 1;
        }
    });
    const double after = ms([&] {
        const auto removed = compatRemovedApps(queue);
        for (int i = 0; i < 1300; ++i) {
            const Operation op{OpKind::RemoveComponent, std::format(L"row{}", i)};
            heldNew += compatBlock(op, active, needs, removed, &recipes[static_cast<std::size_t>(i)]).empty() ? 0 : 1;
        }
    });
    CHECK(heldOld == heldNew);
    line("block() for 1,300 rows (one updateChecks)", before, after);
}

TEST_CASE("perf: the clock of 20,000 log lines" * doctest::skip()) {
    const auto now = std::chrono::system_clock::now();
    std::size_t chars = 0;
    const double before = ms([&] {
        for (int i = 0; i < 20000; ++i) {
            const auto local = std::chrono::zoned_time(std::chrono::current_zone(), now + std::chrono::seconds(i)).get_local_time();
            chars += std::format(L"{:%H:%M:%S}", std::chrono::floor<std::chrono::seconds>(local)).size();
        }
    });
    const double after = ms([&] {
        for (int i = 0; i < 20000; ++i) {
            chars += log::clockText(now + std::chrono::seconds(i), false).size();
        }
    });
    CHECK(chars == 2u * 20000 * 8);
    line("clock text for 20,000 rows (one Logs rebuild)", before, after);
}

TEST_CASE("perf: importing a hosts block list of 30,000 names" * doctest::skip()) {
    std::wstring text;
    for (int i = 0; i < 30000; ++i) {
        text += std::format(L"0.0.0.0 tracker{}.example.com\n", i);
    }
    std::size_t addedOld = 0;
    std::size_t addedNew = 0;
    const double before = ms([&] {
        std::vector<HostEntry> entries;
        for (auto& e : parseHosts(text)) {
            if (std::ranges::none_of(entries, [&](const HostEntry& x) { return x.name == e.name; })) {
                entries.push_back(std::move(e));
                ++addedOld;
            }
        }
    });
    const double after = ms([&] {
        std::vector<HostEntry> entries;
        std::unordered_set<std::wstring> names;
        for (auto& e : parseHosts(text)) {
            if (names.insert(e.name).second) {
                entries.push_back(std::move(e));
                ++addedNew;
            }
        }
    });
    CHECK(addedOld == addedNew);
    line("hosts import, 30,000 new names", before, after);
}
