#include "FolderCompare.h"

#include "IgnoreRules.h"
#include "TextDecode.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <unordered_map>

namespace cc {

std::string toUtf8(const fs::path& p) {
    const auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

fs::path fromUtf8(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

const char* statusName(EntryStatus s) {
    switch (s) {
    case EntryStatus::Identical: return "Identical";
    case EntryStatus::Equivalent: return "Same (ignored diffs)";
    case EntryStatus::Modified: return "Modified";
    case EntryStatus::OnlyInLeft: return "Only in left";
    case EntryStatus::OnlyInRight: return "Only in right";
    case EntryStatus::TypeMismatch: return "File vs folder";
    case EntryStatus::Error: return "Error";
    }
    return "";
}

namespace {

struct ScanItem {
    fs::path rel;
    std::string display;
    std::string name;
    bool isDir = false;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    std::string error;
};

using ScanMap = std::unordered_map<std::string, ScanItem>;

std::string makeKey(const std::string& display, bool caseInsensitive) {
    if (!caseInsensitive) return display;
    std::string k = display;
    for (char& c : k) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return k;
}

std::int64_t toEpochSeconds(fs::file_time_type ft) {
    using namespace std::chrono;
    const auto sys = time_point_cast<system_clock::duration>(ft - fs::file_time_type::clock::now() +
                                                             system_clock::now());
    return duration_cast<seconds>(sys.time_since_epoch()).count();
}

bool isCancelled(const std::atomic<bool>* c) {
    return c && c->load(std::memory_order_relaxed);
}

void scanTree(const fs::path& root, const IgnoreRules& rules, bool caseInsensitive,
              const std::atomic<bool>* cancel, const ProgressFn& progress, std::size_t& counter,
              ScanMap& out) {
    struct Dir { fs::path abs; fs::path rel; std::string display; };
    std::vector<Dir> stack;
    stack.push_back({root, fs::path(), std::string()});

    while (!stack.empty()) {
        if (isCancelled(cancel)) return;
        Dir dir = std::move(stack.back());
        stack.pop_back();

        std::error_code ec;
        fs::directory_iterator it(dir.abs, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            if (!dir.display.empty()) {
                auto f = out.find(makeKey(dir.display, caseInsensitive));
                if (f != out.end()) f->second.error = "Cannot open folder: " + ec.message();
            }
            continue;
        }
        for (fs::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) break;
            const fs::directory_entry& de = *it;
            const fs::path fname = de.path().filename();
            ScanItem item;
            item.name = toUtf8(fname);
            item.display = dir.display.empty() ? item.name : dir.display + "/" + item.name;
            item.rel = dir.rel / fname;

            std::error_code e1, e2;
            const bool symlink = de.is_symlink(e2);
            item.isDir = de.is_directory(e1);   // follows symlinks
            if (rules.isExcluded(item.display, item.name, item.isDir)) continue;

            if (!item.isDir) {
                std::error_code e3;
                item.size = de.file_size(e3);
                if (e3) {
                    if (de.is_regular_file(e1)) item.error = "Cannot read size: " + e3.message();
                    else item.error = symlink ? "Broken link or special file" : "Special file";
                }
            }
            std::error_code e4;
            const auto t = de.last_write_time(e4);
            if (!e4) item.mtime = toEpochSeconds(t);

            if (item.isDir && !symlink) {
                stack.push_back({de.path(), item.rel, item.display});
            }
            const std::string key = makeKey(item.display, caseInsensitive);
            out.emplace(key, std::move(item));

            if (progress && (++counter % 256) == 0) {
                Progress p;
                p.phase = Progress::Phase::Scanning;
                p.done = counter;
                p.current = out.at(key).display;
                progress(p);
            }
        }
    }
}

bool startsWithUtf16Bom(const char* d, std::size_t n) {
    if (n < 2) return false;
    const auto a = static_cast<unsigned char>(d[0]);
    const auto b = static_cast<unsigned char>(d[1]);
    return (a == 0xFF && b == 0xFE) || (a == 0xFE && b == 0xFF);
}

bool headIsBinary(const char* d, std::size_t n) {
    return !startsWithUtf16Bom(d, n) && looksBinary(d, n);
}

bool readHeadBinary(const fs::path& p, bool& binary, std::uint64_t& bytesRead) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    char buf[8000];
    in.read(buf, sizeof(buf));
    const auto got = static_cast<std::size_t>(in.gcount());
    bytesRead += got;
    binary = headIsBinary(buf, got);
    return true;
}

// Exact streamed comparison of two files. Returns false on I/O error.
bool compareBytes(const fs::path& a, const fs::path& b, bool& equal, std::int64_t& firstDiff,
                  bool& binaryA, bool& binaryB, std::uint64_t& bytesRead, std::string& error,
                  const std::atomic<bool>* cancel) {
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa) { error = "Cannot open left file"; return false; }
    if (!fb) { error = "Cannot open right file"; return false; }
    constexpr std::size_t kChunk = 256 * 1024;
    std::vector<char> ba(kChunk), bb(kChunk);
    std::int64_t offset = 0;
    bool first = true;
    equal = true;
    for (;;) {
        if (isCancelled(cancel)) { error = "Cancelled"; return false; }
        fa.read(ba.data(), kChunk);
        fb.read(bb.data(), kChunk);
        const auto na = static_cast<std::size_t>(fa.gcount());
        const auto nb = static_cast<std::size_t>(fb.gcount());
        bytesRead += na + nb;
        if (first) {
            binaryA = headIsBinary(ba.data(), na);
            binaryB = headIsBinary(bb.data(), nb);
            first = false;
        }
        const std::size_t n = na < nb ? na : nb;
        if (std::memcmp(ba.data(), bb.data(), n) != 0) {
            std::size_t i = 0;
            while (i < n && ba[i] == bb[i]) ++i;
            firstDiff = offset + static_cast<std::int64_t>(i);
            equal = false;
            return true;
        }
        if (na != nb) {
            firstDiff = offset + static_cast<std::int64_t>(n);
            equal = false;
            return true;
        }
        if (na < kChunk) break;  // both reached EOF
        offset += static_cast<std::int64_t>(n);
    }
    if (fa.bad() || fb.bad()) { error = "Read error"; return false; }
    return true;
}

bool equivalentText(const fs::path& a, const fs::path& b, const CompareOptions& opt,
                    std::uint64_t& bytesRead) {
    const TextFile ta = loadTextFile(a, opt.maxTextDiffBytes);
    const TextFile tb = loadTextFile(b, opt.maxTextDiffBytes);
    bytesRead += ta.size + tb.size;
    if (!ta.ok || !tb.ok || ta.binary || tb.binary || ta.tooLarge || tb.tooLarge) return false;
    if (ta.lines.size() != tb.lines.size()) return false;
    for (std::size_t i = 0; i < ta.lines.size(); ++i) {
        if (lineKey(ta.lines[i], opt) != lineKey(tb.lines[i], opt)) return false;
    }
    return true;
}

struct SortKey {
    std::vector<std::string> comps;   // case-folded components
    std::vector<std::string> raw;
    bool isDir;
};

SortKey makeSortKey(const Entry& e) {
    SortKey k;
    k.isDir = e.isDir;
    std::size_t start = 0;
    const std::string& s = e.displayPath;
    while (start <= s.size()) {
        std::size_t slash = s.find('/', start);
        if (slash == std::string::npos) slash = s.size();
        std::string part = s.substr(start, slash - start);
        k.raw.push_back(part);
        for (char& c : part) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        k.comps.push_back(std::move(part));
        start = slash + 1;
    }
    return k;
}

bool sortLess(const SortKey& x, const SortKey& y) {
    const std::size_t n = std::min(x.comps.size(), y.comps.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (x.comps[i] == y.comps[i] && x.raw[i] == y.raw[i]) continue;
        const bool xDir = (i + 1 < x.comps.size()) || x.isDir;
        const bool yDir = (i + 1 < y.comps.size()) || y.isDir;
        if (xDir != yDir) return xDir;   // folders first
        if (x.comps[i] != y.comps[i]) return x.comps[i] < y.comps[i];
        return x.raw[i] < y.raw[i];
    }
    return x.comps.size() < y.comps.size();  // parent before child
}

} // namespace

