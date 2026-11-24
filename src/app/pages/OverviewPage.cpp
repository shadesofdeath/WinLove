#include "app/pages/OverviewPage.h"

#include "app/ApplyReport.h"
#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "core/image/WindowsRelease.h"
#include "ui/anim/Tween.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace wl::app {

using ui::RectF;
using ui::icons::Icon;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Next = OverviewModel::Next;
using ListState = OverviewModel::ListState;
using TileKind = OverviewModel::TileKind;
using Outcome = RunSummary::Outcome;
namespace spacing = ui::tokens::spacing;

namespace {

constexpr float kGap = spacing::s5;          // between the blocks
constexpr float kPad = spacing::s6;          // inside a card
constexpr float kIdentity = 84.0f;           // the identity block of the first card
constexpr float kBand = 48.0f;               // its next-step band
constexpr float kIconTile = 40.0f;
constexpr float kTitleArea = 32.0f;          // a section title over its card
constexpr float kRunCard = 88.0f;
constexpr float kTileGap = spacing::s4;
constexpr float kCaptionWidth = 104.0f;      // the queue's group captions ("Yapılandır")
constexpr float kChipGap = spacing::s3;
constexpr float kChipRowGap = spacing::s4;
constexpr float kQueuePadY = 14.0f;
constexpr float kColumnGap = 28.0f;          // between the queue card's two columns
constexpr float kRightColumn = 110.0f;       // size, bar, delta, risk, guards
constexpr float kBadgePadX = spacing::s4;

Icon tileIcon(TileKind kind) {
    switch (kind) {
    case TileKind::Components: return Icon::ComponentsRemove;
    case TileKind::Apps: return Icon::AppxPackage;
    case TileKind::Features: return Icon::PuzzleFeatures;
    case TileKind::Drivers: return Icon::DriverChip;
    case TileKind::Languages: return Icon::LanguageGlobe;
    case TileKind::Services: return Icon::ServicesGear;
    case TileKind::Updates: return Icon::UpdateDownload;
    case TileKind::Count: break;
    }
    return Icon::LayersEditions;
}

Str tileLabel(TileKind kind) {
    switch (kind) {
    case TileKind::Components: return Str::OverviewTileComponents;
    case TileKind::Apps: return Str::OverviewTileApps;
    case TileKind::Features: return Str::OverviewTileFeatures;
    case TileKind::Drivers: return Str::OverviewTileDrivers;
    case TileKind::Languages: return Str::OverviewTileLanguages;
    case TileKind::Services: return Str::OverviewTileServices;
    case TileKind::Updates: return Str::OverviewTileUpdates;
    case TileKind::Count: break;
    }
    return Str::OverviewTileComponents;
}

std::wstring upper(std::wstring text) {
    for (auto& c : text) {
        c = static_cast<wchar_t>(std::towupper(c));
    }
    return text;
}

// "OpenSSH İstemcisi, WSL, Hyper-V …"
std::wstring joinedNames(const std::vector<std::wstring>& names, std::size_t most) {
    std::wstring out;
    for (std::size_t i = 0; i < names.size() && i < most; ++i) {
        out += (i ? L", " : L"") + names[i];
    }
    if (names.size() > most) {
        out += L" …";
    }
    return out;
}

} // namespace

OverviewPage::OverviewPage(AppState& state, const Localization& strings, Language language, Intents intents)
    : m_state(state), m_strings(strings), m_language(language), m_intents(std::move(intents)) {
    setAccessible(ui::AccessRole::Group, strings.get(Str::OverviewTitle));
    m_empty = &add<ui::EmptyState>(Icon::LayersEditions, strings.get(Str::ImagesEmptyTitle), strings.get(Str::ImagesEmptyBody));
    m_empty->setAction(strings.get(Str::ImagesEmptyAction)).onInvoke = [this] {
        if (m_intents.act) {
            m_intents.act(Next::ChooseSource);
        }
    };
    m_primary = &add<ui::Button>(ui::ButtonKind::Primary, L"");
    m_primary->onInvoke = [this] {
        if (m_intents.act) {
            m_intents.act(m_model.next);
        }
    };
    m_runLink = &add<ui::Button>(ui::ButtonKind::Ghost, strings.get(Str::OverviewRunOpen), Icon::ArrowRight);
    m_runRing = &add<ui::ProgressRing>(ui::tokens::size::ring);
    m_runRing->setVisible(false);
    m_runLink->setHeight(26.0f);
    m_runLink->onInvoke = [this] {
        if (m_intents.open) {
            m_intents.open(PageId::Apply);
        }
    };
    for (std::size_t i = 0; i < m_tiles.size(); ++i) {
        m_tiles[i] = &add<OverviewTile>();
        m_tiles[i]->onInvoke = [this, i] {
            if (m_intents.open) {
                m_intents.open(m_model.tiles[i].page);
            }
        };
    }
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        using C = AppState::Change;
        unsigned parts = 0;
        switch (change) {
        case C::Queue: parts = kQueuePart | kCompatPart; break;
        case C::Settings: parts = kCompatPart; break; // the guards are kept in the settings
        case C::Mount:
        case C::Components: parts = kAllParts; break;
        case C::Source:
        case C::Selection:
        case C::Operation: parts = kImagePart; break;
        case C::UpdateFetch:
        case C::LanguageFetch:
        case C::StoreFetch: parts = kQueuePart; break;
        case C::Features:
        case C::Apply:
        case C::Iso:
        case C::Services:
        case C::Drivers:
        case C::Intl: break; // read from the state itself
        default: return;     // nothing the overview shows
        }
        refresh(parts);
    });
    refresh(kAllParts);
}

