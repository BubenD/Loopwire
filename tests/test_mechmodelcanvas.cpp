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
#include "app/PartsLibraryDialog.h"
#include "harness/MechModels.h"
#include "harness/PartsLibrary.h"
#include "render/CanvasWidget.h"
#include "tools/ToolId.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPair>
#include <QTabWidget>
#include <QMouseEvent>
#include <QLineEdit>
#include <QTabBar>
#include <QToolButton>
#include <QLabel>

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


    // ---- panning drags the paper, and does nothing else ---------------------------------------------
    {
        // A middle-drag is dragging the paper. Nothing is being pointed at, nothing is being picked, and no object
        // snap, grip search or overlay preview means anything until the paper stops moving - and every one of them
        // was being done on every tick of the pan, over the whole drawing, on the one gesture where the drawing is
        // guaranteed not to have changed. Which is why panning a big sheet felt heavy while moving the cursor over
        // that same sheet did not.
        MainWindow win;
        win.resize(1000, 700);
        win.show();
        QApplication::processEvents();
        auto* canvas = win.findChild<CanvasWidget*>();
        auto* ed = win.findChild<Editor*>();
        check(canvas != nullptr && ed != nullptr, "pan: there is a canvas and an editor");
        if (!canvas || !ed) return canvasFailures;
        Document* d = ed->document();
        check(d != nullptr, "pan: and a drawing");
        if (!d) return canvasFailures;

        // Something to snap to, right where the pan will pass over it.
        Entity l;
        l.id = QStringLiteral("l");
        l.type = EntityType::Line;
        l.a = {0.0, 0.0};
        l.b = {100.0, 0.0};
        d->entities().push_back(l);
        auto& snaps = ed->snapSettings();
        snaps.objectSnap = true;   // on, so a cursor move over that line WOULD snap to it

        // Where the cursor thinks it is before the pan.
        const Vec2 before = ed->snappedCursor();
        const double vx = d->view().x, vy = d->view().y;

        // Middle-drag: press, move, release.
        const QPointF from(400, 350), to(460, 320);
        QMouseEvent down(QEvent::MouseButtonPress, from, canvas->mapToGlobal(from), Qt::MiddleButton,
                         Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &down);
        QMouseEvent move(QEvent::MouseMove, to, canvas->mapToGlobal(to), Qt::NoButton, Qt::MiddleButton,
                         Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);

        // THE PAPER MOVED, by exactly what the mouse did.
        std::printf("  pan: dragged %+.0f,%+.0f - the view moved %+.0f,%+.0f\n", to.x() - from.x(),
                    to.y() - from.y(), d->view().x - vx, d->view().y - vy);
        check(std::abs((d->view().x - vx) - (to.x() - from.x())) < 1e-6 &&
                  std::abs((d->view().y - vy) - (to.y() - from.y())) < 1e-6,
              "pan: the paper follows the mouse exactly");

        // AND NOTHING WAS SNAPPED. The editor's idea of where the cursor is has not moved, because during a pan
        // it is not being asked - which is the work that was being thrown away.
        std::printf("  pan: the cursor was left at %.2f,%.2f, where it was\n", ed->snappedCursor().x,
                    ed->snappedCursor().y);
        check(distance(ed->snappedCursor(), before) < 1e-9,
              "pan: and the snap was not run - a pan points at nothing, so there is nothing to snap to");

        QMouseEvent up(QEvent::MouseButtonRelease, to, canvas->mapToGlobal(to), Qt::MiddleButton,
                       Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &up);

        // Let go of, an ordinary move snaps again: the pan turned it off for the pan and not for good.
        QMouseEvent after(QEvent::MouseMove, to, canvas->mapToGlobal(to), Qt::NoButton, Qt::NoButton,
                          Qt::NoModifier);
        QApplication::sendEvent(canvas, &after);
        check(distance(ed->snappedCursor(), before) > 1e-9,
              "pan: and once the button is let go the cursor is being followed again");
    }


    // ---- panning shifts the paper it has already drawn, instead of drawing it again -----------------
    {
        // Dragging the paper does not change one line of the drawing: every pixel of it is still valid, just
        // somewhere else on the screen. Re-rendering the whole sheet on every tick of the drag - which is what
        // was happening - costs the sheet sixty times a second, and on a drawing where one render takes longer
        // than a frame the paper cannot help but fall behind the hand. So it is COPIED to where it now belongs
        // and only the strip along the leading edge, which has never been drawn, is drawn.
        //
        // Two things have to be true of that, and neither is obvious: the drag must not re-render (or nothing
        // has been saved), and what it leaves on the screen must be EXACTLY what a re-render would have left
        // (or the saving is bought with a wrong picture).
        MainWindow win;
        win.resize(900, 640);
        win.show();
        QApplication::processEvents();
        auto* canvas = win.findChild<CanvasWidget*>();
        auto* ed = win.findChild<Editor*>();
        if (!canvas || !ed || !ed->document()) {
            check(false, "scroll: there is a canvas, an editor and a drawing");
            return canvasFailures;
        }
        Document* d = ed->document();
        d->entities().clear();
        // Enough on the sheet, and spread widely enough, that the strip coming into view has real content in
        // it - a pan that reveals nothing would pass whatever the code did.
        for (int i = 0; i < 60; ++i) {
            Entity e;
            e.id = QStringLiteral("e%1").arg(i);
            e.type = (i % 3 == 0) ? EntityType::Circle : EntityType::Line;
            const double x = -300.0 + i * 11.0, y = -200.0 + (i % 17) * 25.0;
            e.a = {x, y};
            e.b = {x + 40.0, y + 18.0};
            e.radius = 6.0;
            d->entities().push_back(e);
        }
        canvas->invalidateScene();
        canvas->grab();   // drawn once, in full
        const int rebuiltBefore = canvas->sceneRebuildsForTest();

        // Drag the paper across in several ticks, the way a hand does, painting between them.
        const QPointF start(500, 400);
        QMouseEvent down(QEvent::MouseButtonPress, start, canvas->mapToGlobal(start), Qt::MiddleButton,
                         Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &down);
        QPointF at = start;
        for (int i = 0; i < 6; ++i) {
            at += QPointF(9, -7);
            QMouseEvent mv(QEvent::MouseMove, at, canvas->mapToGlobal(at), Qt::NoButton, Qt::MiddleButton,
                           Qt::NoModifier);
            QApplication::sendEvent(canvas, &mv);
            canvas->grab();   // force the paint the frame limiter would have made
        }

        std::printf("  scroll: six ticks of a drag re-rendered the drawing %d time(s)\n",
                    canvas->sceneRebuildsForTest() - rebuiltBefore);
        check(canvas->sceneRebuildsForTest() == rebuiltBefore,
              "scroll: the drag did not re-render the drawing once - it moved what was already drawn");

        // What is on the screen NOW, arrived at by shifting six times.
        const QImage shifted = canvas->sceneImageForTest();
        // And what the same view looks like drawn from nothing.
        canvas->invalidateScene();
        canvas->grab();
        const QImage rendered = canvas->sceneImageForTest();
        check(canvas->sceneRebuildsForTest() > rebuiltBefore, "scroll: and the reference WAS re-rendered");

        long long differing = 0;
        const long long total = static_cast<long long>(shifted.width()) * shifted.height();
        check(shifted.size() == rendered.size() && total > 0, "scroll: two pictures of the same size");
        if (shifted.size() == rendered.size()) {
            for (int y = 0; y < shifted.height(); ++y)
                for (int x = 0; x < shifted.width(); ++x) {
                    const QRgb a = shifted.pixel(x, y), b = rendered.pixel(x, y);
                    // A line drawn up against the edge of a strip can be a shade off where it was blended
                    // over what was already there; a MISSING line is not a shade off.
                    if (std::abs(qRed(a) - qRed(b)) > 24 || std::abs(qGreen(a) - qGreen(b)) > 24 ||
                        std::abs(qBlue(a) - qBlue(b)) > 24)
                        ++differing;
                }
        }
        std::printf("  scroll: %lld of %lld pixels differ from a full re-render (%.3f%%)\n", differing, total,
                    100.0 * static_cast<double>(differing) / static_cast<double>(total ? total : 1));
        check(total > 0 && differing * 1000 < total,
              "scroll: the shifted paper is the same picture a re-render would have drawn");

        QMouseEvent up(QEvent::MouseButtonRelease, at, canvas->mapToGlobal(at), Qt::MiddleButton,
                       Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &up);
    }

    // ---- a library part's mechanical drawing gets a real session too: grey canvas, seeded marks, and
    // a save that writes straight back into the library dialog's own catalog ------------------------
    {
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_libpart_session.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-SESSION");
            part.kind = PartKind::Connector;
            part.description = QStringLiteral("Session test connector");
            Entity body;
            body.id = QStringLiteral("body");
            body.type = EntityType::Line;
            body.a = {0.0, -5.0};
            body.b = {30.0, 5.0};
            part.mechanical = partDrawingFromEntities({body});
            lib.parts.push_back(part);
            lib.save(libFile);
        }

        MainWindow win;
        win.resize(1200, 800);
        win.show();
        QApplication::processEvents();
        auto* canvas = win.findChild<CanvasWidget*>();
        auto* ed = win.findChild<Editor*>();
        check(canvas != nullptr && ed != nullptr, "library session: there is a canvas and an editor");
        if (canvas && ed) {
            PartsLibraryDialog* dlg = win.partsLibraryDialogForTest();
            check(dlg != nullptr, "library session: the library dialog is reachable");
            if (dlg) {
                dlg->setLibraryPath(libFile);
                const QColor normal = canvas->backgroundColor();

                win.openLibraryPartEditor(QStringLiteral("P-SESSION"), LibraryPartDrawing::Mechanical);
                QApplication::processEvents();

                check(canvas->backgroundColor() != normal,
                      "library session: the canvas goes grey, the same as any other session");

                Document* d = ed->document();
                check(d != nullptr, "library session: the drawing opened in a document of its own");
                if (d) {
                    int marks = 0;
                    for (const Entity& e : d->entities())
                        if (!e.meta.value(QStringLiteral("modelAnchor")).toString().isEmpty()) ++marks;
                    std::printf("  library session: %d mark(s) seeded on opening\n", marks);
                    check(marks == 3, "library session: connect, near and far are seeded the same way "
                                       "a default model's are");

                    // ON A PART the group is its connection point: called that, no "Scale by" (a part out
                    // of the catalogue is the size it is), and a Remove all that takes every mark off.
                    QLabel* title = nullptr;
                    for (QLabel* l : win.findChildren<QLabel*>(QStringLiteral("ribbonGroupTitle")))
                        if (l->text() == QStringLiteral("Connection point")) title = l;
                    check(title != nullptr, "library session: the marks' group is called \"Connection point\"");
                    bool scaleShown = false;
                    for (QToolButton* b : win.findChildren<QToolButton*>())
                        if (b->text() == QStringLiteral("Scale\nby") && b->isVisibleTo(&win)) scaleShown = true;
                    check(!scaleShown, "library session: and it offers no \"Scale by\"");
                    auto* removeAll = win.findChild<QToolButton*>(QStringLiteral("removeAllMarks"));
                    check(removeAll && removeAll->isVisibleTo(&win) && removeAll->isEnabled(),
                          "library session: a Remove all button is there to press");
                    if (removeAll) {
                        removeAll->click();
                        QApplication::processEvents();
                        int left = 0;
                        for (const Entity& e : d->entities())
                            if (!e.meta.value(QStringLiteral("modelAnchor")).toString().isEmpty()) ++left;
                        check(left == 0, "library session: Remove all takes every mark off");
                    }

                    // A drag on the canvas would move the mark and let Editor::changed read it back;
                    // driven directly here, the same way the default-model marks are tested above.
                    win.markMechModelConnectAt(Vec2{2.0, -5.0}, 0.0);
                    QApplication::processEvents();

                    // THE WHOLE DRAWING MOVED: its connection point goes with it.
                    ed->clearSelection();
                    for (const Entity& e : d->entities())
                        if (e.layer != QLatin1String(kMechModelGuideLayer)) ed->selectById(e.id, true);
                    ed->setTool(ToolId::Move);
                    ed->onPointerPress(Vec2{0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
                    ed->onPointerRelease(Vec2{0.0, 0.0}, Qt::LeftButton);
                    ed->onPointerMove(Vec2{10.0, 0.0});
                    ed->onPointerPress(Vec2{10.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
                    ed->onPointerRelease(Vec2{10.0, 0.0}, Qt::LeftButton);
                    ed->setTool(ToolId::Select);
                    QApplication::processEvents();

                    // SHIFT + CLICK PICKS A MARK - a point or an entry face - not the drawing under it, and
                    // a picked face can be deleted like anything else.
                    {
                        emit ed->modelOutputPicked(Vec2{20.0, -3.0}, Vec2{20.0, 3.0});
                        QApplication::processEvents();
                        auto markOf = [d](const char* key) {
                            for (const Entity& e : d->entities())
                                if (e.meta.contains(QLatin1String(key))) return e;
                            return Entity{};
                        };
                        const Entity connect = markOf("modelAnchor");
                        const Entity bar = markOf("modelOutput");
                        check(!connect.id.isEmpty() && !bar.id.isEmpty(), "shift pick: a point and a face on the part");
                        ed->clearSelection();
                        ed->onPointerPress(connect.center, Qt::LeftButton, Qt::ShiftModifier);
                        ed->onPointerRelease(connect.center, Qt::LeftButton);
                        ed->onPointerPress(lerp(bar.a, bar.b, 0.5), Qt::LeftButton, Qt::ShiftModifier);
                        ed->onPointerRelease(lerp(bar.a, bar.b, 0.5), Qt::LeftButton);
                        const auto& sel = ed->selection();
                        std::printf("  shift pick: %d picked\n", static_cast<int>(sel.size()));
                        check(sel.size() == 2 && std::find(sel.begin(), sel.end(), connect.id) != sel.end() &&
                                  std::find(sel.begin(), sel.end(), bar.id) != sel.end(),
                              "shift pick: Shift+click takes the point and the face, not the drawing");
                        ed->clearSelection();
                        ed->onPointerPress(lerp(bar.a, bar.b, 0.5), Qt::LeftButton, Qt::ShiftModifier);
                        ed->onPointerRelease(lerp(bar.a, bar.b, 0.5), Qt::LeftButton);
                        ed->deleteSelected();
                        QApplication::processEvents();
                        check(markOf("modelOutput").id.isEmpty() && !markOf("modelAnchor").id.isEmpty(),
                              "shift pick: Delete takes the picked face off and leaves the point");
                    }

                    win.saveMechModelBeingEdited(); // Save/Cancel are generalised to route by session kind
                    QApplication::processEvents();
                }

                LibraryPart* saved = dlg->partForSession(QStringLiteral("P-SESSION"));
                check(saved != nullptr, "library session: the part is still in the library");
                if (saved) {
                    check(!saved->mechanical.isEmpty(),
                          "library session: the mechanical drawing was saved back");
                    // Everything was removed, then the connection point alone put back: that is what saves.
                    check(saved->hasConnect && !saved->hasNearPoint && !saved->hasFarPoint,
                          "library session: its marks round-tripped through the save too");
                    check(saved->outputs.empty(), "library session: the face deleted on the sheet is not saved");
                    std::printf("  library session: connection point saved at %.2f, %.2f\n", saved->connectPoint.x,
                                saved->connectPoint.y);
                    // Where it is ON THE PART: the save sets the drawing's left end on the origin, so a point that moved
                    // with the drawing comes back where it was on it (2, -5) - left behind, it would be at -8.
                    check(std::abs(saved->connectPoint.x - 2.0) < 1e-6 && std::abs(saved->connectPoint.y + 5.0) < 1e-6,
                          "library session: moving the whole drawing took its connection point with it");
                }
            }
        }
    }


    // ---- drawing a default model's SYMBOL: three tabs, and a way to finish ---------------------------
    {
        // A symbol is a small glyph - a dozen lines and a few pins. Every other tab is about a SHEET:
        // dimensioning and calling out a drawing, importing a supplier's DXF, laying a harness out, setting a
        // title block. None of it applies to a glyph, and the house rule is that while a function is being
        // used the ribbon shows that function rather than the whole program.
        MainWindow win;
        win.resize(1280, 820);
        win.show();
        QApplication::processEvents();
        win.openMechModelEditor(MechModelKind::RingInsulated, QStringLiteral("Insulated"), true);
        QApplication::processEvents();

        auto* ribbon = win.findChild<QTabWidget*>();
        check(ribbon != nullptr, "symbol session: there is a ribbon");
        QStringList shown;
        if (ribbon)
            for (int i = 0; i < ribbon->count(); ++i)
                if (ribbon->tabBar()->isTabVisible(i)) shown << ribbon->tabText(i);
        std::printf("  symbol session: tabs %s\n", qPrintable(shown.join(QStringLiteral(", "))));
        check(shown.contains(QStringLiteral("Home")) && shown.contains(QStringLiteral("Modify")) &&
                  shown.contains(QStringLiteral("Schematic build")),
              "symbol session: Home to draw with, Modify to change it, Schematic build for the "
              "schematic's own pieces");
        check(!shown.contains(QStringLiteral("Harness mechanical")) &&
                  !shown.contains(QStringLiteral("Annotation")) &&
                  !shown.contains(QStringLiteral("View")) && !shown.contains(QStringLiteral("Import")),
              "symbol session: and nothing that is about a sheet rather than a glyph");
        // AND NO HOLE WHERE THE HIDDEN ONES WERE. A tab put away has to give its space back: a strip
        // reading "Home Modify ....... View" tells the eye that something is broken, and it is the first
        // thing seen on entering a session.
        if (ribbon) {
            int gaps = 0;
            QRect prev;
            for (int i = 0; i < ribbon->count(); ++i) {
                if (!ribbon->tabBar()->isTabVisible(i)) continue;
                const QRect r = ribbon->tabBar()->tabRect(i);
                if (prev.isValid() && r.left() - prev.right() > 8) {
                    ++gaps;
                    std::printf("  symbol session: a %d px hole before %s\n", r.left() - prev.right(),
                                qPrintable(ribbon->tabText(i)));
                }
                prev = r;
            }
            check(gaps == 0, "symbol session: the tabs that are showing sit next to one another");
        }

        // A WAY TO FINISH, FROM WHATEVER TAB IS OPEN. A symbol is drawn with Modify and Schematic build as
        // much as with Home, and the two buttons that finish the session were on Home alone: crossing to
        // another tab to draw took them out of sight, which is a session with a way in and no way out.
        //
        // Found by what each one SAYS IT DOES rather than by its word: "Cancel" is written on buttons all
        // over the program, and matching the word picked up a hidden one belonging to something else.
        auto visibleWayOut = [&win] {
            int capture = 0, cancel = 0;
            for (QToolButton* b : win.findChildren<QToolButton*>()) {
                if (!b->isVisible()) continue;
                const QString tip = b->toolTip();
                if (tip.startsWith(QStringLiteral("Take what is drawn here"))) ++capture;
                else if (tip == QStringLiteral("Leave the model as it was")) ++cancel;
            }
            return QPair<int, int>(capture, cancel);
        };
        QToolButton* capture = nullptr;
        QToolButton* cancel = nullptr;
        QToolButton* connect = nullptr;
        for (QToolButton* b : win.findChildren<QToolButton*>()) {
            const QString tip = b->toolTip();
            if (tip.startsWith(QStringLiteral("Take what is drawn here")) && b->isVisible()) capture = b;
            else if (tip == QStringLiteral("Leave the model as it was") && b->isVisible()) cancel = b;
            else if (tip.startsWith(QStringLiteral("Click where the part connects"))) connect = b;
        }
        check(capture != nullptr && cancel != nullptr,
              "symbol session: Capture and Cancel are there, with their icons");
        if (capture) check(!capture->icon().isNull(), "symbol session: Capture is drawn, not lettered");
        if (cancel) check(!cancel->icon().isNull(), "symbol session: and so is Cancel");
        // ONE of each, and on every tab. Two Captures on the screen at once is two answers to "which one
        // finishes this".
        if (ribbon)
            for (int i = 0; i < ribbon->count(); ++i) {
                if (!ribbon->tabBar()->isTabVisible(i)) continue;
                ribbon->setCurrentIndex(i);
                QApplication::processEvents();
                const QPair<int, int> out = visibleWayOut();
                if (out.first != 1 || out.second != 1)
                    std::printf("  symbol session: on %s - %d Capture, %d Cancel\n",
                                qPrintable(ribbon->tabText(i)), out.first, out.second);
                check(out.first == 1 && out.second == 1,
                      "symbol session: one Capture and one Cancel, reachable from every tab");
            }
        // The marks DO go, and should: they describe placing a part by a direction along a run, and a symbol
        // is a fixed glyph with nothing for any of them to point at.
        if (connect)
            check(!connect->isVisible(),
                  "symbol session: the marks go - a glyph is not placed by a direction along a run");

        // And the design-rule check goes with them: it reads a whole schematic - what is joined to what, what
        // is left floating - and a symbol on its own is nothing but floating ends.
        QToolButton* validate = nullptr;
        for (QToolButton* b : win.findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("Validate")) validate = b;
        if (validate)
            check(!validate->isVisible(),
                  "symbol session: no design-rule check - it would report a fault per pin, every one wrong");
    }

    if (canvasFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", canvasFailures);
    return canvasFailures;
}
