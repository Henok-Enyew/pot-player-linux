#include "PlaylistOps.h"
#include "MediaFiles.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

// Identity of an entry for duplicate detection.
QString entryKey(const QString &filename)
{
    const QString local = MediaFiles::localPath(filename);
    if (local.isEmpty())
        return filename;
    const QFileInfo info(local);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

// Compares known values; unknown (negative) values go last in either direction.
template <typename T>
int compareKnown(T a, T b, bool ascending)
{
    const bool knownA = a >= 0;
    const bool knownB = b >= 0;
    if (knownA != knownB)
        return knownA ? -1 : 1;
    if (!knownA || a == b)
        return 0;
    return (a < b) == ascending ? -1 : 1;
}

} // namespace

namespace PlaylistOps {

QList<Entry> fromMpv(const QVariantList &playlist)
{
    QList<Entry> entries;
    entries.reserve(playlist.size());
    for (const QVariant &item : playlist) {
        const QVariantMap map = item.toMap();
        Entry entry;
        entry.filename = map.value(QStringLiteral("filename")).toString();
        entry.title = map.value(QStringLiteral("title")).toString();
        entries.append(entry);
    }
    return entries;
}

QString displayName(const Entry &entry)
{
    if (!entry.title.isEmpty())
        return entry.title;
    const QString local = MediaFiles::localPath(entry.filename);
    return local.isEmpty() ? entry.filename : QFileInfo(local).fileName();
}

QList<int> sortOrder(const QList<Entry> &entries, SortKey key, bool ascending)
{
    QList<int> order(entries.size());
    std::iota(order.begin(), order.end(), 0);

    auto byPath = [&](int a, int b) {
        const int cmp = MediaFiles::naturalCompare(entries[a].filename, entries[b].filename);
        return ascending ? cmp : -cmp;
    };
    auto compare = [&](int a, int b) {
        int cmp = 0;
        switch (key) {
        case SortKey::Name:
            cmp = MediaFiles::naturalCompare(displayName(entries[a]), displayName(entries[b]));
            if (!ascending)
                cmp = -cmp;
            break;
        case SortKey::Duration:
            cmp = compareKnown(entries[a].duration, entries[b].duration, ascending);
            break;
        case SortKey::Size:
            cmp = compareKnown(entries[a].size, entries[b].size, ascending);
            break;
        case SortKey::Path:
            break;
        }
        // Ties (and the path sort itself) fall back to the path.
        return cmp != 0 ? cmp : byPath(a, b);
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return compare(a, b) < 0; });
    return order;
}

QList<std::pair<int, int>> movesForOrder(const QList<int> &order)
{
    // Place the entries front to back; everything before row i is final, so
    // each entry moves up from a row j > i, landing exactly at row i.
    QList<int> current(order.size());
    std::iota(current.begin(), current.end(), 0);
    QList<std::pair<int, int>> moves;
    for (int i = 0; i < order.size(); ++i) {
        const int j = static_cast<int>(current.indexOf(order[i], i));
        if (j < 0 || j == i)
            continue;
        moves.append({j, i});
        current.move(j, i);
    }
    return moves;
}

QList<int> duplicateRows(const QList<Entry> &entries, int keepRow)
{
    QStringList keys;
    keys.reserve(entries.size());
    for (const Entry &entry : entries)
        keys.append(entryKey(entry.filename));

    QHash<QString, int> kept; // key -> the row that stays
    if (keepRow >= 0 && keepRow < keys.size())
        kept.insert(keys[keepRow], keepRow);
    for (int row = 0; row < keys.size(); ++row) {
        if (!kept.contains(keys[row]))
            kept.insert(keys[row], row);
    }

    QList<int> rows;
    for (int row = 0; row < keys.size(); ++row) {
        if (kept.value(keys[row]) != row)
            rows.append(row);
    }
    return rows;
}

QList<int> missingRows(const QList<Entry> &entries)
{
    QList<int> rows;
    for (int row = 0; row < entries.size(); ++row) {
        const QString local = MediaFiles::localPath(entries[row].filename);
        if (local.isEmpty())
            continue;
        const QFileInfo info(local);
        if (!info.exists() || !info.isReadable())
            rows.append(row);
    }
    return rows;
}

bool writeM3u(const QString &path, const QList<Entry> &entries, QString *error)
{
    QString text = QStringLiteral("#EXTM3U\n");
    for (const Entry &entry : entries) {
        const QString local = MediaFiles::localPath(entry.filename);
        const long long seconds = entry.duration >= 0 ? std::llround(entry.duration) : -1;
        QString title = entry.title;
        if (title.isEmpty())
            title = local.isEmpty() ? entry.filename : QFileInfo(local).completeBaseName();
        // A line break in the title would end the #EXTINF line early.
        title.replace(QLatin1Char('\n'), QLatin1Char(' ')).remove(QLatin1Char('\r'));
        text += QStringLiteral("#EXTINF:%1,%2\n").arg(seconds).arg(title);
        text += (local.isEmpty() ? entry.filename : QFileInfo(local).absoluteFilePath()) + QLatin1Char('\n');
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace PlaylistOps
