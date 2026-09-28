#pragma once
// Read-only UDF reader for Windows installation ISOs (ECMA-167 / OSTA UDF 1.02–2.01, Type 1
// partition maps). No mounting, no admin: WinLove lists and extracts ISO files by itself.
// UDF 2.50+ metadata partitions (Type 2 maps) are rejected with ErrorCode::Unsupported.
#include "core/io/ByteSource.h"
#include "core/tasks/Task.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

class UdfImage {
public:
    struct Extent {
        std::uint64_t offset; // absolute byte offset in the ISO
        std::uint64_t length;
    };
    struct Node {
        std::wstring name;
        bool directory = false;
        std::uint64_t size = 0;
        std::vector<Extent> extents; // file data, or directory data for directories
        std::vector<std::byte> embedded; // data stored inside the file entry (small files)
    };

    [[nodiscard]] static Result<UdfImage> open(std::shared_ptr<const ByteSource> iso);
    [[nodiscard]] static Result<UdfImage> open(const std::filesystem::path& isoPath);

    // Path relative to the root, '/' or '\' separated, case-insensitive: "sources/install.wim".
    [[nodiscard]] Result<Node> find(std::wstring_view path) const;
    [[nodiscard]] Result<std::vector<Node>> list(std::wstring_view directoryPath) const;
    // A ByteSource view of a file inside the image (for WimFile, hashing, ...).
    [[nodiscard]] std::shared_ptr<const ByteSource> openFile(Node file) const;
    [[nodiscard]] Result<void> extract(const Node& file, const std::filesystem::path& destination,
                                       const TaskContext& task) const;
    // Copies the whole image into `destination` (created if needed). Progress is by bytes across
    // all files. Existing files of the same size are kept (resumable after a cancel).
    [[nodiscard]] Result<void> extractAll(const std::filesystem::path& destination, const TaskContext& task) const;

    [[nodiscard]] std::wstring_view volumeLabel() const noexcept { return m_label; }

private:
    [[nodiscard]] Result<Node> readFileEntry(std::uint32_t logicalBlock, std::wstring name) const;
    [[nodiscard]] Result<std::vector<Node>> readDirectory(const Node& directory) const;
    [[nodiscard]] Result<std::vector<std::byte>> readAll(const Node& node) const;
    [[nodiscard]] Result<void> copyNode(const Node& file, const std::filesystem::path& destination, const TaskContext& task,
                                        std::uint64_t doneBefore, std::uint64_t total) const;
    [[nodiscard]] std::uint64_t blockOffset(std::uint32_t logicalBlock) const noexcept;

    std::shared_ptr<const ByteSource> m_iso;
    std::uint32_t m_partitionStart = 0; // physical sector of partition 0
    std::uint32_t m_rootBlock = 0;      // logical block of the root directory file entry
    std::wstring m_label;
};

} // namespace wl::core
