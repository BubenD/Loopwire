#include "io/OdaConverter.h"

#include "io/Dxf.h"
#include "model/Document.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
// For CREATE_NO_WINDOW / STARTF_USESHOWWINDOW / SW_HIDE. NOMINMAX and WIN32_LEAN_AND_MEAN because
// windows.h otherwise defines min and max as macros and drags in half the platform SDK, both of
// which break Qt headers that have every right to use std::min.
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace Loopwire {

namespace {

// The executable's name, which is the same on every platform ODA ships for except that Windows puts
// an extension on it.
#ifdef Q_OS_WIN
const char* const kExeName = "ODAFileConverter.exe";
#else
const char* const kExeName = "ODAFileConverter";
#endif

// Where a custom path is remembered. Under io/ rather than ui/ because it is not a preference about
// how the window looks - it is a fact about this machine's installation.
const char* const kSettingsKey = "io/odaFileConverterPath";

// Whether the converter is run without its window. On by default; the escape hatch exists because
// this depends on a Qt plugin in somebody else's build, and a user who hits a case the automatic
// fallback does not cover needs a way to turn it off without waiting for a new Loopwire.
const char* const kHiddenRunKey = "io/odaRunHidden";

// ODA's own vocabulary for the command line. Named rather than written inline at the call site: the
// version string is not free text, and "ACAD2018" mistyped as "ACAD 2018" is rejected by the
// converter with a message about arguments that says nothing about which one.
const char* const kOutputVersion = "ACAD2018";
// No kOutputType constant: the output format is per job now - "DWG" leaving Loopwire, "DXF" coming in -
// and a constant here would be one of the two silently winning.
const char* const kRecurse       = "0";   // one directory, which is the temporary one we just made
const char* const kAudit         = "1";   // let it repair what it can rather than refusing the file

// The DXF handed to the converter. The name is fixed because the converter names its output after
// its input, so a known input name is what makes the output findable.
const char* const kInputStem = "input";

// Directories on Windows that hold installed 64-bit and 32-bit programs.
//
// Read from the environment rather than written as "C:\Program Files", because that is only where
// they are on a machine that put Windows on C: with an English locale. Getting this wrong does not
// produce a wrong answer, it produces "not installed" on a machine where it is installed, and then a
// user who has done everything right is told to go and download it again. The literal paths remain
// as a fallback for a stripped environment.
QStringList windowsProgramDirs(bool wow64) {
    QStringList out;
    const char* var = wow64 ? "ProgramFiles(x86)" : "ProgramFiles";
    const QString fromEnv = qEnvironmentVariable(var);
    if (!fromEnv.isEmpty()) out << fromEnv;
    out << (wow64 ? QStringLiteral("C:/Program Files (x86)") : QStringLiteral("C:/Program Files"));
    out.removeDuplicates();
    return out;
}

// ODA installs into a versioned folder - "ODAFileConverter 25.4" - so the converter is never at a
// fixed path. Every matching folder is considered and the LAST by name wins, which for ODA's
// numbering is the newest. A machine with three versions installed should use the newest rather than
// whichever the directory listing happened to hand back first.
QString newestUnder(const QString& odaDir) {
    QDir dir(odaDir);
    if (!dir.exists()) return QString();
    const QStringList subs = dir.entryList({QStringLiteral("ODAFileConverter*")},
                                           QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (auto it = subs.crbegin(); it != subs.crend(); ++it) {
        const QString candidate = dir.filePath(*it) + QLatin1Char('/')
                                  + QString::fromLatin1(kExeName);
        if (QFileInfo(candidate).isExecutable()) return candidate;
    }
    // Some installers drop it straight into ODA/ with no version folder.
    const QString flat = dir.filePath(QString::fromLatin1(kExeName));
    return QFileInfo(flat).isExecutable() ? flat : QString();
}

// The version folder's name, for display. Empty when the path did not come from one.
QString versionFromPath(const QString& exePath) {
    const QString folder = QFileInfo(exePath).dir().dirName();
    return folder.startsWith(QStringLiteral("ODAFileConverter")) ? folder : QString();
}

} // namespace

// ==================================================================================================
// Detection
// ==================================================================================================

QString OdaConverter::savedPath() {
    return QSettings().value(QLatin1String(kSettingsKey)).toString();
}

void OdaConverter::savePath(const QString& path) {
    QSettings().setValue(QLatin1String(kSettingsKey), path);
}

void OdaConverter::forgetPath() {
    QSettings().remove(QLatin1String(kSettingsKey));
}

bool OdaConverter::looksLikeConverter(const QString& path) {
    if (path.isEmpty()) return false;
    const QFileInfo fi(path);
    if (!fi.isFile() || !fi.isExecutable()) return false;
    // By name, case-insensitively: the point is to catch the user picking the wrong file in the
    // Browse dialog, not to authenticate the binary. A renamed copy that really is the converter
    // will be rejected here, which is the safer way round - it fails at the file dialog with an
    // explanation rather than at conversion time with ODA's own diagnostics.
    return fi.completeBaseName().compare(QStringLiteral("ODAFileConverter"), Qt::CaseInsensitive) == 0;
}

QString OdaConverter::downloadPageUrl() {
    return QStringLiteral("https://www.opendesign.com/guestfiles/oda_file_converter");
}

QStringList OdaConverter::searchedLocations() {
    QStringList out;
    const QString saved = savedPath();
    if (!saved.isEmpty()) out << QStringLiteral("The path set in Loopwire: %1").arg(saved);
#ifdef Q_OS_WIN
    for (const QString& dir : windowsProgramDirs(false))
        out << QDir::toNativeSeparators(dir + QStringLiteral("/ODA/ODAFileConverter*/")
                                        + QString::fromLatin1(kExeName));
    for (const QString& dir : windowsProgramDirs(true))
        out << QDir::toNativeSeparators(dir + QStringLiteral("/ODA/ODAFileConverter*/")
                                        + QString::fromLatin1(kExeName));
#elif defined(Q_OS_MACOS)
    out << QStringLiteral("/Applications/ODAFileConverter.app/Contents/MacOS/ODAFileConverter");
#else
    out << QStringLiteral("/usr/bin/ODAFileConverter");
    out << QStringLiteral("/usr/local/bin/ODAFileConverter");
#endif
    out << QStringLiteral("Anywhere on the system PATH");
    return out;
}

OdaConverter::Location OdaConverter::detect() {
    Location found;

    // 1. What the user chose last time, first: a deliberate choice outranks anything found by
    //    searching, including a newer version installed elsewhere. If it has since been uninstalled
    //    the entry is dropped rather than kept to fail later.
    const QString saved = savedPath();
    if (!saved.isEmpty()) {
        if (QFileInfo(saved).isExecutable()) {
            found.path = saved;
            found.origin = Origin::Settings;
            found.version = versionFromPath(saved);
            return found;
        }
        forgetPath();
    }

#ifdef Q_OS_WIN
    for (const QString& base : windowsProgramDirs(false)) {
        const QString hit = newestUnder(base + QStringLiteral("/ODA"));
        if (!hit.isEmpty()) {
            found.path = hit;
            found.origin = Origin::ProgramFiles;
            found.version = versionFromPath(hit);
            return found;
        }
    }
    for (const QString& base : windowsProgramDirs(true)) {
        const QString hit = newestUnder(base + QStringLiteral("/ODA"));
        if (!hit.isEmpty()) {
            found.path = hit;
            found.origin = Origin::ProgramFilesX86;
            found.version = versionFromPath(hit);
            return found;
        }
    }
#elif defined(Q_OS_MACOS)
    const QString bundle =
        QStringLiteral("/Applications/ODAFileConverter.app/Contents/MacOS/ODAFileConverter");
    if (QFileInfo(bundle).isExecutable()) {
        found.path = bundle;
        found.origin = Origin::ApplicationBundle;
        return found;
    }
#else
    for (const QString& p : {QStringLiteral("/usr/bin/ODAFileConverter"),
                             QStringLiteral("/usr/local/bin/ODAFileConverter")}) {
        if (QFileInfo(p).isExecutable()) {
            found.path = p;
            found.origin = Origin::SystemPath;
            return found;
        }
    }
#endif

    // 2. PATH, last. Costs nothing and covers the user who installed it somewhere of their own and
    //    put it on PATH, which is a reasonable thing to have done and would otherwise not be found.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("ODAFileConverter"));
    if (!onPath.isEmpty()) {
        found.path = onPath;
        found.origin = Origin::SystemPath;
        found.version = versionFromPath(onPath);
    }
    return found;
}

QString OdaConverter::describeOrigin(Origin origin) {
    switch (origin) {
    case Origin::NotFound:          return QStringLiteral("not found");
    case Origin::Settings:          return QStringLiteral("the path set in Loopwire");
    case Origin::ProgramFiles:      return QStringLiteral("installed in Program Files");
    case Origin::ProgramFilesX86:   return QStringLiteral("installed in Program Files (x86)");
    case Origin::ApplicationBundle: return QStringLiteral("installed in Applications");
    case Origin::SystemPath:        return QStringLiteral("found on the system PATH");
    }
    return QStringLiteral("not found");
}

// ==================================================================================================
// Lifetime
// ==================================================================================================

OdaConverter::OdaConverter(QObject* parent) : QObject(parent) {
    where_ = detect();
}

OdaConverter::~OdaConverter() {
    // A converter still running when Loopwire closes is killed rather than left parented to nothing.
    // It is a GUI program: an orphan would sit on the user's desktop with no window they can
    // associate with anything.
    if (proc_ && proc_->state() != QProcess::NotRunning) {
        stopHidingWindows();
        proc_->kill();
        proc_->waitForFinished(2000);
    }
    cleanupTemp();
}

bool OdaConverter::redetect() {
    where_ = detect();
    return available();
}

bool OdaConverter::busy() const {
    return proc_ && proc_->state() != QProcess::NotRunning;
}

// ==================================================================================================
// Export
// ==================================================================================================

bool OdaConverter::exportToDwg(const Document& doc, const QString& targetDwgPath,
                                      QString* error) {
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };

    if (busy()) return fail(QStringLiteral("A DWG export is already running."));
    if (!available())
        return fail(QStringLiteral("The ODA File Converter was not found on this computer."));
    if (targetDwgPath.isEmpty()) return fail(QStringLiteral("No destination was given."));

    // ---- The temporary directory ----------------------------------------------------------------
    //
    // Cleared rather than merely created. A previous run that was killed, or an Loopwire that crashed
    // mid-export, leaves an input.dxf and an out/ behind; converting a stale input and then moving a
    // stale output to the user's chosen filename would hand them last week's drawing under this
    // week's name, with nothing anywhere reporting a problem.
    tempRoot_ = QDir::tempPath() + QStringLiteral("/loopwire_export");
    cleanupTemp();
    QDir tmp(tempRoot_);
    if (!tmp.mkpath(QStringLiteral("out")))
        return fail(QStringLiteral("Could not create the temporary folder:\n%1")
                        .arg(QDir::toNativeSeparators(tempRoot_)));

    tempInput_  = tempRoot_ + QLatin1Char('/') + QString::fromLatin1(kInputStem)
                  + QStringLiteral(".dxf");
    tempOutDir_ = tempRoot_ + QStringLiteral("/out");
    target_     = targetDwgPath;
    outputType_ = QStringLiteral("DWG");
    producedSuffix_ = QStringLiteral(".dwg");
    reportedDone_ = false;

    // ---- Step 1: the drawing, as DXF -------------------------------------------------------------
    emit progress(QStringLiteral("Preparing the drawing..."));
    QString dxfError;
    if (!dxf::exportFile(doc, tempInput_, &dxfError)) {
        cleanupTemp();
        return fail(dxfError.isEmpty()
                        ? QStringLiteral("The drawing could not be written as DXF.")
                        : QStringLiteral("The drawing could not be written as DXF:\n%1").arg(dxfError));
    }

    // ---- Step 2: the converter -------------------------------------------------------------------
    fellBackToVisible_ = false;
    QString startError;
    if (!startConverter(hiddenRunEnabled(), &startError)) {
        cleanupTemp();
        return fail(startError);
    }
    return true;
}

bool OdaConverter::importDwgAsDxf(const QString& dwgPath, const QString& outDxfPath,
                                  QString* error) {
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };

    if (busy()) return fail(QStringLiteral("A conversion is already running."));
    if (!available())
        return fail(QStringLiteral("The ODA File Converter was not found on this computer."));
    if (outDxfPath.isEmpty()) return fail(QStringLiteral("No destination was given."));
    if (!QFileInfo(dwgPath).isFile())
        return fail(QStringLiteral("That file could not be read:\n%1")
                        .arg(QDir::toNativeSeparators(dwgPath)));

    tempRoot_ = QDir::tempPath() + QStringLiteral("/loopwire_import");
    cleanupTemp();
    if (!QDir(tempRoot_).mkpath(QStringLiteral("out")))
        return fail(QStringLiteral("Could not create the temporary folder:\n%1")
                        .arg(QDir::toNativeSeparators(tempRoot_)));

    // A COPY, UNDER A KNOWN NAME. Two reasons, and both are the difference between converting one
    // drawing and converting a folder:
    //
    //   ODA is pointed at a DIRECTORY. Given the one the user picked their file from, it would
    //   convert every DWG in it - somebody's whole drawings folder - and the user asked for one.
    //
    //   The converter names its output after its input, so a fixed input name is what makes the
    //   output findable without guessing at how the original was spelled.
    tempInput_ = tempRoot_ + QLatin1Char('/') + QString::fromLatin1(kInputStem)
                 + QStringLiteral(".dwg");
    if (!QFile::copy(dwgPath, tempInput_)) {
        cleanupTemp();
        return fail(QStringLiteral("The drawing could not be copied for conversion:\n%1")
                        .arg(QDir::toNativeSeparators(dwgPath)));
    }

    tempOutDir_ = tempRoot_ + QStringLiteral("/out");
    target_     = outDxfPath;
    outputType_ = QStringLiteral("DXF");
    producedSuffix_ = QStringLiteral(".dxf");
    reportedDone_ = false;
    fellBackToVisible_ = false;

    emit progress(QStringLiteral("Reading the DWG..."));
    QString startError;
    if (!startConverter(hiddenRunEnabled(), &startError)) {
        cleanupTemp();
        return fail(startError);
    }
    return true;
}

