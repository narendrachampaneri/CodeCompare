// Unit tests for the CodeCompare core engine (no external framework needed).
#include "Diff.h"
#include "FolderCompare.h"
#include "IgnoreRules.h"
#include "Report.h"
#include "TextDecode.h"

#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

using namespace cc;

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            ++g_failed;                                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n";    \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        ++g_checks;                                                                       \
        auto va_ = (a);                                                                   \
        auto vb_ = (b);                                                                   \
        if (!(va_ == vb_)) {                                                              \
            ++g_failed;                                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b \
                      << "  (" << va_ << " vs " << vb_ << ")\n";                          \
        }                                                                                 \
    } while (0)

// ---------------------------------------------------------------- helpers

static void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o << content;
}

static int lcsLength(const std::vector<int>& a, const std::vector<int>& b) {
    std::vector<std::vector<int>> dp(a.size() + 1, std::vector<int>(b.size() + 1, 0));
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j)
            dp[i][j] = a[i - 1] == b[j - 1] ? dp[i - 1][j - 1] + 1 : std::max(dp[i - 1][j], dp[i][j - 1]);
    return dp[a.size()][b.size()];
}

// Validates that ops transform a into b. Returns number of non-equal ops, or -1.
static int validateOps(const std::vector<EditOp>& ops, const std::vector<int>& a, const std::vector<int>& b) {
    int i = 0, j = 0, edits = 0;
    for (const EditOp& op : ops) {
        switch (op.type) {
        case OpType::Equal:
            if (op.a != i || op.b != j) return -1;
            if (a[static_cast<std::size_t>(i)] != b[static_cast<std::size_t>(j)]) return -1;
            ++i; ++j;
            break;
        case OpType::Delete:
            if (op.a != i) return -1;
            ++i; ++edits;
            break;
        case OpType::Insert:
            if (op.b != j) return -1;
            ++j; ++edits;
            break;
        }
    }
    if (i != static_cast<int>(a.size()) || j != static_cast<int>(b.size())) return -1;
    return edits;
}

static TextFile textOf(const std::string& s) { return decodeText(s); }

// ---------------------------------------------------------------- tests

static void testGlob() {
    IgnoreRules r({"*.o", "build/", "/out", "src/*.tmp", "docs/**/*.pdf", ".git"}, true);
    CHECK(r.isExcluded("a.o", "a.o", false));
    CHECK(r.isExcluded("x/y/a.O", "a.O", false));          // case-insensitive
    CHECK(!r.isExcluded("a.oo", "a.oo", false));
    CHECK(r.isExcluded("x/build", "build", true));
    CHECK(!r.isExcluded("x/build", "build", false));        // dir-only rule
    CHECK(r.isExcluded("out", "out", true));
    CHECK(!r.isExcluded("x/out", "out", true));             // anchored
    CHECK(r.isExcluded("src/a.tmp", "a.tmp", false));
    CHECK(!r.isExcluded("src/sub/a.tmp", "a.tmp", false));  // '*' does not cross '/'
    CHECK(r.isExcluded("docs/a/b/x.pdf", "x.pdf", false));
    CHECK(r.isExcluded("docs/x.pdf", "x.pdf", false));      // '**/' matches zero folders
    CHECK(r.isExcluded(".git", ".git", true));
    CHECK(IgnoreRules::globMatch("[a-c]?.txt", "bx.txt", false, false));
    CHECK(!IgnoreRules::globMatch("[!a-c]?.txt", "bx.txt", false, false));
    auto list = IgnoreRules::parseList(" .git ; build/ ,*.o\n");
    CHECK_EQ(list.size(), std::size_t(3));
}

static void testDecode() {
    TextFile t = textOf("a\r\nb\nc");
    CHECK_EQ(t.lines.size(), std::size_t(3));
    CHECK(t.lines[0].eol == Eol::CRLF);
    CHECK(t.lines[1].eol == Eol::LF);
    CHECK(t.lines[2].eol == Eol::None);
    CHECK_EQ(t.eolStyle, std::string("Mixed"));
    CHECK_EQ(t.encoding, std::string("UTF-8"));

    TextFile e = textOf("");
    CHECK_EQ(e.lines.size(), std::size_t(0));

    TextFile one = textOf("x\n");
    CHECK_EQ(one.lines.size(), std::size_t(1));

    TextFile bom = textOf("\xEF\xBB\xBFhello\n");
    CHECK_EQ(bom.encoding, std::string("UTF-8 BOM"));
    CHECK_EQ(bom.lines[0].text, std::string("hello"));

    TextFile u16 = textOf(std::string("\xFF\xFEh\0i\0\n\0", 8));
    CHECK_EQ(u16.encoding, std::string("UTF-16 LE"));
    CHECK_EQ(u16.lines.size(), std::size_t(1));
    CHECK_EQ(u16.lines[0].text, std::string("hi"));
    CHECK(!u16.binary);

    TextFile ansi = textOf("caf\xE9 \x80\n");  // Windows-1252 é and €
    CHECK_EQ(ansi.encoding, std::string("ANSI (Windows-1252)"));
    CHECK_EQ(ansi.lines[0].text, std::string("caf\xC3\xA9 \xE2\x82\xAC"));

    TextFile bin = textOf(std::string("ab\0cd", 5));
    CHECK(bin.binary);

    TextFile cr = textOf("a\rb\r");
    CHECK_EQ(cr.eolStyle, std::string("CR"));
    CHECK_EQ(cr.lines.size(), std::size_t(2));
}

