// DWG export through the ODA File Converter.
//
// WHAT IS WORTH TESTING HERE, given that the converter is not on this machine and cannot be made a
// build dependency:
//
//   1. DETECTION, including its failure. "Not installed" is the state of every machine until somebody
//      installs it, so it is a normal path and not an error path, and the code has to behave itself
//      in it - report not-found, not crash, not offer an export that cannot run.
//   2. THE COMMAND-LINE CONTRACT. Six positional arguments in an order that is ODA's to define and
//      not ours. Getting it wrong compiles perfectly and fails at run time against a program the
//      developer probably does not have. tests/oda_stub.cpp asserts the arguments it is handed, so a
//      swapped pair becomes a named failure here instead.
//   3. WHAT COUNTS AS SUCCESS. The manager deliberately does not trust the exit code - ODA has been
//      known to return 0 having converted nothing, and non-zero having converted everything. Both of
//      those are simulated, because the whole point of that decision is invisible until they happen.
//   4. THE STALE-INPUT TRAP. A killed run leaves an input.dxf and an out/ behind. Converting those
//      would hand the user last week's drawing under this week's name with no error anywhere.
#include "app/MainWindow.h"
#include "io/Dxf.h"
#include "io/OdaConverter.h"
#include "model/Layer.h"
#include "model/Document.h"
#include "model/Entity.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QMenu>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int odaFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++odaFailures;
    }
}

QString stubPath() {
#ifdef Q_OS_WIN
    return QStringLiteral(EDRAW_ODA_STUB_DIR "/ODAFileConverter.exe");
#else
    return QStringLiteral(EDRAW_ODA_STUB_DIR "/ODAFileConverter");
#endif
}

// A drawing with something in it, so an empty export cannot pass by looking the same as a full one.
Document drawingWithContent() {
    Document doc;
    for (int i = 0; i < 12; ++i) {
        Entity e;
        e.type = EntityType::Line;
        e.a = Vec2{double(i), 0.0};
        e.b = Vec2{double(i), 10.0};
        doc.entities().push_back(e);
    }
    return doc;
}

QString tempRoot() { return QDir::tempPath() + QStringLiteral("/edraw_export"); }

// Runs an export to completion and reports what finished() said. Returns false on timeout, which is
// itself a failure worth distinguishing from a reported one.
bool runExport(OdaConverter& mgr, const Document& doc, const QString& target,
               bool* ok, QString* error, int waitMs = 30000) {
    QEventLoop loop;
    bool got = false;
    QObject::connect(&mgr, &OdaConverter::finished, &loop,
                     [&](bool o, const QString& e) {
                         *ok = o;
                         *error = e;
                         got = true;
                         loop.quit();
                     });
    QString startErr;
    if (!mgr.exportToDwg(doc, target, &startErr)) {
        *ok = false;
        *error = startErr;
        return true;   // reported synchronously, which is a legitimate outcome
    }
    QTimer::singleShot(waitMs, &loop, &QEventLoop::quit);
    loop.exec();
    return got;
}

// ---- Detection ------------------------------------------------------------------------------

void testDetectionWithNothingInstalled() {
    OdaConverter::forgetPath();
    const auto loc = OdaConverter::detect();
    // Not asserted to be NotFound: a developer's machine may genuinely have ODA installed, and a test
    // that failed there would be punishing the right setup. What must hold either way is that the
    // answer is self-consistent.
    if (loc.origin == OdaConverter::Origin::NotFound) {
        check(loc.path.isEmpty(), "a not-found result carries no path");
    } else {
        check(!loc.path.isEmpty(), "a found result carries a path");
        check(QFileInfo(loc.path).isExecutable(), "a found path is executable");
    }
    check(!OdaConverter::describeOrigin(loc.origin).isEmpty(),
          "every origin has a sentence describing it");

    // The dialog shows this list when it cannot find the converter; empty would leave the user with
    // "not found" and nowhere to look.
    const QStringList looked = OdaConverter::searchedLocations();
    check(!looked.isEmpty(), "the searched-locations list is not empty");
    check(looked.last().contains(QStringLiteral("PATH")), "PATH is searched last");
}

