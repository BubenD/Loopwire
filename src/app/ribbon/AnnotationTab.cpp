// The Annotation ribbon tab. Split out of MainWindow.cpp so each tab is one file: the ribbon was the
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

// ---- ANNOTATION ----
void MainWindow::buildAnnotationTab() {
    auto [page, lay] = makeTabPage();
    addGroup(lay, QStringLiteral("Annotate"), toolRow({ToolId::Dimension, ToolId::Text, ToolId::MText, ToolId::NetLabel}));
    {
        // Callouts + a Table button that asks rows/cols before you drag the grid.
        auto* calloutW = new QWidget(page);
        auto* ch = new QHBoxLayout(calloutW);
        ch->setContentsMargins(0, 0, 0, 0);
        ch->setSpacing(2);
        // Leader = a split-button: click places a TEXT leader (no circle); the
        // fall-menu offers a Balloon leader plus the multileader operations.
        auto* balloonBtn = new QToolButton(calloutW);
        balloonBtn->setIcon(makeToolIcon(ToolId::TextLeader));
        balloonBtn->setIconSize(QSize(28, 28));
        balloonBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        balloonBtn->setAutoRaise(true);
        balloonBtn->setFocusPolicy(Qt::NoFocus);
        balloonBtn->setToolTip(QStringLiteral("Leader (text) — click the tip then the label"));
        balloonBtn->setPopupMode(QToolButton::MenuButtonPopup);
        connect(balloonBtn, &QToolButton::clicked, this,
                [this] { editor_->setTool(ToolId::TextLeader); });
        auto* lm = new QMenu(balloonBtn);
        {
            QAction* a = lm->addAction(makeToolIcon(ToolId::TextLeader),
                                       QStringLiteral("Leader (text only)"));
            connect(a, &QAction::triggered, this, [this] { editor_->setTool(ToolId::TextLeader); });
            a = lm->addAction(makeToolIcon(ToolId::Balloon), QStringLiteral("Balloon"));
            connect(a, &QAction::triggered, this, [this] { editor_->setTool(ToolId::Balloon); });
            lm->addSeparator();
            a = lm->addAction(QStringLiteral("Add Leader"));
            connect(a, &QAction::triggered, this, [this] { editor_->addLeaderToSelection(); });
            a = lm->addAction(QStringLiteral("Remove Leader"));
            connect(a, &QAction::triggered, this,
                    [this] { editor_->removeLeaderFromSelection(); });
            lm->addSeparator();
            a = lm->addAction(QStringLiteral("Align"));
            connect(a, &QAction::triggered, this, [this] { editor_->alignBalloons(); });
            a = lm->addAction(QStringLiteral("Collect"));
            connect(a, &QAction::triggered, this, [this] { editor_->collectBalloons(); });
        }
        balloonBtn->setMenu(lm);
        ch->addWidget(balloonBtn);
        ch->addWidget(toolRow({ToolId::Note, ToolId::Revision}));
        auto* tableBtn = new QToolButton(calloutW);
        tableBtn->setIcon(makeToolIcon(ToolId::Table));
        tableBtn->setIconSize(QSize(28, 28));
        tableBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        tableBtn->setAutoRaise(true);
        tableBtn->setFocusPolicy(Qt::NoFocus);
        tableBtn->setToolTip(QStringLiteral("Table — set rows/columns, then drag its size"));
        connect(tableBtn, &QToolButton::clicked, this, [this] { insertTableCommand(); });
        ch->addWidget(tableBtn);
        addGroup(lay, QStringLiteral("Callouts"), calloutW);
    }
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("Annotation"));
}

} // namespace Edraw_Desktop
