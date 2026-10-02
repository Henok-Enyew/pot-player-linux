#pragma once

#include <QList>
#include <QTimer>
#include <QWidget>

class QPropertyAnimation;

// Transparent overlay that briefly shows PotPlayer-style status text
// (white labels, amber values) with an optional progress bar.
class OsdWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit OsdWidget(QWidget *parent);

    // Shows "<label> <value>", e.g. "Volume 65%"; progress in [0, 1] draws a bar, < 0 hides it.
    void showValue(const QString &label, const QString &value = {}, qreal progress = -1);
    // Shows "<position> / <duration>" with a seek bar.
    void showTime(double position, double duration);

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    struct Segment {
        QString text;
        bool accent;
    };

    void showSegments(const QList<Segment> &segments, qreal progress);

    QList<Segment> m_segments;
    qreal m_progress = -1;
    qreal m_opacity = 1.0;
    QTimer m_holdTimer;
    QPropertyAnimation *m_fade = nullptr;
};
