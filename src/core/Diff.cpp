#include "Diff.h"

#include "TextDecode.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

namespace cc {

namespace {

using Clock = std::chrono::steady_clock;

struct Ctx {
    const int* A = nullptr;
    const int* B = nullptr;
    std::vector<char>* delA = nullptr;
    std::vector<char>* insB = nullptr;
    Clock::time_point deadline;
    const std::atomic<bool>* cancel = nullptr;
    bool timedOut = false;
    std::vector<int> v1, v2;

    bool expired() {
        if (timedOut) return true;
        if (Clock::now() > deadline || (cancel && cancel->load(std::memory_order_relaxed)))
            timedOut = true;
        return timedOut;
    }
};

// Myers middle snake (forward and reverse searches meet).
// On success returns the split point (sx, sy) inside the sub-problem.
bool bisect(Ctx& c, int aOff, int n, int bOff, int m, int& sx, int& sy) {
    const int* A = c.A + aOff;
    const int* B = c.B + bOff;
    const int maxD = (n + m + 1) / 2;
    const int vOffset = maxD;
    const int vLength = 2 * maxD + 2;
    c.v1.assign(static_cast<std::size_t>(vLength), -1);
    c.v2.assign(static_cast<std::size_t>(vLength), -1);
    int* v1 = c.v1.data();
    int* v2 = c.v2.data();
    v1[vOffset + 1] = 0;
    v2[vOffset + 1] = 0;
    const int delta = n - m;
    const bool front = (delta % 2 != 0);
    int k1start = 0, k1end = 0, k2start = 0, k2end = 0;

    for (int d = 0; d < maxD; ++d) {
        if (c.expired()) return false;

        for (int k1 = -d + k1start; k1 <= d - k1end; k1 += 2) {
            const int k1o = vOffset + k1;
            int x1;
            if (k1 == -d || (k1 != d && v1[k1o - 1] < v1[k1o + 1])) x1 = v1[k1o + 1];
            else x1 = v1[k1o - 1] + 1;
            int y1 = x1 - k1;
            while (x1 < n && y1 < m && A[x1] == B[y1]) { ++x1; ++y1; }
            v1[k1o] = x1;
            if (x1 > n) {
                k1end += 2;
            } else if (y1 > m) {
                k1start += 2;
            } else if (front) {
                const int k2o = vOffset + delta - k1;
                if (k2o >= 0 && k2o < vLength && v2[k2o] != -1) {
                    const int x2 = n - v2[k2o];
                    if (x1 >= x2) { sx = x1; sy = y1; return true; }
                }
            }
        }

        for (int k2 = -d + k2start; k2 <= d - k2end; k2 += 2) {
            const int k2o = vOffset + k2;
            int x2;
            if (k2 == -d || (k2 != d && v2[k2o - 1] < v2[k2o + 1])) x2 = v2[k2o + 1];
            else x2 = v2[k2o - 1] + 1;
            int y2 = x2 - k2;
            while (x2 < n && y2 < m && A[n - x2 - 1] == B[m - y2 - 1]) { ++x2; ++y2; }
            v2[k2o] = x2;
            if (x2 > n) {
                k2end += 2;
            } else if (y2 > m) {
                k2start += 2;
            } else if (!front) {
                const int k1o = vOffset + delta - k2;
                if (k1o >= 0 && k1o < vLength && v1[k1o] != -1) {
                    const int x1 = v1[k1o];
                    const int y1 = vOffset + x1 - k1o;
                    if (x1 >= n - x2) { sx = x1; sy = y1; return true; }
                }
            }
        }
    }
    return false;  // no common element at all
}

void markAll(Ctx& c, int aOff, int n, int bOff, int m) {
    for (int i = 0; i < n; ++i) (*c.delA)[static_cast<std::size_t>(aOff + i)] = 1;
    for (int j = 0; j < m; ++j) (*c.insB)[static_cast<std::size_t>(bOff + j)] = 1;
}

void solve(Ctx& c, int aOff0, int n0, int bOff0, int m0) {
    struct Job { int aOff, n, bOff, m; };
    std::vector<Job> stack;
    stack.push_back({aOff0, n0, bOff0, m0});
    while (!stack.empty()) {
        Job j = stack.back();
        stack.pop_back();
        int aOff = j.aOff, n = j.n, bOff = j.bOff, m = j.m;
        while (n > 0 && m > 0 && c.A[aOff] == c.B[bOff]) { ++aOff; ++bOff; --n; --m; }
        while (n > 0 && m > 0 && c.A[aOff + n - 1] == c.B[bOff + m - 1]) { --n; --m; }
        if (n == 0 || m == 0 || c.timedOut) {
            markAll(c, aOff, n, bOff, m);
            continue;
        }
        int sx = 0, sy = 0;
        if (!bisect(c, aOff, n, bOff, m, sx, sy) || (sx == 0 && sy == 0) || (sx == n && sy == m)) {
            markAll(c, aOff, n, bOff, m);
            continue;
        }
        stack.push_back({aOff + sx, n - sx, bOff + sy, m - sy});
        stack.push_back({aOff, sx, bOff, sy});
    }
}

// ---- block sliding ----

struct RunSlider {
    std::vector<EditOp>& ops;
    const std::vector<int>& seq;     // a for delete runs, b for insert runs
    const std::vector<char>& blank;
    bool isDelete;
    int s, e;                         // run [s, e)

