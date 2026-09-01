#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QStringList>

class QProcess;
class QTimer;

namespace Edraw_Desktop {

class Document;

// ==================================================================================================
// DXF -> DWG, through the ODA File Converter.
//
// WHY AN EXTERNAL UTILITY AT ALL
//
// DWG is Autodesk's proprietary binary format. Writing it correctly is not a weekend's work, and a
// DWG writer that is *almost* right is worse than none: it produces a file that opens, reports no
// error, and is missing the drawing. That is not hypothetical - Edraw's sibling utility edo-convert
// can nominally write DWG, and a DXF -> DWG -> DXF round trip through it came back with an empty
// ENTITIES section: 210 entities in, 0 out, exit code 0, a valid AC1015 header and a plausible file
// size. Nothing about the result said it had failed. So Edraw does not write DWG itself and does not
// use edo-convert to do it; it asks a converter that is known to get it right.
//
// WHY EDRAW DOES NOT SHIP IT
//
// The ODA File Converter is free of charge, but "free of charge" is not "free to redistribute". Its
// licence is granted to the person who downloads it, from the Open Design Alliance, on acceptance of
// their terms. Bundling it in Edraw's installer would put ETS in the business of redistributing
// somebody else's licensed binary, and silently downloading it in the background would take the
// acceptance of those terms out of the user's hands - which is the same problem wearing a different
// hat. So Edraw does neither. It looks for a copy the user installed, and when there is none it says
// so and offers to open the download page, where the user meets ODA's terms themselves.
//
// The consequence to keep in mind: THE CONVERTER MAY LEGITIMATELY BE ABSENT. That is not an error
// state to be papered over. It is the normal condition on a fresh machine, and every path through
// this class has to lead somewhere useful when it holds - which is why the setup dialog offers a DXF
// export as a way out rather than leaving the user with nothing.
//
// WHY OUT OF PROCESS
//
// The same reason it is out of process for edo-convert: a separate program that we run and whose
// output we read is a program whose licence does not reach into Edraw. Nothing here links or includes
// anything of ODA's. The only things that cross the boundary are a command line, a file on disk and
// an exit code.
// ==================================================================================================
class OdaConverter : public QObject {
    Q_OBJECT

public:
    // How the converter was found. Which rule won is the useful thing to report: the absolute path
    // differs on every machine and says nothing about whether the right binary answered, whereas
    // "the path you chose" versus "installed in Program Files" is exactly what distinguishes a
    // deliberate override from a stock installation.
    enum class Origin {
        NotFound,
        Settings,           // a path the user picked, remembered from last time
        ProgramFiles,       // Windows, 64-bit install
        ProgramFilesX86,    // Windows, 32-bit install
        ApplicationBundle,  // macOS, /Applications
        SystemPath,         // Linux /usr/bin, or anywhere on PATH
    };

    struct Location {
        QString path;
        Origin origin = Origin::NotFound;
        // "ODAFileConverter 25.4" - the installation folder's own name, when the path came from one
        // that carried a version. Shown in the About/status line so two machines that behave
        // differently can be told apart without hunting through Program Files.
        QString version;
    };

    explicit OdaConverter(QObject* parent = nullptr);
    ~OdaConverter() override;

    // ---- Detection ------------------------------------------------------------------------------

    // Scans, in this order: the remembered path, then this platform's standard install locations,
    // then PATH. Cheap - a handful of stat calls - so callers may run it whenever they like rather
    // than caching the answer and going stale when the user installs the converter mid-session.
    static Location detect();

    // The remembered custom path, and how it is remembered. Empty when the user never chose one.
    static QString savedPath();
    static void savePath(const QString& path);
    static void forgetPath();

    // Is this file plausibly the converter? Checks the name and that it is executable - NOT that it
    // works, which only running it can establish. Used to keep the Browse dialog from accepting
    // notepad.exe, not as a guarantee.
    static bool looksLikeConverter(const QString& path);

