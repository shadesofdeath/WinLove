#pragma once
// D-138: what the image overview (Görüntü › Özet) says, worked out from what the app already has —
// AppState and what its controllers know (OverviewInputs, which the Shell fills) — with no engine
// call, no file access and no list read for its sake: a list not read yet is said to be so. A pure
// function, so the tests check every state without a window; the page turns it into texts when the
// state changes and paints only those.
#include "app/pages/PageInfo.h"
#include "app/pages/apply/RunSummary.h"
#include "app/state/AppState.h"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wl::app {

struct OverviewInputs {
    // The queue the way the navigation groups it: each nav group (Entegre et · Kaldır · Yapılandır)
    // with its items and their counts — the numbers of the nav badges.
    struct QueueItem {
        PageId page = PageId::Images;
        Str label = Str::NavImages;
        int count = 0;
    };
    struct QueueGroup {
        Str caption = Str::NavImages;
        std::vector<QueueItem> items;
    };
    std::vector<QueueGroup> queue;
    // Each page's own count, by PageId (the Shell's): a tile is one page, a nav item may hold several.
    std::vector<int> pageCounts;
    // CompatController::impact(): the features the queue would break (a guard that is off would keep them).
    std::vector<std::wstring> impacted;
    std::size_t guardsOn = 0;
    // ApplyController::highRisk(), named as the risk dialog names them.
    std::vector<std::wstring> risky;
    double estimateSeconds = 0; // the plan's estimate with the save (planSeconds); 0 with an empty queue
    // ComponentController::groups(): the categories of the component tree and how many items each holds.
    std::vector<std::pair<std::wstring, std::size_t>> categories;
    // ImageController: the selected edition can be mounted now; the source is an ESD / solid image.
    bool canMount = false;
    bool packedSource = false;
    // ApplyController::waitingForDownload(): Uygula waits for a download that will queue packages.
    bool waitingForDownload = false;
};

struct OverviewModel {
    enum class Stage : std::uint8_t { NoSource, Closed, Mounting, Mounted };
    Stage stage = Stage::NoSource;

    // The mounted edition, else the one selected on Sürümler.
    struct Identity {
        std::wstring name;        // "Windows 11 Pro"
        std::wstring editionId;   // "Professional"
        int build = 0;
        int spBuild = 0;
        std::wstring architecture; // "x64"
        std::wstring language;    // the default one, "tr-TR"
        std::size_t languages = 0;
        std::uint64_t bytes = 0;  // expanded size
        std::wstring file;        // "install.wim"
        std::wstring container;   // "WIM" / "ESD"
        std::wstring compression; // "LZX"
        bool solid = false;
        std::uint64_t fileBytes = 0;
        std::wstring source;      // "x64-9448.iso"
        int index = 0;
        std::size_t editions = 0; // in the install image
        bool readOnly = false;
        std::wstring mountDir;
        bool mountedElsewhere = false; // 2026-11-11: taken over from another tool (AppState::mountedElsewhere)
        double progress = 0;      // Stage::Mounting
        // D-141: a VHD / VHDX — `container` is "VHD" / "VHDX", no compression, one edition.
        bool virtualDisk = false;
        bool dynamicDisk = false;
        bool differencingDisk = false;
        std::uint64_t diskBytes = 0; // the disk's size (virtual)
    };
    Identity image;
    // An engine job on the image right now (mounting, reading its lists, saving …).
    std::optional<EngineOperation::Kind> operation;

    enum class ListState : std::uint8_t { NotMounted, Unread, Reading, Ready, Failed };
    enum class TileKind : std::uint8_t { Components, Apps, Features, Drivers, Languages, Services, Updates, Count };
    static constexpr std::size_t kTiles = static_cast<std::size_t>(TileKind::Count);
    struct Tile {
        TileKind kind = TileKind::Components;
        PageId page = PageId::Components;
        ListState state = ListState::NotMounted;
        std::size_t value = 0;     // the count the tile shows
        std::size_t total = 0;     // Features: of how many; Components: categories
        std::size_t secondary = 0; // Features: capabilities installed; Services: disabled; Languages: more than one
        int queued = 0;            // what the queue does there (its page's nav count)
        std::vector<std::pair<std::wstring, std::size_t>> top; // Components: the largest categories
        std::wstring text;         // Languages: the default language
    };
    std::array<Tile, kTiles> tiles{};
    [[nodiscard]] const Tile& tile(TileKind kind) const { return tiles[static_cast<std::size_t>(kind)]; }

    struct Queue {
        std::size_t changes = 0;          // ChangeSet::size()
        std::int64_t sizeDelta = 0;       // estimatedSizeDelta()
        std::uint64_t sizeBefore = 0;     // the image's expanded size (0: unknown)
        std::uint64_t sizeAfter = 0;      // before + delta (estimate)
        double seconds = 0;
        std::vector<OverviewInputs::QueueGroup> groups; // items with a count only; groups with any
        std::vector<std::wstring> risky;
        std::vector<std::wstring> impacted;
        std::size_t guardsOn = 0;
        bool locked = false;              // Uygula is running: the queue is frozen
        bool waitingForDownload = false;
        std::size_t heldDownloads = 0;    // downloads that go into the queue at the next mount
    };
    Queue queue;

    // The running Apply, or the last one while it is still the news (not mounted again with a new queue).
    std::optional<RunSummary> run;

    // The one thing to do next — the page's single filled button.
    enum class Next : std::uint8_t {
        ChooseSource, // nothing open
        Wait,         // an engine job on the image: nothing to press
        Mount,        // open, not mounted
        ConvertEsd,   // an ESD / solid image cannot be mounted
        PickChanges,  // mounted, empty queue
        ReviewQueue,  // mounted, something queued: the Apply page
        WatchRun,     // Uygula is running / paused
        ResolveRun,   // a run left the image mounted and unsaved (held / kept / stopped): its report
        BuildIso,     // the last run saved the image: the setup media
        DiskSaved,    // D-141: the last run saved a virtual disk (merged, detached): mount it again for more
        IsoReady,     // D-144: the last run saved the image and made its ISO: the file, in its folder
    };
    Next next = Next::ChooseSource;
};

// `nowMs`: ui::nowMs() (a running Apply's times).
[[nodiscard]] OverviewModel buildOverview(const AppState& state, const OverviewInputs& inputs, double nowMs);

} // namespace wl::app