    int idx(const EditOp& op) const { return isDelete ? op.a : op.b; }

    bool canUp() const {
        return s > 0 && ops[s - 1].type == OpType::Equal &&
               seq[idx(ops[s - 1])] == seq[idx(ops[e - 1])];
    }
    bool canDown() const {
        return e < static_cast<int>(ops.size()) && ops[e].type == OpType::Equal &&
               seq[idx(ops[s])] == seq[idx(ops[e])];
    }
    void up() {
        const EditOp eq = ops[s - 1];
        const int p0 = idx(eq);
        const int other = isDelete ? eq.b : eq.a;
        const int len = e - s;
        for (int i = 0; i < len; ++i) {
            ops[s - 1 + i] = isDelete ? EditOp{OpType::Delete, p0 + i, -1}
                                      : EditOp{OpType::Insert, -1, p0 + i};
        }
        ops[e - 1] = isDelete ? EditOp{OpType::Equal, p0 + len, other}
                              : EditOp{OpType::Equal, other, p0 + len};
        --s; --e;
    }
    void down() {
        const EditOp eq = ops[e];
        const int other = isDelete ? eq.b : eq.a;
        const int p = idx(ops[s]);
        const int len = e - s;
        ops[s] = isDelete ? EditOp{OpType::Equal, p, other} : EditOp{OpType::Equal, other, p};
        for (int i = 1; i <= len; ++i) {
            ops[s + i] = isDelete ? EditOp{OpType::Delete, p + i, -1}
                                  : EditOp{OpType::Insert, -1, p + i};
        }
        ++s; ++e;
    }
    bool lastIsBlank() const { return blank[static_cast<std::size_t>(idx(ops[e - 1]))] != 0; }