static void testLineKey() {
    CompareOptions o;
    TextLine a{"  int  x = 1;  ", Eol::LF};
    TextLine b{"int x = 1;", Eol::CRLF};
    CHECK(lineKey(a, o) != lineKey(b, o));
    o.whitespace = WhitespaceMode::IgnoreAll;
    CHECK(lineKey(a, o) != lineKey(b, o));  // EOL still differs
    o.ignoreLineEndings = true;
    CHECK(lineKey(a, o) == lineKey(b, o));
    o.whitespace = WhitespaceMode::IgnoreChanges;
    CHECK(lineKey(TextLine{"a  b ", Eol::LF}, o) == lineKey(TextLine{"a b", Eol::LF}, o));
    CHECK(lineKey(TextLine{"ab", Eol::LF}, o) != lineKey(TextLine{"a b", Eol::LF}, o));
    o.whitespace = WhitespaceMode::IgnoreTrailing;
    CHECK(lineKey(TextLine{"a b\t ", Eol::LF}, o) == lineKey(TextLine{"a b", Eol::LF}, o));
    CHECK(lineKey(TextLine{" a b", Eol::LF}, o) != lineKey(TextLine{"a b", Eol::LF}, o));
    o.ignoreCase = true;
    CHECK(lineKey(TextLine{"ABC", Eol::LF}, o) == lineKey(TextLine{"abc", Eol::LF}, o));
}

static void testMyersFuzz() {
    std::mt19937 rng(12345);
    int cases = 0;
    for (int iter = 0; iter < 4000; ++iter) {
        const int alphabet = 1 + static_cast<int>(rng() % 6);
        const int n = static_cast<int>(rng() % 50);
        std::vector<int> a(static_cast<std::size_t>(n));
        for (int& x : a) x = static_cast<int>(rng() % static_cast<unsigned>(alphabet));
        // b = mutated copy of a (realistic) or fully random
        std::vector<int> b;
        if (rng() % 3 == 0) {
            const int m = static_cast<int>(rng() % 50);
            for (int k = 0; k < m; ++k) b.push_back(static_cast<int>(rng() % static_cast<unsigned>(alphabet)));
        } else {
            for (int x : a) {
                const unsigned r = rng() % 10;
                if (r == 0) continue;                                  // delete
                if (r == 1) b.push_back(static_cast<int>(rng() % 7)); // insert before
                b.push_back(x);
            }
        }
        std::vector<EditOp> ops = diffSequences(a, b);
        const int edits = validateOps(ops, a, b);
        const int minimal = static_cast<int>(a.size() + b.size()) - 2 * lcsLength(a, b);
        CHECK(edits >= 0);
        CHECK_EQ(edits, minimal);

        std::vector<char> blankA(a.size()), blankB(b.size());
        for (std::size_t k = 0; k < a.size(); ++k) blankA[k] = a[k] == 0;
        for (std::size_t k = 0; k < b.size(); ++k) blankB[k] = b[k] == 0;
        slideBlocks(ops, a, b, blankA, blankB);
        CHECK_EQ(validateOps(ops, a, b), minimal);
        ++cases;
    }
    // Large input sanity: 20k lines with scattered edits.
    std::vector<int> a(20000), b;
    for (int i = 0; i < 20000; ++i) a[static_cast<std::size_t>(i)] = i % 997 + (i / 997) * 1000;
    for (int i = 0; i < 20000; ++i) {
        if (i % 500 == 0) continue;
        if (i % 777 == 0) b.push_back(-i);
        b.push_back(a[static_cast<std::size_t>(i)]);
    }
    bool approx = true;
    auto ops = diffSequences(a, b, {}, &approx);
    CHECK(!approx);
    CHECK(validateOps(ops, a, b) >= 0);
    (void)cases;
}

