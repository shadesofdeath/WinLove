// D-037: the answer file being edited survives a restart — protected on disk, restored by AppState.
#include "app/controllers/UnattendController.h"
#include "app/state/AnswerStore.h"
#include "app/state/AppState.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace wl;
using namespace wl::app;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"answers";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::string bytesOf(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

StoredAnswers sample() {
    StoredAnswers answers;
    answers.options.accountName = L"berkay";
    answers.options.password = L"gizli-parola";
    answers.options.acceptEula = true;
    answers.options.bypassTpm = true;
    answers.options.timeZone = L"Turkey Standard Time";
    answers.includeInIso = true;
    return answers;
}

} // namespace

TEST_CASE("answers: the document round-trips; what is not an answer document is nothing") {
    const StoredAnswers answers = sample();
    const auto back = answersFromJson(answersToJson(answers));
    REQUIRE(back.has_value());
    CHECK(back->options == answers.options);
    CHECK(back->includeInIso);

    CHECK_FALSE(answersFromJson("").has_value());
    CHECK_FALSE(answersFromJson("[1,2]").has_value());
    CHECK_FALSE(answersFromJson(R"({"xml":"<html/>"})").has_value());
    CHECK_FALSE(answersFromJson(R"({"xml":42})").has_value());
    CHECK(StoredAnswers{}.blank());
    CHECK_FALSE(answers.blank());
}

TEST_CASE("answers: saved protected — the password is not in the file in any readable form") {
    const auto file = scratch(L"answers.dat");
    std::filesystem::remove(file);
    const StoredAnswers answers = sample();
    saveAnswers(file, answers);
    REQUIRE(std::filesystem::exists(file));
    const std::string stored = bytesOf(file);
    CHECK(stored.find("gizli-parola") == std::string::npos);
    CHECK(stored.find("berkay") == std::string::npos);      // not even the plain parts
    CHECK(stored.find("<unattend") == std::string::npos);
    CHECK_FALSE(std::filesystem::exists(file.wstring() + L".new"));

    const auto loaded = loadAnswers(file);
    REQUIRE(loaded.has_value());
    CHECK(loaded->options == answers.options);
    CHECK(loaded->options.password == L"gizli-parola");

    // A file that is not ours (or not this user's) is no answers, not a crash.
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "not a protected blob";
    }
    CHECK_FALSE(loadAnswers(file).has_value());
    CHECK_FALSE(loadAnswers(scratch(L"missing.dat")).has_value());

    // Blank answers take the file away.
    saveAnswers(file, answers);
    saveAnswers(file, StoredAnswers{});
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("answers: what was filled in is there again when the app starts") {
    const auto file = scratch(L"state-answers.dat");
    std::filesystem::remove(file);
    {
        AppState state{scratch(L"recent.json"), scratch(L"settings.json"), file};
        UnattendController controller{state};
        controller.edit([](core::UnattendOptions& o) {
            o.computerName = L"WINLOVE-PC";
            o.bypassStorage = true;
        });
        CHECK(std::filesystem::exists(file)); // written as it is typed, not at exit
    }
    {
        AppState state{scratch(L"recent.json"), scratch(L"settings.json"), file};
        CHECK(state.unattend().options.computerName == L"WINLOVE-PC");
        CHECK(state.unattend().options.bypassStorage);
        CHECK(state.unattend().includeInIso); // the first answer had turned it on (D-034)

        UnattendController controller{state};
        controller.edit([](core::UnattendOptions& o) { o = {}; });
        controller.setIncludeInIso(false);
        CHECK_FALSE(std::filesystem::exists(file)); // nothing left to keep
    }
    // Without an answers file (tests, renders) nothing is read or written.
    AppState plain{scratch(L"recent.json"), scratch(L"settings.json")};
    CHECK(plain.unattend().options == core::UnattendOptions{});
}

TEST_CASE("queue: closing the app or a crash keeps it; it comes back with the same mounted image (audit A3)") {
    using core::ops::OpKind;
    using core::ops::Operation;
    const auto answers = scratch(L"queue-answers.dat");
    const auto queueFile = answers.parent_path() / L"queue.json";
    std::filesystem::remove(queueFile);
    const MountedImage mounted{LR"(C:\WinLove\mount)", LR"(D:\work\sources\install.wim)", 6, L"Windows 11 Pro"};
    {
        AppState state{scratch(L"recent.json"), scratch(L"settings.json"), answers};
        state.setMounted(mounted);
        state.queue(Operation{OpKind::DisableFeature, L"NetFx3"});
        state.queue(Operation{OpKind::SetServiceStart, L"DiagTrack", L"4"});
        CHECK(std::filesystem::exists(queueFile)); // written on every change, not at exit
    }
    {
        AppState state{scratch(L"recent.json"), scratch(L"settings.json"), answers};
        state.setMounted(MountedImage{mounted.mountDir, mounted.imagePath, 1, L"Windows 11 Home"});
        CHECK(state.restoreQueue() == 0); // another edition: not its queue
        state.setMounted(std::nullopt);
        state.setMounted(MountedImage{mounted.mountDir, L"d:/work/sources/INSTALL.wim", 6, L"Windows 11 Pro"});
        CHECK(state.restoreQueue() == 2);
        CHECK(state.changes().size() == 2);
        CHECK(state.restoreQueue() == 0); // only into an empty queue
        state.setMounted(std::nullopt);   // unmounting drops the queue …
        CHECK_FALSE(std::filesystem::exists(queueFile)); // … and the saved copy with it
    }
    // Without an answers file (tests, renders) nothing is read or written.
    AppState plain{scratch(L"recent.json"), scratch(L"settings.json")};
    plain.setMounted(mounted);
    plain.queue(Operation{OpKind::DisableFeature, L"NetFx3"});
    CHECK(plain.restoreQueue() == 0);
}
