#pragma once

#include <QString>

#include <algorithm>
#include <cmath>

// Formats seconds as hh:mm:ss, the way PotPlayer shows times.
inline QString formatTime(double seconds)
{
    const qint64 total = std::max<qint64>(0, static_cast<qint64>(std::floor(seconds)));
    return QStringLiteral("%1:%2:%3")
        .arg(total / 3600, 2, 10, QLatin1Char('0'))
        .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}
