#include "app/MainWindow.h"

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

    Edraw_Desktop::MainWindow window;
    window.show();

    const QStringList args = QApplication::arguments();
    if (args.size() > 1) window.openPath(args.at(1));

    return app.exec();
}
