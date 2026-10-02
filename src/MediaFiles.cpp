#include "MediaFiles.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QObject>
#include <QUrl>

#include <algorithm>

namespace {

const QStringList kVideoExtensions{
    QStringLiteral("3g2"), QStringLiteral("3gp"), QStringLiteral("asf"), QStringLiteral("avi"),
    QStringLiteral("divx"), QStringLiteral("f4v"), QStringLiteral("flv"), QStringLiteral("m2ts"),
    QStringLiteral("m4v"), QStringLiteral("mkv"), QStringLiteral("mov"), QStringLiteral("mp4"),
    QStringLiteral("mpeg"), QStringLiteral("mpg"), QStringLiteral("mts"), QStringLiteral("ogv"),
    QStringLiteral("rm"), QStringLiteral("rmvb"), QStringLiteral("ts"), QStringLiteral("vob"),
    QStringLiteral("webm"), QStringLiteral("wmv"),
};

const QStringList kAudioExtensions{
    QStringLiteral("aac"), QStringLiteral("ac3"), QStringLiteral("aif"), QStringLiteral("aiff"),
    QStringLiteral("alac"), QStringLiteral("ape"), QStringLiteral("dts"), QStringLiteral("flac"),
    QStringLiteral("m4a"), QStringLiteral("mka"), QStringLiteral("mp3"), QStringLiteral("mpc"),
    QStringLiteral("oga"), QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("tta"),
    QStringLiteral("wav"), QStringLiteral("wma"), QStringLiteral("wv"),
};

const QStringList kPlaylistExtensions{
    QStringLiteral("m3u"), QStringLiteral("m3u8"), QStringLiteral("pls"),
};

QString patterns(const QStringList &extensions)
{
    QStringList result;
    for (const QString &ext : extensions)
        result.append(QStringLiteral("*.") + ext);
    return result.join(QLatin1Char(' '));
}

QString suffix(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

// Case-insensitive order that compares runs of digits by value, for when
// QCollator can't (its numeric mode is unavailable in the C locale).
int fallbackCompare(const QString &a, const QString &b)
{
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            const qsizetype startA = i;
            const qsizetype startB = j;
            while (i < a.size() && a[i].isDigit())
                ++i;
            while (j < b.size() && b[j].isDigit())
                ++j;
            // Compare the numbers without leading zeros: shorter is smaller, else digit by digit.
            QStringView numA = QStringView(a).mid(startA, i - startA);
            QStringView numB = QStringView(b).mid(startB, j - startB);
            while (numA.size() > 1 && numA.front() == QLatin1Char('0'))
                numA = numA.mid(1);
            while (numB.size() > 1 && numB.front() == QLatin1Char('0'))
                numB = numB.mid(1);
            if (numA.size() != numB.size())
                return numA.size() < numB.size() ? -1 : 1;
            if (const int cmp = numA.compare(numB); cmp != 0)
                return cmp;
            continue;
        }
        const QChar ca = a[i].toCaseFolded();
        const QChar cb = b[j].toCaseFolded();
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    if (a.size() - i != b.size() - j)
        return a.size() - i < b.size() - j ? -1 : 1;
    return a.compare(b); // equal ignoring case and zeros: keep a stable, total order
}

// A numeric, case-insensitive collator for the user's locale, if the platform
// supports numeric collation there.
struct NaturalCollator {
    NaturalCollator()
    {
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setIgnorePunctuation(false);
        usable = collator.compare(QStringLiteral("Episode 2"), QStringLiteral("Episode 10")) < 0
            && collator.compare(QStringLiteral("track9"), QStringLiteral("track10")) < 0
            && collator.compare(QStringLiteral("a"), QStringLiteral("B")) < 0;
    }
    QCollator collator;
    bool usable = false;
};

// Files sort by folder first (comparing path components naturally), with the
// files of a folder before those of its subfolders.
bool folderOrderLess(const QStringList &a, const QStringList &b)
{
    const qsizetype dirsA = a.size() - 1;
    const qsizetype dirsB = b.size() - 1;
    for (qsizetype k = 0; k < std::min(dirsA, dirsB); ++k) {
        if (const int cmp = MediaFiles::naturalCompare(a[k], b[k]); cmp != 0)
            return cmp < 0;
    }
    if (dirsA != dirsB)
        return dirsA < dirsB;
    return MediaFiles::naturalLess(a.last(), b.last());
}

} // namespace

namespace MediaFiles {

bool isMediaFile(const QString &path)
{
    const QString ext = suffix(path);
    return kVideoExtensions.contains(ext) || kAudioExtensions.contains(ext);
}

bool isPlaylistFile(const QString &path)
{
    return kPlaylistExtensions.contains(suffix(path));
}

QString mediaFileFilter()
{
    return QObject::tr("Media Files (%1 %2);;Video Files (%1);;Audio Files (%2);;All Files (*)")
        .arg(patterns(kVideoExtensions), patterns(kAudioExtensions));
}

QString playlistFileFilter()
{
    return QObject::tr("Playlists (%1);;All Files (*)").arg(patterns(kPlaylistExtensions));
}

QString playlistSaveFilter()
{
    return QObject::tr("M3U8 Playlist (UTF-8) (*.m3u8);;M3U Playlist (*.m3u)");
}

QStringList mediaFilesInFolder(const QString &folder)
{
    const QDir root(folder);
    // Symlinked folders are not followed, so that link cycles can't recurse forever.
    QDirIterator it(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    QList<QStringList> found; // path components relative to `folder`
    while (it.hasNext()) {
        const QString path = it.next();
        if (isMediaFile(path))
            found.append(root.relativeFilePath(path).split(QLatin1Char('/')));
    }
    std::sort(found.begin(), found.end(), folderOrderLess);

    QStringList files;
    files.reserve(found.size());
    for (const QStringList &parts : std::as_const(found))
        files.append(QDir::cleanPath(root.absoluteFilePath(parts.join(QLatin1Char('/')))));
    return files;
}

int naturalCompare(const QString &a, const QString &b)
{
    static const NaturalCollator natural;
    if (natural.usable) {
        if (const int cmp = natural.collator.compare(a, b); cmp != 0)
            return cmp;
    }
    return fallbackCompare(a, b);
}

QString localPath(const QString &entry)
{
    const QUrl url(entry);
    // One-letter "schemes" are not URLs; mpv treats anything else with :// as a stream.
    if (url.scheme().size() > 1 && entry.contains(QLatin1String("://")))
        return url.isLocalFile() ? url.toLocalFile() : QString();
    return entry;
}

} // namespace MediaFiles
