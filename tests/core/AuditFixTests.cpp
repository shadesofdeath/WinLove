// docs/AUDIT-2026-11-10.md: the behaviour its fixes changed, pinned.
#include "base/Log.h"
#include "core/ops/ChangeSet.h"
#include "core/ops/Planner.h"
#include "core/programs/Winget.h"
#include "core/store/MsStore.h"
#include "core/tasks/TaskRunner.h"
#include "core/unattend/Welcome.h"
#include "core/uup/UupCatalog.h"

#include <doctest.h>

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <thread>

using namespace wl;
using namespace wl::core;

TEST_CASE("audit OPS-01: a preset's wrongly typed fields do not throw out of the planner or the welcome") {
    using ops::OpKind;
    using ops::Operation;
    // "afterUpdates" as a number: value() threw a type_error on the UI thread.
    CHECK_NOTHROW((void)ops::phaseOf(Operation{OpKind::RemoveComponent, L"x", LR"({"afterUpdates":1})"}));
    CHECK(ops::phaseOf(Operation{OpKind::RemoveComponent, L"x", LR"({"afterUpdates":1})"}) ==
          ops::phaseOf(OpKind::RemoveComponent));
    CHECK(ops::phaseOf(Operation{OpKind::RemoveComponent, L"x", LR"({"afterUpdates":true})"}) == ops::Phase::DeepRemove);

    const wchar_t* file = LR"(ProgramData\WinLove\Oobe\oobe.json)";
    for (const wchar_t* text : {LR"({"format":5})", LR"({"format":"winlove.welcome","defaults":5})",
                                LR"({"format":"winlove.welcome","allowEmptyPassword":"yes"})",
                                LR"({"format":"winlove.welcome","defaults":{"theme":7}})"}) {
        std::optional<WelcomePlan> plan;
        CHECK_NOTHROW(plan = welcomePlanFromOperations({Operation{OpKind::WriteFile, file, text}}));
        CHECK_FALSE(plan.has_value());
    }
    CHECK(welcomePlanFromOperations({Operation{OpKind::WriteFile, file, LR"({"format":"winlove.welcome"})"}}).has_value());
}

TEST_CASE("audit OPS-08: server answers with fields of another type are errors, not exceptions") {
    Result<uup::FileSet> files = fail(ErrorCode::Unknown, L"");
    CHECK_NOTHROW(files = uup::parseFiles(R"({"response":{"updateName":"x","hasUpdates":1,"appxPresent":null,"files":{}}})"));
    REQUIRE(files.has_value());
    CHECK_FALSE(files->hasUpdates);
    CHECK_FALSE(files->appxPresent);
    files = uup::parseFiles(R"({"response":{"hasUpdates":true,"appxPresent":true}})");
    REQUIRE(files.has_value());
    CHECK(files->hasUpdates);
    CHECK(files->appxPresent);

    Result<StoreProduct> product = fail(ErrorCode::Unknown, L"");
    CHECK_NOTHROW(product = parseStoreProduct(R"({"Product":{"ProductId":"9X","DisplaySkuAvailabilities":[{"Sku":null}]}})"));
    REQUIRE_FALSE(product.has_value());
    CHECK(product.error().code == ErrorCode::ParseError);
    CHECK_NOTHROW(product = parseStoreProduct(R"({"Product":{"ProductId":"9X","DisplaySkuAvailabilities":7}})"));
    CHECK_FALSE(product.has_value());
}

TEST_CASE("audit OPS-09: a winget id cannot name a parent folder") {
    CHECK_FALSE(validWingetId(L"."));
    CHECK_FALSE(validWingetId(L".."));
    CHECK_FALSE(validWingetId(L"a..b"));
    CHECK_FALSE(validWingetId(L".hidden"));
    CHECK_FALSE(validWingetId(L"Publisher."));
    CHECK(validWingetId(L"7zip.7zip"));
    CHECK(validWingetId(L"Notepad++.Notepad++"));
    CHECK(validWingetId(L"Python.Python.3.13"));
}

TEST_CASE("audit APP-01: destroying a TaskRunner cancels the job that is running") {
    std::atomic<bool> started{false};
    std::atomic<bool> sawCancel{false};
    std::atomic<bool> finished{false};
    const auto begun = std::chrono::steady_clock::now();
    {
        TaskRunner runner;
        runner.run<int>(
            [&](const TaskContext& task) -> Result<int> {
                started = true;
                // A long read that looks at its token, as the hash / export jobs do.
                for (int i = 0; i < 1000 && !task.cancel.cancelled(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                sawCancel = task.cancel.cancelled();
                return 1;
            },
            [&](Result<int>) { finished = true; });
        while (!started) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } // the destructor: before the fix it waited the whole 10 s
    const auto took = std::chrono::steady_clock::now() - begun;
    CHECK(sawCancel);
    CHECK(finished); // `done` still runs
    CHECK(took < std::chrono::seconds(5));
}

TEST_CASE("audit ENG-11: the log clock is the local time, written as std::chrono wrote it") {
    for (const auto moment : {std::chrono::system_clock::now(), std::chrono::system_clock::now() - std::chrono::minutes(61)}) {
        const auto local = std::chrono::zoned_time(std::chrono::current_zone(), moment).get_local_time();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(local.time_since_epoch()) % 1000;
        const auto seconds = std::chrono::floor<std::chrono::seconds>(local);
        CHECK(log::clockText(moment, true) == std::format(L"{:%H:%M:%S}.{:03}", seconds, ms.count()));
        CHECK(log::clockText(moment, false) == std::format(L"{:%H:%M:%S}", seconds));
    }
}
