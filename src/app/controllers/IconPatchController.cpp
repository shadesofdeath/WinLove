#include "app/controllers/IconPatchController.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/icons/IconSource.h"
#include "core/system/Picture.h"

#include <json.hpp>

#include <algorithm>
#include <cwctype>
#include <format>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;

namespace {

constexpr std::wstring_view kFolder = L"Windows\\SystemResources";
constexpr std::wstring_view kMun = L".mun";

std::wstring lower(std::wstring_view text) {
    return text::lower(std::wstring(text));
}

// "#3" for an id, the name otherwise — the file name of a group in a pack.
std::wstring packStem(const core::ResourceKey& key) {
    return key.named() ? key.name : std::to_wstring(key.id);
}

// A pack path must stay inside the pack.
std::optional<std::filesystem::path> inside(const std::filesystem::path& root, const std::wstring& relative) {
    if (relative.empty() || relative.find(L"..") != std::wstring::npos || relative.find(L':') != std::wstring::npos ||
        relative.front() == L'\\' || relative.front() == L'/') {
        return std::nullopt;
    }
    return root / relative;
}

} // namespace

IconPatchController::IconPatchController(AppState& state, std::function<void(std::function<void()>)> postToUi)
    : m_state(state), m_post(std::move(postToUi)) {
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Mount) {
            forgetMount();
        }
        // After Uygula the image's files changed (patched / restored).
        if (change == AppState::Change::Apply) {
            const auto& run = m_state.applyRun();
            if (run && run->stage == AppState::ApplyRun::Stage::Done) {
                forgetMount();
            }
        }
    });
}

IconPatchController::~IconPatchController() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

void IconPatchController::forgetMount() {
    m_mountDir.clear();
    m_files.reset();
    m_patched.reset();
    m_groups.clear();
    m_loading.clear();
}

std::filesystem::path IconPatchController::pathInImage(const std::wstring& relative) const {
    return m_state.mounted() ? m_state.mounted()->mountDir / relative : std::filesystem::path(relative);
}

const std::vector<IconPatchController::File>& IconPatchController::files() {
    static const std::vector<File> none;
    if (!m_state.mounted()) {
        return none;
    }
    const auto mountDir = m_state.mounted()->mountDir;
    if (m_files && m_mountDir == mountDir) {
        return *m_files;
    }
    forgetMount();
    m_mountDir = mountDir;
    std::vector<File> list;
    std::error_code ec;
    const auto root = mountDir / kFolder;
    for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_symlink(ec) || !it->is_regular_file(ec)) {
            continue;
        }
        const auto name = it->path().filename().wstring();
        if (name.size() <= kMun.size() || lower(name).substr(name.size() - kMun.size()) != kMun) {
            continue;
        }
        const auto rel = std::filesystem::relative(it->path(), mountDir, ec);
        if (ec) {
            continue;
        }
        list.push_back(File{rel.wstring(), name.substr(0, name.size() - kMun.size()), it->file_size(ec)});
    }
    std::ranges::sort(list, [](const File& a, const File& b) { return lower(a.name) < lower(b.name); });
    m_files = std::move(list);
    return *m_files;
}

bool IconPatchController::patchedInImage(const std::wstring& relative) {
    if (!m_state.mounted()) {
        return false;
    }
    if (!m_patched || m_mountDir != m_state.mounted()->mountDir) {
        (void)files(); // resets the caches for a new mount
        std::set<std::wstring> set;
        for (const auto& f : core::patchedIconFiles(m_state.mounted()->mountDir)) {
            set.insert(lower(f));
        }
        m_patched = std::move(set);
    }
    return m_patched->contains(lower(relative));
}

