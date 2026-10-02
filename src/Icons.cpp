#include "Icons.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

const QColor kNormalColor(0xC8, 0xC8, 0xC8);
const QColor kActiveColor(0xFF, 0xFF, 0xFF);
const QColor kDisabledColor(0x5A, 0x5A, 0x5A);

constexpr int kLogicalSize = 20;
constexpr qreal kScale = 2.0; // rendered at 2x so icons stay sharp on HiDPI screens

QPainterPath polygon(std::initializer_list<QPointF> points)
{
    QPainterPath path;
    path.addPolygon(QPolygonF(points));
    path.closeSubpath();
    return path;
}

void drawSpeaker(QPainter &p)
{
    p.fillPath(polygon({{3, 7.5}, {6, 7.5}, {10, 4}, {10, 16}, {6, 12.5}, {3, 12.5}}), p.pen().color());
}

// Draws the icon on a 20x20 grid using the painter's pen color.
void drawIcon(QPainter &p, IconType type)
{
    const QColor color = p.pen().color();
    switch (type) {
    case IconType::Open:
        p.fillPath(polygon({{4, 11}, {10, 4.5}, {16, 11}}), color);
        p.fillRect(QRectF(4, 13, 12, 2.5), color);
        break;
    case IconType::Play:
        p.fillPath(polygon({{6, 4}, {16, 10}, {6, 16}}), color);
        break;
    case IconType::Pause:
        p.fillRect(QRectF(5.5, 4, 3, 12), color);
        p.fillRect(QRectF(11.5, 4, 3, 12), color);
        break;
    case IconType::Stop:
        p.fillRect(QRectF(5, 5, 10, 10), color);
        break;
    case IconType::Previous:
        p.fillRect(QRectF(4, 5, 2, 10), color);
        p.fillPath(polygon({{16, 5}, {7, 10}, {16, 15}}), color);
        break;
    case IconType::Next:
        p.fillRect(QRectF(14, 5, 2, 10), color);
        p.fillPath(polygon({{4, 5}, {13, 10}, {4, 15}}), color);
        break;
    case IconType::Playlist:
        p.drawLine(QPointF(3, 5), QPointF(17, 5));
        p.drawLine(QPointF(3, 10), QPointF(11, 10));
        p.drawLine(QPointF(3, 15), QPointF(11, 15));
        p.fillPath(polygon({{13.5, 9}, {18, 12.5}, {13.5, 16}}), color);
        break;
    case IconType::Volume:
        drawSpeaker(p);
        p.drawArc(QRectF(8, 7, 5, 6), -60 * 16, 120 * 16);
        p.drawArc(QRectF(8, 4.5, 8.5, 11), -60 * 16, 120 * 16);
        break;
    case IconType::Muted:
        drawSpeaker(p);
        p.drawLine(QPointF(12.5, 8), QPointF(16.5, 12));
        p.drawLine(QPointF(16.5, 8), QPointF(12.5, 12));
        break;
    case IconType::Fullscreen:
        p.drawPolyline(QPolygonF({{3.5, 7.5}, {3.5, 3.5}, {7.5, 3.5}}));
        p.drawPolyline(QPolygonF({{12.5, 3.5}, {16.5, 3.5}, {16.5, 7.5}}));
        p.drawPolyline(QPolygonF({{16.5, 12.5}, {16.5, 16.5}, {12.5, 16.5}}));
        p.drawPolyline(QPolygonF({{7.5, 16.5}, {3.5, 16.5}, {3.5, 12.5}}));
        break;
    case IconType::Minimize:
        p.drawLine(QPointF(5, 13), QPointF(15, 13));
        break;
    case IconType::Maximize:
        p.drawRect(QRectF(5, 5, 10, 10));
        break;
    case IconType::Restore:
        p.drawRect(QRectF(4.5, 7.5, 8, 8));
        p.drawPolyline(QPolygonF({{7.5, 7.5}, {7.5, 4.5}, {15.5, 4.5}, {15.5, 12.5}, {12.5, 12.5}}));
        break;
    case IconType::Close:
        p.drawLine(QPointF(5, 5), QPointF(15, 15));
        p.drawLine(QPointF(15, 5), QPointF(5, 15));
        break;
    case IconType::Add:
        p.drawLine(QPointF(10, 4), QPointF(10, 16));
        p.drawLine(QPointF(4, 10), QPointF(16, 10));
        break;
    case IconType::Remove:
        p.drawLine(QPointF(4, 10), QPointF(16, 10));
        break;
    case IconType::Clear:
        p.drawLine(QPointF(4, 6), QPointF(16, 6));
        p.drawLine(QPointF(8, 6), QPointF(8.5, 3.5));
        p.drawLine(QPointF(8.5, 3.5), QPointF(11.5, 3.5));
        p.drawLine(QPointF(11.5, 3.5), QPointF(12, 6));
        p.drawPolyline(QPolygonF({{5.5, 6}, {6.5, 16.5}, {13.5, 16.5}, {14.5, 6}}));
        break;
    case IconType::Folder:
        p.drawPath(polygon({{2.5, 4.5}, {8, 4.5}, {9.5, 6.5}, {17.5, 6.5}, {17.5, 15.5}, {2.5, 15.5}}));
        p.drawLine(QPointF(2.5, 8.5), QPointF(17.5, 8.5));
        break;
    case IconType::Url:
        // A globe: outline, meridian and two parallels.
        p.drawEllipse(QPointF(10, 10), 7, 7);
        p.drawEllipse(QPointF(10, 10), 3, 7);
        p.drawLine(QPointF(3, 10), QPointF(17, 10));
        p.drawLine(QPointF(4.5, 6.5), QPointF(15.5, 6.5));
        p.drawLine(QPointF(4.5, 13.5), QPointF(15.5, 13.5));
        break;
    case IconType::Shuffle:
        // Two crossing paths with arrowheads on the right.
        p.drawPolyline(QPolygonF({{3, 6}, {7, 6}, {12.5, 14}, {15.5, 14}}));
        p.drawPolyline(QPolygonF({{3, 14}, {7, 14}, {12.5, 6}, {15.5, 6}}));
        p.fillPath(polygon({{15, 3.5}, {18.5, 6}, {15, 8.5}}), color);
        p.fillPath(polygon({{15, 11.5}, {18.5, 14}, {15, 16.5}}), color);
        break;
    case IconType::Sort:
        // Bars getting shorter, beside a downward arrow.
        p.drawLine(QPointF(3, 5), QPointF(11, 5));
        p.drawLine(QPointF(3, 10), QPointF(9, 10));
        p.drawLine(QPointF(3, 15), QPointF(7, 15));
        p.drawLine(QPointF(14.5, 4), QPointF(14.5, 14));
        p.fillPath(polygon({{11.5, 12.5}, {14.5, 17}, {17.5, 12.5}}), color);
        break;
    case IconType::More:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (qreal y : {4.5, 10.0, 15.5})
            p.drawEllipse(QPointF(10, y), 1.6, 1.6);
        break;
    case IconType::Search:
        p.drawEllipse(QPointF(8.5, 8.5), 4.5, 4.5);
        p.drawLine(QPointF(12, 12), QPointF(16.5, 16.5));
        break;
    }
}

