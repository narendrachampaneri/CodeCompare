// CodeCompare - text output of comparison results (unified diff, summaries).
#pragma once

#include "Types.h"

#include <string>

namespace cc {

// Unified diff (git/patch compatible) of one file result.
std::string unifiedDiff(const FileDiffResult& r, const std::string& leftLabel,
                        const std::string& rightLabel, int context = 3);

// One-line human readable summary, e.g. "120 files: 3 modified, 1 added, 2 deleted".
std::string summaryLine(const CompareSummary& s);

} // namespace cc