// The command line, and the environment that decides whether a window appears.
bool OdaConverter::startConverter(bool hidden, QString* error) {
    // The process and the watchdog are made here rather than by the callers, because BOTH directions
    // arrive at this function and only one of them used to make them. An import reached a null proc_.
    if (!proc_) {
        proc_ = new QProcess(this);
        connect(proc_, &QProcess::finished, this, &OdaConverter::onProcessFinished);
        connect(proc_, &QProcess::errorOccurred, this, &OdaConverter::onProcessError);
    }
    if (!watchdog_) {
        watchdog_ = new QTimer(this);
        watchdog_->setSingleShot(true);
        connect(watchdog_, &QTimer::timeout, this, &OdaConverter::onTimeout);
    }

    // Arguments as separate entries, never a joined command string: there is no shell here, so a
    // temporary path containing a space needs no quoting and must not be given any.
    //
    // The order is ODA's and is not negotiable:
    //   input dir, output dir, output version, output type, recurse, audit
    const QStringList args = {
        QDir::toNativeSeparators(tempRoot_),
        QDir::toNativeSeparators(tempOutDir_),
        QString::fromLatin1(kOutputVersion),
        outputType_,
        QString::fromLatin1(kRecurse),
        QString::fromLatin1(kAudit),
    };

    // THE OFFSCREEN PLUGIN IS ASKED FOR ONLY WHEN IT IS THERE.
    //
    // This used to set QT_QPA_PLATFORM whenever hiding was wanted and let the failure be caught
    // downstream. On a converter whose build has no offscreen plugin - which is ODA's Windows build,
    // shipping qwindows.dll and nothing else - that failure is a MODAL dialog the user has to
    // dismiss before the process will even exit. The recovery could not run until the user had
    // already been shown the error, so the effect was an error box in front of every single export.
    //
    // Whether the plugin exists is knowable beforehand for the cost of one stat, so it is checked.
    const bool useOffscreen = hidden && offscreenPluginAvailable(where_.path);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (useOffscreen) {
        // THE CHILD'S ENVIRONMENT ONLY. Qt reads QT_QPA_PLATFORM once, at start-up, to choose the
        // plugin that owns the screen; "offscreen" owns none, so the converter runs with no window.
        // Setting it here rather than with qputenv matters - qputenv would change THIS process too,
        // and Loopwire is the program with the window the user is looking at.
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    } else {
        // Never inherited from a system environment that already had it: Loopwire's own test suite runs
        // offscreen, and a converter that inherited that would hit the very dialog this avoids.
        env.remove(QStringLiteral("QT_QPA_PLATFORM"));
    }
    proc_->setProcessEnvironment(env);

#ifdef Q_OS_WIN
    // Belt to the environment's braces, and cheap. CREATE_NO_WINDOW suppresses a console if the
    // converter allocates one; the SW_HIDE show-state covers a window created without Qt's help.
    // Neither can hide a window that Qt itself shows - that is what the offscreen plugin is for -
    // so this is an addition to that mechanism and not a substitute for it.
    proc_->setCreateProcessArgumentsModifier(
        [hidden](QProcess::CreateProcessArguments* cpa) {
            if (!hidden) return;
            cpa->flags |= CREATE_NO_WINDOW;
            if (cpa->startupInfo) {
                cpa->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
                cpa->startupInfo->wShowWindow = SW_HIDE;
            }
        });
#endif

    lastRunHidden_ = hidden && canRunHidden(where_.path);
    emit progress(QStringLiteral("Converting to DWG..."));
    watchdog_->start(kTimeoutMs);
    proc_->start(where_.path, args);
    if (!proc_->waitForStarted(10000)) {
        watchdog_->stop();
        stopHidingWindows();
        if (error) {
            *error = QStringLiteral("The ODA File Converter would not start:\n%1\n\n%2")
                         .arg(QDir::toNativeSeparators(where_.path), proc_->errorString());
        }
        return false;
    }

    // With no offscreen plugin, the window cannot be prevented - only closed once it appears. Started
    // after the process, because there is nothing to look for until there is a process to own it.
    if (hidden && !useOffscreen) {
        hiddenWindows_.clear();
        hideStartedMs_ = QDateTime::currentMSecsSinceEpoch();
        if (!windowHider_) {
            windowHider_ = new QTimer(this);
            windowHider_->setInterval(50);
            connect(windowHider_, &QTimer::timeout, this,
                    &OdaConverter::hideConverterWindows);
        }
        windowHider_->start();
    }
    return true;
}

