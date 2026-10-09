#include "core/system/Hash.h"

#include "base/Encoding.h"
#include "base/Text.h"
#include "core/system/Files.h"

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <cwctype>
#include <fstream>
#include <optional>
#include <vector>

namespace wl::core {

std::wstring normalizeSha256(std::wstring_view text) {
    std::wstring t = wl::text::lower(text);
    if (const auto colon = t.find(L':'); colon != std::wstring::npos) {
        t = t.substr(colon + 1); // "SHA256: …"
    }
    std::wstring hex;
    for (const wchar_t c : t) {
        if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')) {
            hex.push_back(c);
        } else if (c != L' ' && c != L'-' && c != L'\t' && c != L'\r' && c != L'\n') {
            return {};
        }
    }
    return hex.size() == 64 ? hex : std::wstring();
}

namespace {

template <std::size_t N>
Result<std::wstring> hashFile(const std::filesystem::path& file, LPCWSTR algorithm, const TaskContext& task) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, algorithm, nullptr, 0))) {
        return fail(ErrorCode::Unknown, L"hash provider unavailable", L"bcrypt");
    }
    if (!BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return fail(ErrorCode::Unknown, L"hash object", L"bcrypt");
    }
    std::ifstream in(file, std::ios::binary);
    const std::uint64_t size = treeBytes(file); // progress only
    std::vector<char> buffer(4 * 1024 * 1024);
    std::uint64_t done = 0;
    Result<std::wstring> result = fail(ErrorCode::IoError, L"cannot read", file.wstring());
    bool ok = static_cast<bool>(in);
    while (ok && in) {
        if (task.cancel.cancelled()) {
            ok = false;
            result = fail(ErrorCode::Cancelled, L"hash cancelled", file.wstring());
            break;
        }
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<ULONG>(in.gcount());
        if (got == 0) {
            break;
        }
        if (!BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), got, 0))) {
            ok = false;
            result = fail(ErrorCode::Unknown, L"hash update failed", file.wstring());
            break;
        }
        done += got;
        task.report(size ? static_cast<double>(done) / static_cast<double>(size) : 1.0, L"sha256");
    }
    if (ok && in.bad()) { // a read error mid-file must not yield the hash of a truncated file
        ok = false;
        result = fail(ErrorCode::IoError, L"read error while hashing", file.wstring());
    }
    std::array<UCHAR, N> digest{};
    if (ok && !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0))) {
        ok = false;
        result = fail(ErrorCode::Unknown, L"hash finish failed", file.wstring());
    }
    if (ok) {
        result = hexLower(digest);
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

} // namespace

Result<std::wstring> sha256File(const std::filesystem::path& file, const TaskContext& task) {
    return hashFile<32>(file, BCRYPT_SHA256_ALGORITHM, task);
}

Result<std::wstring> sha1File(const std::filesystem::path& file, const TaskContext& task) {
    return hashFile<20>(file, BCRYPT_SHA1_ALGORITHM, task);
}

} // namespace wl::core
