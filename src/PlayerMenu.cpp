#include "PlayerMenu.h"
#include "MainWindow.h"
#include "MpvWidget.h"

#include <QActionGroup>
#include <QFileDialog>

#include <cmath>
#include <optional>

namespace {

// Parses "1.5", "16:9" or "2.35:1" into a number.
std::optional<double> parseNumber(const QString &text)
{
    bool ok = false;
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        const double value = text.toDouble(&ok);
        return ok ? std::optional<double>(value) : std::nullopt;
    }
    bool okDen = false;
    const double num = text.left(colon).toDouble(&ok);
    const double den = text.mid(colon + 1).toDouble(&okDen);
    if (!ok || !okDen || den == 0)
        return std::nullopt;
    return num / den;
}

// mpv normalizes values ("1.5" reads back as "1.500000", "16:9" as "1.777778").
bool valuesMatch(const QString &current, const QString &candidate)
{
    if (current == candidate)
        return true;
    const auto a = parseNumber(current);
    const auto b = parseNumber(candidate);
    return a && b && std::abs(*a - *b) < 1e-3;
}

QString trackLabel(const QVariantMap &track)
{
    QString label = QStringLiteral("#%1").arg(track.value(QStringLiteral("id")).toLongLong());
    const QString title = track.value(QStringLiteral("title")).toString();
    const QString lang = track.value(QStringLiteral("lang")).toString();
    const QString codec = track.value(QStringLiteral("codec")).toString();
    if (!title.isEmpty())
        label += QStringLiteral(": ") + title;
    if (!lang.isEmpty())
        label += QStringLiteral(" [%1]").arg(lang);
    if (!codec.isEmpty())
        label += QStringLiteral(" (%1)").arg(codec);
    if (track.value(QStringLiteral("external")).toBool())
        label += QStringLiteral(" - external");
    return label;
}

} // namespace

PlayerMenu::PlayerMenu(MpvWidget *mpv, MainWindow *window)
    : QMenu(window)
    , m_mpv(mpv)
    , m_window(window)
{
    buildPlaybackMenu();
    buildVideoMenu();
    buildAudioMenu();
    buildSubtitleMenu();
    addSeparator();
    buildWindowMenu();

    connect(this, &QMenu::aboutToShow, this, &PlayerMenu::syncState);
}