void testSavedPathRoundTrip() {
    OdaConverter::forgetPath();
    check(OdaConverter::savedPath().isEmpty(), "no saved path to begin with");

    OdaConverter::savePath(stubPath());
    check(OdaConverter::savedPath() == stubPath(), "a saved path is remembered");

    // A remembered path outranks a search: the user chose it.
    const auto loc = OdaConverter::detect();
    check(loc.origin == OdaConverter::Origin::Settings,
          "a saved path wins over the standard locations");
    check(loc.path == stubPath(), "the saved path is the one returned");

    OdaConverter::forgetPath();
}

// A remembered path to something that has since been uninstalled must not be kept. Keeping it turns
// one uninstall into every future export failing at conversion time with ODA's diagnostics rather
// than at detection time with Edraw's.
void testStaleSavedPathIsDropped() {
    QTemporaryDir tmp;
    const QString gone = tmp.path() + QStringLiteral("/ODAFileConverter.exe");
    { QFile f(gone); f.open(QIODevice::WriteOnly); f.write("x"); }
    OdaConverter::savePath(gone);
    QFile::remove(gone);

    const auto loc = OdaConverter::detect();
    check(loc.origin != OdaConverter::Origin::Settings,
          "a saved path that no longer exists is not returned");
    check(OdaConverter::savedPath().isEmpty(),
          "a saved path that no longer exists is forgotten, not left to fail later");
    OdaConverter::forgetPath();
}

void testNameCheckRejectsTheWrongFile() {
    QTemporaryDir tmp;
    const QString wrong = tmp.path() + QStringLiteral("/notepad.exe");
    { QFile f(wrong); f.open(QIODevice::WriteOnly); f.write("x"); }
    check(!OdaConverter::looksLikeConverter(wrong),
          "a file that is not named ODAFileConverter is refused");
    check(!OdaConverter::looksLikeConverter(QString()), "an empty path is refused");
    check(!OdaConverter::looksLikeConverter(tmp.path()), "a directory is refused");
    check(OdaConverter::looksLikeConverter(stubPath()),
          "the correctly-named converter is accepted");
}

// ---- Refusing to run when there is nothing to run ---------------------------------------------

void testExportWithoutConverterFailsCleanly() {
    OdaConverter::forgetPath();
    OdaConverter mgr;
    if (mgr.available()) return;   // ODA really is installed here; this case cannot be produced

    QTemporaryDir tmp;
    QString err;
    const bool started = mgr.exportToDwg(drawingWithContent(),
                                         tmp.path() + QStringLiteral("/x.dwg"), &err);
    check(!started, "an export does not start without a converter");
    check(!err.isEmpty(), "and it says why");
    check(!QDir(tempRoot()).exists(), "and leaves no temporary directory behind");
}

// ---- The whole pipeline, against the stand-in --------------------------------------------------

void testSuccessfulExport() {
    OdaConverter::savePath(stubPath());
    OdaConverter mgr;
    check(mgr.available(), "the stand-in converter is detected");

    QTemporaryDir out;
    const QString target = out.path() + QStringLiteral("/harness.dwg");
    qputenv("EDRAW_ODA_STUB_MODE", "ok");

    bool ok = false;
    QString err;
    check(runExport(mgr, drawingWithContent(), target, &ok, &err), "the export completed");
    check(ok, "the export reported success");
    check(err.isEmpty(), "a successful export reports no error");
    check(QFileInfo(target).isFile(), "the DWG is at the path the user chose");
    check(QFileInfo(target).size() > 0, "the DWG is not empty");

    // The temporary directory is not left behind. It holds the whole drawing as DXF; leaving copies
    // of the user's work in %TEMP% after every export is not something to do quietly.
    check(!QDir(tempRoot()).exists(), "the temporary directory is cleaned up");

    OdaConverter::forgetPath();
}

