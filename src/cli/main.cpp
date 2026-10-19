// codecompare-cli - command-line front end of the CodeCompare engine.
// Exit code: 0 = no differences, 1 = differences found, 2 = error (like diff/cmp).

#include "Diff.h"
#include "FolderCompare.h"
#include "IgnoreRules.h"
#include "Report.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

void usage() {
    std::cout <<
        "CodeCompare CLI - compare two folders (or two files) line by line\n\n"
        "Usage: codecompare-cli [options] <left> <right>\n\n"
        "Options:\n"
        "  --diff                 print a unified diff for every modified text file\n"
        "  --all                  also list identical files\n"
        "  --numstat              print 'added<TAB>removed<TAB>path' per changed text file\n"
        "  -w, --ignore-all-space ignore all whitespace\n"
        "  -b, --ignore-space-change  ignore changes in amount of whitespace\n"
        "  --ignore-trailing-space    ignore whitespace at line end\n"
        "  --ignore-eol           ignore CRLF/LF differences\n"
        "  -i, --ignore-case      ignore letter case\n"
        "  --exclude <patterns>   ';'-separated patterns, e.g. \".git;build/;*.o\" (repeatable)\n"
        "  -U <n>                 context lines for --diff (default 3)\n"
        "  -h, --help             show this help\n\n"
        "Exit code: 0 = identical, 1 = differences, 2 = error\n";
}

std::vector<std::string> utf8Args(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    (void)argc; (void)argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(len > 0 ? len - 1 : 0), '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
        out.push_back(s);
    }
    LocalFree(w);
#else
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

const char* marker(cc::EntryStatus s) {
    switch (s) {
    case cc::EntryStatus::Identical: return "  ";
    case cc::EntryStatus::Equivalent: return "= ";
    case cc::EntryStatus::Modified: return "M ";
    case cc::EntryStatus::OnlyInLeft: return "D ";
    case cc::EntryStatus::OnlyInRight: return "A ";
    case cc::EntryStatus::TypeMismatch: return "T ";
    case cc::EntryStatus::Error: return "E ";
    }
    return "? ";
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const std::vector<std::string> args = utf8Args(argc, argv);
    cc::CompareOptions opt;
    bool showDiff = false, showAll = false, numstat = false;
    int context = 3;
    std::vector<std::string> paths;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--diff") showDiff = true;
        else if (a == "--all") showAll = true;
        else if (a == "--numstat") numstat = true;
        else if (a == "-w" || a == "--ignore-all-space") opt.whitespace = cc::WhitespaceMode::IgnoreAll;
        else if (a == "-b" || a == "--ignore-space-change") opt.whitespace = cc::WhitespaceMode::IgnoreChanges;
        else if (a == "--ignore-trailing-space") opt.whitespace = cc::WhitespaceMode::IgnoreTrailing;
        else if (a == "--ignore-eol") opt.ignoreLineEndings = true;
        else if (a == "-i" || a == "--ignore-case") opt.ignoreCase = true;
        else if (a == "--exclude" && i + 1 < args.size()) {
            for (auto& p : cc::IgnoreRules::parseList(args[++i])) opt.excludePatterns.push_back(p);
        } else if (a == "-U" && i + 1 < args.size()) {
            context = std::stoi(args[++i]);
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            return 2;
        } else {
            paths.push_back(a);
        }
    }
    if (paths.size() != 2) { usage(); return 2; }

    const cc::fs::path left = cc::fromUtf8(paths[0]);
    const cc::fs::path right = cc::fromUtf8(paths[1]);
    std::error_code ec;

    // Two files: plain file diff.
    if (cc::fs::is_regular_file(left, ec) && cc::fs::is_regular_file(right, ec)) {
        cc::FileDiffResult r = cc::diffFiles(left, right, opt);
        if (!r.message.empty() && r.rows.empty()) {
            std::cerr << r.message << "\n";
        }
        if (numstat && !r.hunkStarts.empty())
            std::cout << r.linesAdded << "\t" << r.linesRemoved << "\t" << paths[1] << "\n";
        else
            std::cout << cc::unifiedDiff(r, paths[0], paths[1], context);
        return r.hunkStarts.empty() ? 0 : 1;
    }

    cc::FolderCompareResult res = cc::compareFolders(left, right, opt);
    if (!res.fatalError.empty()) {
        std::cerr << res.fatalError << "\n";
        return 2;
    }

    bool anyDiff = false;
    for (const cc::Entry& e : res.entries) {
        if (cc::isDifferent(e.status)) anyDiff = true;
        if (e.isDir) continue;
        if (numstat) {
            if (e.status == cc::EntryStatus::Identical || e.status == cc::EntryStatus::Equivalent) continue;
            if (e.binary) { std::cout << "-\t-\t" << e.displayPath << "\n"; continue; }
            const cc::fs::path pa = e.inLeft ? left / e.relPath : cc::fs::path();
            const cc::fs::path pb = e.inRight ? right / e.relPath : cc::fs::path();
            cc::FileDiffResult r = cc::diffFiles(pa, pb, opt);
            if (r.left.binary || r.right.binary) { std::cout << "-\t-\t" << e.displayPath << "\n"; continue; }
            if (r.hunkStarts.empty() && e.inLeft && e.inRight) continue;  // only encoding/BOM differs
            std::cout << r.linesAdded << "\t" << r.linesRemoved << "\t" << e.displayPath << "\n";
            continue;
        }
        if (!showAll && !cc::isDifferent(e.status)) continue;
        std::cout << marker(e.status) << e.displayPath;
        if (e.binary && e.status == cc::EntryStatus::Modified) std::cout << "  [binary]";
        if (!e.error.empty()) std::cout << "  (" << e.error << ")";
        std::cout << "\n";

        if (showDiff && !e.binary &&
            (e.status == cc::EntryStatus::Modified || e.status == cc::EntryStatus::OnlyInLeft ||
             e.status == cc::EntryStatus::OnlyInRight)) {
            const cc::fs::path pa = e.inLeft ? left / e.relPath : cc::fs::path();
            const cc::fs::path pb = e.inRight ? right / e.relPath : cc::fs::path();
            cc::FileDiffResult r = cc::diffFiles(pa, pb, opt);
            if (r.rows.empty() && !r.message.empty()) {
                std::cout << "   " << r.message << "\n";
            } else {
                std::cout << cc::unifiedDiff(r, e.inLeft ? "a/" + e.displayPath : "/dev/null",
                                             e.inRight ? "b/" + e.displayPath : "/dev/null", context);
            }
        }
    }
    if (!numstat) std::cout << "\n" << cc::summaryLine(res.summary) << "\n";
    return anyDiff ? 1 : 0;
}
