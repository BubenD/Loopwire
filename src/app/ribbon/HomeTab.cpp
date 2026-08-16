// The Home ribbon tab. Split out of MainWindow.cpp so each tab is one file: the ribbon was the
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

namespace {
QIcon modelSessionIcon(bool capture) {
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#d6dae1"));
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    if (capture) {
        p.drawRect(QRectF(4, 5, 20, 17));
        p.drawLine(QPointF(14, 2), QPointF(14, 16));
        p.drawLine(QPointF(9, 11), QPointF(14, 16));
        p.drawLine(QPointF(19, 11), QPointF(14, 16));
    } else {
        p.drawLine(QPointF(7, 7), QPointF(21, 21));
        p.drawLine(QPointF(21, 7), QPointF(7, 21));
    }
    return QIcon(pm);
}

QIcon modelAnchorIcon(const QColor& color) {
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color);
    pen.setWidthF(2.0);
    p.setPen(pen);
    p.drawLine(QPointF(14, 3), QPointF(14, 25));
    p.drawEllipse(QPointF(14, 14), 5, 5);
    return QIcon(pm);
}
} // namespace

// The square a model-session button takes: an icon and up to two short lines under it. Big enough that the
// words are read rather than guessed at, and the same for all five, so the row reads as one group.
constexpr QSize kModelSessionCell(84, 66);