// The failure the manager was written around: exit code 0, no output file. Trusting the exit code
// here would report success and leave the user with no file and no message.
void testSilentFailureIsCaught() {
    OdaConverter::savePath(stubPath());
    OdaConverter mgr;
    QTemporaryDir out;
    const QString target = out.path() + QStringLiteral("/silent.dwg");
    qputenv("EDRAW_ODA_STUB_MODE", "no-output");

    bool ok = true;
    QString err;
    check(runExport(mgr, drawingWithContent(), target, &ok, &err), "the export completed");
    check(!ok, "exit code 0 with no output file is reported as a failure");
    check(!err.isEmpty(), "and it says so in words");
    check(!QFileInfo(target).exists(), "and no file is left at the destination");
    check(!QDir(tempRoot()).exists(), "and the temporary directory is cleaned up");

    qputenv("EDRAW_ODA_STUB_MODE", "ok");
    OdaConverter::forgetPath();
}

// The other half of not trusting the exit code: a non-zero exit that nevertheless produced the file.
// Refusing it would send the user away empty-handed from a conversion that worked.
void testNonZeroExitWithOutputSucceeds() {
    OdaConverter::savePath(stubPath());
    OdaConverter mgr;
    QTemporaryDir out;
    const QString target = out.path() + QStringLiteral("/despite.dwg");
    qputenv("EDRAW_ODA_STUB_MODE", "exit-fail-ok");

    bool ok = false;
    QString err;
    check(runExport(mgr, drawingWithContent(), target, &ok, &err), "the export completed");
    check(ok, "a non-zero exit that still produced the DWG is accepted");
    check(QFileInfo(target).isFile(), "and the file is delivered");

    qputenv("EDRAW_ODA_STUB_MODE", "ok");
    OdaConverter::forgetPath();
}

void testFailedExitIsReported() {
    OdaConverter::savePath(stubPath());
    OdaConverter mgr;
    QTemporaryDir out;
    const QString target = out.path() + QStringLiteral("/failed.dwg");
    qputenv("EDRAW_ODA_STUB_MODE", "exit-fail");

    bool ok = true;
    QString err;
    check(runExport(mgr, drawingWithContent(), target, &ok, &err), "the export completed");
    check(!ok, "a failed conversion is reported as a failure");
    check(err.contains(QStringLiteral("exit code")) || !err.isEmpty(),
          "the failure names what happened");

    qputenv("EDRAW_ODA_STUB_MODE", "ok");
    OdaConverter::forgetPath();
}

// A stale input.dxf and out/input.dwg from a killed run must not be mistaken for this run's work.
void testStaleTempIsNotReused() {
    OdaConverter::savePath(stubPath());

    QDir().mkpath(tempRoot() + QStringLiteral("/out"));
    {
        QFile f(tempRoot() + QStringLiteral("/out/input.dwg"));
        f.open(QIODevice::WriteOnly);
        f.write("STALE-FROM-A-PREVIOUS-RUN");
    }
    {
        QFile f(tempRoot() + QStringLiteral("/input.dxf"));
        f.open(QIODevice::WriteOnly);
        f.write("stale dxf");
    }

    OdaConverter mgr;
    QTemporaryDir out;
    const QString target = out.path() + QStringLiteral("/fresh.dwg");
    qputenv("EDRAW_ODA_STUB_MODE", "ok");

    bool ok = false;
    QString err;
    check(runExport(mgr, drawingWithContent(), target, &ok, &err), "the export completed");
    check(ok, "the export succeeded despite the stale leftovers");

    QFile produced(target);
    produced.open(QIODevice::ReadOnly);
    const QByteArray bytes = produced.readAll();
    check(!bytes.contains("STALE-FROM-A-PREVIOUS-RUN"),
          "the delivered DWG is this run's, not a leftover from a killed one");

    OdaConverter::forgetPath();
}

// An existing file at the destination is replaced. The user has already answered "replace it?" in the
// save dialog, so a rename that quietly refuses would contradict what they were told.
void testExistingDestinationIsReplaced() {
    OdaConverter::savePath(stubPath());
    OdaConverter mgr;
    QTemporaryDir out;
    const QString target = out.path() + QStringLiteral("/existing.dwg");
    {
        QFile f(target);
        f.open(QIODevice::WriteOnly);
        f.write("OLD-CONTENT-THAT-MUST-GO");
    }
    qputenv("EDRAW_ODA_STUB_MODE", "ok");

    bool ok = false;
    QString err;
    check(runExport(mgr, drawingWithContent(), target, &ok, &err), "the export completed");
    check(ok, "exporting over an existing file succeeds");

    QFile f(target);
    f.open(QIODevice::ReadOnly);
    check(!f.readAll().contains("OLD-CONTENT-THAT-MUST-GO"), "the old file was replaced");

    OdaConverter::forgetPath();
}

