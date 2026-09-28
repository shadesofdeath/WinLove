#pragma once
// Random-access read-only bytes: a file on disk, or a file inside an ISO (UdfImage). Lets the WIM
// reader parse install.wim/esd straight out of an ISO without extracting or mounting it.
#include "base/Result.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace wl::core {

class ByteSource {
public:
    virtual ~ByteSource() = default;
    [[nodiscard]] virtual std::uint64_t size() const = 0;
    // Reads exactly out.size() bytes at `offset` or fails.
    [[nodiscard]] virtual Result<void> read(std::uint64_t offset, std::span<std::byte> out) const = 0;
};

class DiskFile final : public ByteSource {
public:
    [[nodiscard]] static Result<std::unique_ptr<DiskFile>> open(const std::filesystem::path& path);
    ~DiskFile() override;

    [[nodiscard]] std::uint64_t size() const override { return m_size; }
    [[nodiscard]] Result<void> read(std::uint64_t offset, std::span<std::byte> out) const override;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    std::uint64_t m_size = 0;
    std::filesystem::path m_path;
};

} // namespace wl::core
