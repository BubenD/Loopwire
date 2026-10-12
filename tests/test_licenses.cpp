// THE LICENCE PAPERWORK, CHECKED LIKE ANYTHING ELSE THE PROGRAM HAS TO DO.
//
// Loopwire links Qt dynamically and ships Qt's DLLs, which makes it a combined work under the LGPL v3.
// Three things are owed in return: a notice that Qt is used and used under the LGPL, the licence texts
// themselves, and a route to Qt's source code. All three are discharged by text - a label in the About
// dialog and three documents compiled into the executable - and text is exactly the kind of thing that
// disappears without anything failing. A .qrc entry gets dropped in a merge, a file is renamed, someone
// tidies the About dialog; the build still succeeds, the program still runs, and every copy that goes out
// of the door from then on is distributed in breach of the licence. Nothing would say so.
//
// So it is said here. These are not tests of clever code - they are tests that the obligation is still
// being met, written so that failing to meet it looks like a broken build.
#include "app/MainWindow.h"
#include "geometry/Environment3D.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>

#include <cstdio>

using namespace Loopwire;

namespace {

int licFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++licFailures;
    }
}

QString resourceText(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(f.readAll());
}

// The notices are hard-wrapped to a fixed column, so any phrase in them may have a newline through the
// middle of it - "within three\nyears" reads as "three years" to a person and as neither to a substring
// search. What is being checked here is what the document SAYS, not where the lines happen to break, so
// the checks run against a copy with its whitespace collapsed.
QString flattened(const QString& text) {
    return text.simplified();
}

