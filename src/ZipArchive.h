#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Reads files out of a ZIP archive held in memory: enough for the subtitle
// archives that subtitle sites hand out (stored or deflated entries).
namespace ZipArchive {

struct Entry {
    QString name;
    QByteArray data;
};

bool isZip(const QByteArray &data);
// The names of the files in the archive; empty if it can't be read.
QStringList fileNames(const QByteArray &zip);
// The first file for which `accept(name)` is true, unpacked.
std::optional<Entry> extract(const QByteArray &zip, const std::function<bool(const QString &)> &accept);

} // namespace ZipArchive
