// CodeCompare - line and word diff engine.
//
// Uses Myers' O(ND) algorithm in linear space (middle-snake bisection), the same
// family of algorithm used by git and GNU diff, so results are minimal edit scripts.
// Change blocks are then slid to more readable positions (e.g. a new C function
// is shown as "void f() { ... }" + blank line, not "}" + blank + "void f() {").
#pragma once

#include "Types.h"

#include <atomic>
#include <chrono>
#include <vector>

namespace cc {

enum class OpType { Equal, Delete, Insert };

struct EditOp {
    OpType type;
    int a;  // index in sequence A (Equal/Delete), -1 otherwise
    int b;  // index in sequence B (Equal/Insert), -1 otherwise
};

struct DiffLimits {
    // Stop refining and fall back to a coarser (still correct) diff after this time.
    std::chrono::milliseconds timeBudget{8000};
    const std::atomic<bool>* cancel = nullptr;
};

// Minimal edit script between two sequences of interned ids.
// Always returns a valid script (applying it to A yields B). If the time budget
// runs out, the remaining region is reported as delete+insert and *approximate=true.
std::vector<EditOp> diffSequences(const std::vector<int>& a, const std::vector<int>& b,
                                  const DiffLimits& limits = {}, bool* approximate = nullptr);

// Slide pure insert/delete blocks to readable positions. blankA/blankB flag lines
// that are empty or whitespace-only.
void slideBlocks(std::vector<EditOp>& ops, const std::vector<int>& a, const std::vector<int>& b,
                 const std::vector<char>& blankA, const std::vector<char>& blankB);

// Intra-line highlight: word/punctuation level differences between two lines.
void intraLineDiff(const std::string& left, const std::string& right, const CompareOptions& opt,
                   std::vector<Span>& leftSpans, std::vector<Span>& rightSpans);

// Full text diff of two decoded files.
void diffTextFiles(FileDiffResult& result, const CompareOptions& opt, const DiffLimits& limits = {});

// Load two files from disk and diff them. Either path may be empty (file absent on that side).
FileDiffResult diffFiles(const fs::path& left, const fs::path& right, const CompareOptions& opt,
                         const DiffLimits& limits = {});

} // namespace cc