QPixmap renderIcon(IconType type, const QColor &color)
{
    QPixmap pixmap(QSize(kLogicalSize, kLogicalSize) * kScale);
    pixmap.setDevicePixelRatio(kScale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    drawIcon(painter, type);
    return pixmap;
}

} // namespace

QIcon skinIcon(IconType type)
{
    QIcon icon;
    icon.addPixmap(renderIcon(type, kNormalColor), QIcon::Normal);
    icon.addPixmap(renderIcon(type, kActiveColor), QIcon::Active);
    icon.addPixmap(renderIcon(type, kDisabledColor), QIcon::Disabled);
    return icon;
}

QPixmap appLogo(int size, qreal devicePixelRatio)
{
    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    // Drawn on the 256x256 grid of packaging/linux/org.github.potlinux.svg.
    p.scale(size / 256.0, size / 256.0);

    QLinearGradient background(0, 16, 0, 240);
    background.setColorAt(0, QColor(0x2B, 0x2B, 0x2B));
    background.setColorAt(1, QColor(0x15, 0x15, 0x15));
    p.setPen(QPen(QColor(0x3A, 0x3A, 0x3A), 1));
    p.setBrush(background);
    p.drawRoundedRect(QRectF(16.5, 16.5, 223, 223), 47.5, 47.5);

    QLinearGradient amber(100, 76, 184, 180);
    amber.setColorAt(0, QColor(0xFF, 0xC8, 0x4A));
    amber.setColorAt(1, QColor(0xF2, 0x9A, 0x00));
    p.setPen(QPen(QBrush(amber), 14, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(amber);
    p.drawPath(polygon({{100, 76}, {184, 128}, {100, 180}}));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x3D, 0x3D, 0x3D));
    p.drawRoundedRect(QRectF(56, 200, 144, 8), 4, 4);
    p.setBrush(QColor(0xFF, 0xB4, 0x1E));
    p.drawRoundedRect(QRectF(56, 200, 88, 8), 4, 4);
    return pixmap;
}