OverviewPage::~OverviewPage() {
    m_state.unsubscribe(m_subscription);
}

void OverviewPage::refresh(unsigned parts) {
    if (m_intents.gather && parts != 0) {
        m_intents.gather(m_inputs, parts);
    }
    m_model = buildOverview(m_state, m_inputs, ui::nowMs());
    rebuildTexts();
    rebuildChips();
    const float before = m_contentHeight;
    layout();
    contentChanged(*this, before);
    invalidate();
}

std::wstring OverviewPage::tileValue(const OverviewModel::Tile& tile, bool& quiet) const {
    quiet = tile.state != ListState::Ready;
    switch (tile.state) {
    case ListState::NotMounted: return m_strings.get(Str::OverviewStateNotMounted);
    case ListState::Unread: return m_strings.get(Str::OverviewStateUnread);
    case ListState::Reading: return m_strings.get(Str::OverviewStateReading);
    case ListState::Failed: return m_strings.get(Str::OverviewStateFailed);
    case ListState::Ready: break;
    }
    if (tile.kind == TileKind::Updates && tile.value == 0) {
        quiet = true;
        return m_strings.get(Str::OverviewUpdatesNone);
    }
    return formatCount(tile.value, m_language);
}

void OverviewPage::rebuildTexts() {
    const auto& m = m_model;
    const auto& id = m.image;
    const bool applying = m.run && m.run->active();

    // ---- identity ----
    m_name = !id.name.empty() ? id.name : id.file;
    m_badge.clear();
    m_badgeKind = BadgeKind::None;
    switch (m.stage) {
    case OverviewModel::Stage::Mounted:
        if (applying) {
            m_badge = m_strings.get(Str::StatusApplying);
            m_badgeKind = BadgeKind::Working;
        } else if (m.operation && *m.operation == EngineOperation::Kind::Reading) {
            m_badge = m_strings.get(Str::StatusReading);
            m_badgeKind = BadgeKind::Working;
        } else {
            m_badge = m_strings.get(Str::StatusMounted);
            m_badgeKind = BadgeKind::Mounted;
        }
        if (id.readOnly) {
            m_badge += L" · " + m_strings.get(Str::OverviewReadOnly);
        }
        break;
    case OverviewModel::Stage::Mounting:
        m_badge = m_strings.format(Str::OverviewMounting, {{L"pct", std::to_wstring(static_cast<int>(id.progress * 100))}});
        m_badgeKind = BadgeKind::Working;
        break;
    case OverviewModel::Stage::Closed:
        m_badge = m_strings.get(Str::OverviewNotMounted);
        m_badgeKind = BadgeKind::Idle;
        break;
    case OverviewModel::Stage::NoSource: break;
    }
    m_facts.clear();
    auto fact = [](std::wstring& line, const std::wstring& part) {
        if (!part.empty()) {
            line += (line.empty() ? L"" : L" · ") + part;
        }
    };
    if (id.build > 0) {
        fact(m_facts, core::releaseLabel(id.build));
        fact(m_facts, std::to_wstring(id.build) + L"." + std::to_wstring(id.spBuild));
    }
    fact(m_facts, id.editionId);
    fact(m_facts, id.architecture);
    if (!id.language.empty()) {
        fact(m_facts, id.languages > 1 ? id.language + L" +" + std::to_wstring(id.languages - 1) : id.language);
    }
    if (id.bytes > 0) {
        fact(m_facts, formatBytes(id.bytes, m_language));
    }
    m_fileFacts.clear();
    fact(m_fileFacts, id.file);
    if (id.virtualDisk) {
        // D-141: "VHDX · dinamik · 40 GB" — and that the file is only written by a save.
        const Str kind = id.differencingDisk ? Str::ImagesVhdDifferencing : id.dynamicDisk ? Str::ImagesVhdDynamic : Str::ImagesVhdFixed;
        fact(m_fileFacts, id.container + L" · " + m_strings.get(kind) +
                              (id.diskBytes > 0 ? L" · " + formatBytes(id.diskBytes, m_language) : std::wstring()));
    }
    if (!id.compression.empty()) {
        fact(m_fileFacts, upper(id.compression) + L" (" + id.container + L")" +
                              (id.solid ? L" · " + m_strings.get(Str::ImagesSolid) : std::wstring()));
    }
    if (id.fileBytes > 0) {
        fact(m_fileFacts, formatBytes(id.fileBytes, m_language));
    }
    if (id.index > 0 && id.editions > 0 && !id.virtualDisk) {
        fact(m_fileFacts, m_strings.format(Str::OverviewEditionOf, {{L"i", std::to_wstring(id.index)}, {L"n", std::to_wstring(id.editions)}}));
    }
    if (id.virtualDisk) {
        fact(m_fileFacts, m_strings.get(Str::OverviewVhdUntouched));
    }
    fact(m_fileFacts, id.source);
    if (m_facts.empty()) {
        m_facts = std::move(m_fileFacts);
        m_fileFacts.clear();
    }
    // 2026-11-11: a mount taken over from another tool says whose folder it is.
    m_mountDir = id.mountDir.empty()      ? std::wstring()
                 : id.mountedElsewhere ? m_strings.format(Str::OverviewMountedElsewhere, {{L"path", id.mountDir}})
                                         : m_strings.format(Str::OverviewMountedAt, {{L"path", id.mountDir}});

    // ---- the next step ----
    m_nextLabel = m_strings.get(Str::OverviewNextLabel);
    std::optional<Str> button;
    switch (m.next) {
    case Next::ChooseSource:
        m_nextText = m_strings.get(Str::OverviewNextChooseSource);
        button = Str::ImagesEmptyAction;
        break;
    case Next::Wait: m_nextText = m_strings.get(Str::OverviewNextWait); break;
    case Next::Mount:
        m_nextText = m_strings.get(Str::OverviewNextMount);
        button = Str::ImagesMount;
        break;
    case Next::ConvertEsd:
        m_nextText = m_strings.get(Str::OverviewNextConvertEsd);
        button = Str::ImagesEsdToWim;
        break;
    case Next::PickChanges:
        m_nextText = m_strings.get(Str::OverviewNextPickChanges);
        button = Str::NavComponents;
        break;
    case Next::ReviewQueue:
        m_nextText = m_strings.format(Str::OverviewNextReviewQueue, {{L"n", std::to_wstring(m.queue.changes)}});
        button = Str::OverviewGoApply;
        break;
    case Next::WatchRun:
        m_nextText = m_strings.get(Str::OverviewNextWatchRun);
        button = Str::OverviewWatch;
        break;
    case Next::ResolveRun:
        m_nextText = m_strings.get(Str::OverviewNextResolveRun);
        button = Str::OverviewSeeResult;
        break;
    case Next::BuildIso:
        m_nextText = m_strings.get(Str::OverviewNextBuildIso);
        button = Str::NavIso;
        break;
    case Next::DiskSaved:
        m_nextText = m_strings.get(Str::OverviewNextDiskSaved);
        button = Str::ImagesMount;
        break;
    case Next::IsoReady: { // D-144
        const auto& run = m_state.applyRun();
        m_nextText = m_strings.format(Str::OverviewNextIsoReady,
                                      {{L"file", run && run->autoIso ? run->autoIso->output.filename().wstring() : std::wstring()}});
        button = Str::ApplyAutoIsoShow;
        break;
    }
    }
    m_primary->setVisible(button.has_value());
    if (button) {
        m_primary->setText(m_strings.get(*button));
    }

    // ---- the run ----
    if (m.run) {
        const auto& r = *m.run;
        m_runSection = m_strings.get(r.active() ? Str::OverviewRunNow : Str::OverviewRun);
        const int pct = static_cast<int>(r.fraction * 100);
        switch (r.outcome) {
        case Outcome::Running: {
            m_runIcon = Icon::QueueClock; // drawn as the progress ring (D-142, below)
            m_runInk = Color::AccentText;
            m_runTitle = m_strings.format(Str::OverviewRunRunning, {{L"pct", std::to_wstring(pct)}});
            const auto& run = m_state.applyRun();
            if (run && r.current >= 0 && static_cast<std::size_t>(r.current) < run->plan.steps.size()) {
                m_runTitle += L" · " + applyPhaseName(m_strings, run->plan.steps[static_cast<std::size_t>(r.current)].phase);
            }
            break;
        }
        case Outcome::Paused:
            m_runIcon = Icon::Pause;
            m_runInk = Color::StatusInfo;
            m_runTitle = m_strings.get(Str::ApplyPausedTitle);
            break;
        case Outcome::Committing:
            m_runIcon = Icon::Save;
            m_runInk = Color::AccentText;
            m_runTitle = m_strings.get(Str::OverviewRunCommitting);
            break;
        case Outcome::Succeeded:
            m_runIcon = Icon::SuccessCircle;
            m_runInk = Color::StatusSuccess;
            m_runTitle = m_strings.get(Str::ApplyDoneTitle);
            break;
        case Outcome::Skipped:
            m_runIcon = Icon::WarningTriangle;
            m_runInk = Color::StatusWarning;
            m_runTitle = m_strings.format(Str::ApplySkippedTitle, {{L"n", std::to_wstring(r.skipped)}});
            break;
        case Outcome::Held:
            m_runIcon = Icon::WarningTriangle;
            m_runInk = Color::StatusWarning;
            m_runTitle = m_strings.format(Str::ApplyHeldTitle, {{L"n", std::to_wstring(r.skipped)}});
            break;
        case Outcome::Kept:
            m_runIcon = Icon::WarningTriangle;
            m_runInk = Color::StatusWarning;
            m_runTitle = m_strings.get(Str::ApplyKeptTitle);
            break;
        case Outcome::Stopped:
            m_runIcon = Icon::Stop;
            m_runInk = Color::StatusWarning;
            m_runTitle = m_strings.get(Str::ApplyStoppedTitle);
            break;
        case Outcome::CommitFailed:
        case Outcome::StartFailed:
            m_runIcon = Icon::ErrorOctagon;
            m_runInk = Color::StatusError;
            m_runTitle = m_strings.get(Str::ApplyFailedTitle);
            break;
        }
        // 2026-11-11: the save / discard of what the run left mounted is the run's news once started.
        using Close = RunSummary::Close;
        m_closeFraction = 0;
        switch (r.close) {
        case Close::None: break;
        case Close::Saving:
        case Close::Discarding:
            m_runIcon = r.close == Close::Saving ? Icon::Save : Icon::Unmount;
            m_runInk = Color::AccentText;
            m_runTitle = m_strings.get(r.close == Close::Saving ? Str::ApplyCloseSavingTitle : Str::ApplyCloseDiscardingTitle);
            if (const auto& op = m_state.operation()) {
                m_closeFraction = static_cast<float>(std::clamp(op->fraction, 0.0, 1.0));
            }
            break;
        case Close::Saved:
            m_runIcon = Icon::SuccessCircle;
            m_runInk = Color::StatusSuccess;
            m_runTitle = m_strings.get(Str::ApplyCloseSavedTitle);
            break;
        case Close::Discarded:
            m_runIcon = Icon::Unmount;
            m_runInk = Color::TextSecondary;
            m_runTitle = m_strings.get(Str::ApplyCloseDiscardedTitle);
            break;
        case Close::SaveFailed:
        case Close::DiscardFailed:
            m_runIcon = Icon::ErrorOctagon;
            m_runInk = Color::StatusError;
            m_runTitle = m_strings.get(r.close == Close::SaveFailed ? Str::ApplyCloseSaveFailedTitle : Str::ApplyCloseDiscardFailedTitle);
            break;
        }
        // D-144: the ISO after the run — while it builds it is the card's news; its end is said beside.
        std::wstring isoNote;
        if (r.iso != RunSummary::Iso::None) {
            const auto& run = m_state.applyRun();
            const std::wstring file = run && run->autoIso ? run->autoIso->output.filename().wstring() : std::wstring();
            switch (r.iso) {
            case RunSummary::Iso::Running: {
                const auto& iso = m_state.isoRun();
                m_isoFraction = iso ? static_cast<float>(std::clamp(iso->fraction, 0.0, 1.0)) : 0.0f;
                const int isoPct = static_cast<int>(m_isoFraction * 100);
                m_runIcon = Icon::IsoBuild;
                m_runInk = Color::AccentText;
                m_runTitle = m_strings.format(Str::OverviewRunIso, {{L"pct", std::to_wstring(isoPct)}});
                isoNote = file;
                break;
            }
            case RunSummary::Iso::Done:
                m_runTitle += L" · " + m_strings.get(Str::OverviewRunIsoDone);
                isoNote = file;
                break;
            case RunSummary::Iso::Failed:
                isoNote = m_strings.format(Str::ApplyAutoIsoFailed,
                                           {{L"e", run && run->autoIso && run->autoIso->error ? run->autoIso->error->message : std::wstring()}});
                break;
            case RunSummary::Iso::Cancelled: isoNote = m_strings.get(Str::ApplyAutoIsoCancelled); break;
            case RunSummary::Iso::NotStarted:
                isoNote = m_strings.get(r.isoSkip == AppState::ApplyRun::AutoIso::Skip::Stopped    ? Str::ApplyAutoIsoSkipStopped
                                        : r.isoSkip == AppState::ApplyRun::AutoIso::Skip::NotSaved ? Str::ApplyAutoIsoSkipNotSaved
                                        : r.isoSkip == AppState::ApplyRun::AutoIso::Skip::Edition  ? Str::ApplyAutoIsoSkipEdition
                                        : r.isoSkip == AppState::ApplyRun::AutoIso::Skip::Welcome  ? Str::ApplyAutoIsoSkipWelcome
                                                                                                   : Str::ApplyAutoIsoSkipRunFailed);
                break;
            case RunSummary::Iso::Waiting:
            case RunSummary::Iso::None: break;
            }
        }
        const bool turning = r.outcome == Outcome::Running || r.iso == RunSummary::Iso::Running;
        m_runRing->setRunning(turning);
        m_runRing->setVisible(turning);
        m_runCounts[0] = {m_strings.format(Str::ApplyRunOk, {{L"n", std::to_wstring(r.ok)}}), Color::StatusSuccess, 0};
        m_runCounts[1] = {m_strings.format(Str::ApplySkipped, {{L"n", std::to_wstring(r.skipped)}}),
                          r.skipped ? Color::StatusWarning : Color::TextTertiary, 0};
        m_runCounts[2] = {r.active() || r.pending > 0
                              ? m_strings.format(r.active() ? Str::ApplyRunPending : Str::ApplyRunNotRun, {{L"n", std::to_wstring(r.pending)}})
                              : std::wstring(),
                          Color::TextTertiary, 0};
        if (r.active()) {
            m_runTime = m_strings.format(Str::ApplyRunElapsed, {{L"t", formatDuration(r.elapsedSeconds, m_language)}});
            if (r.remainingSeconds >= 0) {
                m_runTime += L" · " + m_strings.format(Str::ApplyRunRemaining,
                                                            {{L"t", formatDuration(r.remainingSeconds, m_language, true).substr(1)}});
            }
        } else {
            m_runTime = m_strings.format(Str::ApplyRunTook, {{L"t", formatDuration(r.elapsedSeconds, m_language)}});
            if (r.sizeBefore > 0 && r.sizeAfter > 0) {
                m_runTime += L" · " + formatBytes(r.sizeBefore, m_language) + L" → " + formatBytes(r.sizeAfter, m_language);
            }
        }
        if (!isoNote.empty()) {
            m_runTime += L" · " + isoNote;
        }
    } else {
        m_runRing->setRunning(false);
        m_runRing->setVisible(false);
    }

    // ---- what is in it ----
    m_contentsSection = m_strings.get(Str::OverviewContents);
    m_contentsHint = m_strings.get(m.stage == OverviewModel::Stage::Mounted ? Str::OverviewContentsHint : Str::OverviewContentsClosed);
    for (std::size_t i = 0; i < m_tiles.size(); ++i) {
        const auto& tile = m.tiles[i];
        OverviewTile::Texts texts;
        texts.icon = tileIcon(tile.kind);
        texts.label = m_strings.get(tileLabel(tile.kind));
        texts.value = tileValue(tile, texts.quiet);
        if (tile.state == ListState::Ready) {
            switch (tile.kind) {
            case TileKind::Components:
                if (!tile.top.empty()) {
                    for (const auto& [name, count] : tile.top) {
                        texts.detail += (texts.detail.empty() ? L"" : L" · ") + name + L" " + formatCount(count, m_language);
                    }
                } else {
                    texts.detail = m_strings.format(Str::OverviewComponentsDetail, {{L"n", std::to_wstring(tile.total)}});
                }
                break;
            case TileKind::Apps: texts.detail = m_strings.get(Str::OverviewAppsDetail); break;
            case TileKind::Features:
                texts.detail = m_strings.format(Str::OverviewFeaturesDetail,
                                                {{L"total", formatCount(tile.total, m_language)}, {L"caps", formatCount(tile.secondary, m_language)}});
                break;
            case TileKind::Drivers: texts.detail = m_strings.get(Str::OverviewDriversDetail); break;
            case TileKind::Languages:
                if (!tile.text.empty()) {
                    texts.detail = m_strings.format(Str::OverviewLanguagesDetail, {{L"lang", tile.text}});
                }
                break;
            case TileKind::Services:
                texts.detail = m_strings.format(Str::OverviewServicesDetail, {{L"n", formatCount(tile.secondary, m_language)}});
                break;
            case TileKind::Updates:
                if (tile.value > 0) {
                    texts.detail = m_strings.get(Str::OverviewUpdatesDetail);
                }
                break;
            case TileKind::Count: break;
            }
        }
        if (tile.queued > 0 && tile.kind != TileKind::Updates) {
            texts.badge = std::to_wstring(tile.queued);
        }
        m_tiles[i]->set(std::move(texts));
    }

    // ---- the queue ----
    const auto& q = m.queue;
    m_queueSection = m_strings.get(Str::OverviewQueue);
    m_queueHint.clear();
    if (q.changes > 0) {
        const std::wstring time = q.seconds > 0 ? formatDuration(q.seconds * 0.7, m_language, true) + L"–" +
                                                      formatDuration(q.seconds * 1.4, m_language, true).substr(1)
                                                : std::wstring(L"—");
        m_queueHint = m_strings.format(Str::OverviewQueueSummary, {{L"n", std::to_wstring(q.changes)}, {L"t", time}});
    }
    m_queueMessage.clear();
    if (m.stage != OverviewModel::Stage::Mounted) {
        m_queueMessage = m_strings.get(Str::OverviewQueueNoMount);
    } else if (q.locked) {
        m_queueMessage = m_strings.get(Str::OverviewQueueLocked);
    } else if (q.changes == 0) {
        m_queueMessage = m_strings.get(Str::OverviewQueueEmpty);
    }
    m_heldText = q.heldDownloads > 0 ? m_strings.format(Str::OverviewQueueHeld, {{L"n", std::to_wstring(q.heldDownloads)}})
                 : q.waitingForDownload ? m_strings.get(Str::OverviewQueueWaiting)
                                        : std::wstring();
    m_sizeLabel = m_strings.get(Str::OverviewSize);
    if (q.sizeBefore > 0) {
        m_sizeValue = m_strings.format(Str::OverviewSizeValue,
                                       {{L"before", formatBytes(q.sizeBefore, m_language)}, {L"after", formatBytes(q.sizeAfter, m_language)}});
    } else {
        m_sizeValue = m_strings.get(Str::CommonNone);
    }
    if (q.sizeDelta < 0) {
        const auto saved = static_cast<std::uint64_t>(-q.sizeDelta);
        const int pct = q.sizeBefore > 0
                            ? static_cast<int>(std::lround(100.0 * static_cast<double>(saved) / static_cast<double>(q.sizeBefore)))
                            : 0;
        m_sizeDelta = q.sizeBefore > 0 ? m_strings.format(Str::OverviewSizeSmaller,
                                                          {{L"size", formatBytes(saved, m_language)}, {L"pct", std::to_wstring(pct)}})
                                       : m_strings.format(Str::OverviewSizeSaved, {{L"size", formatBytes(saved, m_language)}});
        m_sizeDeltaInk = Color::StatusSuccess;
    } else if (q.sizeDelta > 0) {
        m_sizeDelta = m_strings.format(Str::OverviewSizeLarger, {{L"size", formatBytes(static_cast<std::uint64_t>(q.sizeDelta), m_language)}});
        m_sizeDeltaInk = Color::TextSecondary;
    } else {
        m_sizeDelta = m_strings.get(Str::OverviewSizeSame);
        m_sizeDeltaInk = Color::TextSecondary;
    }
    m_timeText.clear();
    if (q.seconds > 0) {
        m_timeText = m_strings.format(Str::OverviewTime, {{L"t", formatDuration(q.seconds, m_language, true).substr(1)}});
    }
    if (!q.risky.empty()) {
        m_riskText = m_strings.format(Str::OverviewRisky, {{L"n", std::to_wstring(q.risky.size())}, {L"items", joinedNames(q.risky, 3)}});
        m_riskInk = Color::StatusError;
    } else {
        m_riskText = m_strings.get(Str::OverviewNoRisky);
        // 2026-11-11: a whole glyph (the one-bar RiskLow looked broken in the tertiary ink), as the
        // guards' line under it: a check in the success colour.
        m_riskInk = Color::StatusSuccess;
    }
    m_guardWarn = !q.impacted.empty();
    if (m_guardWarn) {
        m_guardText = m_strings.format(Str::OverviewImpacted, {{L"n", std::to_wstring(q.impacted.size())}, {L"items", joinedNames(q.impacted, 3)}});
        m_guardInk = Color::StatusWarning;
    } else {
        m_guardText = m_strings.format(Str::OverviewGuardsOk, {{L"n", std::to_wstring(q.guardsOn)}});
        m_guardInk = Color::StatusSuccess;
    }
}

