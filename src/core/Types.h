// CodeCompare - core data types shared by the engine, CLI and GUI.
// The core layer is plain C++17 (no Qt) so it can be unit-tested and reused.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cc {

namespace fs = std::filesystem;

// How whitespace differences are treated when comparing lines.
enum class WhitespaceMode {
    Compare,        // every whitespace character matters (default, exact)
    IgnoreTrailing, // ignore whitespace at the end of a line
    IgnoreChanges,  // treat runs of whitespace as one space, ignore trailing (git -b)
    IgnoreAll       // ignore all whitespace (git -w)
};

struct CompareOptions {
    WhitespaceMode whitespace = WhitespaceMode::Compare;
    bool ignoreCase = false;
    bool ignoreLineEndings = false;  // CRLF vs LF vs CR treated as equal

    // Exclude patterns, gitignore-like:
    //   "name" / "*.o"   -> matches a file or folder name at any depth
    //   "dir/sub"        -> pattern containing '/' is matched against the relative path
    std::vector<std::string> excludePatterns;

    // Text files larger than this are only compared byte-wise (no line diff).
    std::uint64_t maxTextDiffBytes = 64ull * 1024 * 1024;

#ifdef _WIN32
    bool caseInsensitivePaths = true;   // Windows file system semantics
#else
    bool caseInsensitivePaths = false;
#endif

    bool anyContentNormalization() const {
        return whitespace != WhitespaceMode::Compare || ignoreCase || ignoreLineEndings;
    }
};

enum class EntryStatus {
    Identical,      // byte-for-byte equal
    Equivalent,     // bytes differ, but equal under the active ignore options
    Modified,       // content differs
    OnlyInLeft,     // exists only in the left folder (deleted)
    OnlyInRight,    // exists only in the right folder (added)
    TypeMismatch,   // a file on one side, a folder on the other
    Error           // could not be read
};

const char* statusName(EntryStatus s);

inline bool isDifferent(EntryStatus s) {
    return s != EntryStatus::Identical && s != EntryStatus::Equivalent;
}

struct Entry {
    std::string key;        // normalized relative path used for matching (generic '/')
    fs::path relPath;       // relative path as it exists on disk (left side preferred)
    std::string displayPath;// UTF-8 relative path for display, '/' separators
    std::string name;       // UTF-8 last path component
    int depth = 0;          // number of '/' in displayPath

    bool isDir = false;
    bool inLeft = false;
    bool inRight = false;
    bool leftIsDir = false;
    bool rightIsDir = false;

    std::uint64_t sizeLeft = 0;
    std::uint64_t sizeRight = 0;
    std::int64_t mtimeLeft = 0;   // seconds since epoch (0 = unknown)
    std::int64_t mtimeRight = 0;

    EntryStatus status = EntryStatus::Identical;
    bool binary = false;          // at least one side detected as binary
    std::int64_t firstDiffOffset = -1; // for byte-different files, first differing byte
    std::string error;
};

struct CompareSummary {
    std::size_t files = 0;
    std::size_t folders = 0;
    std::size_t identical = 0;
    std::size_t equivalent = 0;
    std::size_t modified = 0;
    std::size_t onlyLeft = 0;
    std::size_t onlyRight = 0;
    std::size_t typeMismatch = 0;
    std::size_t errors = 0;
    std::size_t binaryDifferent = 0;
    std::uint64_t bytesRead = 0;
    double seconds = 0.0;
};

struct FolderCompareResult {
    fs::path leftRoot;
    fs::path rightRoot;
    CompareOptions options;
    std::vector<Entry> entries;   // sorted: parents before children, folders before files
    CompareSummary summary;
    bool cancelled = false;
    std::string fatalError;       // set if a root could not be opened
};

// ---------- File (line) diff ----------

enum class RowKind { Equal, Changed, Deleted, Inserted };

// Byte range [begin, end) inside a UTF-8 line that differs.
struct Span {
    int begin = 0;
    int end = 0;
};

// One row of the aligned side-by-side view.
struct DiffRow {
    RowKind kind = RowKind::Equal;
    int left = -1;   // 0-based line index in left file, -1 = gap
    int right = -1;  // 0-based line index in right file, -1 = gap
    std::vector<Span> leftSpans;   // intra-line highlights (Changed rows only)
    std::vector<Span> rightSpans;
};

enum class Eol { None, LF, CRLF, CR };

struct TextLine {
    std::string text;   // UTF-8, without line terminator
    Eol eol = Eol::None;
};

struct TextFile {
    bool ok = false;
    bool binary = false;
    bool tooLarge = false;
    std::string error;
    std::string encoding;  // "UTF-8", "UTF-8 BOM", "UTF-16 LE", "UTF-16 BE", "ANSI (Windows-1252)", "ASCII"
    std::string eolStyle;  // "LF", "CRLF", "CR", "Mixed", "None"
    std::uint64_t size = 0;
    std::vector<TextLine> lines;
};

struct FileDiffResult {
    TextFile left;
    TextFile right;
    std::vector<DiffRow> rows;
    std::vector<int> hunkStarts;   // row index where each change block begins
    int linesAdded = 0;
    int linesRemoved = 0;
    bool identicalUnderOptions = false;
    bool approximate = false;      // diff hit the time limit and fell back
    std::string message;           // e.g. why no line diff is available
};

} // namespace cc
