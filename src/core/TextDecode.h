// CodeCompare - loading files as text: binary detection, encoding detection,
// line splitting with line-ending tracking. No byte is ever silently dropped:
// invalid UTF-8 falls back to Windows-1252, which maps every byte.
#pragma once

#include "Types.h"

#include <string>

namespace cc {

// Git's heuristic: a NUL byte in the first 8000 bytes means binary
// (UTF-16 files are recognised by their BOM first).
bool looksBinary(const char* data, std::size_t size);

// Decode raw bytes into lines. Never fails for text; sets binary=true for binary data.
TextFile decodeText(const std::string& bytes);

// Read and decode a file. Files bigger than maxBytes are not loaded (tooLarge=true).
TextFile loadTextFile(const fs::path& path, std::uint64_t maxBytes);

// Read an entire file into memory. Returns false and sets error on failure.
bool readAllBytes(const fs::path& path, std::string& out, std::string& error);

// Normalized comparison key for a line according to the options.
std::string lineKey(const TextLine& line, const CompareOptions& opt);

const char* eolName(Eol e);

} // namespace cc
