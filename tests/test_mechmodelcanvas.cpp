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

    if (canvasFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", canvasFailures);
    return canvasFailures;
}