    // Every place detect() looked, in order, as text. The "not found" dialog shows this: "could not
    // find it" on its own leaves the user with no move to make, whereas the list tells them where to
    // install it or what their existing install is missing.
    static QStringList searchedLocations();

    static QString describeOrigin(Origin origin);

    // The page the download button opens. Here rather than in the dialog so there is one copy of it.
    static QString downloadPageUrl();

    Location location() const { return where_; }
    bool available() const { return where_.origin != Origin::NotFound; }
    QString executablePath() const { return where_.path; }

    // Re-run detection. Returns true if the converter is available afterwards. This is what the
    // dialog's "Re-detect" button calls, after the user has gone away and installed it.
    bool redetect();

    // ---- Export ---------------------------------------------------------------------------------

    // Starts an export. ASYNCHRONOUS: converting a sheet takes long enough that waiting on the
    // process from the UI thread would freeze the window, and the work being in another process is
    // precisely what makes not waiting straightforward. The outcome arrives on finished().
    //
    // Returns false only when the export could not be STARTED - no converter, an export of the
    // drawing to DXF that failed, a temporary directory that could not be made - with *error set.
    // A false return means finished() will NOT be emitted.
    bool exportToDwg(const Document& doc, const QString& targetDwgPath, QString* error);

    // THE OTHER DIRECTION: a DWG turned into a DXF that Edraw can read.
    //
    // Asynchronous like the export, and reports on the same finished() signal. `outDxfPath` is where
    // the converted DXF is put - a caller's temporary file, normally, which it then parses and
    // deletes.
    //
    // The DWG is COPIED into a temporary directory first, and that matters. ODA converts directories,
    // not files: pointed at the folder the user picked their drawing from, it would convert every DWG
    // sitting beside it and write the lot into the output folder. The user asked for one drawing.
    bool importDwgAsDxf(const QString& dwgPath, const QString& outDxfPath, QString* error);

    bool busy() const;

    // Stops a running conversion and cleans up. finished() is emitted with ok=false.
    void cancel();

    // ---- Running it without its window ------------------------------------------------------------
    //
    // The ODA File Converter is a Qt application driven by a command line, so a conversion puts its
    // window on screen and takes the focus with it. For a batch tool that is merely untidy; for an
    // export triggered from inside another program it is worse than that, because the window steals
    // the keystrokes the user was in the middle of typing.
    //
    // Being a Qt application is also the way out: QT_QPA_PLATFORM names the platform plugin Qt loads,
    // and "offscreen" is one that draws nowhere. Setting it in the CHILD's environment only - never
    // this process's - makes the converter run with no window at all.
    //
    // It is attempted, not assumed. The offscreen plugin is a file (qoffscreen.dll) that ODA's build
    // may or may not have shipped, and when it is missing Qt refuses to start at all rather than
    // falling back. So a hidden run that produces nothing is retried visibly, once, before the export
    // is called a failure: a converter that flashes a window is a far better outcome than one that
    // does not convert. runsHidden() reports which way the last export actually went.
    static bool hiddenRunEnabled();          // the preference; true unless turned off
    static void setHiddenRunEnabled(bool on);
    bool lastRunWasHidden() const { return lastRunHidden_; }
    bool lastRunFellBackToVisible() const { return fellBackToVisible_; }

    // IS THE OFFSCREEN PLUGIN ACTUALLY THERE? ASKED, NOT ASSUMED.
    //
    // Qt loads its platform plugin from <exe dir>/platforms, and ODA's Windows build ships exactly
    // one: qwindows.dll. Asking such a build for QT_QPA_PLATFORM=offscreen does not degrade to a
    // window - it puts a MODAL error box on screen ("no Qt platform plugin could be initialized...
    // Available platform plugins are: windows") and waits for somebody to click OK. The process does
    // not exit until they do, so nothing downstream can recover: the retry that was meant to save it
    // cannot run until the user has already been shown the failure.
    //
    // That is why this is a file check and not a try-and-see. The answer is knowable before starting
    // anything, it costs one stat, and getting it wrong costs a dialog in front of every export.
    static bool offscreenPluginAvailable(const QString& converterPath);