// ==================================================================================================
// Hiding the converter's window when its build gives us no way to stop it having one
// ==================================================================================================

bool OdaConverter::offscreenPluginAvailable(const QString& converterPath) {
    if (converterPath.isEmpty()) return false;
    // Qt looks for its platform plugins in <exe dir>/platforms. macOS keeps them inside the bundle
    // at Contents/PlugIns/platforms, and the converter path there points into Contents/MacOS.
    const QDir exeDir = QFileInfo(converterPath).dir();
    const QStringList candidates = {
        exeDir.filePath(QStringLiteral("platforms/qoffscreen.dll")),
        exeDir.filePath(QStringLiteral("platforms/libqoffscreen.so")),
        exeDir.filePath(QStringLiteral("platforms/libqoffscreen.dylib")),
        exeDir.filePath(QStringLiteral("../PlugIns/platforms/libqoffscreen.dylib")),
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) return true;
    return false;
}

bool OdaConverter::canRunHidden(const QString& converterPath) {
    if (offscreenPluginAvailable(converterPath)) return true;
#ifdef Q_OS_WIN
    // No plugin, but the window can still be taken off the screen after it opens.
    return !converterPath.isEmpty();
#else
    // Elsewhere there is no equivalent that does not amount to running a nested X server, which is
    // more than an export is worth. The window is shown, and the UI says so rather than pretending.
    return false;
#endif
}