FolderCompareResult compareFolders(const fs::path& left, const fs::path& right,
                                   const CompareOptions& options,
                                   const std::atomic<bool>* cancel, const ProgressFn& progress) {
    const auto t0 = std::chrono::steady_clock::now();
    FolderCompareResult res;
    res.leftRoot = left;
    res.rightRoot = right;
    res.options = options;

    std::error_code ec;
    if (!fs::is_directory(left, ec)) {
        res.fatalError = "Left folder does not exist or is not a folder: " + toUtf8(left);
        return res;
    }
    if (!fs::is_directory(right, ec)) {
        res.fatalError = "Right folder does not exist or is not a folder: " + toUtf8(right);
        return res;
    }

    const IgnoreRules rules(options.excludePatterns, options.caseInsensitivePaths);
    ScanMap leftMap, rightMap;
    std::size_t counter = 0;
    scanTree(left, rules, options.caseInsensitivePaths, cancel, progress, counter, leftMap);
    scanTree(right, rules, options.caseInsensitivePaths, cancel, progress, counter, rightMap);
    if (isCancelled(cancel)) { res.cancelled = true; return res; }

    // ---- merge both sides ----
    std::vector<Entry>& entries = res.entries;
    entries.reserve(leftMap.size() + rightMap.size() / 4);
    for (auto& kv : leftMap) {
        Entry e;
        e.key = kv.first;
        e.relPath = kv.second.rel;
        e.displayPath = kv.second.display;
        e.name = kv.second.name;
        e.inLeft = true;
        e.leftIsDir = kv.second.isDir;
        e.sizeLeft = kv.second.size;
        e.mtimeLeft = kv.second.mtime;
        e.error = kv.second.error;
        auto r = rightMap.find(kv.first);
        if (r != rightMap.end()) {
            e.inRight = true;
            e.rightIsDir = r->second.isDir;
            e.sizeRight = r->second.size;
            e.mtimeRight = r->second.mtime;
            if (e.error.empty()) e.error = r->second.error;
            rightMap.erase(r);
        }
        entries.push_back(std::move(e));
    }
    for (auto& kv : rightMap) {
        Entry e;
        e.key = kv.first;
        e.relPath = kv.second.rel;
        e.displayPath = kv.second.display;
        e.name = kv.second.name;
        e.inRight = true;
        e.rightIsDir = kv.second.isDir;
        e.sizeRight = kv.second.size;
        e.mtimeRight = kv.second.mtime;
        e.error = kv.second.error;
        entries.push_back(std::move(e));
    }
    leftMap.clear();
    rightMap.clear();

    for (Entry& e : entries) {
        e.isDir = e.inLeft ? e.leftIsDir : e.rightIsDir;
        if (e.inLeft && e.inRight && e.leftIsDir != e.rightIsDir) e.isDir = false;
        e.depth = static_cast<int>(std::count(e.displayPath.begin(), e.displayPath.end(), '/'));
    }

    // ---- sort into tree order ----
    {
        std::vector<SortKey> keys;
        keys.reserve(entries.size());
        for (const Entry& e : entries) keys.push_back(makeSortKey(e));
        std::vector<std::size_t> order(entries.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&](std::size_t a, std::size_t b) { return sortLess(keys[a], keys[b]); });
        std::vector<Entry> sorted;
        sorted.reserve(entries.size());
        for (std::size_t i : order) sorted.push_back(std::move(entries[i]));
        entries.swap(sorted);
    }

    // ---- compare files ----
    std::size_t totalFiles = 0;
    for (const Entry& e : entries)
        if (e.inLeft && e.inRight && !e.leftIsDir && !e.rightIsDir) ++totalFiles;

    CompareSummary& sum = res.summary;
    std::size_t done = 0;
    for (Entry& e : entries) {
        if (isCancelled(cancel)) { res.cancelled = true; return res; }

        if (!e.inLeft) { e.status = EntryStatus::OnlyInRight; continue; }
        if (!e.inRight) { e.status = EntryStatus::OnlyInLeft; continue; }
        if (e.leftIsDir != e.rightIsDir) { e.status = EntryStatus::TypeMismatch; continue; }
        if (e.isDir) { e.status = e.error.empty() ? EntryStatus::Identical : EntryStatus::Error; continue; }

        if (progress) {
            Progress p;
            p.phase = Progress::Phase::Comparing;
            p.done = done;
            p.total = totalFiles;
            p.current = e.displayPath;
            progress(p);
        }
        ++done;

        if (!e.error.empty()) { e.status = EntryStatus::Error; continue; }

        const fs::path pa = left / e.relPath;
        const fs::path pb = right / e.relPath;
        const bool normalize = options.anyContentNormalization();

        if (e.sizeLeft != e.sizeRight && !normalize) {
            bool binA = false, binB = false;
            readHeadBinary(pa, binA, sum.bytesRead);
            readHeadBinary(pb, binB, sum.bytesRead);
            e.binary = binA || binB;
            e.status = EntryStatus::Modified;
            continue;
        }

        bool equal = false, binA = false, binB = false;
        std::string err;
        if (!compareBytes(pa, pb, equal, e.firstDiffOffset, binA, binB, sum.bytesRead, err, cancel)) {
            if (isCancelled(cancel)) { res.cancelled = true; return res; }
            e.status = EntryStatus::Error;
            e.error = err;
            continue;
        }
        e.binary = binA || binB;
        if (equal) {
            e.status = EntryStatus::Identical;
            e.firstDiffOffset = -1;
        } else if (normalize && !e.binary && equivalentText(pa, pb, options, sum.bytesRead)) {
            e.status = EntryStatus::Equivalent;
        } else {
            e.status = EntryStatus::Modified;
        }
    }

    // ---- folder status rolls up from children (children come after parents) ----
    {
        std::unordered_map<std::string, std::size_t> indexOf;
        indexOf.reserve(entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i) indexOf.emplace(entries[i].key, i);
        for (std::size_t i = entries.size(); i-- > 0;) {
            const Entry& e = entries[i];
            if (!isDifferent(e.status)) continue;
            const std::size_t slash = e.key.rfind('/');
            if (slash == std::string::npos) continue;
            auto p = indexOf.find(e.key.substr(0, slash));
            if (p == indexOf.end()) continue;
            Entry& parent = entries[p->second];
            if (parent.status == EntryStatus::Identical || parent.status == EntryStatus::Equivalent)
                parent.status = EntryStatus::Modified;
        }
    }

    for (const Entry& e : entries) {
        if (e.isDir) { ++sum.folders; continue; }
        ++sum.files;
        switch (e.status) {
        case EntryStatus::Identical: ++sum.identical; break;
        case EntryStatus::Equivalent: ++sum.equivalent; break;
        case EntryStatus::Modified: ++sum.modified; if (e.binary) ++sum.binaryDifferent; break;
        case EntryStatus::OnlyInLeft: ++sum.onlyLeft; break;
        case EntryStatus::OnlyInRight: ++sum.onlyRight; break;
        case EntryStatus::TypeMismatch: ++sum.typeMismatch; break;
        case EntryStatus::Error: ++sum.errors; break;
        }
    }
    sum.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (progress) {
        Progress p;
        p.phase = Progress::Phase::Done;
        p.done = p.total = totalFiles;
        progress(p);
    }
    return res;
}

} // namespace cc