    void run() {
        while (canUp()) up();
        while (canDown()) down();
        const int lowest = s;
        if (lastIsBlank()) return;
        bool found = false;
        while (canUp()) {
            up();
            if (lastIsBlank()) { found = true; break; }
        }
        if (!found) while (s < lowest) down();
    }
};

// ---- tokens for intra-line diff ----

enum class TokClass { Word, Space, Punct };

struct Token {
    int begin, end;
    TokClass cls;
};

inline TokClass classify(unsigned char c) {
    if (c == ' ' || c == '\t' || c == '\v' || c == '\f') return TokClass::Space;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80)
        return TokClass::Word;
    return TokClass::Punct;
}

std::vector<Token> tokenize(const std::string& s) {
    std::vector<Token> out;
    const int n = static_cast<int>(s.size());
    int i = 0;
    while (i < n) {
        const TokClass cls = classify(static_cast<unsigned char>(s[i]));
        int j = i + 1;
        if (cls != TokClass::Punct) {
            while (j < n && classify(static_cast<unsigned char>(s[j])) == cls) ++j;
        }
        out.push_back({i, j, cls});
        i = j;
    }
    return out;
}

// Sorted non-whitespace tokens of a line, for similarity scoring.
std::vector<std::string> tokenBag(const std::string& s) {
    std::vector<std::string> bag;
    for (const Token& t : tokenize(s)) {
        if (t.cls == TokClass::Space) continue;
        bag.emplace_back(s, static_cast<std::size_t>(t.begin), static_cast<std::size_t>(t.end - t.begin));
    }
    std::sort(bag.begin(), bag.end());
    return bag;
}

// Dice coefficient of two token multisets, in [0, 1].
double similarity(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;
    std::size_t i = 0, j = 0, common = 0;
    while (i < a.size() && j < b.size()) {
        const int c = a[i].compare(b[j]);
        if (c == 0) { ++common; ++i; ++j; }
        else if (c < 0) ++i;
        else ++j;
    }
    return 2.0 * static_cast<double>(common) / static_cast<double>(a.size() + b.size());
}

// Arrange one change block (deleted lines `dels` and inserted lines `ins`) into
// side-by-side rows. Similar lines are aligned on the same row (weighted LCS on a
// token-similarity score); the remaining lines in each gap are paired in order.
// Returns (left, right) line pairs, -1 = no line on that side.
std::vector<std::pair<int, int>> alignBlock(const TextFile& L, const TextFile& R,
                                            const std::vector<int>& dels, const std::vector<int>& ins) {
    std::vector<std::pair<int, int>> out;
    const std::size_t n = dels.size(), m = ins.size();

    // Matched (k, l) index pairs into dels/ins, in increasing order.
    std::vector<std::pair<std::size_t, std::size_t>> anchors;
    if (n > 1 && m > 1 && n * m <= 40000) {
        std::vector<std::vector<std::string>> bl(n), br(m);
        for (std::size_t k = 0; k < n; ++k) bl[k] = tokenBag(L.lines[static_cast<std::size_t>(dels[k])].text);
        for (std::size_t l = 0; l < m; ++l) br[l] = tokenBag(R.lines[static_cast<std::size_t>(ins[l])].text);
        constexpr double kThreshold = 0.5;
        std::vector<double> sim(n * m);
        for (std::size_t k = 0; k < n; ++k)
            for (std::size_t l = 0; l < m; ++l) sim[k * m + l] = similarity(bl[k], br[l]);
        std::vector<double> dp((n + 1) * (m + 1), 0.0);
        auto at = [&](std::size_t k, std::size_t l) -> double& { return dp[k * (m + 1) + l]; };
        for (std::size_t k = 1; k <= n; ++k) {
            for (std::size_t l = 1; l <= m; ++l) {
                double best = std::max(at(k - 1, l), at(k, l - 1));
                const double s = sim[(k - 1) * m + (l - 1)];
                if (s >= kThreshold) best = std::max(best, at(k - 1, l - 1) + s);
                at(k, l) = best;
            }
        }
        std::size_t k = n, l = m;
        while (k > 0 && l > 0) {
            const double s = sim[(k - 1) * m + (l - 1)];
            if (s >= kThreshold && at(k, l) == at(k - 1, l - 1) + s) {
                anchors.emplace_back(k - 1, l - 1);
                --k; --l;
            } else if (at(k, l) == at(k - 1, l)) {
                --k;
            } else {
                --l;
            }
        }
        std::reverse(anchors.begin(), anchors.end());
    }
    anchors.emplace_back(n, m);  // sentinel

    std::size_t k = 0, l = 0;
    for (const auto& a : anchors) {
        // Gap before the anchor: pair in order, then leftovers.
        while (k < a.first && l < a.second) out.emplace_back(dels[k++], ins[l++]);
        while (k < a.first) out.emplace_back(dels[k++], -1);
        while (l < a.second) out.emplace_back(-1, ins[l++]);
        if (a.first < n && a.second < m) out.emplace_back(dels[k++], ins[l++]);
    }
    return out;
}

} // namespace

