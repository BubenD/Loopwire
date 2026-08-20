// The canvas a default model is drawn on. A model could be drawn on the sheet and captured from it, but
// the sheet is a drawing of a harness and a model is a piece of the vocabulary those drawings are
// written in; mixing them means finding somewhere clear on a drawing to work and remembering to clear up
// afterwards. So the model gets a canvas of its own - the same canvas the application draws on, with the
// same tools, over a scratch drawing that holds nothing else - and what is drawn there comes back in the
// model's own coordinates.
#include "app/Editor.h"
#include "app/MainWindow.h"
#include "model/Document.h"
#include "app/MechModelCanvasDialog.h"
#include "harness/MechModels.h"
#include "harness/PartsLibrary.h"
#include "render/CanvasWidget.h"
#include "tools/ToolId.h"

#include <QApplication>
#include <QLineEdit>
#include <QTabBar>
#include <QToolButton>

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int canvasFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++canvasFailures;
    }
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

} // namespace

int run_mechmodelcanvas_tests() {
    std::printf("model canvas tests:\n");

    // A boot, with its taper already decided, opened to be drawn.
    MechModel m;
    m.kind = MechModelKind::Boot;
    m.headMm = 6.0;
    m.tailMm = 8.0;
    MechModelCanvasDialog dlg(m);

    check(dlg.findChild<CanvasWidget*>() != nullptr, "canvas: the window has a canvas of its own");
    check(dlg.findChildren<QToolButton*>().size() >= 6,
          "canvas: with the tools to draw a model on it");
    check(dlg.drawing().isEmpty(), "canvas: nothing drawn yet, so nothing to keep");

    auto* editor = dlg.findChild<Editor*>();
    check(editor != nullptr, "canvas: and an editor of its own driving it");
    if (!editor) {
        std::printf("  -> %d failed\n", ++canvasFailures);
        return canvasFailures;
    }

    // A boot: a neck, a cone, a body - drawn well away from the origin, to prove a model is stored in
    // its own coordinates rather than wherever on the canvas it happened to be drawn.
    struct Seg { Vec2 a, b; };
    const Seg segs[] = {
        {{100, 202}, {106, 202}},
        {{107, 203}, {114, 206}},
        {{115, 206}, {130, 206}},
    };
    for (const Seg& s : segs) {
        editor->setTool(ToolId::Line);   // armed for each one: a click, then a click
        editor->onPointerPress(s.a, Qt::LeftButton, Qt::NoModifier);
        editor->onPointerPress(s.b, Qt::LeftButton, Qt::NoModifier);
    }
    check(near(dlg.drawnLengthMm(), 30.0, 0.5), "canvas: what was drawn measures 30 mm long");
    check(near(dlg.drawnAcrossMm(), 4.0, 0.5), "canvas: and 4 mm across");

    const QJsonArray taken = dlg.drawing();
    check(taken.size() == 3, "canvas: the three lines come back as the model");
    {
        Bounds b;
        for (const Entity& e : partDrawingEntities(taken)) {
            const Bounds eb = e.bounds();
            if (eb.valid) {
                b.expand(eb.min);
                b.expand(eb.max);
            }
        }
        check(b.valid && near(b.min.x, 0.0), "canvas: moved so the model starts at x = 0");
        check(b.valid && near(b.min.y + b.max.y, 0.0), "canvas: and is centred across");
    }
    // The dashed marks that showed where the ends are do not become part of the model.
    bool guideKept = false;
    for (const Entity& e : partDrawingEntities(taken))
        if (e.layer != QLatin1String("0")) guideKept = true;
    check(!guideKept, "canvas: the marks showing the ends are left behind, not kept");

    // And what comes off the canvas stretches like any other model.
    MechModel done = m;
    done.drawing = taken;
    done.drawnLengthMm = dlg.drawnLengthMm();
    done.drawnAcrossMm = dlg.drawnAcrossMm();
    double maxX = -1e9;
    for (const Entity& e : mechModelEntities(done, Vec2{0, 0}, 0.0, 200.0))
        maxX = std::max({maxX, e.a.x, e.b.x});
    check(near(maxX, 200.0), "canvas: drawn here, it stretches to the length asked for");

    // The canvas hides the OS pointer because it draws its own crosshair, and a child widget with no
    // cursor of its own INHERITS that. So anything put on the canvas - the Model / Layout tabs along the
    // bottom, the field a length is typed into, the inline text editor - lost the pointer the moment it
    // was moved onto: no crosshair, because that is only painted over the drawing, and no arrow either.
    {
        CanvasWidget canvas;
        check(canvas.cursor().shape() == Qt::BlankCursor,
              "cursor: the canvas itself hides the pointer");
        auto* row = new QWidget(&canvas);
        auto* tabs = new QTabBar(row);
        tabs->addTab(QStringLiteral("Model"));
        tabs->addTab(QStringLiteral("Layout1"));
        canvas.setBottomOverlay(row);
        check(row->cursor().shape() == Qt::ArrowCursor,
              "cursor: an overlay on it has a pointer of its own");
        check(tabs->cursor().shape() != Qt::BlankCursor,
              "cursor: and what is on the overlay inherits that, not the blank one");
        bool sawField = false;
        for (QLineEdit* le : canvas.findChildren<QLineEdit*>()) {
            sawField = true;
            check(le->cursor().shape() == Qt::IBeamCursor,
                  "cursor: the field a value is typed into shows a text cursor");
        }
        check(sawField, "cursor: the canvas has that field");
    }


    // ---- the model canvas fits the PART, and keeps where it was left --------------------------------
    {
        // Fit looks at the drawing's stated sheet first, which is right for a drawing - being the thing you look
        // at is what a sheet is FOR - and wrong for this window, which holds one part a few millimetres across. A
        // sheet round it opened the window on a page of empty grey with the part a speck in the middle of it.
        //
        // And how a part is being looked at has to survive the window closing. Drawing one is not a single
        // sitting: it is opened, zoomed in on the end that matters, closed to check something, opened again.
        MainWindow win;
        win.resize(1200, 800);
        win.show();
        QApplication::processEvents();
        auto* canvas = win.findChild<CanvasWidget*>();
        auto* ed = win.findChild<Editor*>();
        check(canvas != nullptr && ed != nullptr, "grey: there is a canvas and an editor");
        if (canvas && ed) {
            win.openMechModelEditor(MechModelKind::Label, QString());
            QApplication::processEvents();
            Document* modelDoc = ed->document();
            check(modelDoc != nullptr, "grey: the model opened in a window of its own");
            if (modelDoc) {
                // Nothing has been captured for this kind on this machine, so the session opens empty - which is
                // the ordinary way to start one. A part is drawn on it: that is what the window is for.
                Entity drawn;
                drawn.id = QStringLiteral("part");
                drawn.type = EntityType::Line;
                drawn.a = {0.0, 0.0};
                drawn.b = {12.0, 5.0};
                modelDoc->entities().push_back(drawn);
                check(!modelDoc->entities().empty(), "grey: with a part drawn on it");
                check(!modelDoc->sheet().valid,
                      "grey: and no sheet round it - a part is not a sheet, so Fit fits the part");
                canvas->fitView();
                Bounds part;
                for (const Entity& e : modelDoc->entities()) part.expand(e.bounds());
                check(part.valid, "grey: the part has an extent");
                const double onScreen = std::max(part.width(), part.height()) * modelDoc->view().scale;
                const double win2 = std::min(canvas->width(), canvas->height());
                std::printf("  grey: fitted, the part is %.0f px across in a window %.0f px\n", onScreen, win2);
                check(onScreen > win2 * 0.4,
                      "grey: it fills the window rather than sitting a speck in the middle of a fitted sheet");

                // Zoomed in on some corner of it, then closed and opened again: the same view.
                modelDoc->view().scale *= 4.0;
                modelDoc->view().x -= 137.0;
                modelDoc->view().y += 61.0;
                const ViewState was = modelDoc->view();
                win.cancelMechModelEdit();
                QApplication::processEvents();
                win.openMechModelEditor(MechModelKind::Label, QString());
                QApplication::processEvents();
                Document* again = ed->document();
                check(again != nullptr, "grey: it opened again");
                if (again) {
                    std::printf("  grey: left at %.2f/%.0f/%.0f, came back at %.2f/%.0f/%.0f\n", was.scale,
                                was.x, was.y, again->view().scale, again->view().x, again->view().y);
                    check(std::abs(again->view().scale - was.scale) < 1e-9 &&
                              std::abs(again->view().x - was.x) < 1e-6 &&
                              std::abs(again->view().y - was.y) < 1e-6,
                          "grey: and came back looking at exactly what it was left looking at");
                }
                win.cancelMechModelEdit();
                QApplication::processEvents();
            }
        }
    }


    // ---- the marks: a fixed size, hideable, and removable one at a time -----------------------------
    {
        // A MARK IS CHROME. It says where something is, and a mark that changes size when you move a line reads as
        // though the mark itself had been edited - which it had not. Its size used to be a proportion of the
        // drawing's extent AS IT STOOD, and the marks are rebuilt every time anything on the canvas changes, so
        // they swelled and shrank as the part was drawn.
        MainWindow win;
        win.resize(1200, 800);
        win.show();
        QApplication::processEvents();
        auto* ed = win.findChild<Editor*>();
        check(ed != nullptr, "mark: there is an editor");
        if (!ed) return canvasFailures;

        win.openMechModelEditor(MechModelKind::Boot, QStringLiteral("Straight"));
        QApplication::processEvents();
        Document* d = ed->document();
        check(d != nullptr, "mark: the model opened");
        if (!d) return canvasFailures;

        // A part is drawn, and the connection point marked on it.
        Entity line;
        line.id = QStringLiteral("body");
        line.type = EntityType::Line;
        line.a = {0.0, 0.0};
        line.b = {40.0, 12.0};
        d->entities().push_back(line);
        win.markMechModelConnectAt(Vec2{0.0, 0.0}, 0.0);
        QApplication::processEvents();

        auto markRadius = [&]() {
            for (const Entity& e : d->entities())
                if (e.meta.value(QStringLiteral("modelAnchor")).toString() == QStringLiteral("connect"))
                    return e.radius;
            return 0.0;
        };
        const double before = markRadius();
        check(before > 0.0, "mark: the connection point is marked and drawn");

        // Now a primitive is MOVED - which is all that happened. The mark must be the size it was.
        // Made three times as TALL, which is what the old size was a proportion of: moved alone it would not have
        // discriminated, the extent being the same height in a different place.
        // Grown by a quarter, which is the ordinary consequence of moving a line about while drawing - and exactly
        // the change that used to move the marks. A part drawn ten times bigger is a different question and is
        // allowed to change them.
        for (Entity& e : d->entities())
            if (e.id == QStringLiteral("body")) { e.a.y -= 1.5; e.b.y += 1.5; }
        win.markMechModelConnectAt(Vec2{0.0, 0.0}, 0.0);   // the marks are rebuilt, as any change rebuilds them
        QApplication::processEvents();
        const double after = markRadius();
        std::printf("  mark: %.2f before the drawing was moved, %.2f after\n", before, after);
        check(std::abs(after - before) < 1e-9,
              "mark: moving a primitive does not change the size of the marks");

        // THE EYE: the marks put away and brought back, and the button saying which.
        check(win.mechModelMarksVisibleForTest(), "mark: they start shown");
        win.toggleMechModelMarksForTest();
        QApplication::processEvents();
        check(!win.mechModelMarksVisibleForTest(), "mark: the eye puts them away");
        win.toggleMechModelMarksForTest();
        QApplication::processEvents();
        check(win.mechModelMarksVisibleForTest(), "mark: and brings them back");

        // AND TAKEN OFF. A mark in the wrong place could only be moved, never removed - and a model is placed BY
        // its marks, so one that should not be there is a part put down in the wrong place.
        check(markRadius() > 0.0, "mark: the connection point is still there to take off");
        win.clearMechModelConnectForTest();
        QApplication::processEvents();
        std::printf("  mark: after taking the connection point off, %.2f\n", markRadius());
        check(markRadius() == 0.0, "mark: taken off, it is gone rather than merely moved");
        win.cancelMechModelEdit();
        QApplication::processEvents();
    }

    if (canvasFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", canvasFailures);
    return canvasFailures;
}
