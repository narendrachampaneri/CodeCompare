// CodeCompare - conversions between QString and std::filesystem / UTF-8.
#pragma once

#include <QDir>
#include <QString>

#include <filesystem>
#include <string>

inline std::filesystem::path toFsPath(const QString& s) {
#ifdef _WIN32
    return std::filesystem::path(QDir::toNativeSeparators(s).toStdWString());
#else
    return std::filesystem::path(s.toStdString());
#endif
}

inline QString fromFsPath(const std::filesystem::path& p) {
#ifdef _WIN32
    return QString::fromStdWString(p.wstring());
#else
    return QString::fromStdString(p.string());
#endif
}

inline QString qUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

inline QString humanSize(quint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + QStringLiteral(" B");
    const char* units[] = {"KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = -1;
    do { v /= 1024.0; ++u; } while (v >= 1024.0 && u < 3);
    return QString::number(v, 'f', v < 10 ? 1 : 0) + QLatin1Char(' ') + QLatin1String(units[u]);
}