void PlayerMenu::buildVideoMenu()
{
    QMenu *video = addMenu(tr("Video"));
    addTrackMenu(video, tr("Video Track"), QStringLiteral("video"), QStringLiteral("vid"));
    addChoices(video, tr("Hardware Decoding"), QStringLiteral("hwdec"), {
        {tr("Auto (Safe)"), QStringLiteral("auto-safe")},
        {tr("Auto (Copy-back)"), QStringLiteral("auto-copy-safe")},
        {tr("VA-API"), QStringLiteral("vaapi")},
        {tr("NVDEC"), QStringLiteral("nvdec")},
        {tr("Vulkan"), QStringLiteral("vulkan")},
        {tr("Off (Software)"), QStringLiteral("no")},
    });
    addChoices(video, tr("Aspect Ratio"), QStringLiteral("video-aspect-override"), {
        {tr("Default"), QStringLiteral("-1")},
        {QStringLiteral("4:3"), QStringLiteral("4:3")},
        {QStringLiteral("16:9"), QStringLiteral("16:9")},
        {QStringLiteral("1.85:1"), QStringLiteral("1.85:1")},
        {QStringLiteral("2.35:1"), QStringLiteral("2.35:1")},
    });
    addChoices(video, tr("Rotate"), QStringLiteral("video-rotate"), {
        {tr("0°"), QStringLiteral("0")},
        {tr("90°"), QStringLiteral("90")},
        {tr("180°"), QStringLiteral("180")},
        {tr("270°"), QStringLiteral("270")},
    });
    addToggle(video, tr("Deinterlace"), QStringLiteral("deinterlace"), QKeySequence(Qt::CTRL | Qt::Key_D));
    video->addSeparator();
    QAction *screenshot = addCommand(video, tr("Take Screenshot"), {QStringLiteral("screenshot")},
                                     QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(screenshot, &QAction::triggered, this, [this] { Q_EMIT osdRequested(tr("Screenshot"), tr("saved")); });
}

void PlayerMenu::buildAudioMenu()
{
    QMenu *audio = addMenu(tr("Audio"));
    addTrackMenu(audio, tr("Audio Track"), QStringLiteral("audio"), QStringLiteral("aid"));
    audio->addSeparator();
    addCommand(audio, tr("Volume Up"), {QStringLiteral("add"), QStringLiteral("volume"), QStringLiteral("5")},
               QKeySequence(Qt::Key_Up));
    addCommand(audio, tr("Volume Down"), {QStringLiteral("add"), QStringLiteral("volume"), QStringLiteral("-5")},
               QKeySequence(Qt::Key_Down));
    // Mute is observed by MainWindow, which shows its own OSD message.
    addToggle(audio, tr("Mute"), QStringLiteral("mute"), QKeySequence(Qt::Key_M), false);
    audio->addSeparator();
    addCommand(audio, tr("Audio Delay +0.1s"), {QStringLiteral("add"), QStringLiteral("audio-delay"), QStringLiteral("0.1")},
               QKeySequence(Qt::CTRL | Qt::Key_Period));
    addCommand(audio, tr("Audio Delay -0.1s"), {QStringLiteral("add"), QStringLiteral("audio-delay"), QStringLiteral("-0.1")},
               QKeySequence(Qt::CTRL | Qt::Key_Comma));
    addCommand(audio, tr("Reset Audio Delay"), {QStringLiteral("set"), QStringLiteral("audio-delay"), QStringLiteral("0")});
}

void PlayerMenu::buildSubtitleMenu()
{
    QMenu *subs = addMenu(tr("Subtitles"));
    addTrackMenu(subs, tr("Subtitle Track"), QStringLiteral("sub"), QStringLiteral("sid"));
    addToggle(subs, tr("Show Subtitles"), QStringLiteral("sub-visibility"), QKeySequence(Qt::ALT | Qt::Key_H));
    addItem(subs, tr("Load Subtitle File..."), [this] {
        const QString file = QFileDialog::getOpenFileName(m_window, tr("Load Subtitle File"), {},
                                                          tr("Subtitles (*.srt *.ass *.ssa *.vtt *.sub *.sup);;All Files (*)"));
        if (!file.isEmpty())
            m_mpv->command({QStringLiteral("sub-add"), file, QStringLiteral("select")});
    });
    subs->addSeparator();
    addCommand(subs, tr("Subtitle Delay +0.1s"), {QStringLiteral("add"), QStringLiteral("sub-delay"), QStringLiteral("0.1")},
               QKeySequence(Qt::Key_Period));
    addCommand(subs, tr("Subtitle Delay -0.1s"), {QStringLiteral("add"), QStringLiteral("sub-delay"), QStringLiteral("-0.1")},
               QKeySequence(Qt::Key_Comma));
    addCommand(subs, tr("Reset Subtitle Delay"), {QStringLiteral("set"), QStringLiteral("sub-delay"), QStringLiteral("0")});
    subs->addSeparator();
    addCommand(subs, tr("Larger Subtitles"), {QStringLiteral("add"), QStringLiteral("sub-scale"), QStringLiteral("0.1")},
               QKeySequence(Qt::ALT | Qt::Key_Up));
    addCommand(subs, tr("Smaller Subtitles"), {QStringLiteral("add"), QStringLiteral("sub-scale"), QStringLiteral("-0.1")},
               QKeySequence(Qt::ALT | Qt::Key_Down));
}

void PlayerMenu::buildPlaybackMenu()
{
    addItem(this, tr("Open File..."), [this] { m_window->openFileDialog(); }, QKeySequence(Qt::CTRL | Qt::Key_O));
    addSeparator();

    QMenu *playback = addMenu(tr("Playback"));
    // Pause is observed by MainWindow, which shows its own OSD message.
    addToggle(playback, tr("Pause"), QStringLiteral("pause"), QKeySequence(Qt::Key_Space), false);
    addCommand(playback, tr("Stop"), {QStringLiteral("stop")});
    playback->addSeparator();
    addCommand(playback, tr("Seek Forward 5s"), {QStringLiteral("seek"), QStringLiteral("5"), QStringLiteral("relative")},
               QKeySequence(Qt::Key_Right));
    addCommand(playback, tr("Seek Backward 5s"), {QStringLiteral("seek"), QStringLiteral("-5"), QStringLiteral("relative")},
               QKeySequence(Qt::Key_Left));
    addCommand(playback, tr("Seek Forward 30s"), {QStringLiteral("seek"), QStringLiteral("30"), QStringLiteral("relative")},
               QKeySequence(Qt::CTRL | Qt::Key_Right));
    addCommand(playback, tr("Seek Backward 30s"), {QStringLiteral("seek"), QStringLiteral("-30"), QStringLiteral("relative")},
               QKeySequence(Qt::CTRL | Qt::Key_Left));
    playback->addSeparator();
    QMenu *speed = addChoices(playback, tr("Speed"), QStringLiteral("speed"), {
        {QStringLiteral("0.25x"), QStringLiteral("0.25")},
        {QStringLiteral("0.5x"), QStringLiteral("0.5")},
        {QStringLiteral("0.75x"), QStringLiteral("0.75")},
        {tr("1.0x (Normal)"), QStringLiteral("1")},
        {QStringLiteral("1.25x"), QStringLiteral("1.25")},
        {QStringLiteral("1.5x"), QStringLiteral("1.5")},
        {QStringLiteral("2.0x"), QStringLiteral("2")},
    });
    speed->addSeparator();
    // PotPlayer's speed keys: C faster, X slower, Z normal.
    addCommand(speed, tr("Faster (+0.1)"), {QStringLiteral("add"), QStringLiteral("speed"), QStringLiteral("0.1")},
               QKeySequence(Qt::Key_C));
    addCommand(speed, tr("Slower (-0.1)"), {QStringLiteral("add"), QStringLiteral("speed"), QStringLiteral("-0.1")},
               QKeySequence(Qt::Key_X));
    addCommand(speed, tr("Normal Speed"), {QStringLiteral("set"), QStringLiteral("speed"), QStringLiteral("1")},
               QKeySequence(Qt::Key_Z));
    addToggle(playback, tr("Loop File"), QStringLiteral("loop-file"), QKeySequence(Qt::CTRL | Qt::Key_L), true,
              QStringLiteral("inf"), QStringLiteral("no"));
}

void PlayerMenu::buildWindowMenu()
{
    QMenu *window = addMenu(tr("Window"));
    m_fullScreenAction = addItem(window, tr("Fullscreen"), [this] { m_window->toggleFullScreen(); },
                                 QKeySequence(Qt::Key_Return));
    m_fullScreenAction->setCheckable(true);
    m_onTopAction = addItem(window, tr("Always on Top"), [this] {
        m_window->setAlwaysOnTop(m_onTopAction->isChecked());
        Q_EMIT osdRequested(tr("Always on Top"), m_onTopAction->isChecked() ? tr("On") : tr("Off"));
    }, QKeySequence(Qt::CTRL | Qt::Key_T));
    m_onTopAction->setCheckable(true);

    QMenu *size = window->addMenu(tr("Window Size"));
    const QList<QPair<QString, qreal>> scales{
        {QStringLiteral("50%"), 0.5}, {QStringLiteral("100%"), 1.0},
        {QStringLiteral("150%"), 1.5}, {QStringLiteral("200%"), 2.0},
    };
    for (int i = 0; i < scales.size(); ++i) {
        const qreal scale = scales[i].second;
        addItem(size, scales[i].first, [this, scale] { m_window->scaleToVideo(scale); },
                QKeySequence(Qt::ALT | (Qt::Key_1 + i)));
    }

    window->addSeparator();
    addItem(window, tr("Exit"), [this] { m_window->close(); }, QKeySequence(Qt::Key_Q));
}

QAction *PlayerMenu::addItem(QMenu *menu, const QString &text, std::function<void()> handler,
                             const QKeySequence &shortcut)
{
    QAction *action = menu->addAction(text);
    connect(action, &QAction::triggered, this, [handler = std::move(handler)] { handler(); });
    bindShortcut(action, shortcut);
    return action;
}

QAction *PlayerMenu::addCommand(QMenu *menu, const QString &text, const QStringList &command,
                                const QKeySequence &shortcut)
{
    return addItem(menu, text, [this, command] { m_mpv->command(command); }, shortcut);
}

QAction *PlayerMenu::addToggle(QMenu *menu, const QString &text, const QString &property,
                               const QKeySequence &shortcut, bool announce,
                               const QString &onValue, const QString &offValue)
{
    QAction *action = addItem(menu, text, [this, text, property, announce, onValue, offValue] {
        // Read the live value instead of trusting the check state, which may be stale
        // when the shortcut fires while the menu is closed.
        const bool enable = m_mpv->mpvPropertyString(property) == offValue;
        m_mpv->setMpvProperty(property, enable ? onValue : offValue);
        if (announce)
            Q_EMIT osdRequested(text, enable ? tr("On") : tr("Off"));
    }, shortcut);
    action->setCheckable(true);
    m_toggles.append({action, property, offValue});
    return action;
}

QMenu *PlayerMenu::addChoices(QMenu *menu, const QString &title, const QString &property,
                              const QList<Choice> &choices)
{
    QMenu *submenu = menu->addMenu(title);
    auto *group = new QActionGroup(submenu);
    for (const Choice &choice : choices) {
        QAction *action = submenu->addAction(choice.text);
        action->setCheckable(true);
        action->setData(choice.value);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, title, property, choice] {
            m_mpv->setMpvProperty(property, choice.value);
            // Speed has a dedicated OSD driven by the property change itself.
            if (property != QLatin1String("speed"))
                Q_EMIT osdRequested(title, choice.text);
        });
    }
    connect(submenu, &QMenu::aboutToShow, this, [this, group, property] {
        const QString current = m_mpv->mpvPropertyString(property);
        for (QAction *action : group->actions())
            action->setChecked(valuesMatch(current, action->data().toString()));
    });
    return submenu;
}

