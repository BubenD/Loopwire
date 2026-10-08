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
#include <QGridLayout>
#include <QActionGroup>
#include <QMenu>
#include <QEvent>
#include <QFrame>
#include <QHash>
#include <QTimer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace Edraw_Desktop {

void MainWindow::buildView3DTab() {
    auto [page, lay] = makeTabPage();
    view3dTabPage_ = page;
    // EACH GROUP IN A SLOT that can hold it open, or fold it into one button - its picture, its name and an arrow - that
    // opens the group in a popup under it, as a CAD program's ribbon does where the window is narrow.
    const QHash<QString, QPair<QString, int>> foldOf{
        {QStringLiteral("View"), {QStringLiteral("iso"), 5}},          {QStringLiteral("Route"), {QStringLiteral("route"), 6}},
        {QStringLiteral("Show"), {QStringLiteral("harness"), 4}},      {QStringLiteral("Environment"), {QStringLiteral("load"), 3}},
        {QStringLiteral("Bundle"), {QStringLiteral("route"), 2}},      {QStringLiteral("Route to land"), {QStringLiteral("workplane"), 7}},
        {QStringLiteral("Tension"), {QStringLiteral("gravity"), 6}},  {QStringLiteral("Drawing"), {QStringLiteral("lengths"), 0}},
        {QStringLiteral("Keep-out"), {QStringLiteral("keepout"), 4}}};
    auto group = [&](const QString& title, auto fill) {
        auto* slot = new QWidget(page);
        auto* sl = new QHBoxLayout(slot);
        sl->setContentsMargins(0, 0, 0, 0);
        sl->setSpacing(0);
        auto* w = new QWidget(slot);
        auto* row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(1);
        fill(w, row);
        sl->addWidget(w);
        auto* fold = new QToolButton(slot);
        fold->setObjectName(QStringLiteral("preview3dFold") + QString(title).remove(QLatin1Char(' ')));
        fold->setText(title + QStringLiteral(" ▾"));
        fold->setToolTip(QStringLiteral("%1 - folded where the window is narrow: click for all of it").arg(title));
        const QPair<QString, int> how = foldOf.value(title, {QStringLiteral("fit"), 0});
        fold->setIcon(icon3D(how.first, Chrome::ink()));
        fold->setIconSize(QSize(26, 26));
        fold->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        fold->setAutoRaise(true);
        fold->setFocusPolicy(Qt::NoFocus);
        fold->hide();
        sl->addWidget(fold);
        preview3dIcons_.append({how.first, fold});
        auto* popup = new QFrame(slot, Qt::Popup);
        popup->setObjectName(QStringLiteral("preview3dFoldPopup"));
        popup->setFrameShape(QFrame::StyledPanel);
        auto* pl = new QHBoxLayout(popup);
        pl->setContentsMargins(6, 4, 6, 4);
        connect(fold, &QToolButton::clicked, this, [fold, popup] {
            popup->adjustSize();
            popup->move(fold->mapToGlobal(QPoint(0, fold->height())));
            popup->show();
        });
        Preview3DFold f;
        f.title = title;
        f.slot = slot;
        f.content = w;
        f.fold = fold;
        f.popup = popup;
        f.order = how.second;
        preview3dFolds_.append(f);
        addGroup(lay, title, slot);
    };
    // A FIELD'S NAME, in the scheme's own ink - readable on the ribbon, light or dark.
    auto fieldLabel = [](const QString& text, QWidget* parent) {
        auto* l = new QLabel(text, parent);
        l->setStyleSheet(QStringLiteral("color:%1;").arg(Chrome::ink().name()));
        return l;
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
        } views[] = {{"preview3dIso", "Iso", "iso"},       {"preview3dTop", "Top", "top"},    {"preview3dBottom", "Bottom", "bottom"},
                     {"preview3dFront", "Front", "front"}, {"preview3dBack", "Back", "back"}, {"preview3dLeft", "Left", "left"},
                     {"preview3dSide", "Right", "side"}};
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
            gl->addWidget(fieldLabel(QString::fromLatin1(turns[i][0]), g), i, 0);
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
            {RouteTool::Connector, "preview3dConnector", "Connector", "connector",
             "Put a connector - or the harness part chosen - on the face clicked; its arrow: the form it is put in. "
             "Tab: freeze it where it would go, to set it on the right first"},
            {RouteTool::Route, "preview3dRoute", "Route", "route", "Lay a bundle from node to node over the faces clicked"},
            {RouteTool::Measure, "preview3dMeasure", "Measure", "measure",
             "A ruler: one end, then the other - its arrow: point to point, or surface to surface"},
            {RouteTool::Branch, "preview3dBranch", "Branch", "branch", "Put a branch down - on a bundle, dividing it, or on a face"},
            {RouteTool::Clip, "preview3dClip", "Clip", "clip", "Put a clip on the bundle where clicked - it holds it only at that point"},
            {RouteTool::Move, "preview3dMove", "Move", "move", "Drag a node - or a stretch of a bundle between two nodes - to somewhere else"},
            {RouteTool::Stretch, "preview3dStretch", "Stretch", "stretch",
             "Pull a connector: it stays, its cable pulled tight through it and the rest cut off"},
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
        big(w, row, "preview3dAttach", QStringLiteral("Attach"), QStringLiteral("attach"),
            QStringLiteral("Attach to surface: lay the cable selected (every cable, where none is) on the faces under it, the way "
                           "gravity pulls - onto a wall beside it where nothing is under it - and hold it there"),
            [this] {
                if (!view3dPane_) return;
                if (auto* b = view3dPane_->findChild<QToolButton*>(QStringLiteral("view3dAttach"))) b->click();
            });
        // THEIR ARROWS: the form a connector is put in, and how the ruler measures.
        auto popup = [&](const char* name, const QList<QPair<QString, std::function<void()>>>& choices, int on) {
            auto* b = page->findChild<QToolButton*>(QLatin1String(name));
            if (!b) return;
            auto* menu = new QMenu(b);
            auto* group = new QActionGroup(menu);
            for (int i = 0; i < choices.size(); ++i) {
                QAction* a = menu->addAction(choices[i].first);
                a->setCheckable(true);
                a->setChecked(i == on);
                group->addAction(a);
                connect(a, &QAction::triggered, this, [fn = choices[i].second, b] {
                    fn();
                    b->click();
                    b->setChecked(true);
                });
            }
            b->setMenu(menu);
            b->setPopupMode(QToolButton::MenuButtonPopup);
        };
        QList<QPair<QString, std::function<void()>>> forms;
        for (const auto& [text, key] : {std::pair<const char*, const char*>{"Rectangular", "rect"}, {"Cylindrical", "round"}, {"T (flange and body)", "tee"},
                                        {"Right angle (L)", "angle"}, {"D-sub shell", "dsub"}})
            forms.append({QString::fromLatin1(text), [this, k = QString::fromLatin1(key)] { if (view3dPane_) view3dPane_->setPlaceForm(k); }});
        popup("preview3dConnector", forms, 0);
        popup("preview3dMeasure",
              {{QStringLiteral("Point to point"), [this] { if (view3dPane_) view3dPane_->setMeasureMode(0); }},
               {QStringLiteral("Surface to surface"), [this] { if (view3dPane_) view3dPane_->setMeasureMode(1); }}},
              0);
    });
    group(QStringLiteral("Route to land"), [&](QWidget* w, QHBoxLayout* row) {
        // ROUTE TO LAND: where the points of a route land - the model's faces, or a plane - chosen from its menu, as in the
        // 3D view.
        QToolButton* land = big(w, row, "preview3dRouteToLand", QStringLiteral("Route to land"), QStringLiteral("workplane"),
                                QStringLiteral("Where the points of a route land: on the model's faces, or on a plane across X, "
                                               "Y or Z (in the air if need be)"),
                                [] {});
        land->setPopupMode(QToolButton::InstantPopup);
        auto* landMenu = new QMenu(land);
        landMenu->setObjectName(QStringLiteral("preview3dRouteToLandMenu"));
        land->setMenu(landMenu);
        connect(landMenu, &QMenu::aboutToShow, this, [this, landMenu] {
            landMenu->clear();
            auto* box = view3dPane_ ? view3dPane_->findChild<QComboBox*>(QStringLiteral("view3dWorkPlane")) : nullptr;
            if (!box) return;
            for (int i = 0; i < box->count(); ++i) {
                QAction* a = landMenu->addAction(box->itemText(i));
                a->setCheckable(true);
                a->setChecked(box->currentIndex() == i);
                connect(a, &QAction::triggered, box, [box, i] { box->setCurrentIndex(i); });
            }
        });
        auto* g = new QWidget(w);
        auto* gl = new QGridLayout(g);
        gl->setContentsMargins(4, 2, 4, 2);
        gl->setVerticalSpacing(2);
        auto* plane = new QComboBox(g);
        plane->setObjectName(QStringLiteral("preview3dWorkPlane"));
        gl->addWidget(plane, 0, 0, 1, 2);
        preview3dMirrors_.append({plane, QStringLiteral("view3dWorkPlane")});
        gl->addWidget(fieldLabel(QStringLiteral("at"), g), 1, 0);
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
        // NO WIDER THAN THIS, so the group stays open on a narrower window - the whole name in its list.
        source->setMaximumWidth(160);
        look->setMaximumWidth(160);
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
        // EACH SLIDER NAMED: how solid the model is, where the cut is.
        auto* opacityName = fieldLabel(QStringLiteral("Opacity"), g);
        opacityName->setObjectName(QStringLiteral("preview3dEnvOpacityLabel"));
        gl->addWidget(opacityName, 0, 1);
        auto* opacity = new QSlider(Qt::Horizontal, g);
        opacity->setObjectName(QStringLiteral("preview3dEnvOpacity"));
        opacity->setMinimumWidth(90);
        opacity->setToolTip(QStringLiteral("How solid the model is drawn (a solid's own: right-click it, Opacity)"));
        gl->addWidget(opacity, 0, 2);
        preview3dMirrors_.append({opacity, QStringLiteral("view3dEnvOpacity")});
        auto* section = new QComboBox(g);
        section->setObjectName(QStringLiteral("preview3dSection"));
        gl->addWidget(section, 1, 0);
        preview3dMirrors_.append({section, QStringLiteral("view3dSection")});
        auto* sectionAt = new QSlider(Qt::Horizontal, g);
        sectionAt->setObjectName(QStringLiteral("preview3dSectionAt"));
        sectionAt->setMinimumWidth(90);
        sectionAt->setToolTip(QStringLiteral("Where the cut is"));
        gl->addWidget(fieldLabel(QStringLiteral("Cut at"), g), 1, 1);
        gl->addWidget(sectionAt, 1, 2);
        preview3dMirrors_.append({sectionAt, QStringLiteral("view3dSectionAt")});
        row->addWidget(g);
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
            gl->addWidget(fieldLabel(QString::fromUtf8(sizes[i][0]), g), i, 0);
            auto* s = new QDoubleSpinBox(g);
            s->setObjectName(QLatin1String(sizes[i][1]));
            s->setMinimumWidth(84);
            gl->addWidget(s, i, 1);
            preview3dMirrors_.append({s, QString::fromLatin1(sizes[i][2])});
        }
        // Ø's "A": a bundle of wires, as thick as they pack to.
        auto* diaAuto = new QToolButton(g);
        diaAuto->setObjectName(QStringLiteral("preview3dDiameterAuto"));
        diaAuto->setText(QStringLiteral("A"));
        diaAuto->setCheckable(true);
        diaAuto->setFixedWidth(22);
        diaAuto->setToolTip(QStringLiteral("Auto: a bundle of wires - as thick as its wires pack to"));
        diaAuto->setStyleSheet(QStringLiteral("QToolButton{font-weight:bold;color:#e6ecf4;background:#3a4656;border:1px solid #5a6878;"
                                              "border-radius:3px;}QToolButton:checked{background:#2f855a;border-color:#3fa36f;color:white;}"));
        gl->addWidget(diaAuto, 0, 2);
        preview3dMirrors_.append({diaAuto, QStringLiteral("view3dRouteDiameterAuto")});
        // A BUNDLE OF WIRES: how many, how thick each.
        gl->addWidget(fieldLabel(QStringLiteral("Wires"), g), 0, 3);
        auto* wires = new QSpinBox(g);
        wires->setObjectName(QStringLiteral("preview3dWires"));
        wires->setToolTip(QStringLiteral("A bundle of so many wires, drawn wire by wire (0: a plain bundle)"));
        gl->addWidget(wires, 0, 4);
        preview3dMirrors_.append({wires, QStringLiteral("view3dRouteWires")});
        gl->addWidget(fieldLabel(QString::fromUtf8("Wire Ø"), g), 1, 3);
        auto* wireD = new QDoubleSpinBox(g);
        wireD->setObjectName(QStringLiteral("preview3dWireD"));
        wireD->setToolTip(QStringLiteral("Each wire's diameter"));
        gl->addWidget(wireD, 1, 4);
        preview3dMirrors_.append({wireD, QStringLiteral("view3dRouteWireD")});
        row->addWidget(g);
    });
    // HOW TAUT THE CABLES ARE: the route's, the cable selected's own - and gravity shown on them.
    group(QStringLiteral("Tension"), [&](QWidget* w, QHBoxLayout* row) {
        auto* g = new QWidget(w);
        auto* gl = new QGridLayout(g);
        gl->setContentsMargins(4, 2, 4, 2);
        gl->setVerticalSpacing(1);
        gl->addWidget(fieldLabel(QStringLiteral("Cable tension"), g), 0, 0);
        auto* tension = new QComboBox(g);
        tension->setObjectName(QStringLiteral("preview3dTension"));
        tension->setToolTip(QStringLiteral("How taut the cables are put in, where one gives none of its own"));
        gl->addWidget(tension, 0, 1);
        preview3dMirrors_.append({tension, QStringLiteral("view3dTension")});
        gl->addWidget(fieldLabel(QStringLiteral("Down"), g), 2, 0);
        auto* down = new QComboBox(g);
        down->setObjectName(QStringLiteral("preview3dGravityDown"));
        gl->addWidget(down, 2, 1);
        preview3dMirrors_.append({down, QStringLiteral("view3dGravityDown")});
        row->addWidget(g);
        QToolButton* gravity = big(w, row, "preview3dGravity", QStringLiteral("Gravity"), QStringLiteral("gravity"),
                                   QStringLiteral("Simulation preview: each cable hung under its weight by its tension, "
                                                  "and where it would fall shown"),
                                   [] {});
        gravity->setCheckable(true);
        preview3dMirrors_.append({gravity, QStringLiteral("view3dGravity")});
    });
    // KEEP-OUTS: a level across X, Y or Z (from the arrow) or a zone added, all of them listed to be set, and shown or not.
    group(QStringLiteral("Keep-out"), [&](QWidget* w, QHBoxLayout* row) {
        QToolButton* level = big(w, row, "preview3dAddLevel", QStringLiteral("Add level"), QStringLiteral("keepout"),
                                 QStringLiteral("A level no cable may go through - a plane across X, Y or Z, at the middle of the "
                                                "model (set where in Keep-outs)"),
                                 [this] { if (view3dPane_) view3dPane_->addKeepOutLevel(2); });
        level->setPopupMode(QToolButton::MenuButtonPopup);
        auto* levels = new QMenu(level);
        levels->setObjectName(QStringLiteral("preview3dAddLevelMenu"));
        for (int axis = 0; axis < 3; ++axis)
            levels->addAction(QStringLiteral("A level across %1").arg(QLatin1Char("XYZ"[axis])), this,
                              [this, axis] { if (view3dPane_) view3dPane_->addKeepOutLevel(axis); });
        level->setMenu(levels);
        big(w, row, "preview3dAddZone", QStringLiteral("Add zone"), QStringLiteral("keepzone"),
            QStringLiteral("A zone no cable may go through - a box in the middle of the model (set its corners in Keep-outs)"),
            [this] { if (view3dPane_) view3dPane_->addKeepOutZone(); });
        big(w, row, "preview3dKeepOuts", QStringLiteral("Keep-outs"), QStringLiteral("parts"),
            QStringLiteral("Every keep-out: where it is, and whether it blocks a cable or alerts"),
            [this] { if (view3dPane_) view3dPane_->showKeepOuts(); });
        QToolButton* shown = big(w, row, "preview3dKeepOutsShown", QStringLiteral("Show"), QStringLiteral("harness"),
                                 QStringLiteral("Show or hide the keep-outs"), [] {});
        shown->setCheckable(true);
        preview3dMirrors_.append({shown, QStringLiteral("view3dKeepOutsShown")});
    });
    // TO AND FROM THE DRAWING: the route's lengths given to the harness drawn, and the 3D harness built again from it.
    group(QStringLiteral("Drawing"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "preview3dFromHarness", QStringLiteral("Route from harness"), QStringLiteral("route"),
            QStringLiteral("Put the harness in as a route - its parts where it is drawn in 3D and its cables between them - to "
                           "move into the model"),
            [this] {
                if (!view3dPane_) return;
                if (auto* b = view3dPane_->findChild<QToolButton*>(QStringLiteral("view3dRouteFromHarness"))) b->click();
            });
        big(w, row, "preview3dLengths", QStringLiteral("Lengths to harness"), QStringLiteral("lengths"),
            QStringLiteral("Give each cable of the harness drawn its length along the route"),
            [this] { if (view3dPane_) view3dPane_->applyLengths(true); });
        big(w, row, "preview3dRefresh", QStringLiteral("Refresh"), QStringLiteral("refresh"), QStringLiteral("Build the 3D harness again from the drawing"),
            [this] { if (view3dPane_) view3dPane_->rebuild(false); });
    });
    // HELP, never folded away: every command explained, with recordings of it being done.
    {
        auto* w = new QWidget(page);
        auto* row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);
        big(w, row, "preview3dHelp", QStringLiteral("Help"), QStringLiteral("help"),
            QStringLiteral("How to work in the 3D view - every command explained, with recordings and examples of it being done"),
            [this] { if (view3dPane_) view3dPane_->showHelp(); });
        addGroup(lay, QStringLiteral("Help"), w);
    }
    // THE DISPLAY: the background behind the picture and how it is shaded - as on the wheel's options.
    if (auto* fit = page->findChild<QToolButton*>(QStringLiteral("preview3dFit"))) {
        auto* row = qobject_cast<QHBoxLayout*>(fit->parentWidget()->layout());
        auto* display = new QToolButton(fit->parentWidget());
        display->setObjectName(QStringLiteral("preview3dDisplay"));
        display->setText(QStringLiteral("Display"));
        display->setToolTip(QStringLiteral("The background behind the picture - dark, light, a gradient, a colour - and how it is shaded"));
        display->setIcon(icon3D(QStringLiteral("display"), Chrome::ink()));
        display->setAutoRaise(true);
        display->setFocusPolicy(Qt::NoFocus);
        display->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(display);
        connect(menu, &QMenu::aboutToShow, this, [this, menu] {
            menu->clear();
            if (view3dPane_) view3dPane_->fillDisplayMenu(menu);
        });
        display->setMenu(menu);
        preview3dIcons_.append({QStringLiteral("display"), display});
        if (row) row->insertWidget(row->indexOf(fit) + 1, display);
    }
    // ADD LENGTH: the bundle selected made longer by so much.
    if (auto* loose = page->findChild<QToolButton*>(QStringLiteral("preview3dAddLoose"))) {
        auto* row = qobject_cast<QHBoxLayout*>(loose->parentWidget()->layout());
        auto* more = new QToolButton(loose->parentWidget());
        more->setObjectName(QStringLiteral("preview3dAddLength"));
        more->setText(QStringLiteral("Add length"));
        more->setToolTip(QStringLiteral("The bundle selected made longer by so much - laid looser, its ends where they are"));
        more->setIcon(icon3D(QStringLiteral("loose"), Chrome::ink()));
        more->setAutoRaise(true);
        more->setFocusPolicy(Qt::NoFocus);
        connect(more, &QToolButton::clicked, this, [this] { if (view3dPane_) view3dPane_->addLengthToSelected(); });
        preview3dIcons_.append({QStringLiteral("loose"), more});
        if (row) row->insertWidget(row->indexOf(loose) + 1, more);
    }
    // COMPACT, AS A 3D PROGRAM'S RIBBON IS: the commands used most large (Fit, Select, Connector, Route, Load, Lengths),
    // the rest small - their picture with their name beside it - three to a column, so the tab takes half the room.
    auto stack = [&](const QStringList& names) {
        QList<QToolButton*> buttons;
        for (const QString& n : names)
            if (auto* b = page->findChild<QToolButton*>(n)) buttons << b;
        if (buttons.isEmpty()) return;
        QWidget* holder = buttons.first()->parentWidget();
        auto* row = holder ? qobject_cast<QHBoxLayout*>(holder->layout()) : nullptr;
        if (!row) return;
        const int at = row->indexOf(buttons.first());
        auto* column = new QWidget(holder);
        auto* gl = new QGridLayout(column);
        gl->setContentsMargins(2, 0, 2, 0);
        gl->setHorizontalSpacing(2);
        gl->setVerticalSpacing(0);
        for (int i = 0; i < buttons.size(); ++i) {
            QToolButton* b = buttons[i];
            row->removeWidget(b);
            b->setParent(column);
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setIconSize(QSize(18, 18));
            gl->addWidget(b, i % 3, i / 3);
        }
        row->insertWidget(std::max(0, at), column);
    };
    stack({QStringLiteral("preview3dDisplay")});
    stack({QStringLiteral("preview3dIso"), QStringLiteral("preview3dTop"), QStringLiteral("preview3dBottom"), QStringLiteral("preview3dFront"),
           QStringLiteral("preview3dBack"), QStringLiteral("preview3dLeft"), QStringLiteral("preview3dSide"), QStringLiteral("preview3dFlip"),
           QStringLiteral("preview3dFullScreen"), QStringLiteral("preview3dLock")});
    stack({QStringLiteral("preview3dHarness"), QStringLiteral("preview3dParts")});
    stack({QStringLiteral("preview3dEnvClear")});
    stack({QStringLiteral("preview3dBranch"), QStringLiteral("preview3dClip"), QStringLiteral("preview3dMove"), QStringLiteral("preview3dStretch"),
           QStringLiteral("preview3dDelete"), QStringLiteral("preview3dAttach")});
    stack({QStringLiteral("preview3dAddLoose"), QStringLiteral("preview3dAddLength")});
    stack({QStringLiteral("preview3dWorkPlaneHere")});
    stack({QStringLiteral("preview3dRefresh")});
    for (QToolButton* b : page->findChildren<QToolButton*>())
        if (b->toolButtonStyle() == Qt::ToolButtonTextUnderIcon) b->setIconSize(QSize(26, 26));
    lay->addStretch(1);
    // NEVER SQUEEZED: every button its picture and its whole name, every box as wide as what is in it - the page as
    // wide as all of that wants, and scrolled across (a thin bar under it) where the window is narrower.
    auto* scroll = new QScrollArea(ribbon_);
    scroll->setObjectName(QStringLiteral("preview3dScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->horizontalScrollBar()->setStyleSheet(QStringLiteral(
        "QScrollBar:horizontal{height:8px;background:transparent;border:none;}"
        "QScrollBar::handle:horizontal{background:#6b7480;border-radius:3px;min-width:40px;}"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"
        "QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:transparent;}"));
    scroll->setWidget(page);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setStyleSheet(QStringLiteral("QScrollArea#preview3dScroll{background:transparent;}"));
    view3dTabPage_ = scroll;
    ribbon_->addTab(scroll, QStringLiteral("3D Preview"));
    // FITTED TO THE WIDTH whenever it changes: groups folded or opened again.
    class Watcher : public QObject {
    public:
        Watcher(QObject* parent, std::function<void()> fn) : QObject(parent), fn_(std::move(fn)) {}
        bool eventFilter(QObject*, QEvent* e) override {
            if (e->type() == QEvent::Resize) fn_();
            return false;
        }

    private:
        std::function<void()> fn_;
    };
    scroll->viewport()->installEventFilter(new Watcher(scroll, [this] { QTimer::singleShot(0, this, [this] { fitPreview3DRibbon(); }); }));
    fitPreview3DRibbon();
}