std::vector<EditOp> diffSequences(const std::vector<int>& a, const std::vector<int>& b,
                                  const DiffLimits& limits, bool* approximate) {
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());
    std::vector<char> delA(a.size(), 0), insB(b.size(), 0);
    Ctx c;
    c.A = a.data();
    c.B = b.data();
    c.delA = &delA;
    c.insB = &insB;
    c.deadline = Clock::now() + limits.timeBudget;
    c.cancel = limits.cancel;
    solve(c, 0, n, 0, m);
    if (approximate) *approximate = c.timedOut;

    std::vector<EditOp> ops;
    ops.reserve(static_cast<std::size_t>(n + m));
    int i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && delA[static_cast<std::size_t>(i)]) {
            ops.push_back({OpType::Delete, i++, -1});
        } else if (j < m && insB[static_cast<std::size_t>(j)]) {
            ops.push_back({OpType::Insert, -1, j++});
        } else {
            ops.push_back({OpType::Equal, i++, j++});
        }
    }
    return ops;
}

void slideBlocks(std::vector<EditOp>& ops, const std::vector<int>& a, const std::vector<int>& b,
                 const std::vector<char>& blankA, const std::vector<char>& blankB) {
    const int size = static_cast<int>(ops.size());
    int i = 0;
    while (i < size) {
        if (ops[i].type == OpType::Equal) { ++i; continue; }
        int e = i;
        bool hasDel = false, hasIns = false;
        while (e < size && ops[e].type != OpType::Equal) {
            if (ops[e].type == OpType::Delete) hasDel = true; else hasIns = true;
            ++e;
        }
        if (hasDel && hasIns) { i = e; continue; }
        RunSlider r{ops, hasDel ? a : b, hasDel ? blankA : blankB, hasDel, i, e};
        r.run();
        i = r.e;
    }
}

void intraLineDiff(const std::string& left, const std::string& right, const CompareOptions& opt,
                   std::vector<Span>& leftSpans, std::vector<Span>& rightSpans) {
    leftSpans.clear();
    rightSpans.clear();
    const bool skipSpace = opt.whitespace != WhitespaceMode::Compare;

    std::vector<Token> lt = tokenize(left), rt = tokenize(right);
    if (skipSpace) {
        auto drop = [](std::vector<Token>& v) {
            std::vector<Token> o;
            o.reserve(v.size());
            for (const Token& t : v) if (t.cls != TokClass::Space) o.push_back(t);
            v.swap(o);
        };
        drop(lt);
        drop(rt);
    }

    std::unordered_map<std::string, int> ids;
    auto keyOf = [&](const std::string& s, const Token& t) {
        std::string k = s.substr(static_cast<std::size_t>(t.begin), static_cast<std::size_t>(t.end - t.begin));
        if (opt.ignoreCase)
            for (char& ch : k) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        auto it = ids.emplace(std::move(k), static_cast<int>(ids.size())).first;
        return it->second;
    };
    std::vector<int> la, rb;
    la.reserve(lt.size());
    rb.reserve(rt.size());
    for (const Token& t : lt) la.push_back(keyOf(left, t));
    for (const Token& t : rt) rb.push_back(keyOf(right, t));

    DiffLimits lim;
    lim.timeBudget = std::chrono::milliseconds(100);
    const std::vector<EditOp> ops = diffSequences(la, rb, lim, nullptr);

    auto addSpan = [](std::vector<Span>& spans, const Token& t) {
        if (!spans.empty() && spans.back().end >= 0 && spans.back().end == t.begin) {
            spans.back().end = t.end;
        } else {
            spans.push_back({t.begin, t.end});
        }
    };
    // Group consecutive changed tokens (in token order) into one span, so the
    // whitespace between two changed words is highlighted too.
    int lastL = -2, lastR = -2;
    for (const EditOp& op : ops) {
        if (op.type == OpType::Delete) {
            const Token& t = lt[static_cast<std::size_t>(op.a)];
            if (lastL == op.a - 1 && !leftSpans.empty()) leftSpans.back().end = t.end;
            else addSpan(leftSpans, t);
            lastL = op.a;
        } else if (op.type == OpType::Insert) {
            const Token& t = rt[static_cast<std::size_t>(op.b)];
            if (lastR == op.b - 1 && !rightSpans.empty()) rightSpans.back().end = t.end;
            else addSpan(rightSpans, t);
            lastR = op.b;
        }
    }
}

