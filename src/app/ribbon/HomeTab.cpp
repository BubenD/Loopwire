// The Home ribbon tab. Split out of MainWindow.cpp so each tab is one file: the ribbon was the
// largest thing in that file and the place every new feature landed. Still MainWindow members, so
// nothing about what they build or reach has changed.
#include <functional>
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
#include <QInputDialog>
#include <QStatusBar>
#include <QToolButton>
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
// AN EYE, for the button that shows or hides the marks. Drawn as an eye rather than lettered, because what it does
// is "look at this" and there is no shorter way to say that.
QIcon modelMarksEyeIcon() {
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#d6dae1"));
    pen.setWidthF(1.8);
    p.setPen(pen);
    // The lid, as two arcs meeting at the corners of the eye, and the pupil.
    QPainterPath eye;
    eye.moveTo(4, 14);
    eye.quadTo(14, 4, 24, 14);
    eye.quadTo(14, 24, 4, 14);
    p.drawPath(eye);
    p.setBrush(QColor("#d6dae1"));
    p.drawEllipse(QPointF(14, 14), 3.0, 3.0);
    return QIcon(pm);
}
} // namespace

// The square a model-session button takes: an icon and up to two short lines under it. Big enough that the
// words are read rather than guessed at, and the same for all five, so the row reads as one group.
constexpr QSize kModelSessionCell(84, 66);

// THE POLYLINE'S OPTIONS (AutoCAD's PLINE), while it is being drawn: line or arc segments, its width, close it, take back
// the last point - or turn what is drawn into a double line, or close it into a wipeout.
QWidget* MainWindow::polylineOptionsGroup(QHBoxLayout* lay, QWidget* page) {
    auto* w = new QWidget(page);
    auto* grid = new QGridLayout(w);
    grid->setContentsMargins(2, 0, 2, 0);
    grid->setSpacing(2);
    auto button = [&](const char* name, const QString& text, const QString& tip, int row, int col, std::function<void()> fn) {
        auto* b = new QToolButton(w);
        b->setObjectName(QLatin1String(name));
        b->setText(text);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, [fn] { fn(); });
        grid->addWidget(b, row, col);
        return b;
    };
    auto typed = [this](const QString& word) { return [this, word] { if (editor_) editor_->acceptCommandInput(word); }; };
    button("plineLine", QStringLiteral("Line"), QStringLiteral("Straight segments from here [L]"), 0, 0, typed(QStringLiteral("l")));
    button("plineArc", QStringLiteral("Arc"), QStringLiteral("Arc segments, each tangent to the last [A]"), 1, 0, typed(QStringLiteral("a")));
    button("plineClose", QStringLiteral("Close"), QStringLiteral("Close it back to its start [C]"), 0, 1, typed(QStringLiteral("c")));
    button("plineUndo", QStringLiteral("Undo point"), QStringLiteral("Take back the last point [U]"), 1, 1, typed(QStringLiteral("u")));
    button("plineDouble", QStringLiteral("Double line"), QStringLiteral("Make what is drawn a double line, and carry on"), 0, 2,
           [this] { if (editor_) editor_->polylineAsDoubleLine(); });
    button("plineWipeout", QStringLiteral("Wipeout"), QStringLiteral("Close what is drawn into a wipeout"), 1, 2,
           [this] { if (editor_) editor_->polylineAsWipeout(); });
    auto* caption = new QLabel(QStringLiteral("Width"), w);
    grid->addWidget(caption, 0, 3);
    auto* width = new QDoubleSpinBox(w);
    width->setObjectName(QStringLiteral("plineWidth"));
    width->setRange(0.0, 500.0);
    width->setDecimals(2);
    width->setSuffix(QStringLiteral(" mm"));
    width->setKeyboardTracking(false);
    width->setToolTip(QStringLiteral("How wide the polyline is drawn on the sheet [W] - 0 a plain line"));
    connect(width, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (editor_) editor_->setPolylineWidth(v); });
    grid->addWidget(width, 1, 3);
    QWidget* group = addGroupKeep(lay, QStringLiteral("Polyline"), w);
    group->setVisible(false);
    connect(editor_, &Editor::toolChanged, group, [group, width, this](ToolId t) {
        group->setVisible(t == ToolId::Polyline);
        const QSignalBlocker hold(width);
        width->setValue(editor_->polylineWidth());
    });
    return group;
}