void MainWindow::fitPreview3DRibbon() {
    auto* scroll = qobject_cast<QScrollArea*>(view3dTabPage_);
    QWidget* page = scroll ? scroll->widget() : nullptr;
    if (!page || preview3dFolding_) return;
    preview3dFolding_ = true;
    auto setFolded = [](Preview3DFold& f, bool on) {
        if (!f.slot || !f.content || !f.fold || !f.popup || f.folded == on) return;
        f.folded = on;
        if (on) {
            f.popup->layout()->addWidget(f.content);
            f.content->show();
            f.fold->show();
        } else {
            f.popup->hide();
            qobject_cast<QHBoxLayout*>(f.slot->layout())->insertWidget(0, f.content);
            f.content->show();
            f.fold->hide();
        }
    };
    auto wants = [page] {
        page->setMinimumWidth(0);
        page->layout()->invalidate();
        page->layout()->activate();
        return page->sizeHint().width();
    };
    // ALL OPEN, then folded one by one - the least used first - until the tab fits the width there is.
    for (Preview3DFold& f : preview3dFolds_) setFolded(f, false);
    QList<int> order;
    for (int i = 0; i < preview3dFolds_.size(); ++i) order << i;
    std::sort(order.begin(), order.end(), [this](int a, int b) { return preview3dFolds_[a].order < preview3dFolds_[b].order; });
    const int room = scroll->viewport()->width();
    for (int i : order) {
        if (wants() <= room) break;
        setFolded(preview3dFolds_[i], true);
    }
    // ROOM LEFT: a small group folded on the way opened again where it still fits - the most used first.
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        Preview3DFold& f = preview3dFolds_[*it];
        if (!f.folded) continue;
        setFolded(f, false);
        if (wants() > room) setFolded(f, true);
    }
    // Never squeezed past that: scrolled across where even folded it is wider than the window.
    page->setMinimumWidth(wants());
    preview3dFolding_ = false;
}

