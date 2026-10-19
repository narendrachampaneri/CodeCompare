#include "TextDecode.h"

#include <cstring>
#include <fstream>

namespace cc {

namespace {

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool isValidUtf8(const unsigned char* s, std::size_t n) {
    std::size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        if (c < 0x80) { ++i; continue; }
        std::size_t len;
        std::uint32_t cp;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + len > n) return false;
        for (std::size_t k = 1; k < len; ++k) {
            if ((s[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000))
            return false;                       // overlong encoding
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += len;
    }
    return true;
}

// Windows-1252 bytes 0x80..0x9F. Undefined positions map to the C1 control code point.
const std::uint16_t kCp1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

std::string decodeCp1252(const unsigned char* s, std::size_t n) {
    std::string out;
    out.reserve(n + n / 8);
    for (std::size_t i = 0; i < n; ++i) {
        unsigned char c = s[i];
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0xA0) appendUtf8(out, kCp1252[c - 0x80]);
        else appendUtf8(out, c);
    }
    return out;
}

std::string decodeUtf16(const unsigned char* s, std::size_t n, bool bigEndian) {
    std::string out;
    out.reserve(n);
    std::size_t i = 0;
    auto unit = [&](std::size_t at) -> std::uint32_t {
        return bigEndian ? (std::uint32_t(s[at]) << 8) | s[at + 1]
                         : (std::uint32_t(s[at + 1]) << 8) | s[at];
    };
    while (i + 1 < n) {
        std::uint32_t u = unit(i);
        i += 2;
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n) {
            std::uint32_t u2 = unit(i);
            if (u2 >= 0xDC00 && u2 <= 0xDFFF) {
                i += 2;
                appendUtf8(out, 0x10000 + ((u - 0xD800) << 10) + (u2 - 0xDC00));
                continue;
            }
        }
        if (u >= 0xD800 && u <= 0xDFFF) u = 0xFFFD;  // lone surrogate
        appendUtf8(out, u);
    }
    if (i < n) appendUtf8(out, 0xFFFD);  // odd trailing byte
    return out;
}

void splitLines(const std::string& text, TextFile& tf) {
    bool seenLF = false, seenCRLF = false, seenCR = false;
    std::size_t start = 0;
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        char c = text[i];
        if (c == '\n' || c == '\r') {
            TextLine line;
            line.text.assign(text, start, i - start);
            if (c == '\r' && i + 1 < n && text[i + 1] == '\n') {
                line.eol = Eol::CRLF; seenCRLF = true; i += 2;
            } else if (c == '\r') {
                line.eol = Eol::CR; seenCR = true; i += 1;
            } else {
                line.eol = Eol::LF; seenLF = true; i += 1;
            }
            tf.lines.push_back(std::move(line));
            start = i;
        } else {
            ++i;
        }
    }
    if (start < n) {
        TextLine line;
        line.text.assign(text, start, n - start);
        line.eol = Eol::None;
        tf.lines.push_back(std::move(line));
    }
    int kinds = int(seenLF) + int(seenCRLF) + int(seenCR);
    if (kinds == 0) tf.eolStyle = "None";
    else if (kinds > 1) tf.eolStyle = "Mixed";
    else tf.eolStyle = seenCRLF ? "CRLF" : (seenLF ? "LF" : "CR");
}

inline bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

} // namespace

const char* eolName(Eol e) {
    switch (e) {
    case Eol::LF: return "LF";
    case Eol::CRLF: return "CRLF";
    case Eol::CR: return "CR";
    default: return "";
    }
}

bool looksBinary(const char* data, std::size_t size) {
    const std::size_t n = size < 8000 ? size : 8000;
    return std::memchr(data, 0, n) != nullptr;
}

TextFile decodeText(const std::string& bytes) {
    TextFile tf;
    tf.ok = true;
    tf.size = bytes.size();
    const auto* s = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t n = bytes.size();

    if (n >= 2 && s[0] == 0xFF && s[1] == 0xFE) {
        tf.encoding = "UTF-16 LE";
        splitLines(decodeUtf16(s + 2, n - 2, false), tf);
        return tf;
    }
    if (n >= 2 && s[0] == 0xFE && s[1] == 0xFF) {
        tf.encoding = "UTF-16 BE";
        splitLines(decodeUtf16(s + 2, n - 2, true), tf);
        return tf;
    }
    if (looksBinary(bytes.data(), n)) {
        tf.binary = true;
        tf.encoding = "Binary";
        return tf;
    }
    std::size_t skip = 0;
    if (n >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF) skip = 3;
    if (isValidUtf8(s + skip, n - skip)) {
        tf.encoding = skip ? "UTF-8 BOM" : "UTF-8";
        splitLines(bytes.substr(skip), tf);
    } else {
        tf.encoding = "ANSI (Windows-1252)";
        splitLines(decodeCp1252(s, n), tf);
    }
    return tf;
}

bool readAllBytes(const fs::path& path, std::string& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot open file";
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff len = in.tellg();
    in.seekg(0, std::ios::beg);
    if (len < 0) {
        error = "Cannot determine file size";
        return false;
    }
    out.resize(static_cast<std::size_t>(len));
    if (len > 0 && !in.read(&out[0], len)) {
        error = "Read error";
        return false;
    }
    return true;
}

TextFile loadTextFile(const fs::path& path, std::uint64_t maxBytes) {
    TextFile tf;
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        tf.error = "Cannot read file size: " + ec.message();
        return tf;
    }
    if (size > maxBytes) {
        tf.ok = true;
        tf.tooLarge = true;
        tf.size = size;
        return tf;
    }
    std::string bytes;
    std::string err;
    if (!readAllBytes(path, bytes, err)) {
        tf.error = err;
        return tf;
    }
    return decodeText(bytes);
}

std::string lineKey(const TextLine& line, const CompareOptions& opt) {
    std::string k;
    const std::string& t = line.text;
    k.reserve(t.size() + 2);
    switch (opt.whitespace) {
    case WhitespaceMode::Compare:
        k = t;
        break;
    case WhitespaceMode::IgnoreTrailing: {
        std::size_t e = t.size();
        while (e > 0 && isSpace(t[e - 1])) --e;
        k.assign(t, 0, e);
        break;
    }
    case WhitespaceMode::IgnoreChanges: {
        bool pendingSpace = false;
        for (char c : t) {
            if (isSpace(c)) { pendingSpace = true; continue; }
            if (pendingSpace) { k += ' '; pendingSpace = false; }
            k += c;
        }
        break; // trailing whitespace dropped (pendingSpace discarded)
    }
    case WhitespaceMode::IgnoreAll:
        for (char c : t) if (!isSpace(c)) k += c;
        break;
    }
    if (opt.ignoreCase) {
        for (char& c : k) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (!opt.ignoreLineEndings) {
        // Keep the terminator in the key so CRLF vs LF (and a missing final newline)
        // count as a change. Use bytes that cannot appear inside a decoded line.
        k += '\n';
        k += static_cast<char>('0' + static_cast<int>(line.eol));
    }
    // When ignoring line endings, a missing final newline is not a change either.
    return k;
}

} // namespace cc
