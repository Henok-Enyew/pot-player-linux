#pragma once

#include <QDialog>

class MpvWidget;

// Help -> About Top Player: version, credits, links, license, and the Qt,
// libmpv and video acceleration details of the running player.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(MpvWidget *mpv, QWidget *parent = nullptr);

    // Human-readable name of an mpv hwdec value: "vaapi" -> "VA-API",
    // "nvdec-copy" -> "NVDEC (copy-back)"; empty or "no" -> empty.
    static QString hwdecName(const QString &hwdec);
};
