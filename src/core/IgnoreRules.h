// CodeCompare - exclude/ignore patterns (gitignore-like subset).
#pragma once

#include <string>
#include <vector>

namespace cc {

// Supported syntax per pattern:
//   *.o        name glob, matches a file/folder name at any depth
//   build/     trailing '/' = matches folders only
//   /out       leading '/' = anchored to the compared root
//   src/*.tmp  a pattern containing '/' is matched against the whole relative path
//   **         matches across folder levels inside path patterns (e.g. docs/**/*.pdf)
//   ? [abc] [a-z] [!a]  usual glob classes
class IgnoreRules {
public:
    IgnoreRules() = default;
    IgnoreRules(const std::vector<std::string>& patterns, bool caseInsensitive);

    // relPath uses '/' separators, no leading '/'.
    bool isExcluded(const std::string& relPath, const std::string& name, bool isDir) const;

    bool empty() const { return rules_.empty(); }

    // Split a user string like ".git; build/ ; *.o" into patterns.
    static std::vector<std::string> parseList(const std::string& text);

    // Glob match of a whole string. If pathMode, '*' and '?' do not cross '/', '**' does.
    static bool globMatch(const std::string& pattern, const std::string& text,
                          bool pathMode, bool caseInsensitive);

private:
    struct Rule {
        std::string pattern;
        bool dirOnly = false;
        bool pathPattern = false;
    };
    std::vector<Rule> rules_;
    bool caseInsensitive_ = false;
};

} // namespace cc
