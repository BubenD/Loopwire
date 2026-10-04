// The 3D Preview ribbon tab: the harness in 3D where the drawing is, and its commands as a 3D program has them.
//
// While this tab is open the 3D view takes the drawing's place - out of its panel, the whole of the middle of the
// window - and every other tab puts the drawing back (showPreview3D). Its buttons are the 3D view's own commands: how
// it is looked at, the model it is routed in, the route's tools, and the lengths the route gives the harness. Each is
// a drawn picture with its name under it (Icons3D), drawn again in the scheme's ink when the scheme changes.
#include "app/MainWindow.h"

#include "app/Chrome.h"
#include "app/Icons3D.h"
#include "app/View3DPane.h"
#include "render/CanvasWidget.h"

#include <QButtonGroup>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace Edraw_Desktop {

void MainWindow::buildView3DTab() {
    auto [page, lay] = makeTabPage();
    view3dTabPage_ = page;
    auto group = [&](const QString& title, auto fill) {
        auto* w = new QWidget(page);
        auto* row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(1);
        fill(w, row);
        addGroup(lay, title, w);
    };
    auto big = [this](QWidget* parent, QHBoxLayout* row, const char* name, const QString& text, const QString& icon,
                      const QString& tip, auto fn) {
        auto* b = new QToolButton(parent);
        b->setObjectName(QLatin1String(name));
        b->setText(text);
        b->setToolTip(tip);
        b->setIcon(icon3D(icon, Chrome::ink()));
        b->setIconSize(QSize(28, 28));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, fn);
        row->addWidget(b);
        preview3dIcons_.append({icon, b});
        return b;
    };
    group(QStringLiteral("View"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "preview3dFit", QStringLiteral("Fit"), QStringLiteral("fit"), QStringLiteral("Everything in view"),
            [this] { if (view3dPane_) view3dPane_->canvas()->fit(); });
        const struct {
            const char* name;
            const char* text;
            const char* key;
        } views[] = {{"preview3dIso", "Iso", "iso"}, {"preview3dTop", "Top", "top"}, {"preview3dFront", "Front", "front"}, {"preview3dSide", "Side", "side"}};
        for (const auto& v : views)
            big(w, row, v.name, QString::fromLatin1(v.text), QString::fromLatin1(v.key), QStringLiteral("Looked at from there"),
                [this, k = QString::fromLatin1(v.key)] { if (view3dPane_) view3dPane_->canvas()->viewFrom(k); });
        big(w, row, "preview3dFullScreen", QStringLiteral("Full screen"), QStringLiteral("fullscreen"),
            QStringLiteral("The 3D view in a window of its own, filling the screen (F11; Esc back)"),
            [this] { if (view3dPane_) view3dPane_->setFullScreen(true); });
    });
    group(QStringLiteral("Environment"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "preview3dEnvLoad", QStringLiteral("Load"), QStringLiteral("load"),
            QStringLiteral("Load the CAD model the harness is routed in - a STEP, STL or OBJ file"),
            [this] { if (view3dPane_) view3dPane_->chooseEnvironment(); });
        big(w, row, "preview3dEnvClear", QStringLiteral("Clear"), QStringLiteral("clear"), QStringLiteral("Take the environment away"),
            [this] { if (view3dPane_) view3dPane_->clearEnvironment(); });
    });
    group(QStringLiteral("Route"), [&](QWidget* w, QHBoxLayout* row) {
        auto* tools = new QButtonGroup(w);
        tools->setExclusive(true);
        const struct {
            RouteTool tool;
            const char* name;
            const char* text;
            const char* key;
            const char* tip;
        } list[] = {
            {RouteTool::Select, "preview3dSelect", "Select", "select", "Pick a point on the model, or select a node or a bundle"},
            {RouteTool::Connector, "preview3dConnector", "Connector", "connector", "Put a connector - or the harness part chosen - on the face clicked"},
            {RouteTool::Branch, "preview3dBranch", "Branch", "branch", "Put a branch down - on a bundle, dividing it, or on a face"},
            {RouteTool::Clip, "preview3dClip", "Clip", "clip", "Put a clip down - it holds the harness where it is"},
            {RouteTool::Route, "preview3dRoute", "Route", "route", "Lay a bundle from node to node over the faces clicked"},
            {RouteTool::Move, "preview3dMove", "Move", "move", "Drag a node to somewhere else on the model"},
            {RouteTool::Stretch, "preview3dStretch", "Stretch", "stretch",
             "Pull a node - an end - to make the harness shorter or longer; clips and branches hold it"},
            {RouteTool::Delete, "preview3dDelete", "Delete", "delete", "Take away the node or bundle clicked"},
        };
        for (const auto& t : list) {
            QToolButton* b = big(w, row, t.name, QString::fromLatin1(t.text), QString::fromLatin1(t.key), QString::fromLatin1(t.tip),
                                 [this, tool = t.tool] { if (view3dPane_) view3dPane_->setRouteTool(tool); });
            b->setCheckable(true);
            tools->addButton(b, static_cast<int>(t.tool));
            preview3dTools_.append({static_cast<int>(t.tool), b});
        }
        if (QAbstractButton* first = tools->button(static_cast<int>(RouteTool::Select))) first->setChecked(true);
        big(w, row, "preview3dUndo", QStringLiteral("Undo"), QStringLiteral("undo"), QStringLiteral("Take back the last change to the route"),
            [this] { if (view3dPane_) view3dPane_->undoRoute(); });
    });
    group(QStringLiteral("Harness"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "preview3dLengths", QStringLiteral("Lengths to harness"), QStringLiteral("lengths"),
            QStringLiteral("Give each cable of the harness its length along the route"),
            [this] { if (view3dPane_) view3dPane_->applyLengths(true); });
        big(w, row, "preview3dRefresh", QStringLiteral("Refresh"), QStringLiteral("refresh"), QStringLiteral("Build the 3D harness again from the drawing"),
            [this] { if (view3dPane_) view3dPane_->rebuild(false); });
    });
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("3D Preview"));
}

void MainWindow::restylePreview3DIcons() {
    for (const auto& pair : preview3dIcons_)
        if (pair.second) pair.second->setIcon(icon3D(pair.first, Chrome::ink()));
}

void MainWindow::showPreview3D(bool on) {
    if (on == preview3dOn_ || !view3dPane_ || !centralStack_ || !preview3dHost_ || !view3dDock_) return;
    preview3dOn_ = on;
    if (on) {
        // OUT OF ITS PANEL INTO THE MIDDLE: the panel put away for as long as the tab is open, and given back after.
        if (view3dPane_->fullScreenView()) view3dPane_->setFullScreen(false);
        preview3dDockWas_ = view3dDock_->isVisible();
        if (!preview3dStandIn_) preview3dStandIn_ = new QWidget(view3dDock_);
        view3dDock_->setWidget(preview3dStandIn_);
        view3dDock_->hide();
        preview3dHost_->layout()->addWidget(view3dPane_);
        view3dPane_->show();
        centralStack_->setCurrentWidget(preview3dHost_);
        view3dPane_->rebuild(!preview3dShownOnce_);
        preview3dShownOnce_ = true;
        view3dPane_->canvas()->setFocus();
    } else {
        centralStack_->setCurrentIndex(0);
        preview3dHost_->layout()->removeWidget(view3dPane_);
        view3dDock_->setWidget(view3dPane_);
        view3dDock_->setVisible(preview3dDockWas_);
        canvas_->setFocus();
    }
}

} // namespace Edraw_Desktop
