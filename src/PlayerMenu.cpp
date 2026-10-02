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
    addCommand(audio, tr("Volume Up (+2%)"), {QStringLiteral("add"), QStringLiteral("volume"), QStringLiteral("2")},
               QKeySequence(Qt::Key_Up));
    addCommand(audio, tr("Volume Down (-2%)"), {QStringLiteral("add"), QStringLiteral("volume"), QStringLiteral("-2")},
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
    const QString sid = QStringLiteral("sid");
    const QString secondarySid = QStringLiteral("secondary-sid");

    QMenu *subs = addMenu(tr("Subtitles"));
    addTrackMenu(subs, tr("Subtitle Track"), QStringLiteral("sub"), sid);
    addTrackMenu(subs, tr("Secondary Subtitle Track"), QStringLiteral("sub"), secondarySid);
    addItem(subs, tr("Next Subtitle"), [this, sid] { cycleTrack(tr("Subtitle Track"), sid); },
            QKeySequence(Qt::ALT | Qt::Key_L));
    addItem(subs, tr("Next Secondary Subtitle"), [this, secondarySid] {
        cycleTrack(tr("Secondary Subtitle Track"), secondarySid);
    }, QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_L));
    addToggle(subs, tr("Show Subtitles"), QStringLiteral("sub-visibility"), QKeySequence(Qt::ALT | Qt::Key_H));
    addToggle(subs, tr("Show Secondary Subtitles"), QStringLiteral("secondary-sub-visibility"),
              QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_H));
    addItem(subs, tr("Load Subtitle File..."), [this] {
        const QString file = QFileDialog::getOpenFileName(m_window, tr("Load Subtitle File"), {},
                                                          MpvWidget::subtitleFileFilter());
        if (!file.isEmpty())
            m_window->loadSubtitle(file);
    });

    // Delay and position changes are observed by MainWindow, which shows the OSD.
    subs->addSeparator();
    const QString delay = QStringLiteral("sub-delay");
    addCommand(subs, tr("Delay +0.5s"), {QStringLiteral("add"), delay, QStringLiteral("0.5")},
               QKeySequence(Qt::Key_BracketRight));
    addCommand(subs, tr("Delay -0.5s"), {QStringLiteral("add"), delay, QStringLiteral("-0.5")},
               QKeySequence(Qt::Key_BracketLeft));
    addCommand(subs, tr("Delay +0.1s"), {QStringLiteral("add"), delay, QStringLiteral("0.1")},
               QKeySequence(Qt::Key_Period));
    addCommand(subs, tr("Delay -0.1s"), {QStringLiteral("add"), delay, QStringLiteral("-0.1")},
               QKeySequence(Qt::Key_Comma));
    addCommand(subs, tr("Reset Delay"), {QStringLiteral("set"), delay, QStringLiteral("0")});

    subs->addSeparator();
    // sub-pos is the vertical position in percent of the frame height (100 = bottom
    // margin). Unlike sub-margin-y it also moves ASS and embedded SRT subtitles.
    const QString position = QStringLiteral("sub-pos");
    addCommand(subs, tr("Move Up"), {QStringLiteral("add"), position, QStringLiteral("-1")},
               QKeySequence(Qt::ALT | Qt::Key_Up));
    addCommand(subs, tr("Move Down"), {QStringLiteral("add"), position, QStringLiteral("1")},
               QKeySequence(Qt::ALT | Qt::Key_Down));
    addCommand(subs, tr("Reset Position"), {QStringLiteral("set"), position, QStringLiteral("100")});
    addCommand(subs, tr("Larger"), {QStringLiteral("add"), QStringLiteral("sub-scale"), QStringLiteral("0.1")},
               QKeySequence(Qt::ALT | Qt::Key_PageUp));
    addCommand(subs, tr("Smaller"), {QStringLiteral("add"), QStringLiteral("sub-scale"), QStringLiteral("-0.1")},
               QKeySequence(Qt::ALT | Qt::Key_PageDown));
}

