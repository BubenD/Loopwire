#include "io/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace Loopwire {
namespace AppPaths {
namespace {

QString ensure(const QString& path) {
    QDir().mkpath(path);
    return path;
}

} // namespace

QString root() {
    // AppDataLocation already ends in the organisation and the application name, so nothing is
    // added here: appending them again once buried the library under Whatson/Loopwire/Whatson/Loopwire.
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString libraryDir() { return ensure(root() + QStringLiteral("/library")); }
QString templateDir() { return ensure(root() + QStringLiteral("/template")); }
QString builderDir() { return ensure(root() + QStringLiteral("/schbuilder")); }
QString blockDir() { return ensure(root() + QStringLiteral("/block")); }
QString datasheetDir() { return ensure(root() + QStringLiteral("/datasheet")); }

bool adopt(const QString& from, const QString& to) {
    if (from.isEmpty() || to.isEmpty() || from == to) return false;
    if (!QFile::exists(from) || QFile::exists(to)) return false;
    QDir().mkpath(QFileInfo(to).absolutePath());
    return QFile::rename(from, to);
}

} // namespace AppPaths
} // namespace Loopwire