    // Whether hiding is possible for a given converter at all, by any means this platform offers.
    // False means every export will show the converter's window, and the UI should not pretend
    // otherwise.
    static bool canRunHidden(const QString& converterPath);

    // How long a conversion may take before it is assumed to have hung. The ODA converter is a GUI
    // program driven by a command line, and a GUI program that hits an unexpected condition puts a
    // dialog on screen and waits - forever, when nobody is looking at it. Without this, that is an
    // Edraw that never finishes exporting and cannot be made to stop.
    static constexpr int kTimeoutMs = 180000;   // three minutes

    // How long the window-hiding keeps hiding before it puts everything back. A conversion that has
    // not finished in this long is very probably waiting on a dialog, and a dialog nobody can see is
    // worse than a window nobody wanted.
    static constexpr int kUnhideAfterMs = 45000;

signals:
    // ok=false carries a sentence fit to show the user; it never arrives empty.
    void finished(bool ok, const QString& error);
    // Status-line text as the export moves through its steps.
    void progress(const QString& what);

private:
    void onProcessFinished(int exitCode, int exitStatus);
    void onProcessError();
    void onTimeout();
    // Everything after the process ends: find the output, move it, clean up, emit.
    void collectResult(int exitCode, bool crashed);
    void cleanupTemp();
    void failWith(const QString& error);

    // Launches the converter over the temporary directory already prepared. `hidden` sets the child's
    // QT_QPA_PLATFORM so it draws nowhere. Returns false with *error set if it would not start.
    bool startConverter(bool hidden, QString* error);

    // WINDOWS ONLY, AND ONLY WHEN THE OFFSCREEN PLUGIN IS ABSENT.
    //
    // With no offscreen plugin there is no way to stop the converter creating a window, so the window
    // is closed after the fact instead: every few milliseconds, any top-level window belonging to the
    // child process is hidden. The window flickers into existence and goes, rather than sitting in
    // front of the drawing taking the user's keystrokes.
    //
    // The hazard this carries is real and is handled: if the converter puts up a dialog it is WAITING
    // on, hiding it would leave the process blocked on something nobody can see or answer. So the
    // hiding gives up after kUnhideAfterMs and puts back everything it hid - a converter still
    // running by then is not converting, it is asking something.
    void hideConverterWindows();
    void restoreConverterWindows();
    void stopHidingWindows();

    // The DWG the converter was asked to produce, if it is there and non-empty.
    QString producedFile() const;

    Location where_;
    QProcess* proc_ = nullptr;
    QTimer* watchdog_ = nullptr;

    // What this job is converting to - "DWG" when exporting, "DXF" when importing - and the extension
    // the converter will therefore give its output. Per job rather than constant, because the class
    // now runs in both directions and a leftover "DWG" here would have it looking for the wrong file.
    QString outputType_;
    QString producedSuffix_;

    QString tempRoot_;      // <temp>/edraw_export when exporting, <temp>/edraw_import when importing
    QString tempInput_;     // <temp>/edraw_export/input.dxf
    QString tempOutDir_;    // <temp>/edraw_export/out
    QString target_;        // where the finished .dwg is to end up
    bool reportedDone_ = false;   // finished() is emitted exactly once per export
    bool lastRunHidden_ = false;        // did the attempt just made hide the window?
    bool fellBackToVisible_ = false;    // did a hidden attempt fail and get retried visibly?

    // The window-hiding poll, and what it has hidden so far (HWNDs, held as integers so this header
    // does not have to drag in windows.h). Elapsed time is kept so the hiding can give up.
    QTimer* windowHider_ = nullptr;
    QList<quintptr> hiddenWindows_;
    qint64 hideStartedMs_ = 0;
};

} // namespace Edraw_Desktop
