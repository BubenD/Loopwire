#include "app/MainWindow.h"
#include "app/SplashScreen.h"
#include "app/UiBits.h"

#include <QApplication>
#include <QFile>
#include <QIcon>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Edraw"));
    QApplication::setOrganizationName(QStringLiteral("Whatson"));
    QApplication::setApplicationVersion(QStringLiteral("0.1"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/edraw.ico"))); // taskbar / title bar

    if (QFile qss(QStringLiteral(":/edraw.qss")); qss.open(QFile::ReadOnly | QFile::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    // A right-click menu must have a background. See installOpaqueMenus.
    Edraw_Desktop::installOpaqueMenus(&app);
    // The wheel scrolls; it never changes a figure in a box or a drop-down, in any window. See wheelScrollsNotFields.
    Edraw_Desktop::wheelScrollsNotFieldsAnywhere();

    // The logo, and what is being loaded, while the window builds itself.
    Edraw_Desktop::SplashScreen splash(Edraw_Desktop::MainWindow::startupModules());
    splash.show();

    Edraw_Desktop::MainWindow window(nullptr, [&splash](const QString& module) {
        splash.step(module);
    });
    splash.finish();
    window.show();

    const QStringList args = QApplication::arguments();
    if (args.size() > 1) window.openPath(args.at(1));

    return app.exec();
}