const std::vector<IconPatchController::Group>* IconPatchController::groups(const std::wstring& relative) {
    if (!m_state.mounted()) {
        return nullptr;
    }
    (void)files();
    if (const auto it = m_groups.find(relative); it != m_groups.end()) {
        return &it->second;
    }
    if (m_loading.insert(relative).second) {
        const auto file = pathInImage(relative);
        const auto mountDir = m_mountDir;
        auto result = std::make_shared<std::vector<Group>>();
        std::weak_ptr<bool> alive = m_alive;
        m_state.reader().run<bool>(
            [file, result](const core::TaskContext&) -> Result<bool> {
                auto bytes = readFileBytes(file);
                if (!bytes) {
                    return std::unexpected(bytes.error());
                }
                auto pe = core::PeImage::parse(std::move(*bytes));
                if (!pe) {
                    return std::unexpected(pe.error());
                }
                for (const auto& g : core::listIconGroups(pe->resources())) {
                    Group group{g.key, g.index, static_cast<int>(g.images.size()), 0};
                    for (const auto& img : g.images) {
                        group.largest = std::max<int>(group.largest, img.width);
                    }
                    result->push_back(std::move(group));
                }
                return true;
            },
            [this, alive, relative, result, mountDir, post = m_post](Result<bool> done) {
                post([this, alive, relative, result, mountDir, ok = done.has_value(),
                      why = done ? std::wstring() : describe(done.error())] {
                    if (const auto a = alive.lock(); !a || !*a || m_mountDir != mountDir) {
                        return;
                    }
                    if (!ok) {
                        log::warn("icons", L"cannot read the icons of " + relative + L": " + why);
                    }
                    m_loading.erase(relative);
                    m_groups[relative] = std::move(*result);
                    if (onLoaded) {
                        onLoaded();
                    }
                });
            });
    }
    return nullptr;
}

void IconPatchController::preload(const std::wstring& relative) {
    if (!m_state.mounted()) {
        return;
    }
    (void)files();
    auto bytes = readFileBytes(pathInImage(relative));
    auto pe = bytes ? core::PeImage::parse(std::move(*bytes)) : Result<core::PeImage>(std::unexpected(bytes.error()));
    std::vector<Group> list;
    if (pe) {
        for (const auto& g : core::listIconGroups(pe->resources())) {
            Group group{g.key, g.index, static_cast<int>(g.images.size()), 0};
            for (const auto& img : g.images) {
                group.largest = std::max<int>(group.largest, img.width);
            }
            list.push_back(std::move(group));
        }
    }
    m_groups[relative] = std::move(list);
}

Operation IconPatchController::operationFor(const std::wstring& relative, const core::IconPatchRequest& request) {
    Operation op{OpKind::PatchIcons, relative, core::iconPatchValue(request)};
    op.risk = core::ops::Risk::Medium; // a Windows file changes (with a backup and a restore script)
    return op;
}

std::optional<core::IconPatchRequest> IconPatchController::request(const std::wstring& relative) const {
    const auto* op = m_state.changes().find(OpKind::PatchIcons, relative);
    if (!op) {
        return std::nullopt;
    }
    auto parsed = core::iconPatchRequest(op->value);
    if (!parsed) {
        return std::nullopt;
    }
    return std::move(*parsed);
}

void IconPatchController::store(const std::wstring& relative, const core::IconPatchRequest& request) {
    if (!request.restore && request.groups.empty()) {
        m_state.unqueue(OpKind::PatchIcons, relative);
        return;
    }
    m_state.queue(operationFor(relative, request));
}

std::optional<std::filesystem::path> IconPatchController::replacement(const std::wstring& relative, const core::ResourceKey& key) const {
    if (const auto r = request(relative)) {
        for (const auto& [k, source] : r->groups) {
            if (k == key) {
                return source;
            }
        }
    }
    return std::nullopt;
}

bool IconPatchController::isIconSource(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec) || std::filesystem::file_size(file, ec) > (16u << 20)) {
        return false;
    }
    if (lower(file.extension().wstring()) == L".ico") {
        auto bytes = readFileBytes(file);
        return bytes && core::parseIco(*bytes).has_value();
    }
    auto size = core::pictureSize(file);
    return size && size->width >= 16 && size->height >= 16;
}

Result<void> IconPatchController::replace(const std::wstring& relative, const core::ResourceKey& key, const std::filesystem::path& source) {
    if (!isIconSource(source)) {
        return fail(ErrorCode::InvalidArgument, L"not an icon or a picture", source.wstring());
    }
    auto r = request(relative).value_or(core::IconPatchRequest{});
    r.restore = false;
    auto it = std::ranges::find(r.groups, key, &std::pair<core::ResourceKey, std::filesystem::path>::first);
    if (it != r.groups.end()) {
        it->second = source;
    } else {
        r.groups.emplace_back(key, source);
    }
    store(relative, r);
    return {};
}

