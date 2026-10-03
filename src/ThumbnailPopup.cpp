#include "ThumbnailPopup.h"
#include "Theme.h"
#include "TimeFormat.h"

#include <QPainter>

#include <algorithm>

namespace {

constexpr int kThumbnailWidth = 192;
constexpr int kTimeBandHeight = 22;
constexpr int kBorder = 1;

} // namespace

ThumbnailPopup::ThumbnailPopup(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

void ThumbnailPopup::setTime(double seconds)
{
    m_time = formatTime(seconds);
    update();
}

void ThumbnailPopup::setImage(const QImage &image)
{
    const bool resized = m_image.isNull() || image.size() != m_image.size();
    m_image = image;
    if (resized)
        resize(sizeHint());
    update();
}

void ThumbnailPopup::clearImage()
{
    m_image = QImage();
    resize(sizeHint());
}

QSize ThumbnailPopup::imageSize() const
{
    if (m_image.isNull())
        return {};
    return {kThumbnailWidth, qRound(kThumbnailWidth * double(m_image.height()) / m_image.width())};
}

QSize ThumbnailPopup::sizeHint() const
{
    const QSize image = imageSize();
    const int textWidth = fontMetrics().horizontalAdvance(m_time.isEmpty() ? QStringLiteral("00:00:00") : m_time) + 16;
    return {std::max(image.width(), textWidth) + 2 * kBorder, image.height() + kTimeBandHeight + 2 * kBorder};
}

void ThumbnailPopup::showAt(const QPoint &anchor)
{
    resize(sizeHint());
    const QWidget *area = parentWidget();
    const int x = std::clamp(anchor.x() - width() / 2, 4, std::max(4, area->width() - width() - 4));
    move(x, anchor.y() - height());
    show();
    raise();
}

void ThumbnailPopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::Border);
    const QRect inner = rect().adjusted(kBorder, kBorder, -kBorder, -kBorder);
    p.fillRect(inner, Theme::Surface);

    const QSize image = imageSize();
    if (!image.isEmpty()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRect(inner.topLeft(), image), m_image);
    }

    const QRect band(inner.left(), inner.top() + image.height(), inner.width(), kTimeBandHeight);
    QFont font = p.font();
    font.setBold(true);
    p.setFont(font);
    p.setPen(Theme::Accent);
    p.drawText(band, Qt::AlignCenter, m_time);
}