#ifdef Q_OS_WIN
namespace {
struct HideScan {
    DWORD pid = 0;
    QList<quintptr>* hidden = nullptr;
};

BOOL CALLBACK hideIfChildWindow(HWND hwnd, LPARAM param) {
    auto* scan = reinterpret_cast<HideScan*>(param);
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner == scan->pid && IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
        const auto handle = reinterpret_cast<quintptr>(hwnd);
        if (!scan->hidden->contains(handle)) scan->hidden->append(handle);
    }
    return TRUE;
}
} // namespace
#endif

void OdaConverter::hideConverterWindows() {
#ifdef Q_OS_WIN
    if (!proc_ || proc_->state() != QProcess::Running) { stopHidingWindows(); return; }

    // GIVING UP ON PURPOSE. A conversion still going after this long is not converting - it is asking
    // something, behind a dialog that has been hidden and so cannot be answered. Everything hidden
    // goes back on screen and the hiding stops, which turns an invisible deadlock into a visible
    // question. The watchdog still has the last word if nobody answers it.
    if (QDateTime::currentMSecsSinceEpoch() - hideStartedMs_ > kUnhideAfterMs) {
        restoreConverterWindows();
        stopHidingWindows();
        emit progress(QStringLiteral("The converter is asking something - see its window."));
        return;
    }

    HideScan scan;
    scan.pid = static_cast<DWORD>(proc_->processId());
    scan.hidden = &hiddenWindows_;
    if (scan.pid != 0) EnumWindows(&hideIfChildWindow, reinterpret_cast<LPARAM>(&scan));
#endif
}

