#include "ZipArchive.h"

#include <QtEndian>

#include <zlib.h>

namespace {

constexpr quint32 kLocalHeader = 0x04034b50;
constexpr quint32 kCentralHeader = 0x02014b50;
constexpr quint32 kEndOfDirectory = 0x06054b50;
// Unpacked entries larger than this are refused (a subtitle is far smaller).
constexpr quint32 kMaxEntrySize = 64 * 1024 * 1024;

struct CentralEntry {
    QString name;
    quint16 method = 0;
    quint32 compressedSize = 0;
    quint32 size = 0;
    quint32 localOffset = 0;
};

quint16 u16(const QByteArray &data, qsizetype at)
{
    return qFromLittleEndian<quint16>(data.constData() + at);
}

quint32 u32(const QByteArray &data, qsizetype at)
{
    return qFromLittleEndian<quint32>(data.constData() + at);
}

std::optional<QList<CentralEntry>> centralDirectory(const QByteArray &zip)
{
    // The end-of-directory record sits in the last 22 bytes plus a comment.
    const qsizetype lowest = std::max<qsizetype>(0, zip.size() - 22 - 0xFFFF);
    qsizetype end = -1;
    for (qsizetype at = zip.size() - 22; at >= lowest; --at) {
        if (u32(zip, at) == kEndOfDirectory) {
            end = at;
            break;
        }
    }
    if (end < 0)
        return std::nullopt;
    const quint16 count = u16(zip, end + 10);
    qsizetype at = u32(zip, end + 16);
    QList<CentralEntry> entries;
    for (int i = 0; i < count; ++i) {
        if (at + 46 > zip.size() || u32(zip, at) != kCentralHeader)
            return std::nullopt;
        CentralEntry entry;
        entry.method = u16(zip, at + 10);
        entry.compressedSize = u32(zip, at + 20);
        entry.size = u32(zip, at + 24);
        const quint16 nameLength = u16(zip, at + 28);
        const quint16 extraLength = u16(zip, at + 30);
        const quint16 commentLength = u16(zip, at + 32);
        entry.localOffset = u32(zip, at + 42);
        if (at + 46 + nameLength > zip.size())
            return std::nullopt;
        entry.name = QString::fromUtf8(zip.mid(at + 46, nameLength));
        entries.append(entry);
        at += 46 + nameLength + extraLength + commentLength;
    }
    return entries;
}

std::optional<QByteArray> inflateRaw(const QByteArray &input, quint32 size)
{
    if (size > kMaxEntrySize)
        return std::nullopt;
    QByteArray output(static_cast<qsizetype>(size), Qt::Uninitialized);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return std::nullopt;
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END || stream.total_out != size)
        return std::nullopt;
    return output;
}

} // namespace

namespace ZipArchive {

bool isZip(const QByteArray &data)
{
    return data.size() >= 4 && u32(data, 0) == kLocalHeader;
}

QStringList fileNames(const QByteArray &zip)
{
    QStringList names;
    if (const auto entries = centralDirectory(zip)) {
        for (const CentralEntry &entry : *entries) {
            if (!entry.name.endsWith(QLatin1Char('/')))
                names.append(entry.name);
        }
    }
    return names;
}

std::optional<Entry> extract(const QByteArray &zip, const std::function<bool(const QString &)> &accept)
{
    const auto entries = centralDirectory(zip);
    if (!entries)
        return std::nullopt;
    for (const CentralEntry &entry : *entries) {
        if (entry.name.endsWith(QLatin1Char('/')) || !accept(entry.name))
            continue;
        const qsizetype local = entry.localOffset;
        if (local + 30 > zip.size() || u32(zip, local) != kLocalHeader)
            return std::nullopt;
        const qsizetype dataStart = local + 30 + u16(zip, local + 26) + u16(zip, local + 28);
        if (dataStart + entry.compressedSize > zip.size())
            return std::nullopt;
        const QByteArray packed = zip.mid(dataStart, entry.compressedSize);
        std::optional<QByteArray> data;
        if (entry.method == 0)
            data = packed;
        else if (entry.method == 8)
            data = inflateRaw(packed, entry.size);
        if (!data)
            return std::nullopt;
        return Entry{entry.name, *data};
    }
    return std::nullopt;
}

} // namespace ZipArchive
