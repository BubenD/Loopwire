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
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
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
        connect(b, &QToolButton::clicked, this, fn);   // fn takes nothing, or whether it is checked
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
        big(w, row, "preview3dFlip", QStringLiteral("Flip"), QStringLiteral("flip"),
            QStringLiteral("Look from straight opposite - the other side, underneath"),
            [this] { if (view3dPane_) view3dPane_->canvas()->flip(); });
        big(w, row, "preview3dFullScreen", QStringLiteral("Full screen"), QStringLiteral("fullscreen"),
            QStringLiteral("The 3D view in a window of its own, filling the screen (F11; Back to come back)"),
            [this] { if (view3dPane_) view3dPane_->setFullScreen(true); });
        QToolButton* lock = big(w, row, "preview3dLock", QStringLiteral("Lock view"), QStringLiteral("lock"),
                                QStringLiteral("Hold the view still - a drag moves a cable, a corner or a connector across it"),
                                [this](bool on) { if (view3dPane_) view3dPane_->setViewLocked(on); });
        lock->setCheckable(true);
        // ITS THREE TURNS, the wheel and the wireframe, small beside them.
        auto* g = new QWidget(w);
        auto* gl = new QGridLayout(g);
        gl->setContentsMargins(4, 2, 4, 2);
        gl->setHorizontalSpacing(4);
        gl->setVerticalSpacing(1);
        const char* turns[3][2] = {{"Turn", "view3dTurn"}, {"Tilt", "view3dTilt"}, {"Roll", "view3dRoll"}};
        for (int i = 0; i < 3; ++i) {
            gl->addWidget(new QLabel(QString::fromLatin1(turns[i][0]), g), i, 0);
            auto* s = new QSpinBox(g);
            s->setObjectName(QStringLiteral("preview3d%1").arg(QString::fromLatin1(turns[i][0])));
            s->setMinimumWidth(70);
            gl->addWidget(s, i, 1);
            preview3dMirrors_.append({s, QString::fromLatin1(turns[i][1])});
        }
        auto* wheel = new QCheckBox(QStringLiteral("Wheel"), g);
        wheel->setObjectName(QStringLiteral("preview3dWheel"));
        gl->addWidget(wheel, 0, 2);
        preview3dMirrors_.append({wheel, QStringLiteral("view3dWheelShown")});
        auto* wire = new QCheckBox(QStringLiteral("Wireframe"), g);
        wire->setObjectName(QStringLiteral("preview3dWireframe"));
        gl->addWidget(wire, 1, 2);
        preview3dMirrors_.append({wire, QStringLiteral("view3dWireframe")});
        row->addWidget(g);
    });
    group(QStringLiteral("Show"), [&](QWidget* w, QHBoxLayout* row) {
        auto* g = new QWidget(w);
        auto* gl = new QGridLayout(g);
        gl->setContentsMargins(4, 2, 4, 2);
        gl->setVerticalSpacing(2);
        auto* source = new QComboBox(g);
        source->setObjectName(QStringLiteral("preview3dSource"));
        gl->addWidget(source, 0, 0);
        preview3dMirrors_.append({source, QStringLiteral("view3dSource")});
        auto* look = new QComboBox(g);
        look->setObjectName(QStringLiteral("preview3dPartLook"));
        gl->addWidget(look, 1, 0);
        preview3dMirrors_.append({look, QStringLiteral("view3dPartLook")});
        row->addWidget(g);
        QToolButton* harness = big(w, row, "preview3dHarness", QStringLiteral("Harness"), QStringLiteral("harness"),
                                   QStringLiteral("Show or hide the harness drawn on the sheet"), [] {});
        harness->setCheckable(true);
        preview3dMirrors_.append({harness, QStringLiteral("view3dHarnessShown")});
        big(w, row, "preview3dParts", QStringLiteral("Parts"), QStringLiteral("parts"),
            QStringLiteral("The model's solids, each to show or hide"), [this] { if (view3dPane_) view3dPane_->showPartsList(); });
    });
    group(QStringLiteral("Environment"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "preview3dEnvLoad", QStringLiteral("Load"), QStringLiteral("load"),
            QStringLiteral("Load the CAD model the harness is routed in - a STEP, STL or OBJ file"),
            [this] { if (view3dPane_) view3dPane_->chooseEnvironment(); });
        big(w, row, "preview3dEnvClear", QStringLiteral("Clear"), QStringLiteral("clear"),
            QStringLiteral("Take the environment away (Undo brings it back)"),
            [this] { if (view3dPane_) view3dPane_->clearEnvironment(); });
        // SHOWN, HOW SOLID, AND CUT OPEN.
        auto* g = new QWidget(w);
        auto* gl = new QGridLayout(g);
        gl->setContentsMargins(4, 2, 4, 2);
        gl->setVerticalSpacing(2);
        auto* shown = new QCheckBox(QStringLiteral("Show"), g);
        shown->setObjectName(QStringLiteral("preview3dEnvShown"));
        gl->addWidget(shown, 0, 0);
        preview3dMirrors_.append({shown, QStringLiteral("view3dEnvShown")});
        auto* opacity = new QSlider(Qt::Horizontal, g);
        opacity->setObjectName(QStringLiteral("preview3dEnvOpacity"));
        opacity->setMinimumWidth(90);
        opacity->setToolTip(QStringLiteral("How solid the model is drawn"));
        gl->addWidget(opacity, 0, 1);
        preview3dMirrors_.append({opacity, QStringLiteral("view3dEnvOpacity")});
        auto* section = new QComboBox(g);
        section->setObjectName(QStringLiteral("preview3dSection"));
        gl->addWidget(section, 1, 0);
        preview3dMirrors_.append({section, QStringLiteral("view3dSection")});
        auto* sectionAt = new QSlider(Qt::Horizontal, g);
        sectionAt->setObjectName(QStringLiteral("preview3dSectionAt"));
        sectionAt->setMinimumWidth(90);
        sectionAt->setToolTip(QStringLiteral("Where the cut is"));
        gl->addWidget(sectionAt, 1, 1);
        preview3dMirrors_.append({sectionAt, QStringLiteral("view3dSectionAt")});
        row->addWidget(g);
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
            {RouteTool::Clip, "preview3dClip", "Clip", "clip", "Put a clip on the bundle where clicked - it holds it only at that point"},
            {RouteTool::Route, "preview3dRoute", "Route", "route", "Lay a bundle from node to node over the faces clicked"},
            {RouteTool::Move, "preview3dMove", "Move", "move", "Drag a node to somewhere else on the model"},
            {RouteTool::Stretch, "preview3dStretch", "Stretch", "stretch",
             "Pull a node - an end - to make the harness shorter or longer; clips and branches hold it"},
            {RouteTool::Delete, "preview3dDelete", "Delete", "delete", "Take away the node or bundle clicked"},
            {RouteTool::Measure, "preview3dMeasure", "Measure", "measure",
             "A ruler: one end, then the other - the distance straight, and along and across each axis"},
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
    // THE BUNDLES' SIZES, and where a click lands.
    group(QStringLiteral("Bundle"), [&](QWidget* w, QHBoxLayout* row) {
        auto* g = new QWidget(w);
        auto* gl = new QGridLayout(g);
        gl->setContentsMargins(4, 2, 4, 2);
        gl->setVerticalSpacing(1);
        const char* sizes[3][3] = {{"Ø", "preview3dDiameter", "view3dRouteDiameter"},
                                   {"Bend R", "preview3dBend", "view3dRouteBend"},
                                   {"Lead", "preview3dLead", "view3dRouteLead"}};
        for (int i = 0; i < 3; ++i) {
            gl->addWidget(new QLabel(QString::fromUtf8(sizes[i][0]), g), i, 0);
            auto* s = new QDoubleSpinBox(g);
            s->setObjectName(QLatin1String(sizes[i][1]));
            s->setMinimumWidth(84);
            gl->addWidget(s, i, 1);
            preview3dMirrors_.append({s, QString::fromLatin1(sizes[i][2])});
        }
        auto* grow = new QCheckBox(QStringLiteral("Grow length"), g);
        grow->setObjectName(QStringLiteral("preview3dGrow"));
        gl->addWidget(grow, 0, 2);
        preview3dMirrors_.append({grow, QStringLiteral("view3dGrowLength")});
        row->addWidget(g);
    });
    group(QStringLiteral("Clicks land on"), [&](QWidget* w, QHBoxLayout* row) {
        auto* g = new QWidget(w);
        auto* gl = new QGridLayout(g);
        gl->setContentsMargins(4, 2, 4, 2);
        gl->setVerticalSpacing(2);
        auto* plane = new QComboBox(g);
        plane->setObjectName(QStringLiteral("preview3dWorkPlane"));
        gl->addWidget(plane, 0, 0, 1, 2);
        preview3dMirrors_.append({plane, QStringLiteral("view3dWorkPlane")});
        gl->addWidget(new QLabel(QStringLiteral("at"), g), 1, 0);
        auto* at = new QDoubleSpinBox(g);
        at->setObjectName(QStringLiteral("preview3dWorkPlaneAt"));
        gl->addWidget(at, 1, 1);
        preview3dMirrors_.append({at, QStringLiteral("view3dWorkPlaneAt")});
        row->addWidget(g);
        big(w, row, "preview3dWorkPlaneHere", QStringLiteral("Through the point"), QStringLiteral("workplane"),
            QStringLiteral("The plane put through the node selected, or the point last picked"), [this] {
                if (!view3dPane_) return;
                if (auto* b = view3dPane_->findChild<QToolButton*>(QStringLiteral("view3dWorkPlaneHere"))) b->click();
            });
    });
    group(QStringLiteral("Harness"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "preview3dLengths", QStringLiteral("Lengths to harness"), QStringLiteral("lengths"),
            QStringLiteral("Give each cable of the harness its length along the route"),
            [this] { if (view3dPane_) view3dPane_->applyLengths(true); });
        big(w, row, "preview3dRefresh", QStringLiteral("Refresh"), QStringLiteral("refresh"), QStringLiteral("Build the 3D harness again from the drawing"),
            [this] { if (view3dPane_) view3dPane_->rebuild(false); });
        big(w, row, "preview3dHelp", QStringLiteral("Help"), QStringLiteral("help"),
            QStringLiteral("How to work in the 3D view - every command, with recordings"), [this] { if (view3dPane_) view3dPane_->showHelp(); });
    });
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("3D Preview"));
}