static void testSlideReadable() {
    // Inserting a new function after an existing one: the inserted block should be
    // "void b() {", "}", "" rather than "}", "", "void b() {".
    const std::string left = "void a() {\n}\n\nvoid c() {\n}\n";
    const std::string right = "void a() {\n}\n\nvoid b() {\n}\n\nvoid c() {\n}\n";
    FileDiffResult r;
    r.left = textOf(left);
    r.right = textOf(right);
    CompareOptions o;
    diffTextFiles(r, o);
    CHECK_EQ(r.linesAdded, 3);
    CHECK_EQ(r.linesRemoved, 0);
    CHECK_EQ(r.hunkStarts.size(), std::size_t(1));
    const DiffRow& first = r.rows[static_cast<std::size_t>(r.hunkStarts[0])];
    CHECK(first.kind == RowKind::Inserted);
    CHECK_EQ(r.right.lines[static_cast<std::size_t>(first.right)].text, std::string("void b() {"));
}

static void testBlockAlignment() {
    // Similar lines should land on the same row even when an unrelated line was
    // inserted before them.
    FileDiffResult r;
    r.left = textOf("start\nint x = 1;\nfoo(1);\nend\n");
    r.right = textOf("start\nbar(a, b, c);\nint x = 2;\nfoo(2);\nend\n");
    diffTextFiles(r, CompareOptions{});
    CHECK_EQ(r.rows.size(), std::size_t(5));
    CHECK(r.rows[1].kind == RowKind::Inserted);
    CHECK(r.rows[2].kind == RowKind::Changed);
    CHECK_EQ(r.rows[2].left, 1);
    CHECK_EQ(r.rows[2].right, 2);
    CHECK(r.rows[3].kind == RowKind::Changed);
    CHECK_EQ(r.rows[3].left, 2);
    CHECK_EQ(r.rows[3].right, 3);
    CHECK_EQ(r.linesAdded, 3);
    CHECK_EQ(r.linesRemoved, 2);
}

static void testIntraLine() {
    std::vector<Span> l, r;
    CompareOptions o;
    intraLineDiff("int value = 10;", "int value = 20;", o, l, r);
    CHECK_EQ(l.size(), std::size_t(1));
    CHECK_EQ(r.size(), std::size_t(1));
    CHECK_EQ(l[0].begin, 12);
    CHECK_EQ(l[0].end, 14);
    intraLineDiff("foo(a, b)", "foo(a, b)", o, l, r);
    CHECK(l.empty() && r.empty());
    intraLineDiff("x = a+b", "x = c", o, l, r);
    CHECK_EQ(l.size(), std::size_t(1));
    CHECK_EQ(l[0].begin, 4);
    CHECK_EQ(l[0].end, 7);
}

static void testUnified() {
    FileDiffResult r;
    r.left = textOf("a\nb\nc\n");
    r.right = textOf("a\nB\nc\nd");
    CompareOptions o;
    diffTextFiles(r, o);
    const std::string u = unifiedDiff(r, "a/f", "b/f", 3);
    const std::string expected =
        "--- a/f\n+++ b/f\n@@ -1,3 +1,4 @@\n a\n-b\n+B\n c\n+d\n\\ No newline at end of file\n";
    CHECK_EQ(u, expected);

    FileDiffResult e;
    e.left = textOf("");
    e.right = textOf("x\n");
    diffTextFiles(e, o);
    CHECK_EQ(unifiedDiff(e, "a", "b", 3), std::string("--- a\n+++ b\n@@ -0,0 +1 @@\n+x\n"));
}

