// CodeCompare - recursive folder comparison.
#pragma once

#include "Types.h"

#include <atomic>
#include <functional>
#include <string>

namespace cc {

struct Progress {
    enum class Phase { Scanning, Comparing, Done } phase = Phase::Scanning;
    std::size_t done = 0;
    std::size_t total = 0;      // 0 while scanning (unknown)
    std::string current;        // UTF-8 relative path being processed
};

using ProgressFn = std::function<void(const Progress&)>;

// Compare two folder trees.
//  * Files are matched by relative path (case-insensitive on Windows).
//  * Equal size -> exact streamed byte comparison (no hashing shortcuts, no sampling).
//  * Different size -> Modified (unless ignore options make the text equivalent).
//  * Excluded names/paths are skipped and folders matching an exclude are not entered.
// The function is thread-safe with respect to its arguments and may run on a worker thread.
FolderCompareResult compareFolders(const fs::path& left, const fs::path& right,
                                   const CompareOptions& options,
                                   const std::atomic<bool>* cancel = nullptr,
                                   const ProgressFn& progress = {});

// Convert a path to a UTF-8 std::string with '/' separators.
std::string toUtf8(const fs::path& p);

// Convert UTF-8 to a path (handles wide paths on Windows).
fs::path fromUtf8(const std::string& s);

} // namespace cc
