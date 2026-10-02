#pragma once

#include <QImage>
#include <QWidget>

// Floating preview above the seekbar: a thumbnail (when available) with the
// hovered time underneath.
class ThumbnailPopup : public QWidget
{
    Q_OBJECT

public:
    explicit ThumbnailPopup(QWidget *parent);

    void setTime(double seconds);
    void setImage(const QImage &image);
    void clearImage();

    // Places the popup centered on `anchor` (its bottom edge), kept inside the parent.
    void showAt(const QPoint &anchor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QSize imageSize() const;

    QImage m_image;
    QString m_time;
};
