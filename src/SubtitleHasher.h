#pragma once

#include <QString>

// The OpenSubtitles movie hash, which identifies one exact release of a
// video so that subtitles made for it are in sync.
class SubtitleHasher
{
public:
    static constexpr qint64 kChunkSize = 64 * 1024;
    // Smaller files are not hashed; search them by name instead.
    static constexpr qint64 kMinimumSize = 2 * kChunkSize;

    // The file size plus the 64-bit little-endian words of the first and
    // last 64 KiB, as 16 hex digits. Empty for files smaller than 128 KiB
    // or that can't be read.
    static QString hash(const QString &path);
};
