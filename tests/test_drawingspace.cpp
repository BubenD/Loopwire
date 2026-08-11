// The strip along the bottom of the canvas: Model and the layouts, with an x on each layout, a grid that
// belongs to the space it was turned on in, and the scale the layout is plotted at in the corner.
//
// All three are about a thing belonging where it is set. A layout is a sheet, and closing one is the sort
// of thing done where it was made rather than through a menu. A grid over a sheet being laid out has
// nothing to do with the grid over the harness. And the plot scale is the LAYOUT'S - it is kept with the
// drawing, on its viewport, not as a setting of the window that happened to be open.
#include "app/MainWindow.h"
#include "model/Document.h"
#include "render/CanvasWidget.h"

#include <QComboBox>
#include <QTabBar>
#include <QToolButton>

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
        const Document* doc = canvas->document();
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

    if (spaceFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", spaceFailures);
    return spaceFailures;
}