void IconPatchController::revert(const std::wstring& relative, const core::ResourceKey& key) {
    auto r = request(relative);
    if (!r) {
        return;
    }
    std::erase_if(r->groups, [&](const auto& g) { return g.first == key; });
    store(relative, *r);
}

bool IconPatchController::restoreQueued(const std::wstring& relative) const {
    const auto r = request(relative);
    return r && r->restore;
}

void IconPatchController::setRestore(const std::wstring& relative, bool restore) {
    if (restore) {
        store(relative, core::IconPatchRequest{true, {}});
    } else if (restoreQueued(relative)) {
        m_state.unqueue(OpKind::PatchIcons, relative);
    }
}

void IconPatchController::revertFile(const std::wstring& relative) {
    m_state.unqueue(OpKind::PatchIcons, relative);
}

void IconPatchController::resetAll() {
    m_state.unqueueIf([](const Operation& op) { return op.kind == OpKind::PatchIcons; });
}

int IconPatchController::changedIn(const std::wstring& relative) const {
    const auto r = request(relative);
    return !r ? 0 : r->restore ? 1 : static_cast<int>(r->groups.size());
}

int IconPatchController::changedCount() const {
    int n = 0;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::PatchIcons) {
            n += changedIn(op.target);
        }
    }
    return n;
}

const IconPatchController::File* IconPatchController::fileNamed(const std::vector<File>& files, std::wstring_view name) {
    std::wstring want = lower(name);
    if (want.ends_with(kMun)) {
        want.resize(want.size() - kMun.size());
    }
    for (const auto& f : files) {
        const std::wstring n = lower(f.name); // "imageres.dll"
        if (n == want) {
            return &f;
        }
        const auto dot = n.rfind(L'.');
        if (dot != std::wstring::npos && n.substr(0, dot) == want) {
            return &f;
        }
    }
    return nullptr;
}

core::ResourceKey IconPatchController::keyFromPackName(std::wstring_view stem) {
    std::wstring_view s = stem;
    if (!s.empty() && s.front() == L'#') {
        s.remove_prefix(1);
    }
    if (!s.empty() && s.size() <= 5 && std::ranges::all_of(s, [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        const unsigned long v = std::wcstoul(std::wstring(s).c_str(), nullptr, 10);
        if (v <= 0xFFFF) {
            return core::ResourceKey{static_cast<std::uint16_t>(v), {}};
        }
    }
    return core::ResourceKey{0, std::wstring(stem)};
}

Result<IconPatchController::PackResult> IconPatchController::applyPack(const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return fail(ErrorCode::NotFound, L"not a folder", folder.wstring());
    }
    const auto& list = files();
    PackResult result;
    result.name = folder.filename().wstring();
    // file relative → (key → source)
    std::map<std::wstring, std::vector<std::pair<core::ResourceKey, std::filesystem::path>>> plan;
    auto add = [&](std::wstring_view fileName, const core::ResourceKey& key, const std::filesystem::path& source, const std::wstring& label) {
        const File* file = fileNamed(list, fileName);
        if (!file || !isIconSource(source)) {
            result.unmatched.push_back(label);
            return;
        }
        auto& entries = plan[file->relative];
        std::erase_if(entries, [&](const auto& e) { return e.first == key; });
        entries.emplace_back(key, source);
    };
    // iconpack.json first; folders add to / override it.
    const auto manifest = folder / L"iconpack.json";
    if (std::filesystem::is_regular_file(manifest, ec)) {
        auto bytes = readFileBytes(manifest);
        const auto j = bytes ? nlohmann::json::parse(*bytes, nullptr, false) : nlohmann::json();
        if (j.is_object()) {
            if (j.contains("name") && j["name"].is_string()) {
                result.name = utf8::toWide(j["name"].get<std::string>());
            }
            if (const auto f = j.find("files"); f != j.end() && f->is_object()) {
                for (const auto& [fileName, groups] : f->items()) {
                    if (!groups.is_object()) {
                        continue;
                    }
                    for (const auto& [key, source] : groups.items()) {
                        const std::wstring label = utf8::toWide(fileName) + L"/" + utf8::toWide(key);
                        const auto path = source.is_string() ? inside(folder, utf8::toWide(source.get<std::string>())) : std::nullopt;
                        if (!path) {
                            result.unmatched.push_back(label);
                            continue;
                        }
                        add(utf8::toWide(fileName), keyFromPackName(utf8::toWide(key)), *path, label);
                    }
                }
            }
        }
    }
    for (const auto& dir : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!dir.is_directory(ec) || dir.is_symlink(ec)) {
            continue;
        }
        const std::wstring fileName = dir.path().filename().wstring();
        if (!fileNamed(list, fileName)) {
            continue; // other folders of a pack (previews, the redirect kind) are not ours
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir.path(), std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const auto ext = lower(entry.path().extension().wstring());
            if (ext != L".ico" && ext != L".png" && ext != L".bmp" && ext != L".jpg" && ext != L".jpeg") {
                continue;
            }
            add(fileName, keyFromPackName(entry.path().stem().wstring()), entry.path(), fileName + L"/" + entry.path().filename().wstring());
        }
    }
    // Groups the files do not have are unmatched too (the pack is for another Windows build).
    std::vector<Operation> ops;
    for (auto& [relative, entries] : plan) {
        auto r = request(relative).value_or(core::IconPatchRequest{});
        r.restore = false;
        for (auto& [key, source] : entries) {
            auto it = std::ranges::find(r.groups, key, &std::pair<core::ResourceKey, std::filesystem::path>::first);
            if (it != r.groups.end()) {
                it->second = source;
            } else {
                r.groups.emplace_back(key, source);
            }
            ++result.matched;
        }
        ops.push_back(operationFor(relative, r));
    }
    if (!ops.empty()) {
        m_state.queueMany(std::move(ops));
    }
    return result;
}

