#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

// PotPlayer-style seekbar: a thin groove that thickens on hover, chapter
// marks, In/Out brackets for the cutter, and a hover position used to drive the thumbnail preview.
// Colors are Q_PROPERTYs so the QSS skin can set them with qproperty-*.
class SeekBar : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor grooveColor MEMBER m_grooveColor DESIGNABLE true)
    Q_PROPERTY(QColor progressColor MEMBER m_progressColor DESIGNABLE true)
    Q_PROPERTY(QColor hoverColor MEMBER m_hoverColor DESIGNABLE true)
    Q_PROPERTY(QColor handleColor MEMBER m_handleColor DESIGNABLE true)
    Q_PROPERTY(QColor chapterColor MEMBER m_chapterColor DESIGNABLE true)
    Q_PROPERTY(QColor clipColor MEMBER m_clipColor DESIGNABLE true)

public:
    explicit SeekBar(QWidget *parent = nullptr);

    void setDuration(double seconds);
    void setPosition(double seconds);
    void setChapters(const QList<double> &times);
    // The cutter's In (A) and Out (B) points, drawn as brackets around the
    // selected range; negative values are unset.
    void setClipRange(double in, double out);
    double clipIn() const { return m_clipIn; }
    double clipOut() const { return m_clipOut; }

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
    double m_clipIn = -1;
    double m_clipOut = -1;
    bool m_hovering = false;
    bool m_dragging = false;
    qreal m_hoverX = 0;

    // Defaults; the skin sets these through the Q_PROPERTYs above.
    QColor m_grooveColor{0x2A, 0x2D, 0x35};
    QColor m_progressColor{0x00, 0xD2, 0xFF};
    QColor m_hoverColor{0x33, 0xDC, 0xFF, 60};
    QColor m_handleColor{0xFF, 0xFF, 0xFF};
    QColor m_chapterColor{0x12, 0x13, 0x16};
    QColor m_clipColor{0xA7, 0x8B, 0xFA};
};
