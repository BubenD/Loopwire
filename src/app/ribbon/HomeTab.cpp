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
    {
        // Modify group holds the modify tools AND the Copy/Cut clipboard button.
        auto* modW = new QWidget(page);
        auto* mh = new QHBoxLayout(modW);
        mh->setContentsMargins(0, 0, 0, 0);
        mh->setSpacing(2);
        // 9 modify tools lay out 5 + 4, leaving the bottom-right grid cell empty;
        // drop the Match icon into it so it lines up with the other icons in the row.
        auto* modRow = toolRow({ToolId::Move, ToolId::Rotate, ToolId::Mirror, ToolId::Scale,
                                ToolId::Offset, ToolId::Trim, ToolId::Extend, ToolId::Fillet,
                                ToolId::Chamfer});
        if (auto* g = qobject_cast<QGridLayout*>(modRow->layout()))
            g->addWidget(makeMatchBtn(modRow, /*iconOnly=*/true), 1, 4);
        mh->addWidget(modRow);
        // Icon only here, matching the tool icons it sits beside; the name is on hover.
        mh->addWidget(clipboardRow(/*iconOnly=*/true));
        addGroup(lay, QStringLiteral("Modify"), modW);
    }
    addGroup(lay, QStringLiteral("Operations"), operationsRow());
    {
        // Annotation group: the annotation tools + a Table button (Insert Table).
        auto* annoW = new QWidget(page);
        auto* ah = new QHBoxLayout(annoW);
        ah->setContentsMargins(0, 0, 0, 0);
        ah->setSpacing(2);
        ah->addWidget(toolRow({ToolId::Text, ToolId::Dimension, ToolId::Balloon,
                               ToolId::Note, ToolId::Revision}));
        auto* tb = new QToolButton(annoW);
        tb->setIcon(makeToolIcon(ToolId::Table));
        tb->setIconSize(QSize(28, 28));
        tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
        tb->setAutoRaise(true);
        tb->setFocusPolicy(Qt::NoFocus);
        tb->setToolTip(QStringLiteral("Insert Table"));
        connect(tb, &QToolButton::clicked, this, [this] { insertTableCommand(); });
        ah->addWidget(tb);
        addGroup(lay, QStringLiteral("Annotation"), annoW);
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

    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("Home"));
}

} // namespace Edraw_Desktop