QMenu *PlayerMenu::addTrackMenu(QMenu *menu, const QString &title, const QString &type, const QString &property)
{
    QMenu *submenu = menu->addMenu(title);
    connect(submenu, &QMenu::aboutToShow, this, [this, submenu, title, type, property] {
        qDeleteAll(submenu->findChildren<QActionGroup *>(Qt::FindDirectChildrenOnly));
        submenu->clear();
        auto *group = new QActionGroup(submenu);

        auto addTrack = [&](const QString &label, const QString &value, bool selected) {
            QAction *action = submenu->addAction(label);
            action->setCheckable(true);
            action->setChecked(selected);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, title, property, label, value] {
                m_mpv->setMpvProperty(property, value);
                Q_EMIT osdRequested(title, label);
            });
        };

        bool anySelected = false;
        QList<QVariantMap> tracks;
        for (const QVariant &entry : m_mpv->mpvProperty(QStringLiteral("track-list")).toList()) {
            const QVariantMap track = entry.toMap();
            if (track.value(QStringLiteral("type")).toString() != type)
                continue;
            tracks.append(track);
            anySelected |= track.value(QStringLiteral("selected")).toBool();
        }

        addTrack(tr("Off"), QStringLiteral("no"), !anySelected);
        if (!tracks.isEmpty())
            submenu->addSeparator();
        for (const QVariantMap &track : std::as_const(tracks)) {
            addTrack(trackLabel(track), QString::number(track.value(QStringLiteral("id")).toLongLong()),
                     track.value(QStringLiteral("selected")).toBool());
        }
    });
    return submenu;
}

void PlayerMenu::bindShortcut(QAction *action, const QKeySequence &shortcut)
{
    if (shortcut.isEmpty())
        return;
    action->setShortcut(shortcut);
    action->setShortcutVisibleInContextMenu(true);
    m_window->addAction(action);
}

void PlayerMenu::syncState()
{
    for (const Toggle &toggle : std::as_const(m_toggles))
        toggle.action->setChecked(m_mpv->mpvPropertyString(toggle.property) != toggle.offValue);
    m_fullScreenAction->setChecked(m_window->isFullScreen());
    m_onTopAction->setChecked(m_window->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
}