// ---- Running it without its window --------------------------------------------------------------
//
// THE BUG THESE EXIST FOR. Edraw asked for QT_QPA_PLATFORM=offscreen whenever hiding was wanted, and
// caught the failure afterwards. ODA's Windows build ships one platform plugin - qwindows.dll - and a
// Qt program asked for a plugin it does not have does not degrade to a window: it puts up a MODAL
// error box ("Available platform plugins are: windows") and waits. The process cannot exit until
// somebody clicks OK, so the recovery could not run until the user had already been shown the error.
// Every export got a dialog.
//
// The fix is to look for the plugin file before asking for it. These tests are about that decision -
// what environment the converter is handed - because that is the whole of the difference.

// Reads back the QT_QPA_PLATFORM the stand-in was actually given.
QString platformHandedToConverter(const QString& reportPath) {
    QFile f(reportPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QStringLiteral("(no report)");
    return QString::fromUtf8(f.readAll()).trimmed();
}

// A copy of the stand-in with a platforms/ folder beside it, so it looks like a build that DID ship
// the offscreen plugin. The file only has to exist - nothing loads it.
QString converterWithOffscreenPlugin(const QTemporaryDir& dir) {
#ifdef Q_OS_WIN
    const QString exeName = QStringLiteral("ODAFileConverter.exe");
    const QString plugin  = QStringLiteral("qoffscreen.dll");
#else
    const QString exeName = QStringLiteral("ODAFileConverter");
    const QString plugin  = QStringLiteral("libqoffscreen.so");
#endif
    const QString exe = dir.path() + QLatin1Char('/') + exeName;
    QFile::copy(stubPath(), exe);
    QDir(dir.path()).mkpath(QStringLiteral("platforms"));
    QFile f(dir.path() + QStringLiteral("/platforms/") + plugin);
    f.open(QIODevice::WriteOnly);
    f.write("not a real plugin; only its presence is read");
    f.close();
    return exe;
}

// The regression test. The stand-in has no platforms/ folder, so it stands for ODA's real Windows
// build, and Edraw must NOT ask it for the offscreen platform.
void testOffscreenNotRequestedWhenPluginMissing() {
    check(!OdaConverter::offscreenPluginAvailable(stubPath()),
          "a converter with no platforms/ folder is seen to have no offscreen plugin");

    OdaConverter::savePath(stubPath());
    OdaConverter::setHiddenRunEnabled(true);
    OdaConverter mgr;

    QTemporaryDir out;
    const QString report = out.path() + QStringLiteral("/platform.txt");
    qputenv("EDRAW_ODA_STUB_REPORT", report.toLocal8Bit());
    qputenv("EDRAW_ODA_STUB_MODE", "ok");

    bool ok = false;
    QString err;
    check(runExport(mgr, drawingWithContent(), out.path() + QStringLiteral("/a.dwg"), &ok, &err),
          "the export completed");
    check(ok, "it converted");
    check(platformHandedToConverter(report) == QStringLiteral("unset"),
          "QT_QPA_PLATFORM is NOT set when the converter has no offscreen plugin");

    qunsetenv("EDRAW_ODA_STUB_REPORT");
    OdaConverter::forgetPath();
}

// And the other way: a build that does have the plugin is asked for it, so the window never opens.
void testOffscreenRequestedWhenPluginPresent() {
    QTemporaryDir installed;
    const QString exe = converterWithOffscreenPlugin(installed);
    check(OdaConverter::offscreenPluginAvailable(exe),
          "a converter with platforms/qoffscreen is seen to have the plugin");

    OdaConverter::savePath(exe);
    OdaConverter::setHiddenRunEnabled(true);
    OdaConverter mgr;
    check(mgr.available(), "the copied converter is detected");

    QTemporaryDir out;
    const QString report = out.path() + QStringLiteral("/platform.txt");
    qputenv("EDRAW_ODA_STUB_REPORT", report.toLocal8Bit());
    qputenv("EDRAW_ODA_STUB_MODE", "ok");

    bool ok = false;
    QString err;
    check(runExport(mgr, drawingWithContent(), out.path() + QStringLiteral("/b.dwg"), &ok, &err),
          "the export completed");
    check(ok, "it converted");
    check(platformHandedToConverter(report) == QStringLiteral("offscreen"),
          "QT_QPA_PLATFORM=offscreen IS set when the plugin is there");
    check(mgr.lastRunWasHidden(), "and the run is reported as hidden");

    qunsetenv("EDRAW_ODA_STUB_REPORT");
    OdaConverter::forgetPath();
}

// The retry still earns its place: a converter that has the plugin file but still will not start on
// it must not cost the user their export.
void testFallsBackWhenOffscreenFailsAnyway() {
    QTemporaryDir installed;
    const QString exe = converterWithOffscreenPlugin(installed);
    OdaConverter::savePath(exe);
    OdaConverter::setHiddenRunEnabled(true);
    OdaConverter mgr;

    QTemporaryDir out;
    const QString target = out.path() + QStringLiteral("/fallback.dwg");
    qputenv("EDRAW_ODA_STUB_MODE", "needs-display");

    bool ok = false;
    QString err;
    check(runExport(mgr, drawingWithContent(), target, &ok, &err), "the export completed");
    check(ok, "the export still succeeded after the offscreen attempt failed");
    check(mgr.lastRunFellBackToVisible(), "it fell back to running with the window");
    check(QFileInfo(target).isFile(), "and the DWG was delivered");

    qputenv("EDRAW_ODA_STUB_MODE", "ok");
    OdaConverter::forgetPath();
}

// Turned off, the offscreen platform is never asked for even where the plugin exists.
void testHiddenRunCanBeTurnedOff() {
    QTemporaryDir installed;
    const QString exe = converterWithOffscreenPlugin(installed);
    OdaConverter::savePath(exe);
    OdaConverter::setHiddenRunEnabled(false);
    OdaConverter mgr;

    QTemporaryDir out;
    const QString report = out.path() + QStringLiteral("/platform.txt");
    qputenv("EDRAW_ODA_STUB_REPORT", report.toLocal8Bit());
    qputenv("EDRAW_ODA_STUB_MODE", "ok");

    bool ok = false;
    QString err;
    check(runExport(mgr, drawingWithContent(), out.path() + QStringLiteral("/c.dwg"), &ok, &err),
          "the export completed");
    check(ok, "the export succeeded with hiding turned off");
    check(platformHandedToConverter(report) == QStringLiteral("unset"),
          "the offscreen platform is not requested when hiding is off");

    qunsetenv("EDRAW_ODA_STUB_REPORT");
    OdaConverter::setHiddenRunEnabled(true);
    OdaConverter::forgetPath();
}

// ---- Importing a drawing into the current one ---------------------------------------------------

// A DXF merged into a drawing that already has something in it. The trap this guards is real and
// silent: dxf::parse REPLACES the entities and layers of the document it is given, because it was
// written for opening a file. Reading straight into the open document would not import a drawing, it
// would delete the one already there and report success.
void testImportDxfAddsRatherThanReplaces() {
    MainWindow w;

    // Something already on the sheet, with a shape that can be recognised afterwards.
    Document* doc = w.editDoc();
    if (!doc) { check(false, "the window has a document"); return; }

    // SOMETHING ALREADY ON THE SHEET FIRST. Without this the test proves nothing: a new drawing is
    // empty, so "after == before + 7" would hold just as well for an import that threw away what was
    // there - 0 + 7 and 7 are the same number. Seeding the document is what makes the difference
    // between adding and replacing visible at all.
    for (int i = 0; i < 5; ++i) {
        Entity existing;
        existing.type = EntityType::Circle;
        existing.layer = QStringLiteral("0");
        existing.center = Vec2{double(i), double(i)};
        existing.radius = 2.0;
        doc->entities().push_back(existing);
    }
    const int before = static_cast<int>(doc->entities().size());
    check(before >= 5, "the drawing has something in it before the import");

    // A DXF on disk, written by Edraw's own exporter so this is the real file format.
    QTemporaryDir tmp;
    Document source;
    source.entities().clear();
    for (int i = 0; i < 7; ++i) {
        Entity e;
        e.type = EntityType::Line;
        e.layer = QStringLiteral("ImportedLayer");
        e.a = Vec2{double(i) * 3.0, 1.0};
        e.b = Vec2{double(i) * 3.0, 9.0};
        source.entities().push_back(e);
    }
    source.layers().push_back(Layer{QStringLiteral("ImportedLayer"), QStringLiteral("#00ff00"), true, false});
    const QString dxfPath = tmp.path() + QStringLiteral("/incoming.dxf");
    QString exportErr;
    check(dxf::exportFile(source, dxfPath, &exportErr), "the DXF under test was written");

    QString said;
    const bool ok = w.mergeDxfIntoDrawing(dxfPath, QStringLiteral("incoming.dxf"), &said);
    check(ok, "the DXF imported");
    const int after = static_cast<int>(w.editDoc()->entities().size());
    check(after == before + 7, "the imported objects were ADDED, not swapped in for what was there");
    check(said.contains(QStringLiteral("7")), "the message says how many arrived");

    // The layer came too, or the entities would sit on a name the layers panel cannot show.
    bool haveLayer = false;
    for (const Layer& l : w.editDoc()->layers())
        if (l.name == QStringLiteral("ImportedLayer")) haveLayer = true;
    check(haveLayer, "a layer the drawing did not have is brought in with the entities");
}

// AN IMPORT DOES NOT ARRIVE SELECTED.
//
// It used to. An imported sheet is commonly a thousand objects or more, and handing the user a
// selection that size has two costs: their next drag moves the entire import, and every repaint had to
// draw a handle at every vertex of every one of them - which is what made the window crawl. Undo is
// the handle that actually matters if the import was wrong, and appendEntities pushes one.
void testImportLeavesNothingSelected() {
    MainWindow w;
    Document* doc = w.editDoc();
    if (!doc) { check(false, "the window has a document"); return; }

    QTemporaryDir tmp;
    Document source;
    source.entities().clear();
    for (int i = 0; i < 30; ++i) {
        Entity e;
        e.type = EntityType::Line;
        e.layer = QStringLiteral("0");
        e.a = Vec2{double(i), 0.0};
        e.b = Vec2{double(i), 4.0};
        source.entities().push_back(e);
    }
    const QString dxfPath = tmp.path() + QStringLiteral("/many.dxf");
    dxf::exportFile(source, dxfPath, nullptr);

    check(w.mergeDxfIntoDrawing(dxfPath, QStringLiteral("many.dxf")), "the DXF imported");
    check(w.editorForTest()->selection().empty(),
          "nothing is left selected after an import");
}

// An existing layer keeps its own settings: the drawing being imported INTO is the one whose
// conventions win, or every import would quietly restyle the sheet.
void testImportDoesNotRestyleExistingLayers() {
    MainWindow w;
    Document* doc = w.editDoc();
    if (!doc) { check(false, "the window has a document"); return; }

    const QString layerName = doc->layers().empty() ? QStringLiteral("0")
                                                    : doc->layers().front().name;
    const QString mineColour = doc->layers().empty() ? QString()
                                                     : doc->layers().front().color;

    QTemporaryDir tmp;
    Document source;
    source.entities().clear();
    Entity e;
    e.type = EntityType::Line;
    e.layer = layerName;
    e.a = Vec2{0.0, 0.0};
    e.b = Vec2{5.0, 5.0};
    source.entities().push_back(e);
    source.layers().push_back(Layer{layerName, QStringLiteral("#ff00ff"), true, false});
    const QString dxfPath = tmp.path() + QStringLiteral("/same-layer.dxf");
    dxf::exportFile(source, dxfPath, nullptr);

    w.mergeDxfIntoDrawing(dxfPath, QStringLiteral("same-layer.dxf"));

    for (const Layer& l : w.editDoc()->layers())
        if (l.name == layerName)
            check(l.color == mineColour, "an existing layer keeps the colour this drawing gave it");
}

// A file with nothing drawable in it is reported, not silently treated as a successful import of
// nothing.
void testImportOfAnEmptyDrawingIsReported() {
    MainWindow w;
    QTemporaryDir tmp;
    Document empty;
    empty.entities().clear();
    const QString dxfPath = tmp.path() + QStringLiteral("/empty.dxf");
    dxf::exportFile(empty, dxfPath, nullptr);

    QString said;
    const bool ok = w.mergeDxfIntoDrawing(dxfPath, QStringLiteral("empty.dxf"), &said);
    check(!ok, "an empty drawing is not reported as imported");
    check(said.contains(QStringLiteral("nothing"), Qt::CaseInsensitive),
          "and the message says there was nothing in it");
}

// A cancelled file dialog hands back an empty path. That is not an error and must not raise one.
void testCancelledImportDoesNothing() {
    MainWindow w;
    const int before = static_cast<int>(w.editDoc()->entities().size());
    w.importDrawingFile(QString());
    check(static_cast<int>(w.editDoc()->entities().size()) == before,
          "cancelling the file dialog changes nothing");
}

// ---- The way in from the window ----------------------------------------------------------------

void testMenuOffersDwgExport() {
    MainWindow w;

    // The exports live under an Export submenu, so the DWG entry is named for its format alone
    // ("DWG...") and carries no other clue. Looking for a DWG action anywhere would match the Open
    // entry's "Open DWX/DXF" on a bad day; what is checked is the actual structure - a menu called
    // Export, with a DWG item on it.
    const QMenu* exportMenu = nullptr;
    for (const QMenu* m : w.findChildren<QMenu*>()) {
        if (m->title().remove(QLatin1Char('&')).compare(QStringLiteral("Export"),
                                                        Qt::CaseInsensitive) == 0) {
            exportMenu = m;
            break;
        }
    }
    check(exportMenu != nullptr, "the File menu has an Export submenu");
    if (!exportMenu) return;

    QStringList formats;
    for (const QAction* a : exportMenu->actions()) {
        if (a->isSeparator()) continue;
        formats << a->text().remove(QLatin1Char('&')).remove(QStringLiteral("..."));
    }
    check(formats.contains(QStringLiteral("DWG")), "Export offers DWG");
    check(formats.contains(QStringLiteral("DXF")), "Export offers DXF");
    check(formats.contains(QStringLiteral("PDF")), "Export offers PDF");
    // The word that was removed: there is no other DWG to tell this one apart from.
    check(!formats.join(QLatin1Char(' ')).contains(QStringLiteral("native"), Qt::CaseInsensitive),
          "no entry is qualified as \"native\"");
}

} // namespace

