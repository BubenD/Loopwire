// The strip along the bottom of the canvas: Model and the layouts, with an x on each layout, a grid that
// belongs to the space it was turned on in, and the scale the layout is plotted at in the corner.
//
// All three are about a thing belonging where it is set. A layout is a sheet, and closing one is the sort
// of thing done where it was made rather than through a menu. A grid over a sheet being laid out has
// nothing to do with the grid over the harness. And the plot scale is the LAYOUT'S - it is kept with the
// drawing, on its viewport, not as a setting of the window that happened to be open.
#include "app/Editor.h"
#include "app/MainWindow.h"
#include "model/Document.h"
#include "render/CanvasWidget.h"

#include <QComboBox>
#include <QLabel>
#include <QMouseEvent>
#include <QTabBar>
#include <QToolButton>
#include <QApplication>

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
        check(!scale->isEnabled(),
              "scale: model space has no plot scale, so the box is out of reach there");
        spaces->setCurrentIndex(1);
        check(scale->isEnabled(), "scale: on a layout it can be set");
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

    // ---- the scale, findable ----------------------------------------------------------------------
    {
        bool labelled = false;
        for (QLabel* l : win.findChildren<QLabel*>())
            if (l->text() == QStringLiteral("Scale")) labelled = true;
        check(labelled, "scale: the box says what it is, so it reads as something to press");
    }

    if (spaceFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", spaceFailures);
    return spaceFailures;
}
