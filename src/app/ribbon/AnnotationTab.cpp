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

namespace Loopwire {

// ---- ANNOTATION ----
void MainWindow::buildAnnotationTab() {
    auto [page, lay] = makeTabPage();
    // The block arrow is one of these: a shape with a width that says which way a thing goes in or
    // which way it turns. It was on the harness tab under Special, which is where a person looks for a
    // break mark and not for a piece of writing.
    addGroup(lay, QStringLiteral("Annotate"),
             toolRow({ToolId::Dimension, ToolId::Text, ToolId::MText, ToolId::NetLabel,
                      ToolId::VolumeArrow, ToolId::Arrow}));
    // Where the middle of something is. Both are PICKED rather than drawn: the geometry already says where
    // the middle is, and making a drawing office find it with a cursor is asking them to do arithmetic the
    // program can do exactly.
    addGroup(lay, QStringLiteral("Centre marks"),
             toolRow({ToolId::CenterMark, ToolId::CenterLine}));
    // The small thing drawn again bigger. It belongs with the annotations rather than with the drawing tools
    // because it draws nothing: it is a view OF the drawing, and what it says is "look at this bit".
    addGroup(lay, QStringLiteral("Detail"), toolRow({ToolId::DetailView}));
    {
        // Callouts + a Table button that asks rows/cols before you drag the grid.
        // Leader = a split-button: click places a TEXT leader (no circle); the
        // fall-menu offers a Balloon leader plus the multileader operations.
        auto* balloonBtn = new QToolButton(page);
        balloonBtn->setProperty("cadActionIcon",QStringLiteral("leader"));
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
            connect(a, &QAction::triggered, this, [this] { editor_->beginBalloonShape(QStringLiteral("circle")); });
            a = lm->addAction(makeToolIcon(ToolId::Arrow), QStringLiteral("Arrow (no text)"));
            connect(a, &QAction::triggered, this, [this] { editor_->setTool(ToolId::Arrow); });
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
        auto* tableBtn = new QToolButton(page);
        tableBtn->setProperty("cadActionIcon",QStringLiteral("table"));
        tableBtn->setIcon(makeToolIcon(ToolId::Table));
        tableBtn->setIconSize(QSize(28, 28));
        tableBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        tableBtn->setAutoRaise(true);
        tableBtn->setFocusPolicy(Qt::NoFocus);
        tableBtn->setToolTip(QStringLiteral("Table — set rows/columns, then drag its size"));
        connect(tableBtn, &QToolButton::clicked, this, [this] { insertTableCommand(); });
        // NOTE MARKS: the number in a triangle that ties a place on the drawing to a note, and the same in
        // a square, a hexagon or a diamond. A click places the triangle; the fall menu has the others.
        auto* markBtn = new QToolButton(page);
        markBtn->setObjectName(QStringLiteral("annotationNoteMark"));
        markBtn->setProperty("cadActionIcon", QStringLiteral("notemark_triangle"));
        markBtn->setIcon(makeCadActionIcon(QStringLiteral("notemark_triangle")));
        markBtn->setIconSize(QSize(28, 28));
        markBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        markBtn->setAutoRaise(true);
        markBtn->setFocusPolicy(Qt::NoFocus);
        markBtn->setToolTip(QStringLiteral("Note mark — a number in a triangle, with a leader; the menu has "
                                           "the other shapes"));
        markBtn->setPopupMode(QToolButton::MenuButtonPopup);
        connect(markBtn, &QToolButton::clicked, this,
                [this] { editor_->beginBalloonShape(QStringLiteral("triangle")); });
        auto* mm = new QMenu(markBtn);
        for (const auto& [shape, name] : {std::pair{QStringLiteral("triangle"), QStringLiteral("Triangle")},
                                          std::pair{QStringLiteral("square"), QStringLiteral("Square")},
                                          std::pair{QStringLiteral("hexagon"), QStringLiteral("Hexagon")},
                                          std::pair{QStringLiteral("diamond"), QStringLiteral("Diamond")},
                                          std::pair{QStringLiteral("circle"), QStringLiteral("Circle")}}) {
            QAction* a = mm->addAction(makeCadActionIcon(QStringLiteral("notemark_") + shape), name);
            const QString chosen = shape;
            connect(a, &QAction::triggered, this, [this, chosen] { editor_->beginBalloonShape(chosen); });
        }
        markBtn->setMenu(mm);
        // Leader, Note, Revision and Table in the SAME two rows: two by two, the note mark beside them.
        addGroup(lay, QStringLiteral("Callouts"),
                 toolRow({ToolId::Note, ToolId::Revision}, {balloonBtn, tableBtn, markBtn}));
    }
    addGroup(lay,QStringLiteral("Drawing"),makeDrawingDetailsButton(page));
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("Annotation"));
}

} // namespace Loopwire