// ---- HOME : Draw | Modify | Properties | AI ----
void MainWindow::buildHomeTab() {
    auto [page, lay] = makeTabPage();
    QAction* sel = makeToolAction(QStringLiteral("Select"), ToolId::Select);
    sel->setChecked(true);
    // LINE (a line a segment, going on from each end), DOUBLE LINE, POLYLINE - and the rest; Rectangle and Ray each a
    // button of variants, as Circle is.
    addGroup(lay, QStringLiteral("Draw"),
             toolRow({ToolId::Line, ToolId::DoublePolyline, ToolId::Polyline, ToolId::Rect, ToolId::Circle,
                      ToolId::Ellipse, ToolId::Arc, ToolId::Spline, ToolId::Polygon, ToolId::Ray,
                      ToolId::Hatch, ToolId::Wipeout}));
    polylineOptionsGroup(lay, page);
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
        // A callout is a LEADER: an arrow that points at something and says a word about it. Whether it
        // carries a numbered balloon is a choice about that one callout, not a different tool - and Home
        // offered only the balloon, so the plain one could not be reached from the tab most work is done
        // on. Clicking draws one without a balloon; the menu offers the balloon.
        auto* leaderBtn = new QToolButton(page);
        leaderBtn->setIcon(makeToolIcon(ToolId::TextLeader));
        leaderBtn->setIconSize(QSize(28, 28));
        leaderBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        leaderBtn->setAutoRaise(true);
        leaderBtn->setFocusPolicy(Qt::NoFocus);
        leaderBtn->setToolTip(QStringLiteral(
            "Leader — click what it points at, then where its words go. The menu draws one with a "
            "numbered balloon instead."));
        leaderBtn->setPopupMode(QToolButton::MenuButtonPopup);
        connect(leaderBtn, &QToolButton::clicked, this,
                [this] { editor_->setTool(ToolId::TextLeader); });
        {
            auto* lm = new QMenu(leaderBtn);
            QAction* a = lm->addAction(makeToolIcon(ToolId::TextLeader),
                                       QStringLiteral("Leader — without a balloon"));
            connect(a, &QAction::triggered, this, [this] { editor_->setTool(ToolId::TextLeader); });
            a = lm->addAction(makeToolIcon(ToolId::Balloon),
                              QStringLiteral("Leader — with a numbered balloon"));
            connect(a, &QAction::triggered, this, [this] { editor_->setTool(ToolId::Balloon); });
            leaderBtn->setMenu(lm);
        }
        // The centre marks belong here as well as on the Annotation tab: they are used while drawing, and
        // a tool that is only on a tab you have to go to is a tool nobody uses.
        addGroup(lay, QStringLiteral("Annotation"),
                 toolRow({ToolId::Text, ToolId::Dimension, ToolId::Note, ToolId::Revision,
                          ToolId::CenterMark, ToolId::CenterLine, ToolId::DetailView, ToolId::Arrow},
                         {leaderBtn, makeNoteMarkButton(page, QStringLiteral("homeNoteMark")), tb,
                          makeDrawingDetailsButton(page, true)}));
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
        {
            QToolButton* edit = makeBlockButton(bw, BlockAction::Edit);
            edit->setObjectName(QStringLiteral("editBlockButton"));
            bg->addWidget(edit, 0, 3);
        }
        saveBlockBtn_ = makeBlockButton(bw, BlockAction::SaveEdit);
        bg->addWidget(saveBlockBtn_, 0, 4);
        cancelBlockBtn_ = makeBlockButton(bw, BlockAction::CancelEdit);
        bg->addWidget(cancelBlockBtn_, 0, 5);
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
        // AND TAKEN OFF AGAIN. A mark in the wrong place could only be moved, never removed - and a model is
        // placed BY its marks, so one that should not be there is not a blemish, it is a part put down in the
        // wrong place. Each button that puts a mark on now offers to take it off.
        auto clearable = [this](QToolButton* b, const QString& put, const QString& take,
                                std::function<void()> onPut, std::function<void()> onTake) {
            b->setPopupMode(QToolButton::MenuButtonPopup);
            auto* m = new QMenu(b);
            QAction* a = m->addAction(put);
            connect(a, &QAction::triggered, this, [onPut] { onPut(); });
            a = m->addAction(take);
            connect(a, &QAction::triggered, this, [onTake] { onTake(); });
            b->setMenu(m);
        };
        connect(markConnectMechModelBtn_, &QToolButton::clicked, this,
                [this] { armMechModelConnect(); });
        clearable(markConnectMechModelBtn_, tr("Mark the connection point"),
                  tr("Take the connection point off"), [this] { armMechModelConnect(); },
                  [this] { clearMechModelConnect(); });
        sessionButton(markNearMechModelBtn_, tr("Near\npoint"), modelAnchorIcon(QColor("#e6a23c")),
                      tr("The near end of the model - marked in orange"));
        connect(markNearMechModelBtn_, &QToolButton::clicked, this, [this] { armMechModelAnchor(true); });
        clearable(markNearMechModelBtn_, tr("Mark the near point"), tr("Take the near point off"),
                  [this] { armMechModelAnchor(true); }, [this] { clearMechModelAnchor(true); });
        sessionButton(markFarMechModelBtn_, tr("Far\npoint"), modelAnchorIcon(QColor("#62c370")),
                      tr("The far end of the model - marked in green"));
        connect(markFarMechModelBtn_, &QToolButton::clicked, this, [this] { armMechModelAnchor(false); });
        clearable(markFarMechModelBtn_, tr("Mark the far point"), tr("Take the far point off"),
                  [this] { armMechModelAnchor(false); }, [this] { clearMechModelAnchor(false); });
        // The faces the wires leave by. A split has one per branch, so the button stays armed and the
        // fall-menu is where the list is started again.
        sessionButton(markOutputMechModelBtn_, tr("Output\nface"), modelAnchorIcon(QColor("#c678dd")),
                      tr("Click across the face the wires come out of - the back of a connector, the "
                         "mouth of a backshell, each branch of a split. The wires are spread ALONG that "
                         "line and may not run off either end of it, which a connection point alone "
                         "cannot say. One line per branch, in the order the generator hands them out."));
        markOutputMechModelBtn_->setPopupMode(QToolButton::MenuButtonPopup);
        connect(markOutputMechModelBtn_, &QToolButton::clicked, this,
                [this] { armMechModelOutput(); });
        {
            auto* om = new QMenu(markOutputMechModelBtn_);
            QAction* a = om->addAction(tr("Mark an output face"));
            connect(a, &QAction::triggered, this, [this] { armMechModelOutput(); });
            a = om->addAction(tr("Clear the marked faces"));
            connect(a, &QAction::triggered, this, [this] { clearMechModelOutputs(); });
            markOutputMechModelBtn_->setMenu(om);
        }
        // WHICH OBJECT THE APP SCALES THE MODEL BY. A model is scaled to fit what it is placed on, and
        // "the whole drawing" is the wrong answer as soon as a model is a thing inside another thing: a
        // label under clear heatshrink is a label with a sleeve over it, and what has to match the bundle
        // is the LABEL. Scale the pair together and the sleeve fits the bundle while the label comes out
        // too small, which is exactly backwards.
        sessionButton(markScaleRefMechModelBtn_, tr("Scale\nby"), modelAnchorIcon(QColor("#2bd0ff")),
                      tr("Click the object the app scales this model by - the label inside a sleeve, not "
                         "the sleeve. Everything else in the drawing is carried along by the same scale, "
                         "so a sleeve stays a sleeve and ends up standing outside the label. Nothing "
                         "marked means the whole drawing, which is right for a model that is one thing."));
        connect(markScaleRefMechModelBtn_, &QToolButton::clicked, this,
                [this] { armMechModelScaleRef(); });
        clearable(markScaleRefMechModelBtn_, tr("Mark what it is scaled by"),
                  tr("Take that mark off — scale by the whole drawing"),
                  [this] { armMechModelScaleRef(); }, [this] { clearMechModelScaleRef(); });
        sg->addWidget(captureMechModelBtn_, 0, 0);
        sg->addWidget(cancelMechModelBtn_, 0, 1);
        sg->addWidget(markConnectMechModelBtn_, 0, 2);
        sg->addWidget(markNearMechModelBtn_, 0, 3);
        sg->addWidget(markFarMechModelBtn_, 0, 4);
        sg->addWidget(markOutputMechModelBtn_, 0, 5);
        // THE EYE. The marks are chrome over the part, and while the part itself is being drawn they are in the
        // way of seeing it - so they go, and come back. Checkable, because a toggle that does not show its state
        // is a button you press twice to find out what it did.
        sessionButton(showMechModelMarksBtn_, tr("Show\nmarks"), modelMarksEyeIcon(),
                      tr("Show or hide the marks - the connection point, the ends, the output faces and what "
                         "the model is scaled by. They are drawn over the part, and while the part itself is "
                         "being drawn they are in the way of seeing it."));
        showMechModelMarksBtn_->setCheckable(true);
        showMechModelMarksBtn_->setChecked(true);
        connect(showMechModelMarksBtn_, &QToolButton::clicked, this, [this] { toggleMechModelMarks(); });
        sg->addWidget(markScaleRefMechModelBtn_, 0, 6);
        // EVERY MARK OFF AT ONCE: the connection point, both ends, the output faces (and, on a default
        // model, what it is scaled by) - to start the marking again rather than take them off one by one.
        sessionButton(clearMarksMechModelBtn_, tr("Remove\nall"), modelAnchorIcon(QColor("#e06c6c")),
                      tr("Take every mark off - the connection point, the near and far points and the output "
                         "faces - to mark the part again from the start"));
        clearMarksMechModelBtn_->setObjectName(QStringLiteral("removeAllMarks"));
        connect(clearMarksMechModelBtn_, &QToolButton::clicked, this, [this] { clearAllMechModelMarks(); });
        sg->addWidget(clearMarksMechModelBtn_, 0, 7);
        sg->addWidget(showMechModelMarksBtn_, 0, 8);
        mechModelSessionGroup_ = new QWidget(page);
        auto* hg = new QHBoxLayout(mechModelSessionGroup_);
        hg->setContentsMargins(0, 0, 0, 0);
        hg->setSpacing(0);
        {
            QWidget* group = ribbonGroup(QStringLiteral("Default model"), session);
            // Retitled for what is being edited: "Connection point" on a library part - see
            // updateMechanicalRibbonGroups.
            mechModelGroupTitle_ = group->findChild<QLabel*>(QStringLiteral("ribbonGroupTitle"));
            hg->addWidget(group);
        }
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

} // namespace Loopwire
