// The strip along the bottom of the canvas: Model and the layouts, with an x on each layout, a grid that
// belongs to the space it was turned on in, and the scale the layout is plotted at in the corner.
//
// All three are about a thing belonging where it is set. A layout is a sheet, and closing one is the sort
// of thing done where it was made rather than through a menu. A grid over a sheet being laid out has
// nothing to do with the grid over the harness. And the plot scale is the LAYOUT'S - it is kept with the
// drawing, on its viewport, not as a setting of the window that happened to be open.
#include "app/Chrome.h"
#include "app/Editor.h"
#include "io/Dwx.h"
#include "app/MainWindow.h"
#include "app/ScientificCalculatorDialog.h"
#include "model/Document.h"
#include "render/CanvasWidget.h"
#include "render/EntityRenderer.h"

#include <QCheckBox>
#include <QFrame>
#include <QComboBox>
#include <QDir>
#include <QCursor>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QElapsedTimer>
#include <QSettings>
#include <QMouseEvent>
#include <QTableWidget>
#include <QTabBar>
#include <QMenuBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QApplication>

#include <QLineF>

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int spaceFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++spaceFailures;
    }
}

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

} // namespace

int run_drawingspace_tests() {
    std::printf("drawing space tests:\n");
    MainWindow win;

    QTabBar* spaces = nullptr;
    for (QTabBar* bar : win.findChildren<QTabBar*>())
        if (bar->count() > 0 && bar->tabText(0) == QLatin1String("Model")) spaces = bar;
    check(spaces != nullptr, "spaces: the Model / Layout strip is there");
    if (!spaces) {
        std::printf("  -> %d failed\n", ++spaceFailures);
        return spaceFailures;
    }
    check(spaces->count() >= 3, "spaces: a new drawing has Model and two sheets");

    // ---- the x on a tab --------------------------------------------------------------------------
    check(spaces->tabsClosable(), "close: the tabs carry a close button");
    check(spaces->tabButton(0, QTabBar::RightSide) == nullptr,
          "close: Model has none - there is one model space and the drawing is in it");
    bool everyLayout = spaces->count() > 1;
    for (int i = 1; i < spaces->count(); ++i)
        if (!spaces->tabButton(i, QTabBar::RightSide)) everyLayout = false;
    check(everyLayout, "close: and every layout has one");

    for (QToolButton* b : win.findChildren<QToolButton*>())
        if (b->text() == QStringLiteral("+") && b->toolTip().contains(QStringLiteral("layout")))
            b->click();
    const int added = spaces->count();
    check(added >= 4, "close: + adds a layout");
    emit spaces->tabCloseRequested(added - 1);
    check(spaces->count() == added - 1, "close: the x on a layout closes it");
    // Down to the last sheet, which is kept: a drawing plots on something.
    while (spaces->count() > 2) emit spaces->tabCloseRequested(spaces->count() - 1);
    const int kept = spaces->count();
    emit spaces->tabCloseRequested(1);
    check(spaces->count() == kept, "close: the last layout is kept");
    emit spaces->tabCloseRequested(0);
    check(spaces->count() == kept, "close: and Model is never closed");

    // ---- the grid, per space ---------------------------------------------------------------------
    auto* canvas = win.findChild<CanvasWidget*>();
    check(canvas != nullptr, "spaces: the canvas is there");
    if (canvas) {
        spaces->setCurrentIndex(0);
        canvas->setGridVisible(true);
        const bool modelGrid = canvas->gridVisible();
        spaces->setCurrentIndex(1);
        check(modelGrid && !canvas->gridVisible(),
              "grid: a layout starts without the grid the model has on");
        canvas->setGridVisible(true);
        spaces->setCurrentIndex(0);
        check(canvas->gridVisible(), "grid: the model keeps its own");
        spaces->setCurrentIndex(1);
        check(canvas->gridVisible(), "grid: and the layout keeps what it was given");
        canvas->setGridVisible(false);
        spaces->setCurrentIndex(0);
        check(canvas->gridVisible(), "grid: turning it off on a layout leaves the model alone");
    }

    // ---- the plot scale, on the layout -----------------------------------------------------------
    QComboBox* scale = nullptr;
    for (QComboBox* box : win.findChildren<QComboBox*>())
        if (box->count() > 5 && box->itemText(1) == QLatin1String("1:1")) scale = box;
    check(scale != nullptr, "scale: the box is in the corner of the strip");
    if (scale && canvas) {
        spaces->setCurrentIndex(0);
        // The box means something in both spaces now: on a layout the plot scale, in model space the scale
        // the drawing will be plotted at - which is what the dimensions are drawn to suit.
        check(scale->isEnabled(), "scale: it can be set in model space too");
        spaces->setCurrentIndex(1);
        check(scale->isEnabled(), "scale: and on a layout it can be set");
        // The layouts belong to the drawing. On a sheet the canvas is drawing IN the sheet, so the
        // drawing itself is asked for by name.
        const Document* doc = canvas->drawingDocument();
        check(doc && !doc->layouts().empty(), "scale: the drawing has a layout to set it on");
        if (doc && !doc->layouts().empty()) {
            const int half = scale->findText(QStringLiteral("1:2"));
            check(half >= 0, "scale: 1:2 is on the list");
            scale->setCurrentIndex(half);
            check(!doc->layouts()[0].viewports.empty() &&
                      near(doc->layouts()[0].viewports.front().modelScale, 0.5),
                  "scale: 1:2 is one of paper to two of model, kept on the layout itself");
            scale->setCurrentIndex(scale->findText(QStringLiteral("10:1")));
            check(near(doc->layouts()[0].viewports.front().modelScale, 10.0),
                  "scale: and 10:1 the other way about");
            scale->setCurrentIndex(scale->findText(QStringLiteral("Fit")));
            check(near(doc->layouts()[0].viewports.front().modelScale, 0.0),
                  "scale: Fit leaves the drawing to be sized to its viewport");
        }
    }

    // ---- the strip itself: nothing drawn over the last tab's x -----------------------------------
    check(!spaces->usesScrollButtons(),
          "strip: no scroll arrows - they were drawn over the last layout's close button, which is why "
          "the second one could not be closed");
    {
        // Every tab's x is where it can be pressed: inside the tab, and no other tab over it.
        bool reachable = true;
        for (int i = 1; i < spaces->count(); ++i) {
            QWidget* x = spaces->tabButton(i, QTabBar::RightSide);
            if (!x) { reachable = false; continue; }
            const QRect inTab = spaces->tabRect(i);
            const QRect xRect(x->pos(), x->size());   // already in the bar's own coordinates
            if (!inTab.contains(xRect.center())) reachable = false;
            // Against what the bar asks for rather than what it has been given: a window that has not
            // been shown has not laid its strip out yet, and the point being made is that the x is
            // within the tabs - with the scroll arrows off, nothing is drawn over it.
            if (xRect.center().x() > spaces->sizeHint().width()) reachable = false;
        }
        check(reachable, "strip: every layout's x sits inside its own tab and within the bar");
    }

    // ---- a window onto the drawing, put on the paper ---------------------------------------------
    QToolButton* addWindow = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
        if (b->text() == QStringLiteral("+ window")) addWindow = b;
    check(addWindow != nullptr, "window: the + window button is on the strip");
    if (addWindow && canvas) {
        spaces->setCurrentIndex(0);
        check(!addWindow->isEnabled(),
              "window: a window is cut in a sheet, so it is out of reach on the model");
        spaces->setCurrentIndex(1);
        check(addWindow->isEnabled(), "window: on a layout it can be pressed");
        Document* drawing = canvas->drawingDocument();
        check(drawing && !drawing->layouts().empty(), "window: the layout is there to cut it in");
        if (drawing && !drawing->layouts().empty()) {
            const size_t before = drawing->layouts()[0].viewports.size();
            addWindow->click();
            check(drawing->layouts()[0].viewports.size() == before + 1,
                  "window: pressing it puts another window on the sheet");
        }
    }

    // ---- the sheet is drawn on, with the same tools as the model ---------------------------------
    auto* editor = win.findChild<Editor*>();
    check(editor != nullptr, "paper: the editor is there");
    if (canvas && editor) {
        spaces->setCurrentIndex(0);
        Document* drawing = canvas->document();
        check(canvas->drawingDocument() == drawing,
              "paper: in model space the drawing and what is drawn in are one and the same");
        const size_t modelEntities = drawing ? drawing->entities().size() : 0;

        spaces->setCurrentIndex(1);
        Document* paper = canvas->document();
        check(paper && paper != drawing,
              "paper: a layout is drawn in a space of its own - the sheet, not the harness");
        check(canvas->drawingDocument() == drawing,
              "paper: while the drawing itself stays in hand, for the layouts and their windows");
        if (paper && drawing) {
            check(paper->layers().size() == drawing->layers().size(),
                  "paper: the sheet carries the drawing's layers, so a line on it resolves its colour");

            // A line drawn on the sheet, through the editor, exactly as one is drawn on the model.
            editor->setTool(ToolId::Line);
            editor->onPointerPress(Vec2{20.0, 20.0}, Qt::LeftButton, Qt::NoModifier);
            editor->onPointerPress(Vec2{120.0, 20.0}, Qt::LeftButton, Qt::NoModifier);
            check(paper->entities().size() == 1, "paper: the Line tool draws on the sheet");
            check(drawing->entities().size() == modelEntities,
                  "paper: and what is drawn on the sheet does not land in the harness");

            // Select it and move it, as on the model.
            editor->setTool(ToolId::Select);
            editor->onPointerPress(Vec2{70.0, 20.0}, Qt::LeftButton, Qt::NoModifier);
            check(editor->selection().size() == 1, "paper: what is on the sheet can be picked");

            // Off the sheet and back: the sheet keeps what was drawn on it, and the layout holds it.
            spaces->setCurrentIndex(0);
            check(!drawing->layouts()[0].paperEntities.empty(),
                  "paper: leaving the sheet writes what is on it back into the layout, to be saved with "
                  "the drawing");
            check(canvas->document() == drawing,
                  "paper: and the model is drawn in again on the way back");
            spaces->setCurrentIndex(1);
            check(canvas->document() == paper && paper->entities().size() == 1,
                  "paper: coming back to the sheet finds it as it was left");

            // The inside of a window is paper too: a press there draws, it does not grab the window.
            // (What grabs a window is its frame - see viewportGrabAt.)
            const size_t sheetBefore = paper->entities().size();
            if (!drawing->layouts()[0].viewports.empty()) {
                const LayoutViewport& vp = drawing->layouts()[0].viewports.front();
                const Vec2 inside{vp.x + vp.width * 0.5, vp.y + vp.height * 0.5};
                const QPointF at = canvas->mapToScreen(inside);
                QMouseEvent press(QEvent::MouseButtonPress, at, canvas->mapToGlobal(at),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &press);
                editor->setTool(ToolId::Line);
                editor->onPointerPress(inside, Qt::LeftButton, Qt::NoModifier);
                editor->onPointerPress(Vec2{inside.x + 30.0, inside.y}, Qt::LeftButton, Qt::NoModifier);
                check(paper->entities().size() == sheetBefore + 1,
                      "paper: a window is not a hole - a line can be drawn across one");
            }
        }
    }

    // ---- a window is a way into the model: stepped into, drawn in, moved, resized ----------------
    if (canvas && editor) {
        spaces->setCurrentIndex(1);
        Document* drawing = canvas->drawingDocument();
        Document* paper = canvas->document();
        if (drawing && paper && !drawing->layouts().empty() &&
            !drawing->layouts()[0].viewports.empty()) {
            auto press = [&](const QPointF& at, Qt::MouseButton button = Qt::LeftButton) {
                QMouseEvent ev(QEvent::MouseButtonPress, at, canvas->mapToGlobal(at), button, button,
                               Qt::NoModifier);
                QApplication::sendEvent(canvas, &ev);
            };
            auto moveTo = [&](const QPointF& at) {
                QMouseEvent ev(QEvent::MouseMove, at, canvas->mapToGlobal(at), Qt::NoButton,
                               Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &ev);
            };
            auto release = [&](const QPointF& at) {
                QMouseEvent ev(QEvent::MouseButtonRelease, at, canvas->mapToGlobal(at), Qt::LeftButton,
                               Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &ev);
            };

            // One window on the sheet for this, so "the window" is unambiguous: the + window test above
            // left a second one lying exactly over the first.
            while (drawing->layouts()[0].viewports.size() > 1)
                canvas->removeLayoutViewport(
                    static_cast<int>(drawing->layouts()[0].viewports.size()) - 1);
            win.resize(1400, 900);
            win.show();
            QApplication::processEvents();
            canvas->fitView();
            const LayoutViewport vp0 = drawing->layouts()[0].viewports.front();
            const QPointF middle =
                canvas->mapToScreen(Vec2{vp0.x + vp0.width * 0.5, vp0.y + vp0.height * 0.5});

            // Double-clicking a window steps into it - and inside, the model is what is being drawn on.
            QMouseEvent dbl(QEvent::MouseButtonDblClick, middle, canvas->mapToGlobal(middle),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &dbl);
            check(canvas->activeLayoutViewport() == 0, "window: a double click steps into it");
            check(editor->document() == drawing,
                  "window: inside it the editor works in the model, not on the sheet");

            // A part handed to the cursor inside the window is still a placement. Esc puts THAT down:
            // it does not step out of the window leaving the ghost half-handled.
            Entity held;
            held.type = EntityType::Line;
            held.a = {0.0, 0.0};
            held.b = {10.0, 0.0};
            const size_t beforePlace = drawing->entities().size();
            editor->placeEntities({held});
            check(editor->placingPaste(), "window place: the part follows the cursor inside it");
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(canvas, &esc);
            check(!editor->placingPaste() && drawing->entities().size() == beforePlace,
                  "window place: Esc cancels it without putting anything down");
            check(canvas->activeLayoutViewport() == 0 && editor->document() == drawing,
                  "window place: and it does not step out of the window to do it");

            const size_t inModel = drawing->entities().size();
            const size_t onSheet = paper->entities().size();
            editor->setTool(ToolId::Line);
            editor->onPointerPress(Vec2{0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            editor->onPointerPress(Vec2{100.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            check(drawing->entities().size() == inModel + 1,
                  "window: what is drawn inside a window is drawn in the model");
            check(paper->entities().size() == onSheet,
                  "window: and not on the sheet the window is cut in");
            if (drawing->entities().size() == inModel + 1) {
                const Entity& drawn = drawing->entities().back();
                check(near(std::hypot(drawn.b.x - drawn.a.x, drawn.b.y - drawn.a.y), 100.0, 1e-6),
                      "window: at model size - 100 means 100 in the harness, whatever the window shows");
            }

            // Stepping out puts the sheet back under the tools.
            canvas->leaveLayoutViewport();
            check(canvas->activeLayoutViewport() < 0 && editor->document() == paper,
                  "window: stepping out gives the sheet back");

            // Resizing a window changes the window, not what is inside it. Checked where a model point
            // lands on the screen: with the window cropped, it must not have moved.
            press(canvas->mapToScreen(Vec2{vp0.x + vp0.width * 0.5, vp0.y}));   // the bottom edge: picks
            release(canvas->mapToScreen(Vec2{vp0.x + vp0.width * 0.5, vp0.y}));
            canvas->enterLayoutViewport(0);
            const Vec2 mark{50.0, 25.0};
            const QPointF markBefore = canvas->inputTransform().toScreen(mark);
            const double scaleBefore = canvas->inputTransform().scale();
            canvas->leaveLayoutViewport();
            const QPointF corner = canvas->mapToScreen(Vec2{vp0.x + vp0.width, vp0.y + vp0.height});
            const QPointF cornerTo = canvas->mapToScreen(
                Vec2{vp0.x + vp0.width * 0.75, vp0.y + vp0.height * 0.75});
            press(corner);
            moveTo(cornerTo);
            release(cornerTo);
            const LayoutViewport& sized = drawing->layouts()[0].viewports.front();
            check(sized.width < vp0.width - 1.0 && sized.height < vp0.height - 1.0,
                  "resize: a corner drag makes the window smaller");
            canvas->enterLayoutViewport(0);
            check(near(canvas->inputTransform().scale(), scaleBefore, 1e-6),
                  "resize: the drawing inside is not rescaled - only the window changed");
            const QPointF markAfter = canvas->inputTransform().toScreen(mark);
            check(QLineF(markBefore, markAfter).length() < 1.0,
                  "resize: and it stays where it was on the paper, so the window crops");
            canvas->leaveLayoutViewport();

            // A window is carried about by an edge, and may be taken off the paper altogether.
            const LayoutViewport before = drawing->layouts()[0].viewports.front();
            const QPointF edge =
                canvas->mapToScreen(Vec2{before.x + before.width * 0.5, before.y + before.height});
            const QPointF edgeTo = canvas->mapToScreen(
                Vec2{before.x + before.width * 0.5 + 30.0, before.y + before.height});
            press(edge);
            moveTo(edgeTo);
            release(edgeTo);
            const LayoutViewport& moved = drawing->layouts()[0].viewports.front();
            check(moved.x > before.x + 1.0, "move: dragging an edge carries the window across the sheet");
            check(near(moved.width, before.width, 1e-6) && near(moved.height, before.height, 1e-6),
                  "move: and only carries it - the window keeps its size");
            const double sheetWidth = drawing->layouts()[0].paperWidth;
            press(canvas->mapToScreen(Vec2{moved.x + moved.width * 0.5, moved.y + moved.height}));
            moveTo(canvas->mapToScreen(Vec2{sheetWidth + 200.0, moved.y + moved.height}));
            release(canvas->mapToScreen(Vec2{sheetWidth + 200.0, moved.y + moved.height}));
            check(drawing->layouts()[0].viewports.front().x > sheetWidth,
                  "move: right off the paper, if that is where it is wanted - nothing clamps it");

            // And taken off the sheet again.
            const size_t windows = drawing->layouts()[0].viewports.size();
            canvas->removeLayoutViewport(0);
            check(drawing->layouts()[0].viewports.size() == windows - 1,
                  "window: it can be taken off the sheet");
        }
    }

    // ---- a zone drawn on the paper picks the windows out too --------------------------------------
    if (canvas && editor) {
        spaces->setCurrentIndex(1);
        Document* drawing = canvas->drawingDocument();
        if (drawing && !drawing->layouts().empty()) {
            DrawingLayout& sheet = drawing->layouts()[0];
            sheet.viewports.clear();
            LayoutViewport left;
            left.id = QStringLiteral("left");
            left.x = 20.0; left.y = 20.0; left.width = 80.0; left.height = 60.0;
            left.modelScale = 1.0;
            LayoutViewport right = left;
            right.id = QStringLiteral("right");
            right.x = 160.0;
            sheet.viewports.push_back(left);
            sheet.viewports.push_back(right);

            // A blue window zone takes what is WHOLLY inside it: one window, not the other.
            const std::vector<Vec2> roundLeft{{10.0, 10.0}, {120.0, 10.0}, {120.0, 100.0},
                                              {10.0, 100.0}};
            canvas->selectLayoutWindowsIn(roundLeft, /*crossing=*/false, /*subtract=*/false);
            check(canvas->selectedLayoutViewports().size() == 1 && canvas->layoutViewportPicked(0),
                  "zone: a blue window zone takes the window it encloses");
            check(!canvas->layoutViewportPicked(1),
                  "zone: and leaves the one it does not reach");

            // A blue zone that only clips a window does not take it.
            canvas->selectLayoutWindowsIn(roundLeft, false, true);   // let go again
            const std::vector<Vec2> clipBoth{{60.0, 40.0}, {200.0, 40.0}, {200.0, 50.0},
                                             {60.0, 50.0}};
            canvas->selectLayoutWindowsIn(clipBoth, false, false);
            check(canvas->selectedLayoutViewports().empty(),
                  "zone: a blue zone that merely cuts across a window takes nothing - all of it or none");

            // The green crossing zone takes both, because it touches both.
            canvas->selectLayoutWindowsIn(clipBoth, true, false);
            check(canvas->selectedLayoutViewports().size() == 2,
                  "zone: a green crossing zone takes every window it touches");
            canvas->selectLayoutWindowsIn(clipBoth, true, true);
            check(canvas->selectedLayoutViewports().empty(), "zone: and Shift lets them go again");

            // The real path: a rectangle dragged out with the Select tool, left to right, on the paper.
            editor->setTool(ToolId::Select);
            editor->onPointerPress(Vec2{10.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
            editor->onPointerRelease(Vec2{10.0, 10.0}, Qt::LeftButton);
            editor->onPointerPress(Vec2{120.0, 100.0}, Qt::LeftButton, Qt::NoModifier);
            check(canvas->layoutViewportPicked(0),
                  "zone: dragging a zone on the sheet with the Select tool picks the window in it");

            // Several picked windows are carried about together.
            canvas->selectLayoutWindowsIn(std::vector<Vec2>{{0.0, 0.0}, {300.0, 0.0}, {300.0, 200.0},
                                                            {0.0, 200.0}},
                                          true, false);
            const size_t picked = canvas->selectedLayoutViewports().size();
            check(picked == 2, "zone: both windows picked, ready to be moved together");
            if (picked == 2) {
                win.resize(1400, 900);
                win.show();
                QApplication::processEvents();
                canvas->fitView();
                const LayoutViewport wasLeft = sheet.viewports[0], wasRight = sheet.viewports[1];
                const QPointF edge = canvas->mapToScreen(
                    Vec2{wasRight.x + wasRight.width * 0.5, wasRight.y + wasRight.height});
                const QPointF edgeTo = canvas->mapToScreen(
                    Vec2{wasRight.x + wasRight.width * 0.5, wasRight.y + wasRight.height + 25.0});
                QMouseEvent down(QEvent::MouseButtonPress, edge, canvas->mapToGlobal(edge),
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &down);
                QMouseEvent drag(QEvent::MouseMove, edgeTo, canvas->mapToGlobal(edgeTo), Qt::NoButton,
                                 Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &drag);
                QMouseEvent up(QEvent::MouseButtonRelease, edgeTo, canvas->mapToGlobal(edgeTo),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &up);
                check(sheet.viewports[0].y > wasLeft.y + 1.0 && sheet.viewports[1].y > wasRight.y + 1.0,
                      "zone: dragging one of the picked windows carries them all");
            }

            // ---- letting go of the middle button lets go of the drawing inside a window -----------
            canvas->enterLayoutViewport(0);
            const Vec2 lookedAt = sheet.viewports[0].modelCenter;
            const QPointF in = canvas->mapToScreen(Vec2{sheet.viewports[0].x + 10.0,
                                                        sheet.viewports[0].y + 10.0});
            QMouseEvent mid(QEvent::MouseButtonPress, in, canvas->mapToGlobal(in), Qt::MiddleButton,
                            Qt::MiddleButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &mid);
            QMouseEvent midMove(QEvent::MouseMove, in + QPointF(20.0, 0.0),
                                canvas->mapToGlobal(in + QPointF(20.0, 0.0)), Qt::NoButton,
                                Qt::MiddleButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &midMove);
            const Vec2 dragged = sheet.viewports[0].modelCenter;
            check(std::abs(dragged.x - lookedAt.x) > 1e-9,
                  "middle: a middle drag inside a window moves what it looks at");
            QMouseEvent midUp(QEvent::MouseButtonRelease, in + QPointF(20.0, 0.0),
                              canvas->mapToGlobal(in + QPointF(20.0, 0.0)), Qt::MiddleButton,
                              Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &midUp);
            QMouseEvent after(QEvent::MouseMove, in + QPointF(120.0, 40.0),
                              canvas->mapToGlobal(in + QPointF(120.0, 40.0)), Qt::NoButton,
                              Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &after);
            check(near(sheet.viewports[0].modelCenter.x, dragged.x, 1e-9) &&
                      near(sheet.viewports[0].modelCenter.y, dragged.y, 1e-9),
                  "middle: letting the button go lets the drawing go - it stops following the cursor");
            canvas->leaveLayoutViewport();
        }
    }

    // ---- drawing inside a window: the crosshair stays put, and the preview can be seen ------------
    if (canvas && editor) {
        spaces->setCurrentIndex(1);
        Document* drawing = canvas->drawingDocument();
        if (drawing && !drawing->layouts().empty()) {
            DrawingLayout& sheet = drawing->layouts()[0];
            sheet.viewports.clear();
            LayoutViewport only;
            only.id = QStringLiteral("one");
            only.x = 40.0; only.y = 60.0; only.width = 200.0; only.height = 140.0;
            only.modelScale = 0.5;
            only.modelCenter = Vec2{100.0, 50.0};
            sheet.viewports.push_back(only);
            win.resize(1400, 900);
            win.show();
            QApplication::processEvents();
            canvas->fitView();

            // Something in the model with an end to snap to.
            spaces->setCurrentIndex(0);
            editor->setTool(ToolId::Line);
            editor->onPointerPress(Vec2{0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            editor->onPointerPress(Vec2{200.0, 100.0}, Qt::LeftButton, Qt::NoModifier);
            spaces->setCurrentIndex(1);
            canvas->fitView();
            canvas->enterLayoutViewport(0);

            // Hover that end THROUGH the window: the snap catches, and the crosshair stays at the cursor.
            const QPointF at = canvas->inputTransform().toScreen(Vec2{200.0, 100.0});
            QMouseEvent hover(QEvent::MouseMove, at, canvas->mapToGlobal(at), Qt::NoButton,
                              Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &hover);
            check(editor->currentSnap().snapped, "crosshair: the snap catches the model end through the "
                                                 "window");
            check(QLineF(canvas->crosshairScreenPos(), at).length() < 4.0,
                  "crosshair: and the crosshair stays at the cursor - the snapped point is put on the "
                  "screen through the window, not as if it were paper");
            const QPointF asPaper = canvas->mapToScreen(Vec2{200.0, 100.0});
            check(QLineF(asPaper, at).length() > 20.0,
                  "crosshair: which is a different place entirely, or this would prove nothing");
            canvas->leaveLayoutViewport();
        }
    }

    // ---- how far the snap reaches inside a window --------------------------------------------------
    if (canvas && editor) {
        spaces->setCurrentIndex(1);
        Document* drawing = canvas->drawingDocument();
        if (drawing && !drawing->layouts().empty()) {
            DrawingLayout& sheet = drawing->layouts()[0];
            sheet.viewports.clear();
            LayoutViewport only;
            only.id = QStringLiteral("reach");
            only.x = 40.0; only.y = 40.0; only.width = 260.0; only.height = 180.0;
            only.modelCenter = Vec2{200.0, 100.0};
            sheet.viewports.push_back(only);
            win.resize(1400, 900);
            win.show();
            QApplication::processEvents();
            canvas->fitView();

            // A line in the model with an end to catch. (One is there from the tests above; another does
            // no harm and makes this block stand on its own.)
            spaces->setCurrentIndex(0);
            editor->setTool(ToolId::Line);
            editor->onPointerPress(Vec2{0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            editor->onPointerPress(Vec2{200.0, 100.0}, Qt::LeftButton, Qt::NoModifier);
            editor->setTool(ToolId::Select);
            spaces->setCurrentIndex(1);
            canvas->fitView();

            const Vec2 end{200.0, 100.0};
            auto catches = [&](const QPointF& at) {
                QMouseEvent ev(QEvent::MouseMove, at, canvas->mapToGlobal(at), Qt::NoButton,
                               Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &ev);
                const auto snap = editor->currentSnap();
                return snap.snapped && std::abs(snap.point.x - end.x) < 1e-6 &&
                       std::abs(snap.point.y - end.y) < 1e-6;
            };
            // The furthest the cursor can be from that end, in pixels of screen, and still be caught.
            auto reachPx = [&]() {
                const QPointF at = canvas->inputTransform().toScreen(end);
                const QPointF dir(0.894, -0.447);   // out past the end, along the line's own direction
                if (!catches(at)) return -1.0;
                double lo = 0.0, hi = 400.0;
                if (catches(at + dir * hi)) return hi;
                for (int i = 0; i < 20; ++i) {
                    const double mid = (lo + hi) * 0.5;
                    if (catches(at + dir * mid)) lo = mid; else hi = mid;
                }
                return lo;
            };

            // Model space left at a scale that has nothing to do with the window's, which is the whole
            // point: the pick used to be sized by THIS and then seen at the window's scale, so it reached
            // hundreds of pixels across the drawing and snapped to whatever it found - worse with every
            // step of zoom inside the window.
            drawing->view().scale = 0.05;
            sheet.viewports.front().modelScale = 0.5;
            canvas->leaveLayoutViewport();
            canvas->enterLayoutViewport(0);
            QApplication::processEvents();
            const double reachOut = reachPx();
            sheet.viewports.front().modelScale = 4.0;
            canvas->leaveLayoutViewport();
            canvas->enterLayoutViewport(0);
            QApplication::processEvents();
            const double reachIn = reachPx();
            std::printf("  snap reach inside a window: %.1f px at 1:2, %.1f px at 4:1\n", reachOut,
                        reachIn);
            check(reachOut > 4.0 && reachOut < 20.0,
                  "snap: inside a window the snap reaches a few pixels of screen, not a distance in the "
                  "model");
            check(reachIn > 4.0 && reachIn < 20.0,
                  "snap: and zooming the window in does not make it reach further");
            check(std::abs(reachIn - reachOut) < 3.0,
                  "snap: the same few pixels at either zoom - which is what stops it jumping");
            canvas->leaveLayoutViewport();
        }
    }

    // ---- the overlay's ink, on a white sheet ------------------------------------------------------
    {
        const QColor pale(120, 200, 255, 170);   // the ink the line being drawn is made of
        const bool wasWhite = EntityRenderer::whitePaper();
        EntityRenderer::setWhitePaper(false);
        check(Editor::paperInk(pale) == pale,
              "ink: on the dark canvas the overlay keeps the colours chosen for it");
        EntityRenderer::setWhitePaper(true);
        const QColor onPaper = Editor::paperInk(pale);
        check(onPaper.value() <= 150 && onPaper.alpha() >= 205,
              "ink: on a white sheet it comes out dark enough and solid enough to see - which is why a "
              "line being drawn inside a window was invisible before it was placed");
        check(std::abs(onPaper.toHsv().hue() - pale.toHsv().hue()) <= 6,
              "ink: the same colour, though - it is still the blue of a line being drawn");
        EntityRenderer::setWhitePaper(wasWhite);
    }

    // ---- the scale in model space is the annotation scale -----------------------------------------
    if (canvas && scale) {
        // The scale in the corner means two different things in the two spaces, and both of them are real. On
        // a layout it is the plot scale, kept on the window. In MODEL space it is the scale the drawing will
        // be plotted at, and what it sets is how big the dimensions are DRAWN: a drawing to be plotted at
        // 1:10 needs its dimension text ten times the height in the model to come off the paper the size it
        // says. The harness is never touched by it.
        spaces->setCurrentIndex(0);
        QApplication::processEvents();
        check(scale->isEnabled(),
              "annotation: the scale can be set in model space too - it is what the drawing will be plotted "
              "at");
        scale->setCurrentIndex(scale->findText(QStringLiteral("1:10")));
        check(std::abs(EntityRenderer::annotationScale() - 10.0) < 1e-9,
              "annotation: at 1:10 the dimensions are drawn ten times the size they are written");
        scale->setCurrentIndex(scale->findText(QStringLiteral("1:1")));
        check(std::abs(EntityRenderer::annotationScale() - 1.0) < 1e-9,
              "annotation: and at 1:1 they are drawn as written");
        scale->setCurrentIndex(scale->findText(QStringLiteral("2:1")));
        check(std::abs(EntityRenderer::annotationScale() - 0.5) < 1e-9,
              "annotation: a drawing blown up to 2:1 needs its dimensions half size in the model");
        // A layout's annotation is at size on the paper - it is the window that is scaled, not the writing.
        spaces->setCurrentIndex(1);
        QApplication::processEvents();
        check(std::abs(EntityRenderer::annotationScale() - 1.0) < 1e-9,
              "annotation: on a sheet the writing is at size, because the sheet IS the paper");
        spaces->setCurrentIndex(0);
        QApplication::processEvents();
        check(std::abs(EntityRenderer::annotationScale() - 0.5) < 1e-9,
              "annotation: and coming back to the model finds the scale it was left at");
        scale->setCurrentIndex(scale->findText(QStringLiteral("1:1")));
    }

    // ---- + window is not on the strip in model space ----------------------------------------------
    {
        QToolButton* addWindowBtn = nullptr;
        for (QToolButton* b : win.findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("+ window")) addWindowBtn = b;
        check(addWindowBtn != nullptr, "strip: the + window button exists");
        if (addWindowBtn) {
            spaces->setCurrentIndex(0);
            QApplication::processEvents();
            check(!addWindowBtn->isVisibleTo(win.centralWidget() ? win.centralWidget() : &win) ||
                      !addWindowBtn->isEnabled(),
                  "strip: and it is not offered in model space at all - a window onto the drawing is cut in a "
                  "sheet");
            spaces->setCurrentIndex(1);
            QApplication::processEvents();
            check(addWindowBtn->isEnabled(), "strip: on a sheet it is there and can be pressed");
        }
    }

    // ---- the scale, findable ----------------------------------------------------------------------
    {
        bool labelled = false;
        for (QLabel* l : win.findChildren<QLabel*>())
            if (l->text() == QStringLiteral("Scale")) labelled = true;
        check(labelled, "scale: the box says what it is, so it reads as something to press");
    }

    // ---- a drawing opened while a sheet is showing opens ITS sheet --------------------------------
    {
        // A drawing of its own, saved and then opened: what matters is which space it comes up in.
        Document made;
        const QString path = QDir::tempPath() + QStringLiteral("/edraw-open-on-a-sheet.dwx");
        QString err;
        check(dwx::save(made, path, &err), "open: a drawing to open is saved first");
        check(!made.layouts().empty(), "open: and it has sheets of its own");
        spaces->setCurrentIndex(1);
        const int standingOn = spaces->currentIndex();
        win.openPath(path);
        check(spaces->currentIndex() > 0,
              "open: opening a drawing while a sheet is showing stays on a sheet - the work in hand is "
              "laying out sheets");
        check(spaces->currentIndex() == standingOn,
              "open: the same sheet by number, when the drawing goes that far");
        check(canvas && canvas->layoutMode(), "open: the canvas is laying out, not in model space");
        if (canvas && editor)
            check(editor->document() != canvas->drawingDocument(),
                  "open: and the tools are in that sheet's paper space, ready to draw on it");
    }

    // ---- the View tab: which panels are on the screen, and which side -----------------------------
    {
        // Six panels, six buttons, each on its own. Before this a panel closed by its own x could only be
        // brought back by knowing that a floating panel dragged to an edge re-docks - which nobody knows.
        // On the screen for this one: a panel showing or not showing is the whole subject, and a window that
        // was never shown has nothing showing in it.
        win.show();
        QApplication::processEvents();

        QTabBar* ribbonTabs = nullptr;
        for (QTabBar* bar : win.findChildren<QTabBar*>())
            for (int i = 0; i < bar->count(); ++i)
                if (bar->tabText(i) == QStringLiteral("View")) ribbonTabs = bar;
        check(ribbonTabs != nullptr, "view: there is a View tab on the ribbon");

        const QStringList wanted{QStringLiteral("Properties"), QStringLiteral("Layers"),
                                 QStringLiteral("Bill of materials"), QStringLiteral("Parts list"),
                                 QStringLiteral("Blocks list"), QStringLiteral("Calculator")};
        QList<QToolButton*> panelBtns;
        for (QToolButton* b : win.findChildren<QToolButton*>())
            if (b->isCheckable() && wanted.contains(b->text())) panelBtns.append(b);
        check(panelBtns.size() == wanted.size(),
              "view: a button each for the properties, the layers, the bill of materials, the parts, "
              "the blocks and the calculator");
        bool allLive = !panelBtns.isEmpty();
        for (QToolButton* b : panelBtns) {
            if (!b->isEnabled()) allLive = false;              // wired to a panel that exists
            if (!b->menu() || b->menu()->actions().size() < 3) allLive = false;   // left / right / floating
        }
        check(allLive, "view: each one is live, and says which side its panel sits on");

        // Pressed in, the panel is on the screen; pressed out, it is not. The panel's own x has to leave the
        // button pressed out too, or the ribbon and the screen disagree about what is showing.
        QToolButton* propsBtn = nullptr;
        for (QToolButton* b : panelBtns)
            if (b->text() == QStringLiteral("Properties")) propsBtn = b;
        QDockWidget* propsPanel = nullptr;
        for (QDockWidget* d : win.findChildren<QDockWidget*>())
            if (d->windowTitle() == QStringLiteral("PROPERTIES")) propsPanel = d;
        check(propsBtn && propsPanel, "view: the properties button and its panel are both there");
        if (propsBtn && propsPanel) {
            propsBtn->setChecked(false);
            propsBtn->setChecked(true);
            check(!propsPanel->isHidden(), "view: pressed in, the panel is on the screen");
            propsBtn->setChecked(false);
            check(propsPanel->isHidden(), "view: pressed out, it is gone");
            propsPanel->show();
            QApplication::processEvents();
            check(propsBtn->isChecked(), "view: and a panel brought back by hand presses its button in");
        }

        // The two things wanted after moving panels about: all of them, or none and the whole screen.
        for (QToolButton* b : win.findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("Hide all")) b->click();
        QApplication::processEvents();
        bool noneShowing = true;
        for (const char* title : {"PROPERTIES", "LAYERS", "BILL OF MATERIALS", "PARTS", "BLOCKS", "CALCULATOR"})
            for (QDockWidget* d : win.findChildren<QDockWidget*>())
                if (d->windowTitle() == QLatin1String(title) && !d->isHidden()) noneShowing = false;
        check(noneShowing, "view: Hide all leaves the drawing the whole screen");
        for (QToolButton* b : win.findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("Show all")) b->click();
        QApplication::processEvents();
        bool allShowing = true;
        for (const char* title : {"PROPERTIES", "LAYERS", "BILL OF MATERIALS", "PARTS", "BLOCKS", "CALCULATOR"}) {
            bool one = false;
            for (QDockWidget* d : win.findChildren<QDockWidget*>())
                if (d->windowTitle() == QLatin1String(title) && !d->isHidden()) one = true;
            if (!one) allShowing = false;
        }
        check(allShowing, "view: and Show all brings every one of them back");

        // The parts and blocks lists are panels now, not only windows: what is on the shelf is wanted while
        // drawing, and a part is put down by double-clicking it rather than through a dialog.
        QTreeWidget* partsList = nullptr;
        for (QDockWidget* d : win.findChildren<QDockWidget*>())
            if (d->windowTitle() == QStringLiteral("PARTS")) partsList = d->findChild<QTreeWidget*>();
        check(partsList != nullptr, "view: the parts panel is a table of the library");
        check(partsList && partsList->columnCount() == 4,
              "view: the part, and a column each for the mechanical, schematic and fanout drawings");
        QLineEdit* partsFilter = nullptr;
        for (QDockWidget* d : win.findChildren<QDockWidget*>())
            if (d->windowTitle() == QStringLiteral("PARTS")) partsFilter = d->findChild<QLineEdit*>();
        check(partsFilter != nullptr,
              "view: with a filter, because a real library is longer than a panel");
    }

    // ---- the Modify and Annotation groups read as two rows ----------------------------------------
    {
        // A group is two rows of buttons. Array beside two rows of tools, at its own full height, read as
        // something apart from them rather than as one more way of repeating a thing.
        int tall = 0, twoRow = 0, ragged = 0;
        for (QGridLayout* grid : win.findChildren<QGridLayout*>()) {
            if (grid->count() < 3) continue;
            bool everyCellAnIcon = true;
            QList<QSize> cells;
            for (int i = 0; i < grid->count(); ++i) {
                QWidget* w = grid->itemAt(i)->widget();
                auto* b = qobject_cast<QToolButton*>(w);
                // Icon buttons only: a group of labelled buttons is a different thing, and is as wide as
                // its words.
                if (!b || b->toolButtonStyle() != Qt::ToolButtonIconOnly) everyCellAnIcon = false;
                if (w) cells.append(w->size());
            }
            if (!everyCellAnIcon || cells.isEmpty()) continue;
            if (grid->rowCount() > 2) ++tall;
            else ++twoRow;
            // And every square the same. Array, Copy and Table are split buttons - a click and a fall menu -
            // and a split button asks for more room than an icon, so each made its own column wider than the
            // rest: two rows that did not line up, which is what "not ordered" looks like.
            // A grid of BIG buttons is not a row of icons - some groups are made of labelled buttons
            // with widths of their own, and they are not what this is about.
            int narrowest = cells.first().width();
            for (const QSize& s : cells) narrowest = std::min(narrowest, s.width());
            if (narrowest > 60) continue;
            for (const QSize& s : cells)
                if (s != cells.first()) { ++ragged; break; }
        }
        std::printf("  ribbon: %d groups in two rows, %d taller, %d ragged\n", twoRow, tall,
                    ragged);
        check(twoRow > 0, "rows: the ribbon's tool groups are laid out in a grid of two rows");
        check(tall == 0, "rows: and none of them runs to three");
        check(ragged == 0, "rows: every button in a row of icons is the same square, split buttons and all");
    }

    // ---- no contextual page shows while it is not a tab --------------------------------------------
    {
        // The contextual tabs - Hatch Creation, Table Cell, Array, Detail view and the rest - are children of
        // the ribbon but are only TABS of it while they apply. One left showing does not sit quietly out of
        // the way: it floats over whichever page is showing, its groups drawn on top of that page's with that
        // page's group titles underneath them, and the ribbon reads as though two tabs have been dealt into
        // one. Which is what happened the moment the detail-view tab was added and left out of the list that
        // hides them, so this asks the question of ALL of them rather than of that one.
        QTabWidget* ribbon = nullptr;
        for (QTabWidget* tw : win.findChildren<QTabWidget*>())
            if (tw->count() > 3) { ribbon = tw; break; }
        check(ribbon != nullptr, "contextual: the ribbon is there to ask about");
        if (ribbon) {
            QStringList floating;
            for (QWidget* w : ribbon->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
                if (ribbon->indexOf(w) >= 0) continue;          // it is a tab: it is meant to show
                if (!w->isVisible()) continue;
                if (w->layout() == nullptr) continue;           // the tab bar and the like
                if (w->objectName().startsWith(QStringLiteral("qt_"))) continue;   // Qt's own plumbing
                // A page is a page if it holds ribbon groups. Anything else that hangs off a QTabWidget -
                // the bar, the stack, a corner widget - is not what this is about.
                bool holdsGroups = false;
                for (QWidget* g : w->findChildren<QWidget*>())
                    if (g->objectName() == QStringLiteral("ribbonGroupTitle")) { holdsGroups = true; break; }
                if (holdsGroups) floating << (w->objectName().isEmpty() ? w->metaObject()->className()
                                                                       : w->objectName());
            }
            std::printf("  contextual: %lld page(s) showing without being a tab%s%s\n",
                        static_cast<long long>(floating.size()), floating.isEmpty() ? "" : ": ",
                        qPrintable(floating.join(QStringLiteral(", "))));
            check(floating.isEmpty(),
                  "contextual: a page that is not a tab is hidden, not floating over the one that is");
        }
    }

    // ---- one colour over a selection of several KINDS of object ------------------------------------
    {
        // Colour, layer, linetype and lineweight belong to every object whatever kind it is. The panel only
        // offered them when the selection was all of one kind: pick a line and a circle together and it said
        // "2 objects (mixed)" and nothing else, so the commonest thing anybody wants to do to a mixed
        // selection - make it all red - could not be done from the panel at all.
        Editor* editor = win.findChild<Editor*>();
        check(editor != nullptr, "mixed: there is an editor");
        if (editor) {
            editor->selectAll();
            editor->deleteSelected();
            Entity line;
            line.type = EntityType::Line;
            line.id = QStringLiteral("mixed-line");
            line.a = {0.0, 0.0};
            line.b = {50.0, 0.0};
            Entity circle;
            circle.type = EntityType::Circle;
            circle.id = QStringLiteral("mixed-circle");
            circle.center = {20.0, 20.0};
            circle.radius = 8.0;
            Entity text;
            text.type = EntityType::Text;
            text.id = QStringLiteral("mixed-text");
            text.a = {0.0, 40.0};
            text.meta.insert(QStringLiteral("text"), QStringLiteral("N O T E"));
            editor->appendEntities({line, circle, text});
            editor->selectById(QStringLiteral("mixed-line"));
            editor->selectById(QStringLiteral("mixed-circle"), true);
            editor->selectById(QStringLiteral("mixed-text"), true);
            QApplication::processEvents();

            // The properties panel, and the Color row in it.
            QTableWidget* props = nullptr;
            for (QDockWidget* d : win.findChildren<QDockWidget*>())
                if (d->windowTitle() == QStringLiteral("PROPERTIES"))
                    props = d->findChild<QTableWidget*>();
            check(props != nullptr, "mixed: the properties panel is there");
            QComboBox* colorCombo = nullptr;
            QStringList rows;
            if (props)
                for (int r = 0; r < props->rowCount(); ++r) {
                    const QString key = props->item(r, 0) ? props->item(r, 0)->text() : QString();
                    rows << key;
                    if (key == QStringLiteral("Color"))
                        colorCombo = qobject_cast<QComboBox*>(props->cellWidget(r, 1));
                }
            std::printf("  mixed: three kinds selected, the panel offers %s\n",
                        qPrintable(rows.join(QStringLiteral(", "))));
            check(colorCombo != nullptr,
                  "mixed: a colour can be set with a line, a circle and a text selected together");
            check(rows.contains(QStringLiteral("Layer")) && rows.contains(QStringLiteral("Lineweight")),
                  "mixed: and so can the layer and the lineweight - every object has those too");

            if (colorCombo) {
                colorCombo->setCurrentText(QStringLiteral("Red"));
                QApplication::processEvents();
                QApplication::processEvents();   // the apply is deferred, so the combo survives it
                int red = 0;
                for (const Entity& e : editor->document()->entities())
                    if (e.color.toLower() == QLatin1String("#ff0000")) ++red;
                std::printf("  mixed: %d of the 3 went red\n", red);
                check(red == 3, "mixed: all three of them take the colour");
            }

            // And it says the truth about what they are: a mixed selection is not one type.
            bool saysMixed = false;
            if (props)
                for (int r = 0; r < props->rowCount(); ++r)
                    if (props->item(r, 0) && props->item(r, 0)->text() == QStringLiteral("Type") &&
                        props->item(r, 1) && props->item(r, 1)->text().contains(QStringLiteral("mixed")))
                        saysMixed = true;
            check(saysMixed, "mixed: the Type row still says the selection is of several kinds");

            editor->selectAll();
            editor->deleteSelected();
            QApplication::processEvents();
        }
    }

    // ---- a field left to the drawing shows the figure the drawing made of it ---------------------
    {
        // Half of these settings can be left alone, and then the drawing works the figure out - a fan
        // as long as its pitch needs, a knee radius off the bundle width, five millimetres of strip.
        // The field said "Auto" and the number it stood for was nowhere on screen. It now shows that
        // number, and the little A beside it says which it is: lit, the field is the drawing's own;
        // dark, the number is one somebody typed. Clicking the A hands it back to the drawing.
        //
        // WITH A TOOL ARMED, which is the only way these fields are ever seen. The Special group puts
        // its icons away while a tool is in use, and it did that by hiding - and resizing - every
        // tool button in the cell, which is what an A is made of.
        for (QTabWidget* tw : win.findChildren<QTabWidget*>())
            for (int i = 0; i < tw->count(); ++i)
                if (tw->tabText(i).contains(QStringLiteral("Harness"), Qt::CaseInsensitive) &&
                    tw->tabText(i).contains(QStringLiteral("mech"), Qt::CaseInsensitive))
                    tw->setCurrentIndex(i);
        if (Editor* ed = win.findChild<Editor*>()) ed->setTool(ToolId::WiresRun);
        QApplication::processEvents();

        // A field that can be left to the drawing is one with an A beside it, in its own cell.
        QList<QDoubleSpinBox*> autoFields;
        QList<QToolButton*> marks;
        for (QDoubleSpinBox* s : win.findChildren<QDoubleSpinBox*>()) {
            if (!s->isVisible() || !s->parentWidget()) continue;
            if (s->parentWidget()->objectName() != QStringLiteral("autoFieldCell")) continue;
            for (QToolButton* b : s->parentWidget()->findChildren<QToolButton*>())
                if (b->text() == QStringLiteral("A") && b->isVisible()) {
                    autoFields << s;
                    marks << b;
                    break;
                }
        }
        std::printf("  auto: %lld field(s) on screen with an A beside them\n",
                    static_cast<long long>(autoFields.size()));
        check(autoFields.size() >= 5,
              "auto: the fields that can be left to the drawing each have an A beside them");
        if (autoFields.isEmpty()) return spaceFailures;

        // AND IT IS A MARK ON A FIELD, not another tool on the ribbon: the Special group sizes every
        // tool button in a cell to a forty-pixel icon-only square.
        QToolButton* anA = marks.front();
        QDoubleSpinBox* itsField = autoFields.front();
        std::printf("  auto: the A is %dx%d and %s its letter\n", anA->width(), anA->height(),
                    anA->toolButtonStyle() == Qt::ToolButtonIconOnly ? "hides" : "shows");
        check(anA->width() <= 16 && anA->height() <= 16,
              "auto: the A is the size of a mark on a field, not the size of a tool");
        check(anA->toolButtonStyle() != Qt::ToolButtonIconOnly,
              "auto: and it shows its letter, which is the whole of what it says");

        // LIT TO START WITH, because nothing has been typed into any of them - and showing a real
        // figure rather than the word "Auto", which is the point of the whole thing.
        const double drawingsOwn = itsField->value();
        std::printf("  auto: left alone the field reads %.2f and the A is %s\n", drawingsOwn,
                    anA->isChecked() ? "lit" : "dark");
        check(anA->isChecked(), "auto: a field nobody has typed into is the drawing's own, and lit");
        check(drawingsOwn > 0.0,
              "auto: and it shows the figure the drawing made of it, not the word Auto");
        check(itsField->specialValueText().isEmpty() ||
                  std::abs(drawingsOwn - itsField->minimum()) > 1e-9,
              "auto: which is a number on the field, not its bottom stop standing in for one");

        // TYPED INTO, IT GOES DARK: a number somebody meant is not the drawing's own.
        itsField->setValue(drawingsOwn + 9.0);
        QApplication::processEvents();
        std::printf("  auto: typed %.2f, the A is %s\n", itsField->value(),
                    anA->isChecked() ? "lit" : "dark");
        check(!anA->isChecked(), "auto: typing a figure into it takes it off the drawing's own");
        check(std::abs(itsField->value() - (drawingsOwn + 9.0)) < 1e-6,
              "auto: and the field keeps what was typed");

        // AND CLICKING THE A HANDS IT BACK, figure and all.
        anA->click();
        QApplication::processEvents();
        std::printf("  auto: clicked the A, the field reads %.2f and the A is %s\n",
                    itsField->value(), anA->isChecked() ? "lit" : "dark");
        check(anA->isChecked(), "auto: clicking the A hands the field back to the drawing");
        check(std::abs(itsField->value() - drawingsOwn) < 1e-6,
              "auto: and the figure it shows is the drawing's own again");
    }

    // ---- a placed harness run can be set from the panel, not only from the ribbon -----------------
    {
        // The ribbon fields are the settings the NEXT run is drawn with. The values on the run in
        // front of you are the panel's, and four of the ribbon's seven had no row there at all - so
        // a fan length or a knee radius was settable up to the moment the run was placed and not
        // one moment afterwards, and getting one wrong meant deleting the run and drawing it again.
        Editor* editor = win.findChild<Editor*>();
        check(editor != nullptr, "harness panel: there is an editor");
        if (editor) {
            editor->selectAll();
            editor->deleteSelected();
            Entity run;
            run.type = EntityType::TwistedPair;
            run.id = QStringLiteral("panel-run");
            run.points = {{0.0, 0.0}, {200.0, 0.0}, {200.0, -160.0}};   // routed: it has a knee
            run.a = run.points.front();
            run.b = run.points.back();
            run.meta.insert(QStringLiteral("twistStraight"), true);
            run.meta.insert(QStringLiteral("twistWires"), 6);
            run.meta.insert(QStringLiteral("twistWidth"), 12.0);
            run.meta.insert(QStringLiteral("twistPitch"), 8.0);
            editor->appendEntities({run});
            editor->selectById(QStringLiteral("panel-run"));
            QApplication::processEvents();

            QTableWidget* props = nullptr;
            for (QDockWidget* d : win.findChildren<QDockWidget*>())
                if (d->windowTitle() == QStringLiteral("PROPERTIES"))
                    props = d->findChild<QTableWidget*>();
            check(props != nullptr, "harness panel: the properties panel is there");
            QStringList rows;
            QWidget* fanCell = nullptr;
            if (props)
                for (int r = 0; r < props->rowCount(); ++r) {
                    const QString key = props->item(r, 0) ? props->item(r, 0)->text() : QString();
                    rows << key;
                    if (key == QStringLiteral("Fan length")) fanCell = props->cellWidget(r, 1);
                }
            std::printf("  harness panel: a run selected, the panel offers %s\n",
                        qPrintable(rows.join(QStringLiteral(", "))));
            for (const QString& want : {QStringLiteral("Fan length"), QStringLiteral("Wire width"),
                                        QStringLiteral("Fan knee"), QStringLiteral("Knee radius")})
                check(rows.contains(want),
                      qPrintable(QStringLiteral("harness panel: %1 can be set on a placed run")
                                     .arg(want)));

            // AND SETTING ONE RE-LAYS THE RUN, which is the point of it being a value on the object
            // rather than a shape that has been drawn: the wires move, they are not redrawn.
            if (QDoubleSpinBox* fan = qobject_cast<QDoubleSpinBox*>(fanCell)) {
                fan->setValue(48.0);
                emit fan->editingFinished();   // the panel applies on finishing, not per keystroke
                QApplication::processEvents();
                QApplication::processEvents();   // the apply is deferred
                double wrote = 0.0;
                for (const Entity& e : editor->document()->entities())
                    if (e.id == QStringLiteral("panel-run"))
                        wrote = e.meta.value(QStringLiteral("twistEnds")).toDouble();
                std::printf("  harness panel: asked for a 48 mm fan, the run says %.1f\n", wrote);
                check(std::abs(wrote - 48.0) < 1e-6,
                      "harness panel: the fan length typed into the panel lands on the run");
            } else {
                check(false, "harness panel: the Fan length row is a number to type in");
            }

            editor->selectAll();
            editor->deleteSelected();
            QApplication::processEvents();
        }
    }

    // ---- the stock symbols are on the ribbon, not behind a menu of names --------------------------
    {
        // A symbol is picked by its SHAPE. Behind a fall menu the shapes were hidden and the eye had to
        // read words to find a picture it already knew - so they are small icons on the ribbon now, in the
        // same two rows and the same squares as the tools beside them.
        QTabBar* tabs = nullptr;
        for (QTabBar* bar : win.findChildren<QTabBar*>())
            for (int i = 0; i < bar->count(); ++i)
                if (bar->tabText(i) == QStringLiteral("Schematic build")) tabs = bar;
        check(tabs != nullptr, "symbols: there is a Schematic build tab to place from");

        // Every stock schematic symbol has a button of its own, named for the symbol.
        const QStringList wanted{QStringLiteral("Resistor (box)"), QStringLiteral("Resistor (zigzag)"),
                                 QStringLiteral("Capacitor"), QStringLiteral("Coil"),
                                 QStringLiteral("Ferrite")};
        QList<QToolButton*> found;
        for (QToolButton* b : win.findChildren<QToolButton*>()) {
            for (const QString& w : wanted)
                if (b->toolTip().startsWith(w) && b->toolButtonStyle() == Qt::ToolButtonIconOnly)
                    found.append(b);
        }
        std::printf("  symbols: %lld symbol buttons on the ribbon\n",
                    static_cast<long long>(found.size()));
        check(found.size() >= wanted.size(),
              "symbols: a small icon each - both resistors, the capacitor, the coil and the ferrite");
        bool sameSquare = !found.isEmpty();
        for (QToolButton* b : found) {
            if (b->icon().isNull()) sameSquare = false;
            if (b->size() != found.first()->size()) sameSquare = false;
        }
        check(sameSquare, "symbols: each drawn, and all the same square as the tools beside them");
        // In a grid of two rows, like everything else on the ribbon.
        bool twoRows = false;
        for (QGridLayout* g : win.findChildren<QGridLayout*>()) {
            if (g->count() < static_cast<int>(wanted.size())) continue;
            bool holdsSymbols = false;
            for (int i = 0; i < g->count(); ++i)
                if (found.contains(qobject_cast<QToolButton*>(g->itemAt(i)->widget())))
                    holdsSymbols = true;
            if (holdsSymbols && g->rowCount() == 2) twoRows = true;
        }
        check(twoRows, "symbols: laid out in two rows");
    }

    // ---- a side pulled wider stays that wide, and comes back that wide next time -----------------
    {
        // The width of a side is a decision - a bill of materials with part numbers in it wants more room
        // than a list of layers - and a decision has to outlast the panel folding away, and the program
        // being shut. It used to outlast neither: an unpinned side that was pulled wider came back at
        // whatever width it had been measured at before it was ever on screen.
        QSettings settings;
        settings.remove(QStringLiteral("panels"));   // this test's own starting point

        auto propsOf = [](MainWindow& w) -> QDockWidget* {
            for (QDockWidget* d : w.findChildren<QDockWidget*>())
                if (d->objectName() == QStringLiteral("panel_PROPERTIES")) return d;
            return nullptr;
        };
        MainWindow one;
        one.show();
        QApplication::processEvents();
        QDockWidget* props = propsOf(one);
        check(props != nullptr, "size: a panel has a name of its own, or nothing can be saved about it");
        if (props) {
            one.resizeDocks({props}, {320}, Qt::Horizontal);
            QApplication::processEvents();
            const int pulledTo = props->width();
            check(pulledTo > 200, "size: the properties side can be pulled wider");

            // The pin is the one checkable button on a panel's title bar; the other is its x.
            QToolButton* pin = nullptr;
            for (QToolButton* b : props->findChildren<QToolButton*>())
                if (b->isCheckable()) pin = b;
            check(pin != nullptr, "size: the side has a pin");
            if (pin) {
                // Folded away and opened again: the same width, with no file having been near it.
                pin->click();
                QApplication::processEvents();
                check(props->width() < 40, "size: unpinning folds the side away to a strip");
                pin->click();
                QApplication::processEvents();
                check(std::abs(props->width() - pulledTo) <= 8,
                      "size: and opening it again gives back the width it was pulled to");
            }

            one.savePanelLayout();
            const int writtenDown =
                QSettings().value(QStringLiteral("panels/panel_PROPERTIES/extent"), -1).toInt();
            check(writtenDown == pulledTo, "size: the width is written down as it stands");
            MainWindow two;
            two.show();
            // The widths are given out a turn after the window is up, and the layout that carries them out
            // is a turn after that again - so the window is given real time to settle, not just a pass of
            // the event loop.
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 600) QApplication::processEvents();
            QDockWidget* again = propsOf(two);
            check(again != nullptr, "size: the panel is there in a new session");
            if (again) {
                std::printf("  panel width: pulled to %d px, written down %d px, came back %d px\n",
                            pulledTo, writtenDown, again->width());
                check(std::abs(again->width() - pulledTo) <= 8,
                      "size: and it comes back the width it was left at");
            }
        }
        settings.remove(QStringLiteral("panels"));
    }

    // ---- a folded side opens OVER the drawing, not beside it --------------------------------------
    {
        // A panel that is only open while the cursor is on it is a thing held up in front of the drawing,
        // not a thing the drawing has to make room for. It used to come back by making the dock wide again,
        // which shoved the drawing sideways and then shoved it back - and left the panel at whatever width
        // the layout felt like giving it.
        QSettings settings;
        settings.remove(QStringLiteral("panels"));
        MainWindow w;
        w.show();
        QApplication::processEvents();
        auto* sheet = w.findChild<CanvasWidget*>();
        check(sheet != nullptr, "flyout: there is a canvas to cover");

        // Fold the right side away: the bill of materials and the layers are both on it.
        QDockWidget* bom = nullptr;
        for (QDockWidget* d : w.findChildren<QDockWidget*>())
            if (d->objectName() == QStringLiteral("panel_BILL OF MATERIALS")) bom = d;
        check(bom != nullptr, "flyout: the bill of materials is on the right");
        QToolButton* pin = nullptr;
        if (bom)
            for (QToolButton* b : bom->findChildren<QToolButton*>())
                if (b->isCheckable()) pin = b;
        if (sheet && pin) {
            pin->click();   // unpinned: the whole right side folds to strips
            QApplication::processEvents();
            const int canvasWhenFolded = sheet->width();
            const QPoint drawnAt(static_cast<int>(sheet->transform().view.x),
                                 static_cast<int>(sheet->transform().view.y));

            // Hover a strip. In a test the real cursor is wherever it is, so the strip is told directly -
            // and given a few passes, since a side just unpinned holds hover-expand back until the cursor
            // has left it once.
            QElapsedTimer waited;
            waited.start();
            bool anyFlyout = false;
            while (waited.elapsed() < 900 && !anyFlyout) {
                for (QWidget* strip : w.findChildren<QWidget*>())
                    if (strip->toolTip() == QStringLiteral("Hover to expand") && strip->isVisible()) {
                        QEnterEvent enter(QPointF(5, 5), QPointF(5, 5),
                                          strip->mapToGlobal(QPointF(5, 5)));
                        QApplication::sendEvent(strip, &enter);
                    }
                QApplication::processEvents();
                for (QWidget* f : w.findChildren<QWidget*>())
                    if (f->objectName() == QStringLiteral("panelFlyout") && f->isVisible())
                        anyFlyout = true;
            }
            check(anyFlyout, "flyout: hovering a folded side's strip opens the panel again");

            QList<QWidget*> flyouts;
            for (QWidget* f : w.findChildren<QWidget*>())
                if (f->objectName() == QStringLiteral("panelFlyout") && f->isVisible()) flyouts.append(f);
            std::printf("  flyout: %lld open, %d px wide; canvas %d px before and %d after\n",
                        static_cast<long long>(flyouts.size()),
                        flyouts.isEmpty() ? 0 : flyouts.first()->width(), canvasWhenFolded,
                        sheet->width());
            check(sheet->width() == canvasWhenFolded,
                  "flyout: the drawing keeps the whole width it had - the panel is over it, not beside it");
            check(static_cast<int>(sheet->transform().view.x) == drawnAt.x() &&
                      static_cast<int>(sheet->transform().view.y) == drawnAt.y(),
                  "flyout: and the drawing itself has not moved a pixel");
            // Every panel on the side opens at the SAME width: they share the column when they are pinned,
            // so they have to agree when they fly out.
            bool sameWidth = true;
            for (QWidget* f : flyouts)
                if (f->width() != flyouts.first()->width()) sameWidth = false;
            check(sameWidth, "flyout: every panel on a side opens at the one width the side has");
            // And ONE edge to set that width, running the length of the side - an edge per panel would say
            // the panels can be set to different widths, and they cannot.
            QList<QWidget*> edges;
            for (QWidget* e : w.findChildren<QWidget*>())
                if (e->objectName() == QStringLiteral("panelSideEdge") && e->isVisible()) edges.append(e);
            check(edges.size() == 1, "flyout: one edge for the whole side, not one per panel");
            if (edges.size() == 1 && !flyouts.isEmpty()) {
                QWidget* edge = edges.first();
                check(edge->width() >= 8, "flyout: the edge is wide enough to catch");
                // Right against the panels, so reaching for it does not mean crossing a gap.
                int gap = 1 << 20;
                for (QWidget* f : flyouts)
                    gap = std::min(gap, std::abs(edge->x() + edge->width() - f->x()));
                check(gap <= 1, "flyout: and it sits against them, with nothing to cross to reach it");

                // The cursor ON the edge rather than on the panel: the side has to stay open, or its width
                // could never be set - reaching for the edge used to fold it away every time.
                const QPoint onEdge =
                    edge->mapToGlobal(QPoint(edge->width() / 2, edge->height() / 2));
                QCursor::setPos(onEdge);
                if (QCursor::pos() == onEdge) {
                    QElapsedTimer held;
                    held.start();
                    while (held.elapsed() < 1200) QApplication::processEvents();
                    bool stillOpen = false;
                    for (QWidget* f : w.findChildren<QWidget*>())
                        if (f->objectName() == QStringLiteral("panelFlyout") && f->isVisible())
                            stillOpen = true;
                    check(stillOpen, "flyout: with the cursor on the edge the side stays open");
                } else {
                    std::printf("  flyout: no cursor to move here - the reach itself is not exercised\n");
                }
            }
            if (edges.size() == 1 && flyouts.size() > 1) {
                int top = flyouts.first()->y(), bottom = 0;
                for (QWidget* f : flyouts) {
                    top = std::min(top, f->y());
                    bottom = std::max(bottom, f->y() + f->height());
                }
                check(edges.first()->height() >= bottom - top - 2,
                      "flyout: and it runs the whole length of them");
            }
            for (QWidget* f : flyouts)
                check(f->width() > 120, "flyout: opened wide enough to read");
        }
        settings.remove(QStringLiteral("panels"));
    }

    // ---- the light scheme: chrome only, and the text with it ---------------------------------------
    {
        // A drawing office is not always a dark room. The ribbon, the menus and the panels can be light -
        // and the whole of getting that right is the TEXT: pale grey lettering that reads on charcoal is a
        // smudge on white, which is what makes a half-themed program unusable rather than merely ugly.
        QSettings settings;
        settings.setValue(QStringLiteral("ui/darkChrome"), false);
        MainWindow lit;
        check(!Chrome::dark(), "scheme: it comes up in the scheme that was set last time");
        check(Chrome::ink().lightnessF() < 0.4 && Chrome::bar().lightnessF() > 0.6,
              "scheme: light ground, dark ink - not the dark scheme with its greys kept");

        QTabWidget* ribbon = nullptr;
        for (QTabWidget* tw : lit.findChildren<QTabWidget*>())
            for (int i = 0; i < tw->count(); ++i)
                if (tw->tabText(i) == QStringLiteral("View")) ribbon = tw;
        check(ribbon != nullptr, "scheme: the ribbon is there to be coloured");
        if (ribbon) {
            const QString ss = ribbon->styleSheet();
            check(ss.contains(QStringLiteral("#1e2126")),
                  "scheme: the ribbon letters in near-black");
            check(!ss.contains(QStringLiteral("#c9ccd1")) && !ss.contains(QStringLiteral("#f2f4f7")),
                  "scheme: and nothing on it is still lettered for a dark ribbon");
        }
        // A group's name under its buttons: named, so the ribbon's own sheet colours it. Styled where it is
        // built, it would keep its pale grey - an inline sheet beats an ancestor's.
        int named = 0;
        for (QLabel* l : lit.findChildren<QLabel*>())
            if (l->objectName() == QStringLiteral("ribbonGroupTitle")) ++named;
        std::printf("  scheme: %d ribbon group names take the scheme's colour\n", named);
        check(named > 10, "scheme: every ribbon group name is coloured by the scheme");

        // A panel's title bar: ground AND lettering, or it is a white bar with white writing on it.
        bool titleReadable = false;
        for (QDockWidget* d : lit.findChildren<QDockWidget*>()) {
            QWidget* bar = d->titleBarWidget();
            if (!bar) continue;
            for (QLabel* l : bar->findChildren<QLabel*>())
                if (l->styleSheet().contains(QStringLiteral("#1e2126"))) titleReadable = true;
        }
        check(titleReadable, "scheme: a panel's name is dark on its light title bar");

        // The switch on the View tab and the one in Options are the same setting.
        QToolButton* schemeBtn = nullptr;
        for (QToolButton* b : lit.findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("Light chrome")) schemeBtn = b;
        check(schemeBtn != nullptr, "scheme: the View tab carries the switch");
        if (schemeBtn) {
            check(schemeBtn->isChecked(), "scheme: and it says which scheme is on");
            schemeBtn->click();
            QApplication::processEvents();
            check(Chrome::dark(), "scheme: pressing it goes back to the dark chrome");
            if (ribbon)
                check(ribbon->styleSheet().contains(QStringLiteral("#3a3d43")),
                      "scheme: the ribbon goes with it");
        }
        settings.setValue(QStringLiteral("ui/darkChrome"), true);
    }


    // ---- the shared Tools group carries a scientific calculator -----------------------------------
    {
        ScientificCalculatorDialog calc;
        double out = 0.0;
        QString error;
        check(calc.evaluateForTest(QStringLiteral("2 + 3 * 4"), true, out, &error) &&
                  near(out, 14.0),
              "calculator: ordinary precedence");
        check(calc.evaluateForTest(QStringLiteral("sqrt(9) + pow(2,4)"), true, out) &&
                  near(out, 19.0),
              "calculator: roots, powers and functions");
        check(calc.evaluateForTest(QStringLiteral("sin(30)"), true, out) && near(out, 0.5),
              "calculator: degrees mode");
        check(calc.evaluateForTest(QStringLiteral("sin(pi/2)"), false, out) && near(out, 1.0),
              "calculator: radians mode");
        check(calc.evaluateForTest(QStringLiteral("5! + 50% * 200"), true, out) &&
                  near(out, 220.0),
              "calculator: factorial and per cent");
        check(!calc.evaluateForTest(QStringLiteral("sqrt(-1)"), true, out, &error) &&
                  error.contains(QStringLiteral("not negative")),
              "calculator: a domain error says what it is");

        calc.setExpressionForTest(QStringLiteral("2 + 2"));
        calc.calculateForTest();
        calc.setExpressionForTest(QStringLiteral("ans * 3"));
        calc.calculateForTest();
        check(calc.displayForTest() == QStringLiteral("12") && calc.historyCountForTest() == 2,
              "calculator: Ans carries the last answer and the work is remembered");

        MainWindow toolWin;
        QList<QToolButton*> buttons;
        for (QToolButton* b : toolWin.findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("Scientific\ncalculator")) buttons << b;
        check(buttons.size() == 2,
              "calculator: on the Tools group of both schematic and mechanical tabs");
        if (!buttons.isEmpty() && buttons.first()->parentWidget()) {
            auto* toolsGrid = qobject_cast<QGridLayout*>(buttons.first()->parentWidget()->layout());
            check(toolsGrid && toolsGrid->rowCount() == 1,
                  "calculator: the Tools buttons get the ribbon's full height, so their labels are not clipped");
        }
        check(toolWin.hasCommandNamed(QStringLiteral("calculator")),
              "calculator: it can also be typed as a command");
        QDockWidget* dock = nullptr;
        for (QDockWidget* d : toolWin.findChildren<QDockWidget*>())
            if (d->windowTitle() == QStringLiteral("CALCULATOR")) dock = d;
        check(dock != nullptr, "calculator: it is a side panel, not only a floating window");
        if (dock) {
            check(dock->isHidden(), "calculator: it waits until it is asked for");
            check(dock->findChild<ScientificCalculatorDialog*>() != nullptr,
                  "calculator: the panel carries the calculator itself");
        }
        if (!buttons.isEmpty() && dock) {
            buttons.first()->click();
            QApplication::processEvents();
            check(!dock->isHidden(), "calculator: the ribbon button opens the panel");
            dock->hide();
        }
    }

    // ---- the break mark's fields say what they are going to draw ---------------------------------
    {
        // A field reading "auto" tells nobody how far apart the strokes will be or how far they
        // lean. The figure the drawing has worked out does, and the little A beside it is what says
        // the figure is the drawing's own rather than one somebody typed.
        MainWindow win;
        win.resize(1500, 860);
        win.show();
        QApplication::processEvents();
        Editor* ed = win.editorForTest();
        if (ed) ed->setTool(ToolId::LengthBreak);
        QApplication::processEvents();
        // The break mark's own three fields, found in the box the first of them stands in - "Angle"
        // on its own is a word half the properties in the program use.
        QWidget* box = nullptr;
        for (QLabel* l : win.findChildren<QLabel*>())
            if (l->text() == QStringLiteral("Distance between")) box = l->parentWidget();
        auto besideLabel = [&](const QString& text) -> QDoubleSpinBox* {
            auto* g = box ? qobject_cast<QGridLayout*>(box->layout()) : nullptr;
            if (!g) return nullptr;
            for (QLabel* l : box->findChildren<QLabel*>()) {
                if (l->text() != text) continue;
                int r = 0, c = 0, rs = 0, cs = 0;
                const int at = g->indexOf(l);
                if (at < 0) continue;
                g->getItemPosition(at, &r, &c, &rs, &cs);
                if (QLayoutItem* it = g->itemAtPosition(r, c + 1))
                    if (QWidget* cell = it->widget()) return cell->findChild<QDoubleSpinBox*>();
            }
            return nullptr;
        };
        QDoubleSpinBox* gap = besideLabel(QStringLiteral("Distance between"));
        QDoubleSpinBox* lean = besideLabel(QStringLiteral("Angle"));
        check(gap != nullptr && lean != nullptr,
              "break fields: the gap and the lean are both on the ribbon");
        if (gap && lean && ed) {
            // The lean is the one figure the two styles differ on - an S sweeps square, a stroke
            // leans - so the field is asked about a stroke, where there is a lean to report.
            ed->breakStyle() = QStringLiteral("straight");
            ed->setTool(ToolId::Select);
            ed->setTool(ToolId::LengthBreak);
            QApplication::processEvents();
            std::printf("  break fields: gap shows %.1f, lean shows %.1f, and the word is \"%s\"\n",
                        gap->value(), lean->value(), qPrintable(gap->specialValueText()));
            check(gap->value() > 0.1,
                  "break fields: the gap shows the figure the drawing would use, not nothing");
            check(lean->value() > 1.0,
                  "break fields: and so does the lean - a straight stroke leans, and says how far");
            check(gap->specialValueText().isEmpty(),
                  "break fields: and neither of them says the word auto any more");
        }
    }

    // ---- the option boxes keep their size and their places, whatever is in them -------------------
    {
        // Which fields a box shows depends on the sort of body picked and on which ticks are set,
        // and a grid gives a hidden widget no room at all - so unticking Flush ends took the strip
        // in by the width of a field and everything to the right of it jumped sideways. A ribbon is
        // a place on the screen you learn: a field that moves because a DIFFERENT field went away
        // has to be found again every time.
        MainWindow win;
        win.resize(1700, 900);
        win.show();
        QApplication::processEvents();
        for (QTabWidget* t : win.findChildren<QTabWidget*>())
            for (int i = 0; i < t->count(); ++i)
                if (t->tabText(i).contains(QStringLiteral("Harness"))) t->setCurrentIndex(i);
        QApplication::processEvents();
        Editor* ed = win.editorForTest();
        if (ed) ed->setTool(ToolId::WiresRun);
        QApplication::processEvents();
        QFrame* fanBox = nullptr;
        QFrame* bodyBox = nullptr;
        for (QFrame* f : win.findChildren<QFrame*>()) {
            if (f->objectName() != QStringLiteral("harnessSection")) continue;
            for (QLabel* l : f->findChildren<QLabel*>()) {
                if (l->text() == QStringLiteral("Fan out")) fanBox = f;
                if (l->text() == QStringLiteral("Body") &&
                    l->objectName() == QStringLiteral("harnessSectionName"))
                    bodyBox = f;
            }
        }
        check(fanBox != nullptr && bodyBox != nullptr,
              "boxes: the fan-out and body boxes are both on the ribbon");
        QComboBox* body = nullptr;
        for (QComboBox* c : win.findChildren<QComboBox*>())
            if (c->findText(QStringLiteral("Hook-up wires")) >= 0) body = c;
        QCheckBox* flush = nullptr;
        for (QCheckBox* c : win.findChildren<QCheckBox*>())
            if (c->text() == QStringLiteral("Flush ends")) flush = c;
        if (fanBox && bodyBox && body && flush) {
            // THE FIELDS SIT IN THREE LINES, with the caption under them.
            QGridLayout* fg = fanBox->findChild<QGridLayout*>();
            auto cellOf = [](QGridLayout* g, const QString& text) {
                for (int i = 0; g && i < g->count(); ++i) {
                    QWidget* w = g->itemAt(i)->widget();
                    QLabel* l = qobject_cast<QLabel*>(w);
                    if (!l || l->text() != text) continue;
                    int r = 0, c = 0, rs = 0, cs = 0;
                    g->getItemPosition(i, &r, &c, &rs, &cs);
                    return QPoint(c, r);
                }
                return QPoint(-1, -1);
            };
            const QPoint knee = cellOf(fg, QStringLiteral("Fan knee"));
            const QPoint flushAt = cellOf(fg, QStringLiteral("Flush length"));
            std::printf("  boxes: fan knee at row %d col %d, flush length at row %d col %d, %d "
                        "row(s)\n", knee.y(), knee.x(), flushAt.y(), flushAt.x(),
                        fg ? fg->rowCount() : 0);
            check(knee.x() >= 0 && flushAt.x() == knee.x() && flushAt.y() == knee.y() + 1,
                  "boxes: the flush length stands directly under the fan knee");
            check(fg && fg->rowCount() == 4,
                  "boxes: three lines of fields, and the name of the box under them");

            // AND THE SIZE IS SETTLED BY THE SORT OF BODY, not by which ticks happen to be set. A
            // field a tick has taken away keeps its room - ticking Flush ends off must not take the
            // strip in by the width of a field and shuffle everything to the right of it - while a
            // sort with nothing to set takes no room for the fields it will never show.
            QCheckBox* transition = nullptr;
            for (QCheckBox* c : win.findChildren<QCheckBox*>())
                if (c->text() == QStringLiteral("Transition heatshrink")) transition = c;
            struct Shot { QSize fan, body; };
            std::vector<Shot> ticked;
            auto snap = [&] {
                QApplication::processEvents();
                ticked.push_back({fanBox->sizeHint(), bodyBox->sizeHint()});
            };
            body->setCurrentIndex(body->findText(QStringLiteral("Cable")));
            snap();
            flush->click();       // the flush length goes
            snap();
            flush->click();
            if (transition) {
                transition->click();   // and the two boot lengths
                snap();
                transition->click();
            }
            bool steady = true;
            for (const Shot& s : ticked)
                steady = steady && s.fan == ticked.front().fan && s.body == ticked.front().body;
            QStringList said;
            for (const Shot& s : ticked)
                said << QStringLiteral("%1x%2/%3x%4")
                            .arg(s.fan.width()).arg(s.fan.height())
                            .arg(s.body.width()).arg(s.body.height());
            const int asCable = bodyBox->sizeHint().width();
            body->setCurrentIndex(body->findText(QStringLiteral("Hook-up wires")));
            QApplication::processEvents();
            const int asWires = bodyBox->sizeHint().width();
            std::printf("  boxes: a cable, ticks either way - %s; the body box is %d wide for a "
                        "cable and %d for hook-up wires\n",
                        qPrintable(said.join(QStringLiteral(", "))), asCable, asWires);
            check(steady,
                  "boxes: a tick taking a field away does not change the size of either box");
            check(asWires < asCable / 2,
                  "boxes: and a sort with nothing to set takes no room for what it cannot show");
        }
    }

    // ---- the transition heatshrink is a tick, and only a cable has one ----------------------------
    {
        // A cable over the converged stretch has a boot at each end of it: the transition the wires
        // come out of. A length of tubing over the whole run has none - it IS the transition - so
        // the tick is a cable thing and goes away with the other sort.
        MainWindow win;
        win.resize(1400, 860);
        win.show();
        QApplication::processEvents();
        Editor* ed = win.editorForTest();
        QCheckBox* tick = nullptr;
        for (QCheckBox* c : win.findChildren<QCheckBox*>())
            if (c->text() == QStringLiteral("Transition heatshrink")) tick = c;
        QComboBox* body = nullptr;
        for (QComboBox* c : win.findChildren<QComboBox*>())
            if (c->findText(QStringLiteral("Hook-up wires")) >= 0) body = c;
        check(tick != nullptr && body != nullptr && ed != nullptr,
              "transition tick: the body menu and the transition tick are both on the ribbon");
        if (tick && body && ed) {
            ed->setTool(ToolId::WiresRun);
            QApplication::processEvents();
            body->setCurrentIndex(body->findText(QStringLiteral("Heatshrink")));
            QApplication::processEvents();
            const bool onTubing = !tick->isHidden();
            // AND THE TUBING HAS A TICK OF ITS OWN IN THE SAME PLACE: which of its two sorts it is.
            QCheckBox* overCable = nullptr;
            for (QCheckBox* c : win.findChildren<QCheckBox*>())
                if (c->text() == QStringLiteral("Over the cable")) overCable = c;
            check(overCable != nullptr, "over the cable: the tubing offers its two sorts");
            const bool sortOnTubing = overCable && !overCable->isHidden();
            body->setCurrentIndex(body->findText(QStringLiteral("Cable")));
            QApplication::processEvents();
            const bool onCable = !tick->isHidden();
            const bool sortOnCable = overCable && !overCable->isHidden();
            std::printf("  over the cable: shown for tubing %d, for a cable %d\n",
                        sortOnTubing ? 1 : 0, sortOnCable ? 1 : 0);
            check(sortOnTubing && !sortOnCable,
                  "over the cable: it is a tubing question, and stands where the cable asks its own");
            std::printf("  transition tick: shown for a cable %d, for tubing %d, ticked %d\n",
                        onCable ? 1 : 0, onTubing ? 1 : 0, tick->isChecked() ? 1 : 0);
            check(onCable && !onTubing,
                  "transition tick: a cable is offered one, a length of tubing is not");
            check(tick->isChecked() && ed->twistBodyTransition(),
                  "transition tick: and a cable starts with it, which is what a cable looks like");
            tick->click();
            QApplication::processEvents();
            check(!ed->twistBodyTransition(),
                  "transition tick: unticking it takes the boots off the cable");
            tick->click();
            QApplication::processEvents();
            // AND CONVERGE-TO IS A FIGURE OF THE HOOK-UP-WIRES BODY AND OF NO OTHER: it says how
            // many lines the stretch between the fans is drawn as, and a stretch covered by a cable
            // or by a length of tubing draws none of them. So it stands in the Body box with the
            // sort it belongs to, and it comes and goes with it.
            QLabel* converge = nullptr;
            for (QLabel* l : win.findChildren<QLabel*>())
                if (l->text() == QStringLiteral("Converge to")) converge = l;
            check(converge != nullptr, "converge: the field is on the ribbon");
            if (converge) {
                QStringList boxedWith;
                if (QWidget* box = converge->parentWidget())
                    for (QLabel* l : box->findChildren<QLabel*>()) boxedWith << l->text();
                const bool onACover = !converge->isHidden();
                body->setCurrentIndex(body->findText(QStringLiteral("Hook-up wires")));
                QApplication::processEvents();
                const bool onWires = !converge->isHidden();
                std::printf("  converge: boxed with [%s], shown for wires %d, for a cable %d\n",
                            qPrintable(boxedWith.join(QStringLiteral(", "))), onWires ? 1 : 0,
                            onACover ? 1 : 0);
                check(boxedWith.contains(QStringLiteral("Body")),
                      "converge: it stands in the Body box, with the sort of body it belongs to");
                check(onWires && !onACover,
                      "converge: offered for hook-up wires, and not against a cover that hides them");
            }
            body->setCurrentIndex(body->findText(QStringLiteral("Hook-up wires")));
        }
    }

    // ---- the Special group: two equal rows of icons, and no dead columns ----------------------------
    {
        // The pieces of furniture a mechanical drawing needs - the break mark, the twisted runs, the
        // heatshrinks, the part symbol, the label. Seven icons, put down as they were made: the seventh
        // landed in a column of its own with a hole under it, and each column reserved 78 pixels because the
        // buttons used to carry a two-line caption. The captions have long since become tooltips, so all that
        // width was doing was pushing seven 40-pixel icons across four 78-pixel columns - a comb rather than
        // a block, and no two rows of it the same length.
        MainWindow win;
        win.resize(1400, 860);
        win.show();
        QApplication::processEvents();
        QWidget* group = nullptr;
        for (QLabel* l : win.findChildren<QLabel*>())
            if (l->text() == QStringLiteral("Special") && l->parentWidget()) group = l->parentWidget();
        check(group != nullptr, "special: the group is on the harness-mechanical tab");
        QGridLayout* grid = group ? group->findChild<QGridLayout*>() : nullptr;
        check(grid != nullptr, "special: laid out as a grid");
        if (grid) {
            int perRow[2] = {0, 0};
            int widest = 0;
            for (int i = 0; i < grid->count(); ++i) {
                int r = 0, c = 0, rs = 0, cs = 0;
                grid->getItemPosition(i, &r, &c, &rs, &cs);
                if (r >= 0 && r < 2) ++perRow[r];
                if (QWidget* w = grid->itemAt(i)->widget()) widest = std::max(widest, w->sizeHint().width());
            }
            std::printf("  special: %d row(s), %d on top and %d under, %d column(s), widest icon %d px\n",
                        grid->rowCount(), perRow[0], perRow[1], grid->columnCount(), widest);
            check(grid->rowCount() == 2, "special: two rows, no more and no fewer");
            check(std::abs(perRow[0] - perRow[1]) <= 1,
                  "special: the two rows are the same length - as equal as an odd number allows");
            check(perRow[0] >= perRow[1], "special: and the odd one out is on top, where the eye starts");
            check(grid->columnCount() == (perRow[0] + perRow[1] + 1) / 2,
                  "special: exactly as many columns as it takes to halve them - no column of one");
            // No dead space: a column reserves the width of the icon in it and not the width of a caption
            // that is no longer there.
            for (int col = 0; col < grid->columnCount(); ++col)
                check(grid->columnMinimumWidth(col) <= widest + 8,
                      "special: no column reserves room for a caption that has become a tooltip");
        }
        QToolButton* heatshrink = nullptr;
        for (QToolButton* b : group ? group->findChildren<QToolButton*>() : QList<QToolButton*>())
            if (b->defaultAction() &&
                b->defaultAction()->data().toInt() == static_cast<int>(ToolId::HeatshrinkRoute))
                heatshrink = b;
        check(heatshrink && heatshrink->menu(),
              "special: the heatshrink button has the sleeve choices behind it");
        if (heatshrink && heatshrink->menu()) {
            const QList<QAction*> acts = heatshrink->menu()->actions();
            QStringList labels;
            QList<qint64> glyphs;
            for (QAction* a : acts) {
                labels << a->text();
                glyphs << a->icon().pixmap(QSize(24, 24)).cacheKey();
            }
            const QString allLabels = labels.join(QStringLiteral(" | "));
            check(acts.size() == 4 &&
                      allLabels.contains(
                          QStringLiteral("Cable over the run with heatshrink on ends")) &&
                      allLabels.contains(QStringLiteral("Transition heatshrink")),
                  "special: cable-end and transition sleeves are both on the heatshrink menu");
            // AND THE WHOLE-RUN CABLE IS CALLED WHAT IT IS. It was "cable over the run with
            // heatshrink", which is the NEXT choice down - the one with heatshrink on its ends -
            // and having the two of them share most of a name meant picking between them by
            // reading to the end of both.
            check(labels.contains(QStringLiteral("Cable over the run")),
                  "special: the whole-run cable is named for what it is, and only that");
            bool distinctGlyphs = glyphs.size() == 4;
            for (int i = 0; distinctGlyphs && i < glyphs.size(); ++i)
                for (int j = i + 1; j < glyphs.size(); ++j)
                    distinctGlyphs = glyphs[i] != glyphs[j];
            check(distinctGlyphs, "special: each sleeve choice wears its own picture");
            if (acts.size() == 4 && win.editorForTest()) {
                acts[2]->trigger();
                check(win.editorForTest()->shrinkRouteMode() == ShrinkRouteMode::CableEnds,
                      "special: choosing it sets the tool to cable with heatshrink ends");
                acts[3]->trigger();
                check(win.editorForTest()->shrinkRouteMode() == ShrinkRouteMode::Transition,
                      "special: choosing Transition heatshrink sets its crossing-line mode");
                acts[0]->trigger();
            }
        }
    }

    // ---- THE QUICK-ACCESS STRIP: New, Open, Save, Save all ------------------------------------
    //
    // Four things done constantly, on the row above the tabs. They are all on the file menu behind
    // the logo, which was the argument for not having them - and "reachable" is not "to hand".
    {
        QWidget* corner = win.menuBar()->cornerWidget(Qt::TopLeftCorner);
        check(corner != nullptr, "quick: there is a strip in the top-left of the menu row");
        if (corner) {
            // The logo is still the file button, and the four are beside it - the file commands next
            // to the thing they came out of.
            const QList<QToolButton*> btns = corner->findChildren<QToolButton*>();
            std::printf("  quick: %lld button(s) on the strip\n",
                        static_cast<long long>(btns.size()));
            check(btns.size() == 5, "quick: the logo and four commands");
            QStringList tips;
            for (QToolButton* b : btns) tips << b->toolTip();
            const QString all = tips.join(QStringLiteral(" | "));
            check(all.contains(QStringLiteral("file menu")), "quick: the logo is still there");
            check(all.contains(QStringLiteral("New drawing")), "quick: New");
            check(all.contains(QStringLiteral("Open a drawing")), "quick: Open");
            check(all.contains(QStringLiteral("Save this drawing")), "quick: Save");
            check(all.contains(QStringLiteral("Save every open drawing")), "quick: Save all");
            // EVERY ONE OF THEM CARRIES ITS SHORTCUT, which is how somebody stops reaching for the
            // button: the tooltip on the thing they are already pointing at teaches the key.
            check(all.contains(QStringLiteral("Ctrl+N")) && all.contains(QStringLiteral("Ctrl+O")) &&
                      all.contains(QStringLiteral("Ctrl+S")) &&
                      all.contains(QStringLiteral("Ctrl+Shift+S")),
                  "quick: and each says the key that does the same thing");
            // AND EACH HAS A PICTURE. Drawn in code, so there is nothing to fail to load - but an
            // icon that came out empty would be a strip of four blank buttons, so it is checked.
            int drawn = 0;
            for (QToolButton* b : btns)
                if (!b->icon().isNull() && !b->icon().pixmap(QSize(18, 18)).isNull()) ++drawn;
            check(drawn == btns.size(), "quick: every button has a glyph on it");

            // SAVE ALL IS LIVE ONLY WHILE THERE IS SOMETHING TO WRITE. Greyed, it answers the
            // question somebody glances at that corner to ask: is everything saved?
            QToolButton* saveAll = nullptr;
            for (QToolButton* b : btns)
                if (b->toolTip().contains(QStringLiteral("Save every open drawing"))) saveAll = b;
            check(saveAll != nullptr, "quick: the Save all button is findable");
            if (saveAll) {
                // A drawing that has been drawn on has something to save.
                Document* doc = win.editorForTest() ? win.editorForTest()->document() : nullptr;
                check(doc != nullptr, "quick: there is a drawing open");
                if (doc) {
                    doc->setDirty(true);
                    win.refreshQuickAccessForTest();
                    check(saveAll->isEnabled(),
                          "quick: with unsaved work, Save all is live and says how much");
                    check(saveAll->toolTip().contains(QStringLiteral("unsaved")),
                          "quick: and its tooltip counts it");
                    doc->setDirty(false);
                    win.refreshQuickAccessForTest();
                    // Every open drawing, not just the active one: what the button reports is the
                    // whole window's state, and this test window may be holding more than one.
                    const int stillDirty = win.dirtyDocumentCountForTest();
                    if (stillDirty == 0)
                        check(!saveAll->isEnabled(),
                              "quick: with everything saved it greys out rather than doing nothing");
                    // And pressing it then writes nothing at all. Asked only when nothing is dirty:
                    // a dirty drawing that has never been named would be asked for a name, and a
                    // modal file dialogue in a test run is a test run that never finishes.
                    if (stillDirty == 0)
                        check(win.saveAllDocumentsForTest() == 0,
                              "quick: a clean drawing is not rewritten - its timestamp is a fact");
                }
            }
        }
    }

    // ---- The menu bar: what is on it, and what is no longer on it --------------------------------
    {
        MainWindow mw;
        auto menuNamed = [&mw](const QString& title) -> QMenu* {
            for (QMenu* m : mw.menuBar()->findChildren<QMenu*>())
                if (m->title().remove(QLatin1Char('&')) == title) return m;
            return nullptr;
        };
        auto itemsOf = [](QMenu* m) {
            QStringList out;
            if (m)
                for (QAction* a : m->actions())
                    if (!a->isSeparator()) out << a->text().remove(QLatin1Char('&'));
            return out;
        };

        QMenu* edit = menuNamed(QStringLiteral("Edit"));
        QMenu* help = menuNamed(QStringLiteral("Help"));
        QMenu* file = menuNamed(QStringLiteral("File"));
        check(edit && help, "menus: Edit and Help are both on the bar");
        const QStringList editItems = itemsOf(edit);
        const QStringList helpItems = itemsOf(help);
        std::printf("  menus: Edit = %s\n", qPrintable(editItems.join(QStringLiteral(" | "))));
        std::printf("  menus: Help = %s\n", qPrintable(helpItems.join(QStringLiteral(" | "))));

        // COLOUR BY LAYER IS OFF THE EDIT MENU. It is a property command, not an edit one, and it sat
        // alone under a separator looking like the menu had been left half-finished.
        bool anyColour = false;
        for (const QString& s : editItems)
            if (s.contains(QLatin1String("Color"), Qt::CaseInsensitive) ||
                s.contains(QLatin1String("Colour"), Qt::CaseInsensitive))
                anyColour = true;
        check(!anyColour, "menus: Edit no longer offers Color: ByLayer");
        // But the command itself is still reachable by typing it - removing the menu entry was not
        // meant to remove the feature.
        check(mw.hasCommandNamed(QStringLiteral("bylayer")),
              "menus: and BYLAYER is still there to be typed");
        // The rest of Edit is untouched.
        check(editItems.contains(QStringLiteral("Undo")) &&
                  editItems.contains(QStringLiteral("Select all")),
              "menus: Undo and Select all are where they were");

        // ABOUT IS UNDER HELP, which is where a person looks for it.
        bool aboutInHelp = false, aboutInFile = false;
        for (const QString& s : helpItems)
            if (s.startsWith(QLatin1String("About"))) aboutInHelp = true;
        for (const QString& s : itemsOf(file))
            if (s.startsWith(QLatin1String("About"))) aboutInFile = true;
        check(aboutInHelp, "menus: Help has About Edraw");
        check(!aboutInFile, "menus: and File no longer does");
        bool commandsInHelp = false;
        for (const QString& s : helpItems)
            if (s.contains(QLatin1String("Commands"))) commandsInHelp = true;
        check(commandsInHelp, "menus: Help still opens the command list");
    }

    if (spaceFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", spaceFailures);
    return spaceFailures;
}