void OdaConverter::restoreConverterWindows() {
#ifdef Q_OS_WIN
    for (quintptr handle : hiddenWindows_) {
        HWND hwnd = reinterpret_cast<HWND>(handle);
        if (IsWindow(hwnd)) ShowWindow(hwnd, SW_SHOW);
    }
#endif
    hiddenWindows_.clear();
}

void OdaConverter::stopHidingWindows() {
    if (windowHider_) windowHider_->stop();
    hiddenWindows_.clear();
}

QString OdaConverter::producedFile() const {
    const QString produced = tempOutDir_ + QLatin1Char('/') + QString::fromLatin1(kInputStem)
                             + producedSuffix_;
    const QFileInfo fi(produced);
    return (fi.isFile() && fi.size() > 0) ? produced : QString();
}

bool OdaConverter::hiddenRunEnabled() {
    return QSettings().value(QLatin1String(kHiddenRunKey), true).toBool();
}

void OdaConverter::setHiddenRunEnabled(bool on) {
    QSettings().setValue(QLatin1String(kHiddenRunKey), on);
}

void OdaConverter::cancel() {
    if (!busy()) return;
    if (watchdog_) watchdog_->stop();
    stopHidingWindows();
    proc_->kill();
    proc_->waitForFinished(2000);
    failWith(QStringLiteral("The DWG export was cancelled."));
}

