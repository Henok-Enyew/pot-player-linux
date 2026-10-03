#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

// PotPlayer-style seekbar: a thin groove that thickens on hover, chapter
// marks, and a hover position used to drive the thumbnail preview.
// Colors are Q_PROPERTYs so the QSS skin can set them with qproperty-*.
class SeekBar : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor grooveColor MEMBER m_grooveColor DESIGNABLE true)
    Q_PROPERTY(QColor progressColor MEMBER m_progressColor DESIGNABLE true)
    Q_PROPERTY(QColor hoverColor MEMBER m_hoverColor DESIGNABLE true)
    Q_PROPERTY(QColor handleColor MEMBER m_handleColor DESIGNABLE true)
    Q_PROPERTY(QColor chapterColor MEMBER m_chapterColor DESIGNABLE true)

public:
    explicit SeekBar(QWidget *parent = nullptr);

    void setDuration(double seconds);
    void setPosition(double seconds);
    void setChapters(const QList<double> &times);

    QSize sizeHint() const override;

Q_SIGNALS:
    // `exact` is false while dragging (fast keyframe seeks) and true on release.
    void seekRequested(double seconds, bool exact);
    // The pointer hovers over `seconds`, at x position `x` in this widget.
    void hovered(double seconds, int x);
    void hoverEnded();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRectF grooveRect() const;
    double timeAt(qreal x) const;
    qreal xFor(double seconds) const;

    double m_duration = 0;
    double m_position = 0;
    double m_dragPosition = 0;
    QList<double> m_chapters;
    bool m_hovering = false;
    bool m_dragging = false;
    qreal m_hoverX = 0;

    // Defaults; the skin sets these through the Q_PROPERTYs above.
    QColor m_grooveColor{0x2A, 0x2D, 0x35};
    QColor m_progressColor{0x00, 0xD2, 0xFF};
    QColor m_hoverColor{0x33, 0xDC, 0xFF, 60};
    QColor m_handleColor{0xFF, 0xFF, 0xFF};
    QColor m_chapterColor{0x12, 0x13, 0x16};
};