void OverviewPage::rebuildChips() {
    std::vector<std::tuple<Str, PageId, int>> key;
    for (const auto& group : m_model.queue.groups) {
        for (const auto& item : group.items) {
            key.emplace_back(group.caption, item.page, item.count);
        }
    }
    if (m_model.queue.locked || !m_queueMessage.empty()) {
        key.clear(); // a message in their place
    }
    if (key == m_chipKey && m_chips.size() == key.size()) {
        return;
    }
    m_chipKey = key;
    for (auto* chip : m_chips) {
        removeChild(chip);
    }
    m_chips.clear();
    m_chipGroup.clear();
    m_groupCaptions.clear();
    if (key.empty()) {
        return;
    }
    for (std::size_t g = 0; g < m_model.queue.groups.size(); ++g) {
        const auto& group = m_model.queue.groups[g];
        m_groupCaptions.push_back(m_strings.get(group.caption));
        for (const auto& item : group.items) {
            auto& chip = add<OverviewChip>(m_strings.get(item.label), std::to_wstring(item.count));
            const PageId page = item.page;
            chip.onInvoke = [this, page] {
                if (m_intents.open) {
                    m_intents.open(page);
                }
            };
            m_chips.push_back(&chip);
            m_chipGroup.push_back(g);
        }
    }
}