// ---- The three documents are in the binary, and are the documents they claim to be ----------------
//
// Checked by content, not by existence. An empty file, or a placeholder somebody meant to fill in
// later, satisfies "the resource is present" and satisfies nothing the licence asks for.
void testDocumentsArePresent() {
    const QString lgpl = resourceText(QStringLiteral(":/licenses/LGPL-3.0.txt"));
    check(!lgpl.isEmpty(), "the LGPL v3 text is compiled into the binary");
    check(lgpl.contains(QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE")),
          "the LGPL resource contains the LGPL");
    check(lgpl.contains(QStringLiteral("Version 3, 29 June 2007")), "the LGPL text is version 3");

    // The GPL is not optional and not a mistake: the LGPL v3 is drafted as a set of additional
    // permissions on top of the GPL v3, so a program supplied with only the LGPL has supplied half a
    // licence. This is the check that stops someone "tidying up" the one that looks redundant.
    const QString gpl = resourceText(QStringLiteral(":/licenses/GPL-3.0.txt"));
    check(!gpl.isEmpty(), "the GPL v3 text is compiled into the binary");
    check(gpl.contains(QStringLiteral("GNU GENERAL PUBLIC LICENSE")),
          "the GPL resource contains the GPL");
    check(gpl.contains(QStringLiteral("Version 3, 29 June 2007")), "the GPL text is version 3");

    const QString notices = resourceText(QStringLiteral(":/licenses/THIRD-PARTY-NOTICES.txt"));
    check(!notices.isEmpty(), "the third-party notices are compiled into the binary");
    check(notices.size() > 5000, "the notices document is a document, not a stub");
}

// ---- What the notices document has to actually say ------------------------------------------------
void testNoticesSayWhatIsOwed() {
    const QString n = flattened(resourceText(QStringLiteral(":/licenses/THIRD-PARTY-NOTICES.txt")));

    // The notice itself: Qt is used, and under which licence.
    check(n.contains(QStringLiteral("Qt")), "the notices name Qt");
    check(n.contains(QStringLiteral("LESSER GENERAL PUBLIC LICENSE"))
              || n.contains(QStringLiteral("Lesser General Public License")),
          "the notices name the LGPL as the licence Qt is used under");

    // The route to the source. An LGPL notice without one is the obligation half-met.
    //
    // GPL v3 §6 offers several ways to discharge this and ETS has chosen §6(d): access from a
    // designated place, which §6(d) expressly allows to be a server operated by a third party, on the
    // condition that clear directions to it sit next to the object code. That address IS those
    // directions, so it is the thing worth testing. There is deliberately no three-year written offer -
    // that is §6(b), an alternative meant for object code shipped on physical media, and ETS is not
    // making a commitment it does not owe.
    //
    // Note the duty §6(d) leaves behind: ETS remains obliged to ensure the source stays available for
    // as long as needed, whoever hosts it.
    check(n.contains(QStringLiteral("download.qt.io")),
          "the notices give the designated place to obtain Qt's source (GPL v3 §6(d))");
    check(n.contains(QStringLiteral("designated place")),
          "the notices say that address IS the designated place, not a courtesy link");
    check(!n.contains(QStringLiteral("three years")),
          "no three-year written offer is made (§6(b) was not the route chosen)");

    // The version, so the source offer points at the source that was actually shipped. Read from Qt
    // rather than typed, and compared against Qt, so a Qt upgrade with a stale notices file fails here.
    check(n.contains(QStringLiteral(QT_VERSION_STR)),
          "the notices name the Qt version actually linked (regenerate with "
          "resources/notices/gen_notices.py after a Qt upgrade)");

    // The components whose licences ask, in terms, for their notices to be carried. If the generator
    // is ever pointed at the wrong place these are the ones that go quiet first.
    check(n.contains(QStringLiteral("FreeType Project")), "FreeType is credited by name (FTL asks for it)");
    check(n.contains(QStringLiteral("LibPNG")) || n.contains(QStringLiteral("libpng")),
          "libpng is credited");
    check(n.contains(QStringLiteral("HarfBuzz")), "HarfBuzz is credited");
    check(n.contains(QStringLiteral("PCRE2")), "PCRE2 is credited");
    check(n.contains(QStringLiteral("Public Suffix List")),
          "the Public Suffix List is credited (MPL-2.0, inside Qt6Network)");
    check(n.contains(QStringLiteral("Unicode")), "the Unicode tables are credited");

    // WHY THE GPL TEXT IS THERE, SAID OUT LOUD. Shipping the GNU GPL beside a proprietary product
    // invites exactly one question - "is there GPL code in this?" - and the answer is no: the GPL is
    // supplied because LGPL §4(b) requires it, the LGPL being a set of additional permissions on top of
    // the GPL rather than a standalone licence. A reader who cannot find that explanation is left to
    // draw the wrong conclusion from a 35 KB GPL sitting in the install folder, so the explanation is
    // part of the notice and is tested like the rest of it.
    check(n.contains(QStringLiteral("Loopwire contains no GPL-licensed code")),
          "the notices state plainly that there is no GPL code in the product");
    check(n.contains(QStringLiteral("incorporates the terms and conditions")),
          "the notices explain that the LGPL incorporates the GPL, which is why the GPL ships");

    // Our own borrowing. The zlib licence asks that the origin not be misrepresented; this is where
    // that is said to the user rather than only in a source comment.
    check(n.contains(QStringLiteral("Mark Adler")),
          "the DEFLATE decoder's origin is acknowledged (zlib licence)");

    // Components that belong to Qt modules Loopwire does not ship. Crediting them is not harmless
    // generosity - it tells the reader that code is inside a binary they were never given.
    // OPEN CASCADE: named, its licence and its exception named, and the place for its source given.
    check(n.contains(QStringLiteral("Open CASCADE Technology")), "the notices name Open CASCADE Technology");
    check(n.contains(QStringLiteral("version 2.1")) && n.contains(QStringLiteral("Open CASCADE exception")),
          "the notices name the LGPL 2.1 and the Open CASCADE exception as the terms it is used under");
    check(n.contains(QStringLiteral("github.com/Open-Cascade-SAS/OCCT/releases/tag/V8.0.1")),
          "the notices give the place to obtain Open CASCADE's source, for the version shipped");
    check(n.contains(QStringLiteral("Qt OpenGL")), "Qt OpenGL is among the Qt modules named (the 3D view uses it)");
    check(n.contains(QStringLiteral("built without any of Open CASCADE's")),
          "the notices say Open CASCADE comes with none of its optional third-party libraries");
    check(!n.contains(QStringLiteral("Catch2")), "Qt Test's Catch2 is not falsely credited");
    check(!n.contains(QStringLiteral("litehtml")), "Qt Tools' litehtml is not falsely credited");
}

// ---- The About dialog gives the notice, and reaches the licences ----------------------------------
//
// §4(a) asks for the notice to be given "with each copy of the combined work". A file in the folder is
// necessary but a user never opens it; the label in About is the part a person actually sees, so its
// presence is worth a test of its own.
void testAboutGivesTheNotice() {
    MainWindow w;

    bool sawNotice = false;
    bool sawLicenceButton = false;
    bool sawOcct = false;

    // About is modal, so it is inspected from a timer that fires once it is up and then closes it.
    QTimer::singleShot(0, &w, [&] {
        QDialog* dlg = nullptr;
        for (QWidget* top : QApplication::topLevelWidgets()) {
            auto* d = qobject_cast<QDialog*>(top);
            if (d && d->isVisible() && d->windowTitle() == QStringLiteral("About Loopwire")) { dlg = d; break; }
        }
        if (!dlg) { check(false, "the About dialog opened"); return; }

        for (const QLabel* l : dlg->findChildren<QLabel*>()) {
            const QString t = l->text();
            // The notice has to say all three things: that Qt is used, which version, and that the
            // LGPL covers it. Any one of them alone is not the notice.
            if (t.contains(QStringLiteral("Qt")) && t.contains(QStringLiteral(QT_VERSION_STR))
                && t.contains(QStringLiteral("Lesser General Public License"))
                // §4(c): the Library's own copyright notice, and a pointer to the licence copies.
                && t.contains(QStringLiteral("The Qt Company"))
                && t.contains(QStringLiteral("licenses"))) {
                sawNotice = true;
            }
            // Open CASCADE's: used, under the LGPL 2.1 and its exception, its copyright, where the texts are.
            if (t.contains(QStringLiteral("Open CASCADE Technology")) && t.contains(QStringLiteral("Open CASCADE SAS"))
                && t.contains(QStringLiteral("v2.1")) && t.contains(QStringLiteral("exception"))
                && t.contains(QStringLiteral("licenses"))) {
                sawOcct = true;
            }
        }
        for (const QPushButton* b : dlg->findChildren<QPushButton*>()) {
            if (b->text().contains(QStringLiteral("licence"), Qt::CaseInsensitive)
                || b->text().contains(QStringLiteral("license"), Qt::CaseInsensitive)) {
                sawLicenceButton = true;
            }
        }
        dlg->accept();
    });

    w.showAbout();

    check(sawNotice, "About gives the full §4 notice: Qt, its version, the LGPL, Qt's "
          "copyright, and where the licence copies are");
    check(sawLicenceButton, "About offers a way through to the licence texts");
    check(sawOcct == fullStepReader(), "About gives Open CASCADE's notice exactly where this build ships it");
}

// ---- The licence viewer shows all three documents -------------------------------------------------
void testLicenceViewerShowsEverything() {
    MainWindow w;

    int tabCount = 0;
    bool allNonEmpty = true;
    bool sawIncompleteWarning = false;

    QTimer::singleShot(0, &w, [&] {
        QDialog* dlg = nullptr;
        for (QWidget* top : QApplication::topLevelWidgets()) {
            auto* d = qobject_cast<QDialog*>(top);
            if (d && d->isVisible() && d->windowTitle() == QStringLiteral("Third-party licences")) {
                dlg = d; break;
            }
        }
        if (!dlg) { check(false, "the licences dialog opened"); return; }

        if (auto* tabs = dlg->findChild<QTabWidget*>()) {
            tabCount = tabs->count();
            for (int i = 0; i < tabs->count(); ++i) {
                auto* view = qobject_cast<QPlainTextEdit*>(tabs->widget(i));
                // The Open CASCADE exception is one paragraph; every other document runs to pages.
                const int least = tabs->tabText(i).contains(QStringLiteral("exception")) ? 400 : 1000;
                if (!view || view->toPlainText().size() < least) allNonEmpty = false;
                // The dialog says so loudly when a resource is missing rather than showing a blank
                // box. If that message is on screen, the notice is NOT being given.
                if (view && view->toPlainText().contains(QStringLiteral("incomplete"))) {
                    sawIncompleteWarning = true;
                }
            }
        }
        dlg->accept();
    });

    w.showLicenses();

    check(tabCount == 5, "the viewer shows the notices, the LGPL and the GPL, and Open CASCADE's LGPL 2.1 and its exception");
    check(allNonEmpty, "every licence tab has its text");
    check(!sawIncompleteWarning, "no licence document is missing from the binary");
}

// ---- About is the only route, so About had better be a route --------------------------------------
//
// There is deliberately no separate menu entry for the licences: §4 asks for a prominent notice, for
// Qt's copyright among the ones the program shows with a reference to where the licence copies are, and
// for the texts to ACCOMPANY the program - which the files beside the executable do on their own. That
// makes the button inside About the single way through from the UI, and a single way through is a thing
// worth testing, because there is nothing to fall back on if it goes.
void testAboutIsTheRouteToTheLicences() {
    MainWindow w;

    bool aboutIsReachable = false;
    for (const QAction* a : w.findChildren<QAction*>()) {
        if (a->text().contains(QStringLiteral("About Loopwire"))) aboutIsReachable = true;
    }
    check(aboutIsReachable, "About Loopwire is reachable from the menu");

    // And the button on it actually opens the licences, rather than merely being labelled as if it did.
    bool licencesOpened = false;
    QTimer::singleShot(0, &w, [&] {
        QDialog* about = nullptr;
        for (QWidget* top : QApplication::topLevelWidgets()) {
            auto* d = qobject_cast<QDialog*>(top);
            if (d && d->isVisible() && d->windowTitle() == QStringLiteral("About Loopwire")) { about = d; break; }
        }
        if (!about) { check(false, "the About dialog opened"); return; }

        QPushButton* licBtn = nullptr;
        for (QPushButton* b : about->findChildren<QPushButton*>()) {
            if (b->text().contains(QStringLiteral("licence"), Qt::CaseInsensitive)
                || b->text().contains(QStringLiteral("license"), Qt::CaseInsensitive)) {
                licBtn = b;
            }
        }
        if (!licBtn) { check(false, "About carries the licences button"); about->accept(); return; }

        // The licences dialog is modal too, so it is caught and closed from a further timer.
        QTimer::singleShot(0, &w, [&] {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                auto* d = qobject_cast<QDialog*>(top);
                if (d && d->isVisible() && d->windowTitle() == QStringLiteral("Third-party licences")) {
                    licencesOpened = true;
                    d->accept();
                }
            }
        });
        licBtn->click();       // closes About and opens the licences
    });

    w.showAbout();

    check(licencesOpened, "the button in About opens the third-party licences");
}

} // namespace

int run_licenses_tests() {
    std::printf("Running licence-compliance tests...\n");
    licFailures = 0;
    testDocumentsArePresent();
    testNoticesSayWhatIsOwed();
    testAboutGivesTheNotice();
    testLicenceViewerShowsEverything();
    testAboutIsTheRouteToTheLicences();
    if (licFailures == 0) std::printf("  licence compliance: OK\n");
    return licFailures;
}