// ---- HOME : Draw | Modify | Properties | AI ----
void MainWindow::buildHomeTab() {
    auto [page, lay] = makeTabPage();
    QAction* sel = makeToolAction(QStringLiteral("Select"), ToolId::Select);
    sel->setChecked(true);
    addGroup(lay, QStringLiteral("Draw"),
             toolRow({ToolId::Line, ToolId::DoubleLine, ToolId::Polyline,
                      ToolId::DoublePolyline, ToolId::Rect, ToolId::Circle,
                      ToolId::Ellipse, ToolId::Arc, ToolId::Spline, ToolId::Polygon,
                      ToolId::Hatch, ToolId::Wipeout}));
    // The modify tools, Match, Array and Copy: twelve buttons in the same two rows of six. Array and Copy
    // used to stand beside the grid, each at its own width, and Match was dropped into a spare cell - three
    // ways of putting a button in a group, in one group.
    addGroup(lay, QStringLiteral("Modify"),
             toolRow({ToolId::Move, ToolId::Rotate, ToolId::Mirror, ToolId::Scale, ToolId::Offset,
                      ToolId::Trim, ToolId::Extend, ToolId::Fillet, ToolId::Chamfer},
                     {makeMatchBtn(page, /*iconOnly=*/true), makeArrayButton(page),
                      clipboardRow(/*iconOnly=*/true)}));
    addGroup(lay, QStringLiteral("Operations"), operationsRow());
    {
        // The annotation tools and the Table button, in the same two rows.
        auto* tb = new QToolButton(page);
        tb->setIcon(makeToolIcon(ToolId::Table));
        tb->setIconSize(QSize(28, 28));
        tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
        tb->setAutoRaise(true);
        tb->setFocusPolicy(Qt::NoFocus);
        tb->setToolTip(QStringLiteral("Insert Table"));
        connect(tb, &QToolButton::clicked, this, [this] { insertTableCommand(); });
        addGroup(lay, QStringLiteral("Annotation"),
                 toolRow({ToolId::Text, ToolId::Dimension, ToolId::Balloon, ToolId::Note,
                          ToolId::Revision},
                         {tb}));
    }

    // Properties group: Layer + Color dropdowns.
    auto* propsW = new QWidget(page);
    auto* pg = new QGridLayout(propsW);
    pg->setContentsMargins(0, 0, 0, 0);
    pg->setSpacing(2);
    layerCombo_ = new QComboBox(propsW);
    layerCombo_->setMinimumWidth(120);
    colorCombo_ = new QComboBox(propsW);
    colorCombo_->setMinimumWidth(120);
    colorCombo_->addItems({"ByLayer", "White", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta"});
    // Bright, full-width labels so "Layer"/"Color" read clearly in dark mode.
    auto propLabel = [&](const QString& t) {
        auto* l = new QLabel(t, propsW);
        l->setStyleSheet(QStringLiteral("color:#e6e9ef; background:transparent;"));
        l->setMinimumWidth(38);
        return l;
    };
    // Lineweight dropdown (ByLayer + standard weights) for new entities / selection.
    auto* lwCombo = new QComboBox(propsW);
    lwCombo->setMinimumWidth(120);
    lwCombo->addItems({"ByLayer", "0.00", "0.13", "0.25", "0.35", "0.50", "0.70",
                       "1.00", "1.40", "2.00"});
    pg->addWidget(propLabel(QStringLiteral("Layer")), 0, 0);
    pg->addWidget(layerCombo_, 0, 1);
    pg->addWidget(propLabel(QStringLiteral("Color")), 1, 0);
    pg->addWidget(colorCombo_, 1, 1);
    pg->addWidget(propLabel(QStringLiteral("Weight")), 2, 0);
    pg->addWidget(lwCombo, 2, 1);
    connect(lwCombo, &QComboBox::activated, this, [this, lwCombo](int) {
        const QString t = lwCombo->currentText();
        const double w = (t == QLatin1String("ByLayer")) ? 0.0 : t.toDouble();
        editor_->setCurrentWeight(w);   // applies to newly drawn entities
        editor_->setSelectionWeight(w); // and to any current selection
    });
    connect(layerCombo_, &QComboBox::activated, this, [this](int) {
        const QString name = layerCombo_->currentText();
        doc_->setActiveLayer(name);
        editor_->setSelectionLayer(name);
    });
    connect(colorCombo_, &QComboBox::activated, this, [this](int) {
        static const QHash<QString, QString> map = {
            {"ByLayer", ""}, {"White", "#ffffff"}, {"Red", "#ff0000"},
            {"Yellow", "#ffff00"}, {"Green", "#00ff00"}, {"Cyan", "#00ffff"},
            {"Blue", "#0000ff"}, {"Magenta", "#ff00ff"}};
        const QString hex = map.value(colorCombo_->currentText(), QString());
        editor_->setCurrentColor(hex);     // applies to newly drawn entities
        editor_->setSelectionColor(hex);   // and recolors any current selection
    });
    addGroup(lay, QStringLiteral("Properties"), propsW);
    {
        // Blocks: a piece of drawing named once and dropped in wherever it is wanted.
        auto* bw = new QWidget(page);
        auto* bg = new QGridLayout(bw);
        bg->setContentsMargins(0, 0, 0, 0);
        bg->setSpacing(2);
        bg->addWidget(makeBlockButton(bw, BlockAction::Create), 0, 0);
        bg->addWidget(makeBlockButton(bw, BlockAction::Insert), 0, 1);
        bg->addWidget(makeBlockButton(bw, BlockAction::Manage), 0, 2);
        saveBlockBtn_ = makeBlockButton(bw, BlockAction::SaveEdit);
        bg->addWidget(saveBlockBtn_, 0, 3);
        cancelBlockBtn_ = makeBlockButton(bw, BlockAction::CancelEdit);
        bg->addWidget(cancelBlockBtn_, 0, 4);
        addGroup(lay, QStringLiteral("Block"), bw);
}
{
    // Finishing a template that is being drawn. The whole group is only on the ribbon WHILE one is
    // open - it is meaningless on a drawing, and a permanently greyed-out group is just clutter. It
    // lives on Home, where the drawing tools are, so a template can be drawn and saved from one tab.
    auto* tw = new QWidget(page);
    auto* tg = new QGridLayout(tw);
    tg->setContentsMargins(0, 0, 0, 0);
    tg->setSpacing(2);
    for (int which = 0; which < 3; ++which)
        tg->addWidget(makeTemplateSessionButton(tw, which), 0, which);
    // The group and its divider rule go in and out together, so hiding it leaves no double rule.
    tplHomeGroup_ = new QWidget(page);
    auto* hg = new QHBoxLayout(tplHomeGroup_);
    hg->setContentsMargins(0, 0, 0, 0);
    hg->setSpacing(0);
    hg->addWidget(ribbonGroup(QStringLiteral("Template"), tw));
    auto* rule = new QFrame(tplHomeGroup_);
    rule->setFrameShape(QFrame::VLine);
    rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
    hg->addWidget(rule);
    lay->addWidget(tplHomeGroup_);
    tplHomeGroup_->hide();
    }
    {
        // The five things done while a default model is being drawn. Long labels under small icons in a
        // two-row grid ran into one another and were cut off by the row below - the words were there and
        // could not be read, which is worse than no words. One row, one square each, and a label of at most
        // two short lines: the group is called "Default model", so no button has to say so as well.
        auto* session = new QWidget(page);
        auto* sg = new QGridLayout(session);
        sg->setContentsMargins(0, 0, 0, 0);
        sg->setSpacing(4);
        auto sessionButton = [this, session](QToolButton*& into, const QString& text, const QIcon& icon,
                                             const QString& tip) {
            into = new QToolButton(session);
            into->setText(text);
            into->setIcon(icon);
            into->setIconSize(QSize(26, 26));
            into->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            into->setToolTip(tip);
            into->setAutoRaise(true);
            into->setFocusPolicy(Qt::NoFocus);
            // Room for two short lines under the icon, and the same room for each: a row of buttons of
            // different widths reads as a row of different things.
            into->setFixedSize(kModelSessionCell);
            return into;
        };
        sessionButton(captureMechModelBtn_, tr("Capture"), modelSessionIcon(true),
                      tr("Take what is drawn here as this default model and close the session"));
        connect(captureMechModelBtn_, &QToolButton::clicked, this,
                [this] { saveMechModelBeingEdited(); });
        sessionButton(cancelMechModelBtn_, tr("Cancel"), modelSessionIcon(false),
                      tr("Leave the model as it was"));
        connect(cancelMechModelBtn_, &QToolButton::clicked, this, [this] { cancelMechModelEdit(); });
        sessionButton(markConnectMechModelBtn_, tr("Connection\npoint"), modelAnchorIcon(QColor("#5aa0ff")),
                      tr("Click where the part connects to what it is placed on, then click along the "
                         "direction it runs from there. The wizard places the part by those two: the "
                         "drawing is turned so that direction runs along the run, and set down so that "
                         "point lands on it."));
        connect(markConnectMechModelBtn_, &QToolButton::clicked, this,
                [this] { armMechModelConnect(); });
        sessionButton(markNearMechModelBtn_, tr("Near\npoint"), modelAnchorIcon(QColor("#e6a23c")),
                      tr("The near end of the model - marked in orange"));
        connect(markNearMechModelBtn_, &QToolButton::clicked, this, [this] { armMechModelAnchor(true); });
        sessionButton(markFarMechModelBtn_, tr("Far\npoint"), modelAnchorIcon(QColor("#62c370")),
                      tr("The far end of the model - marked in green"));
        connect(markFarMechModelBtn_, &QToolButton::clicked, this, [this] { armMechModelAnchor(false); });
        sg->addWidget(captureMechModelBtn_, 0, 0);
        sg->addWidget(cancelMechModelBtn_, 0, 1);
        sg->addWidget(markConnectMechModelBtn_, 0, 2);
        sg->addWidget(markNearMechModelBtn_, 0, 3);
        sg->addWidget(markFarMechModelBtn_, 0, 4);
        mechModelSessionGroup_ = new QWidget(page);
        auto* hg = new QHBoxLayout(mechModelSessionGroup_);
        hg->setContentsMargins(0, 0, 0, 0);
        hg->setSpacing(0);
        hg->addWidget(ribbonGroup(QStringLiteral("Default model"), session));
        auto* rule = new QFrame(mechModelSessionGroup_);
        rule->setFrameShape(QFrame::VLine);
        rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
        hg->addWidget(rule);
        lay->addWidget(mechModelSessionGroup_);
        mechModelSessionGroup_->hide();
    }
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("Home"));
}

} // namespace Edraw_Desktop
