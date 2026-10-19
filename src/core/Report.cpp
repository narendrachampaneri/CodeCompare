#include "Report.h"

#include <algorithm>
#include <cstdio>

namespace cc {

namespace {

std::string range(int start, int count) {
    if (count == 1) return std::to_string(start);
    return std::to_string(start) + "," + std::to_string(count);
}

void emitLine(std::string& out, char prefix, const TextLine& l) {
    out += prefix;
    out += l.text;
    out += '\n';
    if (l.eol == Eol::None) out += "\\ No newline at end of file\n";
}

} // namespace

std::string unifiedDiff(const FileDiffResult& r, const std::string& leftLabel,
                        const std::string& rightLabel, int context) {
    std::string out;
    if (r.hunkStarts.empty()) return out;
    out += "--- " + leftLabel + "\n";
    out += "+++ " + rightLabel + "\n";

    const int nRows = static_cast<int>(r.rows.size());
    // Number of left/right lines before each row.
    std::vector<int> lb(static_cast<std::size_t>(nRows) + 1, 0), rb(static_cast<std::size_t>(nRows) + 1, 0);
    for (int i = 0; i < nRows; ++i) {
        lb[i + 1] = lb[i] + (r.rows[i].left >= 0 ? 1 : 0);
        rb[i + 1] = rb[i] + (r.rows[i].right >= 0 ? 1 : 0);
    }

    // Change blocks: [start, end) row ranges.
    std::vector<std::pair<int, int>> blocks;
    for (int s : r.hunkStarts) {
        int e = s;
        while (e < nRows && r.rows[e].kind != RowKind::Equal) ++e;
        blocks.emplace_back(s, e);
    }

    std::size_t bi = 0;
    while (bi < blocks.size()) {
        std::size_t bj = bi;
        while (bj + 1 < blocks.size() && blocks[bj + 1].first - blocks[bj].second <= 2 * context) ++bj;
        const int gs = std::max(0, blocks[bi].first - context);
        const int ge = std::min(nRows, blocks[bj].second + context);

        const int lc = lb[ge] - lb[gs];
        const int rc = rb[ge] - rb[gs];
        const int ls = lc > 0 ? lb[gs] + 1 : lb[gs];
        const int rs = rc > 0 ? rb[gs] + 1 : rb[gs];
        out += "@@ -" + range(ls, lc) + " +" + range(rs, rc) + " @@\n";

        int i = gs;
        while (i < ge) {
            if (r.rows[i].kind == RowKind::Equal) {
                emitLine(out, ' ', r.left.lines[static_cast<std::size_t>(r.rows[i].left)]);
                ++i;
                continue;
            }
            int e = i;
            while (e < ge && r.rows[e].kind != RowKind::Equal) ++e;
            for (int k = i; k < e; ++k)
                if (r.rows[k].left >= 0) emitLine(out, '-', r.left.lines[static_cast<std::size_t>(r.rows[k].left)]);
            for (int k = i; k < e; ++k)
                if (r.rows[k].right >= 0) emitLine(out, '+', r.right.lines[static_cast<std::size_t>(r.rows[k].right)]);
            i = e;
        }
        bi = bj + 1;
    }
    return out;
}

std::string summaryLine(const CompareSummary& s) {
    std::string out = std::to_string(s.files) + " files, " + std::to_string(s.folders) + " folders: ";
    out += std::to_string(s.modified) + " modified, ";
    out += std::to_string(s.onlyRight) + " added (only right), ";
    out += std::to_string(s.onlyLeft) + " deleted (only left), ";
    out += std::to_string(s.identical) + " identical";
    if (s.equivalent) out += ", " + std::to_string(s.equivalent) + " same after ignore rules";
    if (s.typeMismatch) out += ", " + std::to_string(s.typeMismatch) + " file/folder conflicts";
    if (s.errors) out += ", " + std::to_string(s.errors) + " errors";
    char buf[64];
    std::snprintf(buf, sizeof(buf), " (%.2f s)", s.seconds);
    out += buf;
    return out;
}

} // namespace cc
