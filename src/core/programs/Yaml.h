#pragma once
// The YAML winget writes (D-078): its merged manifests and version lists, read into a JSON tree
// (nlohmann::json) — mappings, sequences (also "key:" followed by "- item" at the same column),
// plain / 'single' / "double" quoted scalars over several lines (folded as YAML folds them),
// block scalars (| |- > >-), comments, empty flow collections ([] {}). Every scalar is a string.
// Not a general YAML reader: anchors, tags, flow collections with content and multi-document
// streams are refused (ParseError), never guessed at.
#include "base/Result.h"

#include <json.hpp>

#include <string_view>

namespace wl::core {

[[nodiscard]] Result<nlohmann::json> parseYaml(std::string_view text);

} // namespace wl::core
