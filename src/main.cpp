#include "app/MainWindow.h"
#include "app/SplashScreen.h"
#include "app/UiBits.h"

#include <QApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>

namespace {

// COPY A FOLDER and everything in it, leaving what is already in the new one as it is.
void copyTree(const QDir& from, const QDir& to) {
    QDir().mkpath(to.absolutePath());
    for (const QFileInfo& f : from.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString target = to.absoluteFilePath(f.fileName());
        if (f.isDir()) copyTree(QDir(f.absoluteFilePath()), QDir(target));
        else if (!QFile::exists(target)) QFile::copy(f.absoluteFilePath(), target);
    }
}

// THE APPLICATION WAS CALLED LOOPWIRE: its settings and its folder of parts libraries, templates, blocks and datasheets are
// copied across - once, the first time Loopwire starts with none of its own. Copied, not moved: the old ones stay where
// they were, so going back to an older build loses nothing.
void takeOverFromEdraw() {
    QSettings now;
    QSettings old(QApplication::organizationName(), QStringLiteral("Edraw"));
    if (now.allKeys().isEmpty() && !old.allKeys().isEmpty())
        for (const QString& key : old.allKeys()) now.setValue(key, old.value(key));
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QDir before(QDir::cleanPath(root + QStringLiteral("/../Edraw")));
    if (before.exists() && (!QDir(root).exists() || QDir(root).isEmpty())) copyTree(before, QDir(root));
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Loopwire"));
    QApplication::setOrganizationName(QStringLiteral("Whatson"));
    QApplication::setApplicationVersion(QStringLiteral("0.1"));
    takeOverFromEdraw();
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/loopwire.ico"))); // taskbar / title bar

    if (QFile qss(QStringLiteral(":/loopwire.qss")); qss.open(QFile::ReadOnly | QFile::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    // A right-click menu must have a background. See installOpaqueMenus.
    Loopwire::installOpaqueMenus(&app);
    // The wheel scrolls; it never changes a figure in a box or a drop-down, in any window. See wheelScrollsNotFields.
    Loopwire::wheelScrollsNotFieldsAnywhere();

    // The logo, and what is being loaded, while the window builds itself.
    Loopwire::SplashScreen splash(Loopwire::MainWindow::startupModules());
    splash.show();

    Loopwire::MainWindow window(nullptr, [&splash](const QString& module) {
        splash.step(module);
    });
    splash.finish();
    window.show();

    const QStringList args = QApplication::arguments();
    if (args.size() > 1) window.openPath(args.at(1));

    return app.exec();
}
