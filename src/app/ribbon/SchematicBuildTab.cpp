// The Schematic build ribbon tab. Split out of MainWindow.cpp so each tab is one file: the ribbon was the
// largest thing in that file and the place every new feature landed. Still MainWindow members, so
// nothing about what they build or reach has changed.
#include "app/MainWindow.h"

#include "app/BlockDialog.h"
#include "app/ribbon/RibbonIcons.h"
#include "app/Editor.h"
#include "harness/Harness.h"
#include "render/CanvasWidget.h"
#include "tools/QuickSelect.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>

namespace Edraw_Desktop {

// ---- SCHEMATIC BUILD: placement tools; the "Connector build" groups live in a
//      contextual tab that opens only when a connector is being placed / edited. ----
void MainWindow::buildSchematicBuildTab() {
    auto [page, lay] = makeTabPage();
    // Undo and Redo on this tab too: a harness is built here, and coming back to Home to undo one
    // wire is the sort of thing that makes a person stop using the tab.
    // Screen / shield button (joins the Place group below).
    auto* screenBtn = new QToolButton(page);
    // Icon-only: the name shows on hover (tooltip), not as a label.
    screenBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    screenBtn->setIconSize(QSize(28, 28));
    screenBtn->setAutoRaise(true);
    screenBtn->setFocusPolicy(Qt::NoFocus);
    screenBtn->setFixedSize(kRibbonCell);   // the Place group is one grid: this is part of it
    screenBtn->setToolTip(QStringLiteral(
        "Shield: click, then drag a crossing line across the wires to shield them"));
    {
        // Braided-shield cable: a cable body with a criss-cross braid mesh in the middle
        // section (matching the reference picture of a braid shield over a wire).
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        // Cable body: top + bottom edges running the full width.
        QPen body(QColor("#cfe3ff"));
        body.setWidthF(1.6);
        ip.setPen(body);
        ip.drawLine(2, 9, 26, 9);
        ip.drawLine(2, 19, 26, 19);
        // Braid mesh: criss-cross diagonals clipped to the middle section of the cable.
        ip.save();
        ip.setClipRect(8, 9, 12, 11);
        QPen mesh(QColor("#8fb7ff"));
        mesh.setWidthF(1.1);
        ip.setPen(mesh);
        for (int x = 0; x <= 26; x += 4) {
            ip.drawLine(x, 8, x + 11, 20); // "\" strands
            ip.drawLine(x, 20, x + 11, 8); // "/" strands
        }
        ip.restore();
        // Ticks where the braided section starts / ends.
        ip.setPen(body);
        ip.drawLine(8, 8, 8, 20);
        ip.drawLine(20, 8, 20, 20);
        ip.end();
        screenBtn->setIcon(QIcon(pm));
    }
    connect(screenBtn, &QToolButton::clicked, this, [this] {
        editor_->beginScreen();
        canvas_->setFocus(Qt::OtherFocusReason);
    });
    // Twist: split-button + per-type menu. Lives in the Place group (it places a twist
    // symbol); the same handlers as before.
    auto* modTwistBtn = new QToolButton(page);
    modTwistBtn->setFixedSize(kRibbonCell);
    // Icon-only: the name shows on hover (tooltip). The MenuButtonPopup arrow stays as
    // the fall-menu (type) affordance.
    modTwistBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    modTwistBtn->setIconSize(QSize(28, 28));
    modTwistBtn->setAutoRaise(true);
    modTwistBtn->setFocusPolicy(Qt::NoFocus);
    modTwistBtn->setPopupMode(QToolButton::MenuButtonPopup);
    modTwistBtn->setToolTip(QStringLiteral(
        "Twist type: click, then drag a crossing line across the wires (▾ for the type)"));
    {
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor("#cfe3ff"));
        pen.setWidthF(1.8);
        ip.setPen(pen);
        QPolygonF a, b;
        for (int k = 0; k <= 3; ++k) {
            const double x = 4 + k * 6.7;
            a << QPointF(x, (k % 2 == 0) ? 9 : 19);
            b << QPointF(x, (k % 2 == 0) ? 19 : 9);
        }
        ip.drawPolyline(a);
        ip.drawPolyline(b);
        ip.end();
        modTwistBtn->setIcon(QIcon(pm));
    }
    {
        auto* modTm = new QMenu(modTwistBtn);
        modTm->addAction(QStringLiteral("Type 1 (crossed)"), this, [this] {
            editor_->beginTwist(QStringLiteral("crossed"));
            canvas_->setFocus(Qt::OtherFocusReason);
        });
        modTm->addAction(QStringLiteral("Type 2 (twice twisted)"), this, [this] {
            editor_->beginTwist(QStringLiteral("jog"));
            canvas_->setFocus(Qt::OtherFocusReason);
        });
        modTm->addAction(QStringLiteral("Type 3 (sharp diamond)"), this, [this] {
            editor_->beginTwist(QStringLiteral("type3"));
            canvas_->setFocus(Qt::OtherFocusReason);
        });
        modTm->addSeparator();
        modTm->addAction(QStringLiteral("Remove twist…"), this, [this] {
            editor_->beginTwistRemove();
            canvas_->setFocus(Qt::OtherFocusReason);
        });
        modTwistBtn->setMenu(modTm);
    }
    connect(modTwistBtn, &QToolButton::clicked, this, [this] {
        // Re-arm the LAST chosen type (defaults to crossed), so the picked type sticks.
        editor_->beginTwist(editor_->lastTwistType());
        canvas_->setFocus(Qt::OtherFocusReason);
    });
    auto* placeW = new QWidget(page);
    auto* placeLay = new QHBoxLayout(placeW);
    placeLay->setContentsMargins(0, 0, 0, 0);
    placeLay->setSpacing(4);
    placeLay->addWidget(toolRow({ToolId::Connector, ToolId::Wire, ToolId::Splice,
                                 ToolId::NetLabel, ToolId::NotConnected}));
    // Shield + Twist share one 2-row icon column (same small-icon style as the tool row),
    // with Twist directly UNDER Shield.
    auto* stW = new QWidget(page);
    auto* stG = new QGridLayout(stW);
    stG->setContentsMargins(0, 0, 0, 0);
    stG->setSpacing(1);
    stG->addWidget(screenBtn, 0, 0);
    stG->addWidget(modTwistBtn, 1, 0);
    placeLay->addWidget(stW);
    // The stock symbols - both resistors, capacitor, coil, ferrite - as a small icon each in two rows,
    // like the tools beside them. A harness schematic is not only wires: there is a resistor across a
    // pair, a capacitor to the shell, a bead on a line, and each of them has to be drawn.
    placeLay->addWidget(makePartSymbolRow(placeW, /*schematic=*/true));
    schemPlaceGroup_ = ribbonGroup(QStringLiteral("Place"), placeW);
    lay->addWidget(schemPlaceGroup_);
    {
        auto* rule = new QFrame(page);
        rule->setFrameShape(QFrame::VLine);
        rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
        lay->addWidget(rule);
        schemPlaceRule_ = rule;
    }
    // Marker size — appears ONLY while the Splice or X (termination) tool is active,
    // editing that tool's size; also typeable in the flying command field.
    auto* mkW = new QWidget(page);
    auto* mkG = new QGridLayout(mkW);
    mkG->setContentsMargins(0, 0, 0, 0);
    mkG->setSpacing(2);
    markerLabel_ = new QLabel(QStringLiteral("Size"), mkW);
    markerSpin_ = new QDoubleSpinBox(mkW);
    markerSpin_->setRange(0.5, 500.0);
    markerSpin_->setDecimals(1);
    markerSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons); // field only, no +/-
    connect(markerSpin_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (editor_->tool() == ToolId::Splice) editor_->spliceSize() = markerSpin_->value();
        else if (editor_->tool() == ToolId::NotConnected)
            editor_->ncSize() = markerSpin_->value();
    });
    mkG->addWidget(markerLabel_, 0, 0);
    mkG->addWidget(markerSpin_, 1, 0);
    auto* mkGroup = ribbonGroup(QStringLiteral("Marker"), mkW);
    // Bright text (the ribbon label cascade doesn't reach inline labels/fields).
    mkGroup->setStyleSheet(QStringLiteral("QLabel,QCheckBox{color:#e6e9ef;background:transparent;}"));
    mkGroup->setVisible(false);
    markerGroup_ = mkGroup;
    lay->addWidget(mkGroup);
    // Show + relabel the marker size for the active tool (Splice / X), hide otherwise.
    // The group show/hide (marker vs twist vs the rest) is centralised in
    // updateSchematicRibbonGroups(); this lambda only sets the marker label/value.
    auto syncMarker = [this] {
        const ToolId t = editor_->tool();
        const bool splice = t == ToolId::Splice, x = t == ToolId::NotConnected;
        updateSchematicRibbonGroups();
        if ((splice || x) && markerSpin_) {
            QSignalBlocker b(markerSpin_);
            markerLabel_->setText(splice ? QStringLiteral("Joint ⌀") : QStringLiteral("X size"));
            markerSpin_->setValue(splice ? editor_->spliceSize() : editor_->ncSize());
        }
    };
    connect(editor_, &Editor::toolChanged, this, [syncMarker](ToolId) { syncMarker(); });
    connect(editor_, &Editor::markerSizeChanged, this, [syncMarker] { syncMarker(); });
    syncMarker();
    // Modify: straighten the wiring around a reference connector.
    auto* modW = new QWidget(page);
    auto* modG = new QGridLayout(modW);
    modG->setContentsMargins(0, 0, 0, 0);
    modG->setSpacing(2);
    auto* alignBtn = new QToolButton(modW);
    alignBtn->setText(QStringLiteral("Align\nrouting"));
    alignBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    alignBtn->setIconSize(QSize(26, 26));
    alignBtn->setMinimumWidth(66);
    // Icon: two pin stubs on the left joined by a right-angle (0°/90°) route to a
    // pin stub on the right — the "make wires orthogonal" glyph.
    {
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor("#cfe3ff"));
        pen.setWidthF(1.8);
        ip.setPen(pen);
        ip.drawLine(3, 7, 12, 7);    // top pin exits right
        ip.drawLine(12, 7, 12, 21);  // vertical run (90°)
        ip.drawLine(12, 21, 25, 21); // into the lower-right pin
        ip.drawLine(3, 14, 8, 14);   // a second (straight) pin stub
        ip.drawLine(8, 14, 8, 21);
        ip.end();
        alignBtn->setIcon(QIcon(pm));
    }
    alignBtn->setToolTip(QStringLiteral(
        "Click a reference connector; connectors wired to it shift so their wires "
        "become horizontal/vertical"));
    connect(alignBtn, &QToolButton::clicked, this, [this] {
        editor_->beginAlignRouting();
        canvas_->setFocus(Qt::OtherFocusReason);
    });
    modG->addWidget(alignBtn, 0, 0);
    // Cross switch: toggle a wire crossing between a bridge (hop) and a plain cross.
    auto* modCrossBtn = new QToolButton(modW);
    modCrossBtn->setText(QStringLiteral("Cross\nswitch"));
    modCrossBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    modCrossBtn->setIconSize(QSize(26, 26));
    modCrossBtn->setMinimumWidth(66);
    modCrossBtn->setToolTip(QStringLiteral(
        "Click a wire crossing to toggle it between a bridge (hop) and a plain cross"));
    {
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor("#cfe3ff"));
        pen.setWidthF(2.0);
        ip.setPen(pen);
        ip.drawLine(3, 20, 9, 20);
        QPainterPath hop;
        hop.moveTo(9, 20);
        hop.arcTo(9, 12, 10, 16, 180, -180);
        ip.drawPath(hop);
        ip.drawLine(19, 20, 25, 20);
        ip.drawLine(14, 6, 14, 26);
        ip.end();
        modCrossBtn->setIcon(QIcon(pm));
    }
    connect(modCrossBtn, &QToolButton::clicked, this, [this] {
        editor_->beginCrossSwitch();
        canvas_->setFocus(Qt::OtherFocusReason);
    });
    modG->addWidget(modCrossBtn, 0, 1);
    // Align: select whatever rides on the wires - twist marks, shields, a wire carrying a
    // colour/gauge marking or a label - click this, then click the column to line them all
    // up on. Each stays on its own wire; the column only says how far along.
    auto* twistAlignBtn = new QToolButton(modW);
    twistAlignBtn->setText(QStringLiteral("Align"));
    twistAlignBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    twistAlignBtn->setIconSize(QSize(26, 26));
    twistAlignBtn->setMinimumWidth(66);
    twistAlignBtn->setToolTip(QStringLiteral(
        "Line up what rides on the wires - twist marks, shields, colour/gauge markings and "
        "wire labels - on one column: select them, click this, then click the column"));
    {
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor("#cfe3ff"));
        pen.setWidthF(1.8);
        ip.setPen(pen);
        // Two small braids (twist marks) with a dashed alignment line through them.
        for (int cx : {8, 20}) {
            ip.drawLine(cx - 4, 8, cx + 4, 16);
            ip.drawLine(cx + 4, 8, cx - 4, 16);
        }
        QPen dash(QColor("#8fb7ff"));
        dash.setWidthF(1.2);
        dash.setStyle(Qt::DashLine);
        ip.setPen(dash);
        ip.drawLine(14, 4, 14, 24);
        ip.end();
        twistAlignBtn->setIcon(QIcon(pm));
    }
    connect(twistAlignBtn, &QToolButton::clicked, this, [this] {
        editor_->beginTwistAlign();
        canvas_->setFocus(Qt::OtherFocusReason);
    });
    modG->addWidget(twistAlignBtn, 0, 2);
    modG->addWidget(makeMatchBtn(modW, /*iconOnly=*/false), 0, 3);
    // "Crossed" twist parameters (apply to the next Crossed twist placed): the diagonal
    // angle and the horizontal cap length.
    {
        // Contextual twist parameters — shown while a twist is armed, with only the
        // fields relevant to the chosen type:
        //   Type 1 (crossed): Diag°, H len, Amount
        //   Type 2 (jog):     H len, Repeats, Amount
        //   Type 3 (diamond): Repeats, Amount
        auto* tpW = new QWidget(page);
        auto* tpG = new QGridLayout(tpW);
        tpG->setContentsMargins(0, 0, 0, 0);
        tpG->setHorizontalSpacing(4);
        tpG->setVerticalSpacing(3);
        auto lbl = [&](const QString& t) {
            auto* l = new QLabel(t, tpW);
            l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
            return l;
        };
        auto spin = [&](double lo, double hi, int dec) {
            auto* s = new QDoubleSpinBox(tpW);
            s->setRange(lo, hi);
            s->setDecimals(dec);
            s->setKeyboardTracking(false);
            s->setButtonSymbols(QAbstractSpinBox::NoButtons);
            s->setMinimumWidth(48);
            return s;
        };
        auto* twAng = spin(0.0, 89.0, 1);
        twAng->setToolTip(QStringLiteral("Type 1: diagonal angle (deg)"));
        connect(twAng, &QDoubleSpinBox::editingFinished, this,
                [this, twAng] { editor_->twistDiagAngle() = twAng->value(); });
        auto* twLen = spin(0.0, 500.0, 1);
        twLen->setToolTip(QStringLiteral("Horizontal length (Type 1 cap / Type 2 flat)"));
        connect(twLen, &QDoubleSpinBox::editingFinished, this, [this, twLen] {
            if (editor_->twistArmType() == QLatin1String("jog"))
                editor_->twistFlatLen() = twLen->value();
            else
                editor_->twistCapLen() = twLen->value();
        });
        auto* twRep = spin(1.0, 20.0, 0);
        twRep->setToolTip(QStringLiteral("Type 2/3: number of twists (repeats)"));
        connect(twRep, &QDoubleSpinBox::editingFinished, this, [this, twRep] {
            editor_->twistRepeats() = std::max(1, static_cast<int>(twRep->value()));
        });
        // Amount is a fall-menu (dropdown), matching the Shield "Amount" control.
        auto* twAmt = new QComboBox(tpW);
        for (int n = 1; n <= 9; ++n) twAmt->addItem(QString::number(n));
        twAmt->setMinimumWidth(52);
        twAmt->setToolTip(QStringLiteral("How many twist marks to place side by side"));
        connect(twAmt, &QComboBox::currentTextChanged, this,
                [this](const QString& t) { editor_->twistAmount() = std::max(1, t.toInt()); });
        auto* angLbl = lbl(QStringLiteral("Diag°"));
        auto* lenLbl = lbl(QStringLiteral("H len"));
        auto* repLbl = lbl(QStringLiteral("Twists"));
        auto* amtLbl = lbl(QStringLiteral("Amount"));
        // Two columns: (label,field) at cols 0-1 and 2-3; rows fill per type.
        tpG->addWidget(angLbl, 0, 0); tpG->addWidget(twAng, 0, 1);
        tpG->addWidget(lenLbl, 1, 0); tpG->addWidget(twLen, 1, 1);
        tpG->addWidget(repLbl, 0, 2); tpG->addWidget(twRep, 0, 3);
        tpG->addWidget(amtLbl, 1, 2); tpG->addWidget(twAmt, 1, 3);
        // The twist parameters live in their OWN ribbon group. While a twist is armed the
        // Schematic-build ribbon shows ONLY this group (see updateSchematicRibbonGroups);
        // otherwise it is hidden and the Place / Modify / DRC groups show.
        schemTwistGroup_ = ribbonGroup(QStringLiteral("Twist"), tpW);
        lay->addWidget(schemTwistGroup_);
        {
            auto* rule = new QFrame(page);
            rule->setFrameShape(QFrame::VLine);
            rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
            lay->addWidget(rule);
            schemTwistRule_ = rule;
        }
        schemTwistGroup_->setVisible(false);
        schemTwistRule_->setVisible(false);
        connect(editor_, &Editor::twistArmChanged, this,
                [this, twAng, twLen, twRep, twAmt, angLbl, lenLbl, repLbl] {
                    const QString ty = editor_->twistArmType();
                    // Show only the twist menu in the ribbon (or restore the rest).
                    updateSchematicRibbonGroups();
                    if (ty.isEmpty()) return;
                    const bool crossed = ty == QLatin1String("crossed");
                    const bool jog = ty == QLatin1String("jog");
                    const bool type3 = ty == QLatin1String("type3");
                    angLbl->setVisible(crossed); twAng->setVisible(crossed);
                    lenLbl->setVisible(crossed || jog); twLen->setVisible(crossed || jog);
                    repLbl->setVisible(jog || type3); twRep->setVisible(jog || type3);
                    QSignalBlocker b1(twAng), b2(twLen), b3(twRep), b4(twAmt);
                    twAng->setValue(editor_->twistDiagAngle());
                    twLen->setValue(jog ? editor_->twistFlatLen() : editor_->twistCapLen());
                    twRep->setValue(editor_->twistRepeats());
                    twAmt->setCurrentText(QString::number(std::max(1, editor_->twistAmount())));
                });
    }
    // Built manually (not via addGroup) so the group + its separator rule can be
    // captured and hidden while the Splice / Termination tool is armed.
    schemModifyGroup_ = ribbonGroup(QStringLiteral("Modify"), modW);
    lay->addWidget(schemModifyGroup_);
    {
        auto* rule = new QFrame(page);
        rule->setFrameShape(QFrame::VLine);
        rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
        lay->addWidget(rule);
        schemModifyRule_ = rule;
    }

    // ---- DRC (design-rule check) group: Validate / Bypass table / Rules ----
    auto* drcW = new QWidget(page);
    auto* drcG = new QGridLayout(drcW);
    drcG->setContentsMargins(0, 0, 0, 0);
    drcG->setSpacing(2);
    // Validate button: schematic design-rule check.
    auto* valBtn = new QToolButton(drcW);
    valBtn->setText(QStringLiteral("Validate"));
    valBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    valBtn->setIconSize(QSize(26, 26));
    valBtn->setMinimumWidth(66);
    valBtn->setToolTip(QStringLiteral(
        "Check the schematic: every connector pin wired or X-terminated, no duplicate "
        "pin labels, no open wires"));
    {
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor("#7ee0a0"));
        pen.setWidthF(2.4);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        ip.setPen(pen);
        ip.drawLine(6, 15, 12, 21); // check mark
        ip.drawLine(12, 21, 23, 7);
        ip.end();
        valBtn->setIcon(QIcon(pm));
    }
    connect(valBtn, &QToolButton::clicked, this, [this] { showValidation(); });
    drcG->addWidget(valBtn, 0, 0);
    // Bypass table button: review discrepancies the user accepted (bypassed).
    auto* bypassBtn = new QToolButton(drcW);
    bypassBtn->setText(QStringLiteral("Bypass table"));
    bypassBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    bypassBtn->setIconSize(QSize(26, 26));
    bypassBtn->setMinimumWidth(66);
    bypassBtn->setToolTip(QStringLiteral("Show discrepancies you have bypassed (accepted)"));
    {
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor("#ffb038"));
        pen.setWidthF(2.4);
        ip.setPen(pen);
        ip.setBrush(Qt::NoBrush);
        ip.drawEllipse(QPointF(14, 14), 9.0, 9.0);
        ip.drawLine(8, 8, 20, 20); // slash = "bypassed"
        ip.end();
        bypassBtn->setIcon(QIcon(pm));
    }
    connect(bypassBtn, &QToolButton::clicked, this, [this] { showBypassTable(); });
    drcG->addWidget(bypassBtn, 0, 1);
    // Validation rules button: choose which design-rule checks run.
    auto* rulesBtn = new QToolButton(drcW);
    rulesBtn->setText(QStringLiteral("Rules"));
    rulesBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    rulesBtn->setIconSize(QSize(26, 26));
    rulesBtn->setMinimumWidth(66);
    rulesBtn->setToolTip(QStringLiteral("Choose which validation rules are checked"));
    {
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor("#8fb7ff"));
        pen.setWidthF(2.2);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        ip.setPen(pen);
        // A checklist: three rows, each a tiny check + a rule line.
        for (int i = 0; i < 3; ++i) {
            const int y = 8 + i * 6;
            ip.drawLine(6, y, 8, y + 2);
            ip.drawLine(8, y + 2, 11, y - 1);
            ip.drawLine(14, y + 1, 22, y + 1);
        }
        ip.end();
        rulesBtn->setIcon(QIcon(pm));
    }
    connect(rulesBtn, &QToolButton::clicked, this, [this] { showValidationRules(); });
    drcG->addWidget(rulesBtn, 0, 2);
    // Built manually (see Modify above) so the group + rule can be hidden while the
    // Splice / Termination tool is armed.
    schemDrcGroup_ = ribbonGroup(QStringLiteral("DRC"), drcW);
    lay->addWidget(schemDrcGroup_);
    {
        auto* rule = new QFrame(page);
        rule->setFrameShape(QFrame::VLine);
        rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
        lay->addWidget(rule);
        schemDrcRule_ = rule;
    }
    // ---- The Harness Builder: describe the whole harness, then generate it. It lives
    //      here, with the rest of the schematic-building tools, rather than on a tab of
    //      its own. ----
    {
        auto* hbW = new QWidget(page);
        auto* hbg = new QGridLayout(hbW);
        hbg->setContentsMargins(0, 0, 0, 0);
        hbg->setSpacing(2);
        auto* hbBtn = new QToolButton(hbW);
        hbBtn->setText(QStringLiteral("Schematic harness\nbuilder"));
        hbBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        hbBtn->setIconSize(QSize(30, 30));
        hbBtn->setMinimumWidth(112);
        hbBtn->setAutoRaise(true);
        hbBtn->setToolTip(QStringLiteral(
            "Describe the whole harness - connectors, their order and side, the pinout, "
            "every pin-to-pin wire, shields and twisted pairs - then generate the schematic"));
        {
            // A connector on each side joined by three wires: the harness-in-one-picture.
            QPixmap pm(32, 32);
            pm.fill(Qt::transparent);
            QPainter ip(&pm);
            ip.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(QColor("#8fb7ff"));
            pen.setWidthF(1.7);
            ip.setPen(pen);
            ip.drawRect(3, 8, 6, 16);
            ip.drawRect(23, 8, 6, 16);
            ip.setPen(QPen(QColor("#7ee0a0"), 1.5));
            for (int i = 0; i < 3; ++i) {
                const int y = 12 + i * 4;
                ip.drawLine(9, y, 23, y);
            }
            ip.end();
            hbBtn->setIcon(QIcon(pm));
        }
        connect(hbBtn, &QToolButton::clicked, this, [this] { showHarnessBuilder(); });
        hbg->addWidget(hbBtn, 0, 0);
        hbg->addWidget(makePartsLibraryButton(hbW), 0, 1);
        addGroup(lay, QStringLiteral("Harness"), hbW);
    }
    {
        // Tools: the calculations. The same group is on the mechanical tab, because these are
        // asked on one side and checked on the other - the answers do not belong to either.
        auto* tw = new QWidget(page);
        auto* tg = new QGridLayout(tw);
        tg->setContentsMargins(0, 0, 0, 0);
        tg->setSpacing(2);
        tg->addWidget(makeVoltageDropButton(tw), 0, 0);
        tg->addWidget(makeBundleDiameterButton(tw), 0, 1);
        tg->addWidget(makeThermalButton(tw), 0, 2);
        addGroup(lay, QStringLiteral("Tools"), tw);
    }
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("Schematic build"));
}

} // namespace Edraw_Desktop
