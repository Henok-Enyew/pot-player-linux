#include "SeekBar.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace {

constexpr qreal kSidePadding = 6;
constexpr qreal kGrooveHeight = 4;
constexpr qreal kGrooveHoverHeight = 6;
constexpr qreal kHandleRadius = 6;

} // namespace

SeekBar::SeekBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SeekBar::setDuration(double seconds)
{
    m_duration = std::max(0.0, seconds);
    setEnabled(m_duration > 0);
    update();
}

void SeekBar::setPosition(double seconds)
{
    m_position = seconds;
    if (!m_dragging)
        update();
}

void SeekBar::setChapters(const QList<double> &times)
{
    m_chapters = times;
    update();
}

QSize SeekBar::sizeHint() const
{
    return {200, 18};
}

QRectF SeekBar::grooveRect() const
{
    const qreal h = (m_hovering || m_dragging) ? kGrooveHoverHeight : kGrooveHeight;
    return {kSidePadding, (height() - h) / 2, width() - 2 * kSidePadding, h};
}

double SeekBar::timeAt(qreal x) const
{
    const QRectF groove = grooveRect();
    if (groove.width() <= 0)
        return 0;
    return std::clamp((x - groove.left()) / groove.width(), 0.0, 1.0) * m_duration;
}

qreal SeekBar::xFor(double seconds) const
{
    const QRectF groove = grooveRect();
    const double fraction = m_duration > 0 ? std::clamp(seconds / m_duration, 0.0, 1.0) : 0.0;
    return groove.left() + groove.width() * fraction;
}

void SeekBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    const QRectF groove = grooveRect();
    p.setBrush(m_grooveColor);
    p.drawRoundedRect(groove, groove.height() / 2, groove.height() / 2);

    if (m_duration <= 0)
        return;

    const double shown = m_dragging ? m_dragPosition : m_position;
    const qreal progressX = xFor(shown);

    if (m_hovering && !m_dragging && m_hoverX > progressX) {
        p.setBrush(m_hoverColor);
        p.drawRect(QRectF(QPointF(progressX, groove.top()), QPointF(std::min(m_hoverX, groove.right()), groove.bottom())));
    }

    p.setBrush(m_progressColor);
    p.drawRoundedRect(QRectF(groove.topLeft(), QPointF(progressX, groove.bottom())),
                      groove.height() / 2, groove.height() / 2);

    // Chapter boundaries are cut into the groove as thin gaps.
    p.setBrush(m_chapterColor);
    for (double chapter : std::as_const(m_chapters)) {
        if (chapter > 0 && chapter < m_duration)
            p.drawRect(QRectF(xFor(chapter) - 1, groove.top(), 2, groove.height()));
    }

    if (m_hovering || m_dragging) {
        p.setBrush(m_handleColor);
        p.drawEllipse(QPointF(progressX, groove.center().y()), kHandleRadius, kHandleRadius);
    }
}

void SeekBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_duration <= 0) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_dragPosition = timeAt(event->position().x());
    Q_EMIT seekRequested(m_dragPosition, false);
    update();
}

void SeekBar::mouseMoveEvent(QMouseEvent *event)
{
    m_hovering = true;
    m_hoverX = event->position().x();
    const double time = timeAt(m_hoverX);
    if (m_dragging && time != m_dragPosition) {
        m_dragPosition = time;
        Q_EMIT seekRequested(time, false);
    }
    if (m_duration > 0)
        Q_EMIT hovered(time, qRound(std::clamp(m_hoverX, grooveRect().left(), grooveRect().right())));
    update();
}

void SeekBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    m_dragPosition = timeAt(event->position().x());
    m_position = m_dragPosition;
    Q_EMIT seekRequested(m_dragPosition, true);
    update();
}

void SeekBar::leaveEvent(QEvent *)
{
    m_hovering = false;
    Q_EMIT hoverEnded();
    update();
}
