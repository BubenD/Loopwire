#include "app/FontLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QStringListModel>

#include <algorithm>

namespace Loopwire {

namespace {
QStringList loadLibrary() {
    QStringList out;
    // 1) Bundled font files: <exe dir>/fonts/*.ttf|*.otf|*.ttc — drop files
    //    there to extend the library without rebuilding.
    const QDir dir(QCoreApplication::applicationDirPath() + QStringLiteral("/fonts"));
    const QStringList patterns = {QStringLiteral("*.ttf"), QStringLiteral("*.otf"),
                                  QStringLiteral("*.ttc")};
    for (const QFileInfo& fi : dir.entryInfoList(patterns, QDir::Files)) {
        const int id = QFontDatabase::addApplicationFont(fi.absoluteFilePath());
        if (id >= 0) out += QFontDatabase::applicationFontFamilies(id);
    }
    // 2) Curated standard families, kept only if actually installed here.
    const QStringList curated = {
        QStringLiteral("Segoe UI"),   QStringLiteral("Arial"),
        QStringLiteral("Calibri"),    QStringLiteral("Cambria"),
        QStringLiteral("Consolas"),   QStringLiteral("Courier New"),
        QStringLiteral("Georgia"),    QStringLiteral("Lucida Console"),
        QStringLiteral("Tahoma"),     QStringLiteral("Times New Roman"),
        QStringLiteral("Trebuchet MS"), QStringLiteral("Verdana")};
    const QStringList installed = QFontDatabase::families();
    for (const QString& f : curated)
        if (installed.contains(f)) out << f;
    out.removeDuplicates();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    if (out.isEmpty()) out = installed; // never leave a picker empty
    return out;
}
} // namespace

QStringList FontLibrary::families() {
    static const QStringList fams = loadLibrary();
    return fams;
}

void FontLibrary::applyTo(QFontComboBox* box) {
    if (!box) return;
    const QFont keep = box->currentFont();
    // QFontComboBox is backed by a string-list model of family names; replacing
    // the list restricts the picker to the library (each item still previews in
    // its own typeface via the font delegate).
    if (auto* m = qobject_cast<QStringListModel*>(box->model()))
        m->setStringList(families());
    else {
        box->clear();
        box->addItems(families());
    }
    box->setCurrentFont(keep);
}

} // namespace Loopwire
