#include "MediaFiles.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>

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

// Case-insensitive order that compares runs of digits by value. QCollator's
// numeric mode would do this, but not in the C locale.
bool naturalLess(const QString &a, const QString &b)
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
                return numA.size() < numB.size();
            if (const int cmp = numA.compare(numB); cmp != 0)
                return cmp < 0;
            continue;
        }
        const QChar ca = a[i].toCaseFolded();
        const QChar cb = b[j].toCaseFolded();
        if (ca != cb)
            return ca < cb;
        ++i;
        ++j;
    }
    if (a.size() - i != b.size() - j)
        return a.size() - i < b.size() - j;
    return a < b; // equal ignoring case and zeros: keep a stable, total order
}

void collect(const QDir &dir, QStringList &files)
{
    auto naturalOrder = [](const QFileInfo &a, const QFileInfo &b) { return naturalLess(a.fileName(), b.fileName()); };
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Readable);
    std::sort(entries.begin(), entries.end(), naturalOrder);
    for (const QFileInfo &entry : std::as_const(entries)) {
        if (MediaFiles::isMediaFile(entry.fileName()))
            files.append(entry.absoluteFilePath());
    }
    // Symlinked folders are skipped so that link cycles can't recurse forever.
    QFileInfoList folders = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::NoSymLinks);
    std::sort(folders.begin(), folders.end(), naturalOrder);
    for (const QFileInfo &folder : std::as_const(folders))
        collect(QDir(folder.absoluteFilePath()), files);
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

QStringList mediaFilesInFolder(const QString &folder)
{
    QStringList files;
    collect(QDir(folder), files);
    return files;
}

} // namespace MediaFiles
