#include "app/pages/overview/OverviewModel.h"

#include <algorithm>

namespace wl::app {

namespace {

using Model = OverviewModel;
using ListState = OverviewModel::ListState;
using TileKind = OverviewModel::TileKind;

// A list of the mounted image: not read, being read, read, or failed.
template <class List>
ListState listState(const std::optional<List>& list, bool mounted) {
    if (!mounted) {
        return ListState::NotMounted;
    }
    if (!list) {
        return ListState::Unread;
    }
    switch (list->status) {
    case List::Status::Loading: return ListState::Reading;
    case List::Status::Failed: return ListState::Failed;
    case List::Status::Ready: break;
    }
    return ListState::Ready;
}

// The page's own count (the queue groups sum the pages of a nav item; a tile is one page).
int pageCount(const std::vector<int>& counts, PageId page) {
    const auto i = static_cast<std::size_t>(page);
    return i < counts.size() ? counts[i] : 0;
}

} // namespace

OverviewModel buildOverview(const AppState& state, const OverviewInputs& inputs, double nowMs) {
    const std::vector<int>& pageCounts = inputs.pageCounts;
    Model m;
    const auto& source = state.source();
    const auto& mounted = state.mounted();
    const auto& op = state.operation();
    if (op) {
        m.operation = op->kind;
    }

    // ---- what the image is ----
    const core::ImageInfo* image = nullptr;
    if (source) {
        if (mounted) {
            for (const auto& candidate : source->install.images) {
                if (candidate.index == mounted->index) {
                    image = &candidate;
                }
            }
        }
        if (!image) {
            image = state.selectedImage();
        }
        if (!image && !source->install.images.empty()) {
            image = &source->install.images.front();
        }
    }
    // The ISO's copy step counts as mounting only when it is made for a mount (2026-11-11: a delete's
    // copy step said "Bağlanıyor" here too).
    const bool mounting = op && (op->kind == EngineOperation::Kind::Mounting ||
                                 (op->kind == EngineOperation::Kind::Preparing &&
                                  op->purpose.value_or(EngineOperation::Kind::Mounting) == EngineOperation::Kind::Mounting));
    m.stage = mounted ? Model::Stage::Mounted : mounting ? Model::Stage::Mounting : source ? Model::Stage::Closed : Model::Stage::NoSource;
    auto& id = m.image;
    if (image) {
        id.name = image->name;
        id.editionId = image->editionId;
        id.build = image->build;
        id.spBuild = image->spBuild;
        id.architecture = core::architectureName(image->architecture);
        id.language = image->defaultLanguage.empty() && !image->languages.empty() ? image->languages.front() : image->defaultLanguage;
        id.languages = image->languages.size();
        id.bytes = image->totalBytes;
        id.index = image->index;
    }
    if (source) {
        id.file = std::filesystem::path(source->installImage).filename().wstring();
        id.container = source->installImage.ends_with(L".esd") || source->installImage.ends_with(L".ESD") ? L"ESD" : L"WIM";
        id.compression = core::compressionName(source->install.header.compression);
        id.solid = source->install.header.solid;
        id.fileBytes = source->installImageSize;
        id.source = source->path.filename().empty() ? source->path.wstring() : source->path.filename().wstring();
        id.editions = source->install.images.size();
        if (source->disk) {
            // D-141: the file is the disk itself — no WIM container, no compression.
            id.virtualDisk = true;
            id.container = core::formatName(source->format);
            id.compression.clear();
            id.solid = false;
            id.dynamicDisk = source->disk->dynamic;
            id.differencingDisk = source->disk->differencing;
            id.diskBytes = source->disk->virtualSize;
            id.source.clear(); // the same file as id.file
        }
    }
    if (mounted) {
        if (id.name.empty()) {
            id.name = mounted->edition;
        }
        if (id.index == 0) {
            id.index = mounted->index;
        }
        if (id.file.empty()) {
            id.file = mounted->imagePath.filename().wstring();
        }
        id.readOnly = mounted->readOnly;
        id.mountDir = mounted->mountDir.wstring();
        id.mountedElsewhere = state.mountedElsewhere();
    }
    if (mounting) {
        id.progress = std::clamp(op->fraction, 0.0, 1.0);
    }

    // ---- what is in it ----
    const bool isMounted = mounted.has_value();
    for (std::size_t i = 0; i < Model::kTiles; ++i) {
        m.tiles[i].kind = static_cast<TileKind>(i);
    }
    {
        auto& t = m.tiles[static_cast<std::size_t>(TileKind::Components)];
        t.page = PageId::Components;
        t.state = listState(state.appxList(), isMounted);
        if (t.state == ListState::Ready) {
            for (const auto& [name, count] : inputs.categories) {
                t.value += count;
                t.total += count > 0 ? 1 : 0;
            }
            auto ranked = inputs.categories;
            std::ranges::stable_sort(ranked, [](const auto& a, const auto& b) { return a.second > b.second; });
            for (std::size_t k = 0; k < ranked.size() && k < 3 && ranked[k].second > 0; ++k) {
                t.top.push_back(ranked[k]);
            }
        }
        t.queued = pageCount(pageCounts, PageId::Components);
    }
    {
        auto& t = m.tiles[static_cast<std::size_t>(TileKind::Apps)];
        t.page = PageId::Components; // the image's own apps are removed there
        t.state = listState(state.appxList(), isMounted);
        if (t.state == ListState::Ready) {
            t.value = state.appxList()->items.size();
        }
        t.queued = isMounted ? static_cast<int>(state.changes().count(core::ops::OpKind::RemoveAppx)) : 0;
    }
    {
        auto& t = m.tiles[static_cast<std::size_t>(TileKind::Features)];
        t.page = PageId::Features;
        t.state = listState(state.optionalFeatures(), isMounted);
        if (t.state == ListState::Ready) {
            for (const auto& f : state.optionalFeatures()->items) {
                if (f.kind == core::OptionalFeature::Kind::Feature) {
                    ++t.total;
                    t.value += f.isOn() ? 1 : 0;
                } else {
                    t.secondary += f.isOn() ? 1 : 0;
                }
            }
        }
        t.queued = pageCount(pageCounts, PageId::Features);
    }
    {
        auto& t = m.tiles[static_cast<std::size_t>(TileKind::Drivers)];
        t.page = PageId::Drivers;
        t.state = listState(state.imageDrivers(), isMounted);
        if (t.state == ListState::Ready) {
            t.value = static_cast<std::size_t>(
                std::ranges::count_if(state.imageDrivers()->items, [](const core::DriverEntry& d) { return !d.inBox; }));
        }
        t.queued = pageCount(pageCounts, PageId::Drivers);
    }
    {
        // From the WIM's description: known without mounting. The image's own list once read.
        auto& t = m.tiles[static_cast<std::size_t>(TileKind::Languages)];
        t.page = PageId::Languages;
        const auto& intl = state.imageIntl();
        if (isMounted && intl && intl->status == AppState::ImageIntl::Status::Ready) {
            t.state = ListState::Ready;
            t.value = intl->intl.languages.size();
        } else if (image) {
            t.state = ListState::Ready;
            t.value = image->languages.size();
        } else {
            t.state = listState(intl, isMounted);
        }
        t.text = id.language;
        t.queued = pageCount(pageCounts, PageId::Languages);
    }
    {
        auto& t = m.tiles[static_cast<std::size_t>(TileKind::Services)];
        t.page = PageId::Services;
        t.state = listState(state.serviceList(), isMounted);
        if (t.state == ListState::Ready) {
            t.value = state.serviceList()->items.size();
            t.secondary = static_cast<std::size_t>(std::ranges::count_if(
                state.serviceList()->items, [](const core::ServiceEntry& s) { return s.start == core::StartType::Disabled; }));
        }
        t.queued = pageCount(pageCounts, PageId::Services);
    }
    {
        // What goes in: the update packages queued (the image's installed ones are not listed anywhere).
        auto& t = m.tiles[static_cast<std::size_t>(TileKind::Updates)];
        t.page = PageId::Updates;
        t.state = isMounted ? ListState::Ready : ListState::NotMounted;
        t.queued = pageCount(pageCounts, PageId::Updates);
        t.value = isMounted ? static_cast<std::size_t>(std::max(t.queued, 0)) : 0;
    }

    // ---- what is queued ----
    const auto& changes = state.changes();
    auto& q = m.queue;
    q.changes = isMounted ? changes.size() : 0;
    q.sizeDelta = isMounted ? changes.estimatedSizeDelta() : 0;
    q.sizeBefore = isMounted ? id.bytes : 0;
    if (q.sizeBefore > 0) {
        const auto after = static_cast<std::int64_t>(q.sizeBefore) + q.sizeDelta;
        q.sizeAfter = static_cast<std::uint64_t>(std::max<std::int64_t>(after, 0));
    }
    q.seconds = q.changes > 0 ? inputs.estimateSeconds : 0.0;
    for (const auto& group : inputs.queue) {
        OverviewInputs::QueueGroup kept{group.caption, {}};
        for (const auto& item : group.items) {
            if (item.count > 0) {
                kept.items.push_back(item);
            }
        }
        if (!kept.items.empty() && isMounted) {
            q.groups.push_back(std::move(kept));
        }
    }
    if (q.changes > 0) {
        q.risky = inputs.risky;
        q.impacted = inputs.impacted;
    }
    q.guardsOn = inputs.guardsOn;
    q.locked = state.queueLocked();
    q.waitingForDownload = inputs.waitingForDownload;
    if (const auto& held = state.heldDownloads(); source && held.source == source->path) {
        q.heldDownloads = held.count();
    }

    // ---- the run ----
    bool resolving = false;
    if (const auto& run = state.applyRun()) {
        RunSummary summary = summarizeRun(*run, nowMs);
        // A held / kept / stopped run stays the news until the queue is changed (the Apply page's own rule).
        resolving = !summary.active() && run->result && isMounted && changes.version() == run->queueVersionAtEnd &&
                    (run->result->held || run->result->kept || !run->result->report.completed);
        if (summary.active() || resolving || !isMounted) {
            m.run = summary;
        }
    }

    // ---- the next step ----
    const bool working = op && op->kind != EngineOperation::Kind::Reading;
    if (m.run && (m.run->active() || m.run->isoRunning())) {
        m.next = Model::Next::WatchRun; // D-144: the ISO after the run is still the run's work
    } else if (m.run && m.run->closing()) {
        m.next = Model::Next::Wait; // the save / discard of what it left mounted is running
    } else if (resolving) {
        m.next = Model::Next::ResolveRun;
    } else if (m.stage == Model::Stage::NoSource) {
        m.next = Model::Next::ChooseSource;
    } else if (m.stage == Model::Stage::Mounting || working) {
        m.next = Model::Next::Wait;
    } else if (m.stage == Model::Stage::Mounted) {
        m.next = q.changes > 0 ? Model::Next::ReviewQueue : Model::Next::PickChanges;
    } else if (m.run && m.run->saved()) {
        // D-141: a saved virtual disk is the result itself (no setup media is made from it). D-144: the
        // ISO the run made is the next thing to look at.
        m.next = id.virtualDisk                          ? Model::Next::DiskSaved
                 : m.run->iso == RunSummary::Iso::Done ? Model::Next::IsoReady
                                                         : Model::Next::BuildIso;
    } else if (inputs.packedSource) {
        m.next = Model::Next::ConvertEsd;
    } else if (inputs.canMount) {
        m.next = Model::Next::Mount;
    } else {
        m.next = Model::Next::Wait;
    }
    return m;
}

} // namespace wl::app