void OverviewPage::layout() {
    const RectF b = bounds();
    const bool none = m_model.stage == OverviewModel::Stage::NoSource;
    m_empty->setVisible(none);
    m_primary->setVisible(!none && m_model.next != Next::Wait);
    m_runLink->setVisible(!none && m_model.run.has_value());
    for (auto* tile : m_tiles) {
        tile->setVisible(!none);
    }
    for (auto* chip : m_chips) {
        chip->setVisible(!none);
    }
    if (none) {
        m_empty->setBounds(b);
        m_contentHeight = 0;
        return;
    }
    const float x = b.x;
    const float w = b.width;
    float y = b.y;

    // The first card: what the image is, then the next step and its button.
    m_identity = {x, y, w, kIdentity};
    m_band = {x, y + kIdentity, w, kBand};
    m_nameWidth = std::ceil(textWidth(m_name, TypeStyle::Title));
    m_badgeWidth = m_badge.empty() ? 0.0f : std::ceil(textWidth(m_badge, TypeStyle::Small)) + 2 * kBadgePadX;
    m_nextLabelWidth = std::ceil(textWidth(m_nextLabel, TypeStyle::BodySemi));
    if (m_primary->visible()) {
        const ui::SizeF size = m_primary->measure({});
        m_primary->setBounds({m_band.right() - kPad - size.width, m_band.y + std::round((kBand - size.height) / 2), size.width, size.height});
    }
    y += kIdentity + kBand + kGap;

    // The run, while it is the news.
    if (m_model.run) {
        m_runTitleArea = {x, y, w, kTitleArea};
        y += kTitleArea;
        m_runCard = {x, y, w, kRunCard};
        m_runRing->setBounds({m_runCard.x + kPad, m_runCard.y + 14, ui::tokens::size::ring, ui::tokens::size::ring});
        const ui::SizeF size = m_runLink->measure({});
        m_runLink->setBounds({m_runCard.right() - kPad + 8 - size.width, m_runCard.y + 10, size.width, size.height});
        for (auto& count : m_runCounts) {
            count.width = std::ceil(textWidth(count.text, TypeStyle::Caption));
        }
        y += kRunCard + kGap;
    } else {
        m_runTitleArea = m_runCard = {};
    }

    // What is in it: four columns (two when narrow), the components tile two wide.
    m_contentsTitleArea = {x, y, w, kTitleArea};
    y += kTitleArea;
    const int columns = w >= 600.0f ? 4 : 2;
    const float column = std::floor((w - kTileGap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
    int slot = 0;
    for (std::size_t i = 0; i < m_tiles.size(); ++i) {
        const int span = i == 0 ? 2 : 1;
        if (slot % columns + span > columns) {
            slot += columns - slot % columns;
        }
        const int col = slot % columns;
        const int row = slot / columns;
        const float tx = x + static_cast<float>(col) * (column + kTileGap);
        const float tw = col + span == columns ? x + w - tx : column * static_cast<float>(span) + kTileGap * static_cast<float>(span - 1);
        m_tiles[i]->setBounds({tx, y + static_cast<float>(row) * (OverviewTile::kHeight + kTileGap), tw, OverviewTile::kHeight});
        slot += span;
    }
    const int rows = (slot + columns - 1) / columns;
    y += static_cast<float>(rows) * OverviewTile::kHeight + static_cast<float>(rows - 1) * kTileGap + kGap;

    // The queue: its groups as the navigation has them | size, time, risk, compatibility.
    m_queueTitleArea = {x, y, w, kTitleArea};
    y += kTitleArea;
    const float inner = w - 2 * kPad;
    m_twoColumns = m_queueMessage.empty() && m_model.queue.changes > 0;
    // Nothing for the left (no chip, no download note): the right column takes the card, no empty half.
    m_rightOnly = m_twoColumns && m_chips.empty() && m_heldText.empty();
    const float leftWidth = m_rightOnly ? 0.0f : m_twoColumns ? std::floor(inner * 0.56f) : inner;
    const float left = x + kPad;
    const float top = y + kQueuePadY;
    float ly = top;
    m_captionRects.assign(m_groupCaptions.size(), RectF{});
    if (!m_queueMessage.empty()) {
        ly += 20;
    } else {
        std::size_t c = 0;
        for (std::size_t g = 0; g < m_groupCaptions.size(); ++g) {
            m_captionRects[g] = {left, ly, kCaptionWidth - 8, OverviewChip::kHeight};
            float cx = left + kCaptionWidth;
            for (; c < m_chips.size() && m_chipGroup[c] == g; ++c) {
                const ui::SizeF size = m_chips[c]->measure({});
                if (cx + size.width > left + leftWidth && cx > left + kCaptionWidth) {
                    cx = left + kCaptionWidth;
                    ly += OverviewChip::kHeight + kChipGap;
                }
                m_chips[c]->setBounds({cx, ly, size.width, size.height});
                cx += size.width + kChipGap;
            }
            ly += OverviewChip::kHeight + kChipRowGap;
        }
        ly -= kChipRowGap;
    }
    if (!m_heldText.empty()) {
        ly += 6;
        m_heldRect = {left, ly, leftWidth, 18};
        ly += 18;
    } else {
        m_heldRect = {};
    }
    const float leftHeight = ly - top;
    const float rightHeight = m_twoColumns ? kRightColumn : 0.0f;
    const float cardHeight = std::max(leftHeight, rightHeight) + 2 * kQueuePadY;
    m_queueCard = {x, y, w, cardHeight};
    m_queueLeft = {left, top, leftWidth, leftHeight};
    m_queueRight = m_rightOnly    ? RectF{left, top, inner, rightHeight}
                   : m_twoColumns ? RectF{left + leftWidth + kColumnGap, top, inner - leftWidth - kColumnGap, rightHeight}
                                  : RectF{};
    m_sizeLabelWidth = std::ceil(textWidth(m_sizeLabel, TypeStyle::Caption));
    y += cardHeight;
    m_contentHeight = y - b.y;
}

void OverviewPage::paint(ui::Canvas& canvas) {
    if (m_empty->visible()) {
        return;
    }
    // ---- the image ----
    const RectF card{m_identity.x, m_identity.y, m_identity.width, kIdentity + kBand};
    canvas.fillRoundRect(card, ui::tokens::radius::r3, Color::BgRaised);
    const RectF tile{card.x + kPad, card.y + std::round((kIdentity - kIconTile) / 2), kIconTile, kIconTile};
    canvas.fillRoundRect(tile, ui::tokens::radius::r2, Color::AccentSubtle);
    canvas.drawIcon(Icon::LayersEditions, {tile.x + 8, tile.y + 8}, Color::AccentBase, ui::IconVariant::Regular24);
    const float tx = tile.right() + 14;
    const float right = card.right() - kPad;
    const float nameWidth = std::min(m_nameWidth + 1, std::max(right - tx - m_badgeWidth - 12, 0.0f));
    canvas.drawText(m_name, {tx, card.y + 12, nameWidth, 22}, TypeStyle::Title, Color::TextPrimary);
    float afterName = tx + nameWidth;
    if (m_badgeWidth > 0) {
        const RectF badge{afterName + 10, card.y + 14, m_badgeWidth, 18};
        const Color fill = m_badgeKind == BadgeKind::Mounted ? Color::StatusSuccessSubtle
                           : m_badgeKind == BadgeKind::Working ? Color::AccentSubtle
                                                               : Color::BgPressed;
        const Color ink = m_badgeKind == BadgeKind::Mounted ? Color::StatusSuccess
                          : m_badgeKind == BadgeKind::Working ? Color::AccentText
                                                              : Color::TextSecondary;
        canvas.fillRoundRect(badge, ui::tokens::radius::badge, fill);
        canvas.drawText(m_badge, badge, TypeStyle::Small, ink, ui::TextAlign::Center);
        afterName = badge.right();
    }
    if (!m_mountDir.empty() && afterName + 24 < right) {
        canvas.drawText(m_mountDir, {afterName + 24, card.y + 12, right - afterName - 24, 22}, TypeStyle::Caption, Color::TextTertiary,
                        ui::TextAlign::Trailing);
    }
    canvas.drawText(m_facts, {tx, card.y + 37, std::max(right - tx, 0.0f), 18}, TypeStyle::Body, Color::TextSecondary);
    canvas.drawText(m_fileFacts, {tx, card.y + 57, std::max(right - tx, 0.0f), 16}, TypeStyle::Caption, Color::TextTertiary);
    canvas.hairlineH(card.x + kPad, m_band.y, card.width - 2 * kPad, Color::LineSubtle);
    canvas.drawIcon(Icon::ArrowRight, {card.x + kPad, m_band.y + std::round((kBand - 16) / 2)}, Color::AccentText);
    const float labelX = card.x + kPad + ui::tokens::size::icon + 8;
    canvas.drawText(m_nextLabel, {labelX, m_band.y, m_nextLabelWidth + 1, kBand}, TypeStyle::BodySemi, Color::AccentText);
    const float textX = labelX + m_nextLabelWidth + 12;
    const float textRight = m_primary->visible() ? m_primary->bounds().x - kPad : right;
    canvas.drawText(m_nextText, {textX, m_band.y, std::max(textRight - textX, 0.0f), kBand}, TypeStyle::Body,
                    m_model.next == Next::Wait ? Color::TextSecondary : Color::TextPrimary);

    // ---- the run ----
    if (m_model.run) {
        const auto& r = *m_model.run;
        ui::FormView::paintSectionTitle(canvas, m_runTitleArea, m_runSection);
        const RectF rc = m_runCard;
        canvas.fillRoundRect(rc, ui::tokens::radius::r3, Color::BgRaised);
        canvas.fillRoundRect({rc.x + 1, rc.y + 10, 3, rc.height - 20}, 1.5f, m_runInk);
        if (!m_runRing->visible()) {
            canvas.drawIcon(m_runIcon, {rc.x + kPad, rc.y + 14}, m_runInk);
        }
        const float titleX = rc.x + kPad + ui::tokens::size::icon + 10;
        canvas.drawText(m_runTitle, {titleX, rc.y + 12, std::max(m_runLink->bounds().x - 12 - titleX, 0.0f), 20}, TypeStyle::BodySemi,
                        Color::TextPrimary);
        const RectF bar{rc.x + kPad, rc.y + 44, rc.width - 2 * kPad, 6};
        if (r.active()) {
            paintMeter(canvas, bar, static_cast<float>(r.fraction));
        } else if (r.closing()) {
            paintMeter(canvas, bar, m_closeFraction);
        } else if (r.isoRunning()) {
            paintMeter(canvas, bar, m_isoFraction); // D-144: the ISO's progress, as the title says
        } else if (r.sizeBefore > 0 && r.sizeAfter > 0) {
            paintSizeBar(canvas, bar, r.sizeBefore, r.sizeAfter);
        } else {
            paintMeter(canvas, bar, r.total ? static_cast<float>(r.ok + r.skipped) / static_cast<float>(r.total) : 1.0f, m_runInk);
        }
        float cx = rc.x + kPad;
        const float cy = rc.y + 58;
        for (const auto& count : m_runCounts) {
            if (count.text.empty()) {
                continue;
            }
            canvas.fillEllipse({cx + 3.5f, cy + 9}, 3.5f, count.dot);
            canvas.drawText(count.text, {cx + 12, cy, count.width + 1, 18}, TypeStyle::Caption, Color::TextSecondary);
            cx += 12 + count.width + 18;
        }
        if (cx < rc.right() - kPad) {
            canvas.drawText(m_runTime, {cx, cy, rc.right() - kPad - cx, 18}, TypeStyle::Caption, Color::TextSecondary,
                            ui::TextAlign::Trailing);
        }
    }

    // ---- what is in it (the tiles draw themselves) ----
    ui::FormView::paintSectionTitle(canvas, m_contentsTitleArea, m_contentsSection);
    const RectF hintLine{m_contentsTitleArea.x, m_contentsTitleArea.bottom() - ui::FormView::kSectionBelow - ui::FormView::kSectionLine,
                         m_contentsTitleArea.width, ui::FormView::kSectionLine};
    canvas.drawText(m_contentsHint, hintLine, TypeStyle::Caption, Color::TextTertiary, ui::TextAlign::Trailing);

    // ---- the queue ----
    ui::FormView::paintSectionTitle(canvas, m_queueTitleArea, m_queueSection);
    if (!m_queueHint.empty()) {
        const RectF line{m_queueTitleArea.x, m_queueTitleArea.bottom() - ui::FormView::kSectionBelow - ui::FormView::kSectionLine,
                         m_queueTitleArea.width, ui::FormView::kSectionLine};
        canvas.drawText(m_queueHint, line, TypeStyle::Caption, Color::TextSecondary, ui::TextAlign::Trailing);
    }
    canvas.fillRoundRect(m_queueCard, ui::tokens::radius::r3, Color::BgRaised);
    if (!m_queueMessage.empty()) {
        canvas.drawText(m_queueMessage, {m_queueLeft.x, m_queueLeft.y, m_queueLeft.width, 20}, TypeStyle::Body, Color::TextSecondary);
    }
    for (std::size_t g = 0; g < m_groupCaptions.size() && g < m_captionRects.size(); ++g) {
        canvas.drawText(m_groupCaptions[g], m_captionRects[g], TypeStyle::Caption, Color::TextSecondary);
    }
    if (!m_heldText.empty()) {
        canvas.drawIcon(Icon::Download, {m_heldRect.x, m_heldRect.y + 1}, Color::TextSecondary);
        canvas.drawText(m_heldText, {m_heldRect.x + 22, m_heldRect.y, std::max(m_heldRect.width - 22, 0.0f), m_heldRect.height},
                        TypeStyle::Caption, Color::TextSecondary);
    }
    if (m_twoColumns) {
        const RectF rr = m_queueRight;
        if (!m_rightOnly) {
            canvas.hairlineV(rr.x - kColumnGap / 2, m_queueCard.y + kQueuePadY, m_queueCard.height - 2 * kQueuePadY, Color::LineSubtle);
        }
        canvas.drawText(m_sizeLabel, {rr.x, rr.y, m_sizeLabelWidth + 1, 20}, TypeStyle::Caption, Color::TextSecondary);
        const float vx = rr.x + m_sizeLabelWidth + 10;
        canvas.drawText(m_sizeValue, {vx, rr.y, std::max(rr.right() - vx, 0.0f), 20}, TypeStyle::BodySemi, Color::TextPrimary);
        paintSizeBar(canvas, {rr.x, rr.y + 26, rr.width, 6}, m_model.queue.sizeBefore, m_model.queue.sizeAfter);
        canvas.drawText(m_sizeDelta, {rr.x, rr.y + 38, rr.width, 18}, TypeStyle::Caption, m_sizeDeltaInk);
        if (!m_timeText.empty()) {
            canvas.drawText(m_timeText, {rr.x, rr.y + 38, rr.width, 18}, TypeStyle::Caption, Color::TextSecondary, ui::TextAlign::Trailing);
        }
        canvas.drawIcon(m_model.queue.risky.empty() ? Icon::SuccessCircle : Icon::RiskHigh, {rr.x, rr.y + 66}, m_riskInk);
        canvas.drawText(m_riskText, {rr.x + 22, rr.y + 64, std::max(rr.width - 22, 0.0f), 20}, TypeStyle::Caption,
                        m_model.queue.risky.empty() ? Color::TextSecondary : Color::TextPrimary);
        canvas.drawIcon(m_guardWarn ? Icon::ShieldWarning : Icon::ShieldCheck, {rr.x, rr.y + 90}, m_guardInk);
        canvas.drawText(m_guardText, {rr.x + 22, rr.y + 88, std::max(rr.width - 22, 0.0f), 20}, TypeStyle::Caption,
                        m_guardWarn ? Color::TextPrimary : Color::TextSecondary);
    }
}

} // namespace wl::app
