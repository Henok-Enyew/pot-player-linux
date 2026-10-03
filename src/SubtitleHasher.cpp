#include "SubtitleHasher.h"

#include <QFile>
#include <QtEndian>

QString SubtitleHasher::hash(const QString &path)
{
    QFile file(path);
    const qint64 size = file.size();
    if (size < kMinimumSize || !file.open(QIODevice::ReadOnly))
        return {};
    quint64 sum = static_cast<quint64>(size);
    auto addChunk = [&](qint64 offset) {
        if (!file.seek(offset))
            return false;
        const QByteArray data = file.read(kChunkSize);
        if (data.size() != kChunkSize)
            return false;
        // Unsigned arithmetic wraps around, as the algorithm expects.
        for (qsizetype i = 0; i < data.size(); i += 8)
            sum += qFromLittleEndian<quint64>(data.constData() + i);
        return true;
    };
    if (!addChunk(0) || !addChunk(size - kChunkSize))
        return {};
    return QStringLiteral("%1").arg(sum, 16, 16, QLatin1Char('0'));
}
