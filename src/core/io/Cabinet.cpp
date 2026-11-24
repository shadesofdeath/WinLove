#include "core/io/Cabinet.h"

#include "base/Text.h"
#include "base/Utf8.h"

#include <windows.h>

#include <fcntl.h>
#include <fdi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <memory>

namespace wl::core {

namespace {

// One open "file" of FDI: the cabinet (read from memory) or the file it unpacks (into a string).
struct MemFile {
    std::span<const std::uint8_t> data; // the cabinet
    std::string* out = nullptr;         // an unpacked file
    std::size_t pos = 0;
    std::size_t max = 0;
    bool overflow = false;
};

// FDI's callbacks get no user pointer: the cabinet of the call on this thread (FDICopy is synchronous).
thread_local std::span<const std::uint8_t> t_cab;

FNALLOC(memAlloc) {
    return ::operator new(cb, std::nothrow);
}
FNFREE(memFree) {
    ::operator delete(pv);
}
FNOPEN(memOpen) {
    (void)pszFile;
    (void)pmode;
    if ((oflag & (_O_WRONLY | _O_RDWR)) != 0) {
        return -1; // FDI writes only through the handles fdintCOPY_FILE returns
    }
    auto* f = new (std::nothrow) MemFile{};
    if (!f) {
        return -1;
    }
    f->data = t_cab;
    return reinterpret_cast<INT_PTR>(f);
}
FNREAD(memRead) {
    auto* f = reinterpret_cast<MemFile*>(hf);
    if (!f || f->out) {
        return static_cast<UINT>(-1);
    }
    const std::size_t left = f->pos < f->data.size() ? f->data.size() - f->pos : 0;
    const std::size_t n = std::min<std::size_t>(cb, left);
    if (n > 0) {
        std::memcpy(pv, f->data.data() + f->pos, n);
    }
    f->pos += n;
    return static_cast<UINT>(n);
}
FNWRITE(memWrite) {
    auto* f = reinterpret_cast<MemFile*>(hf);
    if (!f || !f->out) {
        return static_cast<UINT>(-1);
    }
    if (f->out->size() + cb > f->max) {
        f->overflow = true;
        return static_cast<UINT>(-1);
    }
    f->out->append(static_cast<const char*>(pv), cb);
    return cb;
}
FNCLOSE(memClose) {
    delete reinterpret_cast<MemFile*>(hf);
    return 0;
}
FNSEEK(memSeek) {
    auto* f = reinterpret_cast<MemFile*>(hf);
    if (!f || f->out) {
        return -1;
    }
    long long to = dist;
    if (seektype == SEEK_CUR) {
        to += static_cast<long long>(f->pos);
    } else if (seektype == SEEK_END) {
        to += static_cast<long long>(f->data.size());
    }
    if (to < 0 || to > static_cast<long long>(f->data.size())) {
        return -1;
    }
    f->pos = static_cast<std::size_t>(to);
    return static_cast<long>(to);
}

struct Walk {
    std::string wanted;              // lower-case; empty: list only
    std::vector<std::string> names;
    std::string* out = nullptr;
    std::size_t max = 0;
    bool found = false;
    bool overflow = false;
};

FNFDINOTIFY(notify) {
    auto* walk = static_cast<Walk*>(pfdin->pv);
    switch (fdint) {
    case fdintCOPY_FILE: {
        const std::string name = pfdin->psz1 ? pfdin->psz1 : "";
        walk->names.push_back(name);
        std::string lower = name;
        std::ranges::transform(lower, lower.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        if (walk->wanted.empty() || lower != walk->wanted || walk->found) {
            return 0; // skipped
        }
        auto* f = new (std::nothrow) MemFile{};
        if (!f) {
            return -1;
        }
        f->out = walk->out;
        f->max = walk->max;
        walk->found = true;
        return reinterpret_cast<INT_PTR>(f);
    }
    case fdintCLOSE_FILE_INFO: {
        auto* f = reinterpret_cast<MemFile*>(pfdin->hf);
        if (f && f->overflow) {
            walk->overflow = true;
        }
        delete f;
        return TRUE;
    }
    default: return 0;
    }
}

Result<void> walkCabinet(std::span<const std::uint8_t> cab, Walk& walk) {
    if (cab.size() < 36 || std::memcmp(cab.data(), "MSCF", 4) != 0) {
        return fail(ErrorCode::ParseError, L"not a cabinet file (no MSCF header)");
    }
    ERF erf{};
    const HFDI fdi = FDICreate(memAlloc, memFree, memOpen, memRead, memWrite, memClose, memSeek, cpuUNKNOWN, &erf);
    if (!fdi) {
        return fail(ErrorCode::IoError, std::format(L"FDICreate failed (FDI error {})", erf.erfOper));
    }
    t_cab = cab;
    char name[] = "memory.cab";
    char path[] = "";
    const BOOL ok = FDICopy(fdi, name, path, 0, notify, nullptr, &walk);
    t_cab = {};
    const int oper = erf.erfOper;
    FDIDestroy(fdi);
    if (walk.overflow) {
        return fail(ErrorCode::IoError, L"the file in the cabinet is larger than expected", utf8::toWide(walk.wanted));
    }
    if (!ok) {
        return fail(ErrorCode::ParseError, std::format(L"the cabinet could not be read (FDI error {})", oper));
    }
    return {};
}

} // namespace

Result<std::vector<std::string>> cabinetNames(std::span<const std::uint8_t> cab) {
    Walk walk;
    if (auto r = walkCabinet(cab, walk); !r) {
        return std::unexpected(r.error());
    }
    return std::move(walk.names);
}

Result<std::string> cabinetFile(std::span<const std::uint8_t> cab, std::string_view name, std::size_t maxBytes) {
    std::string out;
    Walk walk;
    walk.wanted = std::string(name);
    std::ranges::transform(walk.wanted, walk.wanted.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    walk.out = &out;
    walk.max = maxBytes;
    if (auto r = walkCabinet(cab, walk); !r) {
        return std::unexpected(r.error());
    }
    if (!walk.found) {
        return fail(ErrorCode::NotFound, L"the file is not in the cabinet", utf8::toWide(name));
    }
    return out;
}

} // namespace wl::core
