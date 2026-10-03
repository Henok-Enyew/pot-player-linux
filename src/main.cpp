#include "Icons.h"
#include "MainWindow.h"
#include "PlaylistSession.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QNetworkProxyFactory>
#include <QSurfaceFormat>

#include <clocale>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("top-player"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.github.topplayer"));
    QApplication::setApplicationDisplayName(QStringLiteral("Top Player"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    // The installed icon theme wins; the built-in logo covers running from the build tree.
    QIcon icon = QIcon::fromTheme(QStringLiteral("org.github.topplayer"));
    if (icon.isNull()) {
        for (int size : {16, 24, 32, 48, 64, 128, 256})
            icon.addPixmap(appLogo(size, 1.0));
    }
    QApplication::setWindowIcon(icon);

    // libmpv requires the C numeric locale; QApplication may have changed it.
    std::setlocale(LC_NUMERIC, "C");
    PlaylistSession::migrateLegacyConfig();
    applyDarkSkin(app);
    // Subtitle downloads go through the desktop's proxy settings ($https_proxy, ...).
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Top Player: a high-performance, lightweight media player for Linux"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Media files or URLs to play; extra files are queued."),
                                 QStringLiteral("[files...]"));
    parser.process(app);

    MainWindow window;
    window.resize(960, 540);
    window.show();

    // Files on the command line replace the queue from the last run.
    const QStringList args = parser.positionalArguments();
    window.startSession(args.isEmpty());
    if (!args.isEmpty())
        window.openFiles(args);

    return app.exec();
}