void diffTextFiles(FileDiffResult& r, const CompareOptions& opt, const DiffLimits& limits) {
    r.rows.clear();
    r.hunkStarts.clear();
    r.linesAdded = r.linesRemoved = 0;

    if (r.left.binary || r.right.binary) {
        r.message = "Binary file - line comparison is not available.";
        return;
    }
    if (r.left.tooLarge || r.right.tooLarge) {
        r.message = "File is too large for a line-by-line comparison.";
        return;
    }

    std::unordered_map<std::string, int> ids;
    auto intern = [&](const TextFile& tf, std::vector<int>& seq, std::vector<char>& blank) {
        seq.reserve(tf.lines.size());
        blank.reserve(tf.lines.size());
        for (const TextLine& l : tf.lines) {
            auto it = ids.emplace(lineKey(l, opt), static_cast<int>(ids.size())).first;
            seq.push_back(it->second);
            bool isBlank = true;
            for (char ch : l.text)
                if (ch != ' ' && ch != '\t' && ch != '\v' && ch != '\f') { isBlank = false; break; }
            blank.push_back(isBlank ? 1 : 0);
        }
    };
    std::vector<int> a, b;
    std::vector<char> blankA, blankB;
    intern(r.left, a, blankA);
    intern(r.right, b, blankB);

    bool approx = false;
    std::vector<EditOp> ops = diffSequences(a, b, limits, &approx);
    r.approximate = approx;
    if (!approx) slideBlocks(ops, a, b, blankA, blankB);

    const std::size_t n = ops.size();
    r.rows.reserve(n);
    std::size_t i = 0;
    std::vector<int> dels, ins;
    while (i < n) {
        if (ops[i].type == OpType::Equal) {
            DiffRow row;
            row.kind = RowKind::Equal;
            row.left = ops[i].a;
            row.right = ops[i].b;
            r.rows.push_back(std::move(row));
            ++i;
            continue;
        }
        dels.clear();
        ins.clear();
        while (i < n && ops[i].type != OpType::Equal) {
            if (ops[i].type == OpType::Delete) dels.push_back(ops[i].a);
            else ins.push_back(ops[i].b);
            ++i;
        }
        r.hunkStarts.push_back(static_cast<int>(r.rows.size()));
        r.linesRemoved += static_cast<int>(dels.size());
        r.linesAdded += static_cast<int>(ins.size());
        for (const auto& pr : alignBlock(r.left, r.right, dels, ins)) {
            DiffRow row;
            row.left = pr.first;
            row.right = pr.second;
            if (row.left >= 0 && row.right >= 0) {
                row.kind = RowKind::Changed;
                const std::string& lt = r.left.lines[static_cast<std::size_t>(row.left)].text;
                const std::string& rt = r.right.lines[static_cast<std::size_t>(row.right)].text;
                if (lt.size() + rt.size() <= 20000)
                    intraLineDiff(lt, rt, opt, row.leftSpans, row.rightSpans);
            } else {
                row.kind = row.left >= 0 ? RowKind::Deleted : RowKind::Inserted;
            }
            r.rows.push_back(std::move(row));
        }
    }
    r.identicalUnderOptions = r.hunkStarts.empty();
    if (approx)
        r.message = "The comparison took too long; some change blocks are shown coarsely.";
}

FileDiffResult diffFiles(const fs::path& left, const fs::path& right, const CompareOptions& opt,
                         const DiffLimits& limits) {
    FileDiffResult r;
    auto load = [&](const fs::path& p, TextFile& tf) {
        if (p.empty()) {
            tf.ok = true;  // absent side = empty file
            return;
        }
        tf = loadTextFile(p, opt.maxTextDiffBytes);
    };
    load(left, r.left);
    load(right, r.right);
    if (!r.left.ok || !r.right.ok) {
        r.message = "Cannot read file: " + (!r.left.ok ? r.left.error : r.right.error);
        return r;
    }
    diffTextFiles(r, opt, limits);
    return r;
}

} // namespace cc