static void testFolderCompare() {
    std::mt19937 rng(static_cast<unsigned>(std::random_device{}()));
    const fs::path root = fs::temp_directory_path() / ("cc_test_" + std::to_string(rng()));
    const fs::path L = root / "left", R = root / "right";
    writeFile(L / "same.txt", "hello\n");
    writeFile(R / "same.txt", "hello\n");
    writeFile(L / "mod.c", "int a = 1;\n");
    writeFile(R / "mod.c", "int a = 2;\n");
    writeFile(L / "sub/deep/x.h", "x\n");
    writeFile(R / "sub/deep/x.h", "y\n");
    writeFile(L / "gone.txt", "bye\n");
    writeFile(R / "new.txt", "hi\n");
    writeFile(L / "eol.txt", "a\r\nb\r\n");
    writeFile(R / "eol.txt", "a\nb\n");
    writeFile(L / "bin.dat", std::string("\0\1\2\3", 4));
    writeFile(R / "bin.dat", std::string("\0\1\2\4", 4));
    writeFile(L / "build/out.o", "obj");
    writeFile(R / "build/out.o", "different obj");
    writeFile(L / "onlydir/f.txt", "z\n");
    writeFile(L / "conflict", "file\n");
    fs::create_directories(R / "conflict");
    writeFile(L / "samedir/s.txt", "s\n");
    writeFile(R / "samedir/s.txt", "s\n");

    CompareOptions o;
    o.excludePatterns = {"build/"};
    FolderCompareResult res = compareFolders(L, R, o);
    CHECK(res.fatalError.empty());

    auto find = [&](const std::string& p) -> const Entry* {
        for (const Entry& e : res.entries) if (e.displayPath == p) return &e;
        return nullptr;
    };
    auto statusOf = [&](const std::string& p) {
        const Entry* e = find(p);
        return e ? static_cast<int>(e->status) : -1;
    };
    CHECK_EQ(statusOf("same.txt"), static_cast<int>(EntryStatus::Identical));
    CHECK_EQ(statusOf("mod.c"), static_cast<int>(EntryStatus::Modified));
    CHECK_EQ(statusOf("sub/deep/x.h"), static_cast<int>(EntryStatus::Modified));
    CHECK_EQ(statusOf("sub/deep"), static_cast<int>(EntryStatus::Modified));
    CHECK_EQ(statusOf("sub"), static_cast<int>(EntryStatus::Modified));
    CHECK_EQ(statusOf("samedir"), static_cast<int>(EntryStatus::Identical));
    CHECK_EQ(statusOf("gone.txt"), static_cast<int>(EntryStatus::OnlyInLeft));
    CHECK_EQ(statusOf("new.txt"), static_cast<int>(EntryStatus::OnlyInRight));
    CHECK_EQ(statusOf("eol.txt"), static_cast<int>(EntryStatus::Modified));
    CHECK_EQ(statusOf("bin.dat"), static_cast<int>(EntryStatus::Modified));
    CHECK(find("bin.dat") && find("bin.dat")->binary);
    CHECK_EQ(find("bin.dat")->firstDiffOffset, std::int64_t(3));
    CHECK(find("build") == nullptr);
    CHECK(find("build/out.o") == nullptr);
    CHECK_EQ(statusOf("onlydir"), static_cast<int>(EntryStatus::OnlyInLeft));
    CHECK_EQ(statusOf("onlydir/f.txt"), static_cast<int>(EntryStatus::OnlyInLeft));
    CHECK_EQ(statusOf("conflict"), static_cast<int>(EntryStatus::TypeMismatch));

    // Tree order: folders before files, parents before children.
    CHECK(!res.entries.empty() && res.entries.front().isDir);
    for (std::size_t i = 0; i < res.entries.size(); ++i) {
        const std::string& p = res.entries[i].displayPath;
        const auto slash = p.rfind('/');
        if (slash == std::string::npos) continue;
        const std::string parent = p.substr(0, slash);
        bool parentBefore = false;
        for (std::size_t j = 0; j < i; ++j) if (res.entries[j].displayPath == parent) parentBefore = true;
        CHECK(parentBefore);
    }

    // Ignoring line endings makes eol.txt equivalent.
    o.ignoreLineEndings = true;
    FolderCompareResult res2 = compareFolders(L, R, o);
    for (const Entry& e : res2.entries)
        if (e.displayPath == "eol.txt") CHECK(e.status == EntryStatus::Equivalent);

    // File diff of a one-sided file shows all lines as inserted.
    FileDiffResult added = diffFiles(fs::path(), R / "new.txt", CompareOptions{});
    CHECK_EQ(added.linesAdded, 1);
    CHECK_EQ(added.linesRemoved, 0);

    FileDiffResult binDiff = diffFiles(L / "bin.dat", R / "bin.dat", CompareOptions{});
    CHECK(binDiff.left.binary);
    CHECK(binDiff.rows.empty());

    // Cancellation.
    std::atomic<bool> cancel{true};
    FolderCompareResult res3 = compareFolders(L, R, o, &cancel);
    CHECK(res3.cancelled);

    // Missing root.
    FolderCompareResult res4 = compareFolders(root / "nope", R, o);
    CHECK(!res4.fatalError.empty());

    std::error_code ec;
    fs::remove_all(root, ec);
}

int main() {
    testGlob();
    testDecode();
    testLineKey();
    testMyersFuzz();
    testSlideReadable();
    testBlockAlignment();
    testIntraLine();
    testUnified();
    testFolderCompare();
    if (g_failed) {
        std::cerr << g_failed << " of " << g_checks << " checks FAILED\n";
        return 1;
    }
    std::cout << "All " << g_checks << " checks passed\n";
    return 0;
}