void OdaConverter::onTimeout() {
    if (!busy()) return;
    // Whatever it had on screen is the explanation for the hang, so it goes back on screen before the
    // process is killed rather than being destroyed along with it.
    restoreConverterWindows();
    stopHidingWindows();
    proc_->kill();
    proc_->waitForFinished(2000);
    failWith(QStringLiteral(
        "The ODA File Converter did not finish within %1 minutes and was stopped.\n\n"
        "It is a windowed program, so it may be waiting on a message box of its own. Try running it "
        "once by hand on this drawing to see what it asks.")
                 .arg(kTimeoutMs / 60000));
}

void OdaConverter::onProcessError() {
    if (reportedDone_) return;
    if (watchdog_) watchdog_->stop();
    // A start failure is reported synchronously by exportToDwg; anything else lands here.
    if (proc_->error() == QProcess::FailedToStart) return;
    failWith(QStringLiteral("The ODA File Converter failed:\n%1").arg(proc_->errorString()));
}

void OdaConverter::onProcessFinished(int exitCode, int exitStatus) {
    if (watchdog_) watchdog_->stop();
    stopHidingWindows();
    collectResult(exitCode, exitStatus == QProcess::CrashExit);
}

void OdaConverter::collectResult(int exitCode, bool crashed) {
    if (reportedDone_) return;

    // WHAT COUNTS AS SUCCESS, AND WHY IT IS NOT THE EXIT CODE.
    //
    // The obvious test is exitCode == 0, and on its own it is not safe in either direction. The ODA
    // converter is a GUI application wearing a command line, and across versions it has been
    // reported to return 0 after converting nothing and non-zero after converting everything asked
    // of it. The file it was supposed to produce is the thing that actually matters, so that is what
    // is checked; the exit code is used only to explain a failure the missing file has already
    // established.
    const QString produced = producedFile();
    const bool haveOutput = !produced.isEmpty();

    // THE HIDDEN RUN'S ONE RETRY.
    //
    // Running the converter with QT_QPA_PLATFORM=offscreen depends on ODA's build having shipped the
    // offscreen plugin. When it has not, Qt does not fall back to a window - it refuses to start at
    // all, with "this application failed to start because no Qt platform plugin could be
    // initialized". From out here that is indistinguishable from any other failure to produce a file,
    // and it is not worth distinguishing: whatever went wrong on a hidden attempt, the thing to try
    // next is the attempt that does not depend on the plugin.
    //
    // Once only, and only when hiding was actually attempted, so a converter that genuinely cannot
    // convert this drawing fails twice as fast as it would otherwise and no faster.
    if (!haveOutput && lastRunHidden_ && !fellBackToVisible_) {
        fellBackToVisible_ = true;
        emit progress(QStringLiteral("Retrying with the converter's window..."));
        QString startError;
        if (startConverter(/*hidden=*/false, &startError)) return;   // collectResult runs again
        failWith(startError);
        return;
    }

    if (!haveOutput) {
        QString why;
        if (crashed) {
            why = QStringLiteral("The ODA File Converter stopped unexpectedly.");
        } else {
            why = QStringLiteral("The ODA File Converter ran (exit code %1) but produced no %2.")
                      .arg(exitCode).arg(outputType_);
            if (fellBackToVisible_) {
                why += QStringLiteral("\n\nThis was tried both without and with the converter's "
                                      "window, so its window is not the cause.");
            }
            // Its own words, when it left any. Truncated: ODA can be verbose, and a dialog is not
            // the place for pages of it.
            const QString err = QString::fromLocal8Bit(proc_->readAllStandardError()).trimmed();
            if (!err.isEmpty()) why += QStringLiteral("\n\n%1").arg(err.left(600));
        }
        failWith(why);
        return;
    }

    // ---- Step 3: into place ---------------------------------------------------------------------
    emit progress(QStringLiteral("Saving..."));

    // An existing destination is removed first: QFile::rename will not overwrite, and the user has
    // already answered the "replace it?" question in the save dialog.
    if (QFile::exists(target_) && !QFile::remove(target_)) {
        failWith(QStringLiteral("The drawing was converted, but the existing file could not be "
                                "replaced:\n%1\n\nIs it open in another program?")
                     .arg(QDir::toNativeSeparators(target_)));
        return;
    }

    // rename() is a move within a volume and fails across them - the temporary directory and the
    // user's chosen folder are very often on different drives - so a failed rename falls back to a
    // copy. Not an error until both have been tried.
    bool moved = QFile::rename(produced, target_);
    if (!moved) {
        moved = QFile::copy(produced, target_);
        if (moved) QFile::remove(produced);
    }
    if (!moved) {
        failWith(QStringLiteral("The drawing was converted, but could not be saved to:\n%1")
                     .arg(QDir::toNativeSeparators(target_)));
        return;
    }

    cleanupTemp();
    reportedDone_ = true;
    emit finished(true, QString());
}

void OdaConverter::failWith(const QString& error) {
    if (reportedDone_) return;
    reportedDone_ = true;
    cleanupTemp();
    emit finished(false, error);
}

void OdaConverter::cleanupTemp() {
    if (tempRoot_.isEmpty()) return;
    // Guarded, because this removes a directory tree: only ever the one this class made, never
    // whatever tempRoot_ might have been set to by a future edit.
    const QString here = QDir::cleanPath(tempRoot_);
    if (here != QDir::cleanPath(QDir::tempPath() + QStringLiteral("/loopwire_export"))
        && here != QDir::cleanPath(QDir::tempPath() + QStringLiteral("/loopwire_import"))) return;
    QDir(tempRoot_).removeRecursively();
}

} // namespace Loopwire