Result<int> IconPatchController::exportPack(const std::filesystem::path& folder) const {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot create the pack folder", folder.wstring());
    }
    nlohmann::ordered_json files = nlohmann::ordered_json::object();
    int written = 0;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind != OpKind::PatchIcons) {
            continue;
        }
        const auto r = core::iconPatchRequest(op.value);
        if (!r || r->restore) {
            continue;
        }
        std::wstring name = std::filesystem::path(op.target).filename().wstring();
        if (lower(name).ends_with(kMun)) {
            name.resize(name.size() - kMun.size());
        }
        nlohmann::ordered_json groups = nlohmann::ordered_json::object();
        for (const auto& [key, source] : r->groups) {
            const auto target = folder / name / (packStem(key) + lower(source.extension().wstring()));
            std::filesystem::create_directories(target.parent_path(), ec);
            if (!std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec)) {
                return fail(ErrorCode::IoError, L"cannot copy an icon into the pack", source.wstring());
            }
            groups[utf8::fromWide(packStem(key))] = utf8::fromWide(name + L"/" + target.filename().wstring());
            ++written;
        }
        files[utf8::fromWide(name)] = std::move(groups);
    }
    nlohmann::ordered_json manifest;
    manifest["name"] = utf8::fromWide(folder.filename().wstring());
    manifest["files"] = std::move(files);
    if (auto ok = writeFileAtomic(folder / L"iconpack.json", manifest.dump(2)); !ok) {
        return std::unexpected(ok.error());
    }
    return written;
}

Result<void> IconPatchController::exportOriginal(const std::wstring& relative, const core::ResourceKey& key,
                                                 const std::filesystem::path& out) const {
    auto bytes = readFileBytes(pathInImage(relative));
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto pe = core::PeImage::parse(std::move(*bytes));
    if (!pe) {
        return std::unexpected(pe.error());
    }
    for (const auto& g : core::listIconGroups(pe->resources())) {
        if (g.key == key) {
            return writeFileAtomic(out, core::makeIco(g.images));
        }
    }
    return fail(ErrorCode::NotFound, L"no such icon", key.text());
}

} // namespace wl::app
