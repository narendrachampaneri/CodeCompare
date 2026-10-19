#include "IgnoreRules.h"

#include <cctype>

namespace cc {

namespace {

inline char fold(char c, bool ci) {
    if (ci && c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Match a bracket expression starting at p[pi] == '['. Returns true and advances pi
// past ']' if the class matches c. If the bracket is malformed, treats '[' literally.
bool matchClass(const std::string& p, std::size_t& pi, char c, bool ci, bool& valid) {
    std::size_t i = pi + 1;
    bool negate = false;
    if (i < p.size() && (p[i] == '!' || p[i] == '^')) { negate = true; ++i; }
    bool matched = false;
    bool first = true;
    const char fc = fold(c, ci);
    while (i < p.size() && (first || p[i] != ']')) {
        first = false;
        char lo = fold(p[i], ci);
        char hi = lo;
        if (i + 2 < p.size() && p[i + 1] == '-' && p[i + 2] != ']') {
            hi = fold(p[i + 2], ci);
            i += 3;
        } else {
            i += 1;
        }
        if (fc >= lo && fc <= hi) matched = true;
    }
    if (i >= p.size()) { valid = false; return false; }  // no closing ']'
    valid = true;
    pi = i + 1;
    return matched != negate;
}

bool globImpl(const std::string& p, std::size_t pi, const std::string& t, std::size_t ti,
              bool pathMode, bool ci) {
    while (pi < p.size()) {
        const char pc = p[pi];
        if (pc == '*') {
            const bool doubleStar = pathMode && pi + 1 < p.size() && p[pi + 1] == '*';
            if (doubleStar) {
                std::size_t next = pi + 2;
                // "**/" may also match zero folders
                if (next < p.size() && p[next] == '/') {
                    if (globImpl(p, next + 1, t, ti, pathMode, ci)) return true;
                }
                for (std::size_t k = ti; k <= t.size(); ++k)
                    if (globImpl(p, next, t, k, pathMode, ci)) return true;
                return false;
            }
            std::size_t next = pi + 1;
            for (std::size_t k = ti; k <= t.size(); ++k) {
                if (globImpl(p, next, t, k, pathMode, ci)) return true;
                if (k < t.size() && pathMode && t[k] == '/') break;
            }
            return false;
        }
        if (ti >= t.size()) return false;
        if (pc == '?') {
            if (pathMode && t[ti] == '/') return false;
            ++pi; ++ti;
            continue;
        }
        if (pc == '[') {
            bool valid = false;
            std::size_t save = pi;
            bool m = matchClass(p, pi, t[ti], ci, valid);
            if (valid) {
                if (!m || (pathMode && t[ti] == '/')) return false;
                ++ti;
                continue;
            }
            pi = save; // literal '['
        }
        if (pc == '\\' && pi + 1 < p.size()) {
            ++pi;
        }
        if (fold(p[pi], ci) != fold(t[ti], ci)) return false;
        ++pi; ++ti;
    }
    return ti == t.size();
}

} // namespace

IgnoreRules::IgnoreRules(const std::vector<std::string>& patterns, bool caseInsensitive)
    : caseInsensitive_(caseInsensitive) {
    for (std::string raw : patterns) {
        for (char& c : raw) if (c == '\\') c = '/';
        raw = trim(raw);
        if (raw.empty() || raw[0] == '#') continue;
        Rule r;
        if (raw.size() > 1 && raw.back() == '/') { r.dirOnly = true; raw.pop_back(); }
        if (!raw.empty() && raw.front() == '/') { r.pathPattern = true; raw.erase(0, 1); }
        if (raw.find('/') != std::string::npos) r.pathPattern = true;
        if (raw.empty()) continue;
        r.pattern = raw;
        rules_.push_back(std::move(r));
    }
}

bool IgnoreRules::isExcluded(const std::string& relPath, const std::string& name, bool isDir) const {
    for (const Rule& r : rules_) {
        if (r.dirOnly && !isDir) continue;
        if (r.pathPattern) {
            if (globMatch(r.pattern, relPath, true, caseInsensitive_)) return true;
        } else {
            if (globMatch(r.pattern, name, false, caseInsensitive_)) return true;
        }
    }
    return false;
}

std::vector<std::string> IgnoreRules::parseList(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        std::string t = trim(cur);
        if (!t.empty()) out.push_back(t);
        cur.clear();
    };
    for (char c : text) {
        if (c == ';' || c == ',' || c == '\n' || c == '\r') flush();
        else cur += c;
    }
    flush();
    return out;
}

bool IgnoreRules::globMatch(const std::string& pattern, const std::string& text,
                            bool pathMode, bool caseInsensitive) {
    return globImpl(pattern, 0, text, 0, pathMode, caseInsensitive);
}

} // namespace cc