void PlayerMenu::buildPlaybackMenu()
{
    addItem(this, tr("Open File..."), [this] { m_window->openFileDialog(); }, QKeySequence(Qt::CTRL | Qt::Key_O));
    addItem(this, tr("Open Folder..."), [this] { m_window->openFolderDialog(); });
    addItem(this, tr("Open URL / Stream..."), [this] { m_window->openUrlDialog(); });
    addItem(this, tr("Open Playlist..."), [this] { m_window->openPlaylistDialog(); });
    addSeparator();

    QMenu *playback = addMenu(tr("Playback"));
    // Pause is observed by MainWindow, which shows its own OSD message.
    // Goes through MpvWidget so that it also restarts playback after Stop.
    QAction *pause = addItem(playback, tr("Pause"), [this] { m_mpv->togglePause(); }, QKeySequence(Qt::Key_Space));
    pause->setCheckable(true);
    m_toggles.append({pause, QStringLiteral("pause"), QStringLiteral("no")});
    addItem(playback, tr("Stop"), [this] { m_mpv->stop(); });
    addItem(playback, tr("Previous File"), [this] { m_mpv->playlistPrev(); }, QKeySequence(Qt::Key_PageUp));
    addItem(playback, tr("Next File"), [this] { m_mpv->playlistNext(); }, QKeySequence(Qt::Key_PageDown));
    playback->addSeparator();
    addCommand(playback, tr("Seek Forward 5s"), {QStringLiteral("seek"), QStringLiteral("5"), QStringLiteral("relative")},
               QKeySequence(Qt::Key_Right));
    addCommand(playback, tr("Seek Backward 5s"), {QStringLiteral("seek"), QStringLiteral("-5"), QStringLiteral("relative")},
               QKeySequence(Qt::Key_Left));
    addCommand(playback, tr("Seek Forward 30s"), {QStringLiteral("seek"), QStringLiteral("30"), QStringLiteral("relative")},
               QKeySequence(Qt::CTRL | Qt::Key_Right));
    addCommand(playback, tr("Seek Backward 30s"), {QStringLiteral("seek"), QStringLiteral("-30"), QStringLiteral("relative")},
               QKeySequence(Qt::CTRL | Qt::Key_Left));
    addCommand(playback, tr("Seek Forward 60s"), {QStringLiteral("seek"), QStringLiteral("60"), QStringLiteral("relative")},
               QKeySequence(Qt::SHIFT | Qt::Key_Right));
    addCommand(playback, tr("Seek Backward 60s"), {QStringLiteral("seek"), QStringLiteral("-60"), QStringLiteral("relative")},
               QKeySequence(Qt::SHIFT | Qt::Key_Left));
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
    m_playlistAction = addItem(window, tr("Playlist"), [this] {
        m_window->setPlaylistVisible(!m_window->isPlaylistVisible());
    }, QKeySequence(Qt::Key_F6));
    m_playlistAction->setCheckable(true);

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
        // Compare against the property itself: a track's "selected" flag is also set
        // when it is shown as the secondary subtitle.
        const QString current = m_mpv->mpvPropertyString(property);
        // mpv refuses to show one subtitle track as both primary and secondary.
        const QString taken = otherSubtitleSlot(property);

        auto addTrack = [&](const QString &label, const QString &value) {
            QAction *action = submenu->addAction(label);
            action->setCheckable(true);
            action->setChecked(value == current);
            action->setEnabled(value != taken);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, title, property, label, value] {
                m_mpv->setMpvProperty(property, value);
                Q_EMIT osdRequested(title, label);
            });
        };

        const QList<QVariantMap> tracks = m_mpv->tracks(type);
        addTrack(tr("Off"), QStringLiteral("no"));
        if (!tracks.isEmpty())
            submenu->addSeparator();
        for (const QVariantMap &track : tracks)
            addTrack(MpvWidget::trackLabel(track), QString::number(track.value(QStringLiteral("id")).toLongLong()));
    });
    return submenu;
}

void PlayerMenu::cycleTrack(const QString &title, const QString &property)
{
    // Steps through Off, #1, #2, ... and wraps around, announcing the result.
    // The track shown in the other subtitle slot is skipped.
    const QString taken = otherSubtitleSlot(property);
    QList<QVariantMap> tracks;
    for (const QVariantMap &track : m_mpv->tracks(QStringLiteral("sub"))) {
        if (QString::number(track.value(QStringLiteral("id")).toLongLong()) != taken)
            tracks.append(track);
    }
    const QString current = m_mpv->mpvPropertyString(property);
    int index = -1; // -1 is "Off"
    for (int i = 0; i < tracks.size(); ++i) {
        if (QString::number(tracks[i].value(QStringLiteral("id")).toLongLong()) == current)
            index = i;
    }
    const int next = index + 1 < tracks.size() ? index + 1 : -1;
    if (next < 0) {
        m_mpv->setMpvProperty(property, QStringLiteral("no"));
        Q_EMIT osdRequested(title, tr("Off"));
        return;
    }
    m_mpv->setMpvProperty(property, QString::number(tracks[next].value(QStringLiteral("id")).toLongLong()));
    Q_EMIT osdRequested(title, MpvWidget::trackLabel(tracks[next]));
}

QString PlayerMenu::otherSubtitleSlot(const QString &property) const
{
    if (property == QLatin1String("sid"))
        return m_mpv->mpvPropertyString(QStringLiteral("secondary-sid"));
    if (property == QLatin1String("secondary-sid"))
        return m_mpv->mpvPropertyString(QStringLiteral("sid"));
    return {};
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
    m_playlistAction->setChecked(m_window->isPlaylistVisible());
}