void MainWindow::restylePreview3DIcons() {
    for (const auto& pair : preview3dIcons_)
        if (pair.second) pair.second->setIcon(icon3D(pair.first, Chrome::ink()));
}

void MainWindow::mirrorPreview3D() {
    // EACH ON THE RIBBON THE 3D VIEW'S OWN, in step both ways: what it can be set to, and what it is set to.
    if (preview3dMirrored_ || !view3dPane_) return;
    preview3dMirrored_ = true;
    for (const auto& [ribbonW, name] : preview3dMirrors_) {
        QWidget* paneW = view3dPane_->findChild<QWidget*>(name);
        if (!ribbonW || !paneW) continue;
        QWidget* rw = ribbonW;
        if (auto* rb = qobject_cast<QAbstractButton*>(rw)) {
            auto* p = qobject_cast<QAbstractButton*>(paneW);
            if (!p) continue;
            rb->setChecked(p->isChecked());
            connect(rb, &QAbstractButton::toggled, p, [p](bool on) { if (p->isChecked() != on) p->setChecked(on); });
            connect(p, &QAbstractButton::toggled, rb, [rb](bool on) {
                const QSignalBlocker hold(rb);
                rb->setChecked(on);
            });
        } else if (auto* rd = qobject_cast<QDoubleSpinBox*>(rw)) {
            auto* p = qobject_cast<QDoubleSpinBox*>(paneW);
            if (!p) continue;
            rd->setRange(p->minimum(), p->maximum());
            rd->setDecimals(p->decimals());
            rd->setSuffix(p->suffix());
            rd->setSingleStep(p->singleStep());
            rd->setSpecialValueText(p->specialValueText());
            rd->setKeyboardTracking(false);
            rd->setValue(p->value());
            connect(rd, &QDoubleSpinBox::valueChanged, p, [p](double v) { p->setValue(v); });
            connect(p, &QDoubleSpinBox::valueChanged, rd, [rd](double v) {
                const QSignalBlocker hold(rd);
                rd->setValue(v);
            });
        } else if (auto* ri = qobject_cast<QSpinBox*>(rw)) {
            auto* p = qobject_cast<QSpinBox*>(paneW);
            if (!p) continue;
            ri->setRange(p->minimum(), p->maximum());
            ri->setWrapping(p->wrapping());
            ri->setSuffix(p->suffix());
            ri->setSingleStep(p->singleStep());
            ri->setKeyboardTracking(false);
            ri->setValue(p->value());
            connect(ri, &QSpinBox::valueChanged, p, [p](int v) { p->setValue(v); });
            connect(p, &QSpinBox::valueChanged, ri, [ri](int v) {
                const QSignalBlocker hold(ri);
                ri->setValue(v);
            });
        } else if (auto* rc = qobject_cast<QComboBox*>(rw)) {
            auto* p = qobject_cast<QComboBox*>(paneW);
            if (!p) continue;
            for (int i = 0; i < p->count(); ++i) rc->addItem(p->itemText(i));
            rc->setCurrentIndex(p->currentIndex());
            connect(rc, &QComboBox::activated, p, [p](int i) {
                p->setCurrentIndex(i);
                emit p->activated(i);
            });
            connect(p, &QComboBox::currentIndexChanged, rc, [rc](int i) {
                const QSignalBlocker hold(rc);
                rc->setCurrentIndex(i);
            });
        } else if (auto* r = qobject_cast<QSlider*>(rw)) {
            auto* p = qobject_cast<QSlider*>(paneW);
            if (!p) continue;
            r->setRange(p->minimum(), p->maximum());
            r->setValue(p->value());
            connect(r, &QSlider::valueChanged, p, [p](int v) { p->setValue(v); });
            connect(p, &QSlider::valueChanged, r, [r](int v) {
                const QSignalBlocker hold(r);
                r->setValue(v);
            });
        }
    }
    // The view locked from wherever: the ribbon's button says so.
    if (auto* lock = view3dTabPage_ ? view3dTabPage_->findChild<QToolButton*>(QStringLiteral("preview3dLock")) : nullptr)
        connect(view3dPane_, &View3DPane::viewLockChanged, lock, [lock](bool on) {
            const QSignalBlocker hold(lock);
            lock->setChecked(on);
        });
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
        // ALL ITS OPTIONS ON THE RIBBON: the pane's own rows put away while the tab is open.
        mirrorPreview3D();
        view3dPane_->setRowsShown(false);
        view3dPane_->show();
        centralStack_->setCurrentWidget(preview3dHost_);
        view3dPane_->rebuild(!preview3dShownOnce_);
        preview3dShownOnce_ = true;
        view3dPane_->canvas()->setFocus();
    } else {
        centralStack_->setCurrentIndex(0);
        view3dPane_->setRowsShown(true);
        preview3dHost_->layout()->removeWidget(view3dPane_);
        view3dDock_->setWidget(view3dPane_);
        view3dDock_->setVisible(preview3dDockWas_);
        canvas_->setFocus();
    }
}

} // namespace Edraw_Desktop