int run_odaexport_tests() {
    std::printf("Running ODA DWG export tests...\n");
    odaFailures = 0;

    // Saved under the test application's own name (see test_main.cpp), so none of this treads on the
    // converter path somebody's real Edraw remembers.
    const QString restore = OdaConverter::savedPath();

    testDetectionWithNothingInstalled();
    testSavedPathRoundTrip();
    testStaleSavedPathIsDropped();
    testNameCheckRejectsTheWrongFile();
    testExportWithoutConverterFailsCleanly();
    testSuccessfulExport();
    testSilentFailureIsCaught();
    testNonZeroExitWithOutputSucceeds();
    testFailedExitIsReported();
    testStaleTempIsNotReused();
    testExistingDestinationIsReplaced();
    testImportDxfAddsRatherThanReplaces();
    testImportLeavesNothingSelected();
    testImportDoesNotRestyleExistingLayers();
    testImportOfAnEmptyDrawingIsReported();
    testCancelledImportDoesNothing();
    testOffscreenNotRequestedWhenPluginMissing();
    testOffscreenRequestedWhenPluginPresent();
    testFallsBackWhenOffscreenFailsAnyway();
    testHiddenRunCanBeTurnedOff();
    testMenuOffersDwgExport();

    if (restore.isEmpty()) OdaConverter::forgetPath();
    else OdaConverter::savePath(restore);
    qunsetenv("EDRAW_ODA_STUB_MODE");

    if (odaFailures == 0) std::printf("  ODA DWG export: OK\n");
    return odaFailures;
}