void MainWindow::placeView3DDock(Qt::DockWidgetArea area) {
    // AT THE SIDE: the picture alone. BELOW (or floating): everything, and - having come from a side - a way back there.
    if (!view3dPane_ || !view3dDock_ || preview3dOn_) return;
    const bool side = (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) && !view3dDock_->isFloating();
    if (side) view3dSideArea_ = area;
    view3dPane_->setCompact(side);
    view3dPane_->setSideDocked(!side && view3dSideArea_ != Qt::NoDockWidgetArea);
    view3dPane_->setDockedBelow(!side);
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
        view3dPane_->setCompact(false);
        view3dPane_->setSideDocked(false);
        view3dPane_->setDockedBelow(false);
        preview3dDockWas_ = view3dDock_->isVisible();
        preview3dLayout_ = saveState();
        // THE PANELS ROUND THE DRAWING PUT AWAY: the 3D view has its own on either side - what is in view on the left,
        // the route on the right. Given back as they were when the tab is left.
        preview3dDocksHidden_.clear();
        for (QDockWidget* d : findChildren<QDockWidget*>())
            if (d != view3dDock_ && d->isVisible() && !d->isFloating()) {
                preview3dDocksHidden_.append(d);
                d->hide();
            }
        if (!preview3dStandIn_) preview3dStandIn_ = new QWidget(view3dDock_);
        view3dDock_->setWidget(preview3dStandIn_);
        view3dDock_->hide();
        preview3dHost_->layout()->addWidget(view3dPane_);
        // ALL ITS OPTIONS ON THE RIBBON: the pane's own rows put away while the tab is open.
        mirrorPreview3D();
        // ITS BOXES AS WIDE AS WHAT IS NOW IN THEM - filled from the 3D view just now - and the page as wide as all that.
        if (auto* scroll = qobject_cast<QScrollArea*>(view3dTabPage_))
            if (QWidget* page = scroll->widget()) {
                for (QComboBox* c : page->findChildren<QComboBox*>()) c->setSizeAdjustPolicy(QComboBox::AdjustToContents);
                fitPreview3DRibbon();
            }
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
        placeView3DDock(dockWidgetArea(view3dDock_));
        for (const QPointer<QDockWidget>& d : preview3dDocksHidden_)
            if (d) d->show();
        preview3dDocksHidden_.clear();
        // THE PANELS AS THEY WERE: each where it was, as wide, the same tab of a group in front.
        if (!preview3dLayout_.isEmpty()) {
            restoreState(preview3dLayout_);
            view3dDock_->setVisible(preview3dDockWas_);
            preview3dLayout_.clear();
        }
        canvas_->setFocus();
    }
}

} // namespace Edraw_Desktop
