#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSurfaceFormat>

#include <clocale>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("pot-player"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.github.potlinux"));
    QApplication::setApplicationDisplayName(QStringLiteral("Pot Player"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    // libmpv requires the C numeric locale; QApplication may have changed it.
    std::setlocale(LC_NUMERIC, "C");
    applyDarkSkin(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A lightweight media player for Linux"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Media files or URLs to play; extra files are queued."),
                                 QStringLiteral("[files...]"));
    parser.process(app);

    MainWindow window;
    window.resize(960, 540);
    window.show();

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty())
        window.openFiles(args);

    return app.exec();
}
