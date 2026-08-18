// The Harness mechanical ribbon tab. Split out of MainWindow.cpp so each tab is one file: the ribbon was the
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
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>

namespace Edraw_Desktop {

namespace {
// Room for an icon and two short lines of caption. The same idea as the model session's own cell: a row of
// buttons of different widths reads as a row of different things.
// Icon only, so the section is a row of pictures rather than a wall of two-line captions. The name is on
// hover instead - which is where every other icon-only group on the ribbon puts it.
constexpr QSize kSpecialCell(40, 40);
} // namespace

// ---- HARNESS MECHANICAL ---- the physical side of the harness: the bundle and its
//      branches drawn to shape, their lengths dimensioned, and the parts called out.
//      (The schematic side lives on Schematic build.) ----
void MainWindow::buildHarnessMechanicalTab() {
    auto [page, lay] = makeTabPage();
    // The bundle used to have a group of its own - a double polyline for the body, a plain one for a
    // centreline, Offset and Fillet - but every one of those four is in Draw and Modify below, so it
    // was the same four buttons on the tab twice.
    mechGroups_.clear();
    mechModelHiddenGroups_.clear();
    // The general drawing and editing tools, the same ones Home carries. A mechanical layout is drawn
    // with them as much as with the harness tools - a bracket, a panel cut-out, a centreline, a fillet
    // on a branch - and having to cross to another tab for a line and back for a break mark is the kind
    // of thing that makes a drawing take twice as long as it should.
    mechGroups_ << addGroupKeep(lay, QStringLiteral("Draw"),
                               toolRow({ToolId::Line, ToolId::DoubleLine, ToolId::Polyline,
                                        ToolId::DoublePolyline, ToolId::Rect, ToolId::Circle,
                                        ToolId::Ellipse, ToolId::Arc, ToolId::Spline, ToolId::Polygon,
                                        ToolId::Hatch, ToolId::Wipeout}));
    // The modify tools, Match, Array and Copy: twelve buttons in the same two rows of six, as on Home.
    // Array and Copy standing beside the grid at their own widths is what made this group read as three
    // groups.
    mechGroups_ << addGroupKeep(
        lay, QStringLiteral("Modify"),
        toolRow({ToolId::Move, ToolId::Rotate, ToolId::Mirror, ToolId::Scale, ToolId::Offset,
                 ToolId::Trim, ToolId::Extend, ToolId::Fillet, ToolId::Chamfer},
                {makeMatchBtn(page, /*iconOnly=*/true), makeArrayButton(page),
                 clipboardRow(/*iconOnly=*/true)}));
    {
        // Annotation: Home's group, with the leader the harness tools use folded in - one group with
        // every way of writing on the drawing, rather than the same balloon on the tab twice. The Table
        // button goes in the grid with them: seven buttons, two rows.
        auto* tb = new QToolButton(page);
        tb->setIcon(makeToolIcon(ToolId::Table));
        tb->setIconSize(QSize(28, 28));
        tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
        tb->setAutoRaise(true);
        tb->setFocusPolicy(Qt::NoFocus);
        tb->setToolTip(QStringLiteral("Insert Table"));
        connect(tb, &QToolButton::clicked, this, [this] { insertTableCommand(); });
        mechGroups_ << addGroupKeep(
            lay, QStringLiteral("Annotation"),
            toolRow({ToolId::Text, ToolId::Dimension, ToolId::Balloon, ToolId::TextLeader, ToolId::Note,
                     ToolId::Revision, ToolId::DetailView},
                    {tb}));
    }
    // SPECIAL: the marks and symbols that belong to a harness drawing and to nothing else - the break
    // that says a run is longer than the paper, and the twisted pair. They are not drawing tools and
    // they are not annotation; they are the conventions of this kind of drawing, so they have their own
    // place on the tab rather than being scattered among the general tools.
    {
        auto* lenW = new QWidget(page);
        // TWO ROWS, not one. Six wide buttons in a single row made this the widest group on the tab by a
        // long way, pushing the data group off the end of the ribbon on a narrow window. The pieces of
        // furniture a mechanical drawing needs sit two deep like every other group here.
        auto* lg = new QGridLayout(lenW);
        lg->setContentsMargins(0, 0, 0, 0);
        lg->setSpacing(2);
        int specialAt = 0;
        // ONE SIZE for every button here, wide enough for two short lines under the icon.
        //
        // Sizing each to its own icon let the captions be cut off - "Twisted harness", "Bill of materials",
        // "Label under clear heatshrink" are all wider than a 26-pixel icon, and a caption that cannot be
        // read is worse than none. A column minimum was not enough: a button is free to be narrower than its
        // column, and Qt elides the text inside it rather than growing. Fixed, so each is the same and each
        // has room - the same thing the model-session buttons do, for the same reason.
        auto sizeCell = [](QWidget* w) {
            auto oneButton = [](QToolButton* b) {
                // The caption becomes the TOOLTIP, unless the button already had one worth more than the
                // caption - the label button's tip explains what a label under clear heatshrink is for, and
                // that is not something to throw away for the word "Label".
                QString caption = b->text();
                caption.replace(QLatin1Char('\n'), QLatin1Char(' '));
                caption = caption.trimmed();
                if (b->toolTip().isEmpty() && !caption.isEmpty()) b->setToolTip(caption);
                b->setToolButtonStyle(Qt::ToolButtonIconOnly);
                b->setIconSize(QSize(26, 26));
                b->setFixedSize(kSpecialCell);
            };
            for (QToolButton* b : w->findChildren<QToolButton*>()) oneButton(b);
            if (auto* b = qobject_cast<QToolButton*>(w)) oneButton(b);
        };
        auto lgAdd = [&](QWidget* w) {
            sizeCell(w);
            lg->addWidget(w, specialAt % 2, specialAt / 2);
            ++specialAt;
        };
        // The break mark, in either style. A harness is drawn shorter than it is built - a two-metre
        // loom on an A3 sheet - and the mark is how a drawing says so: the run is interrupted, the
        // dimension says the real length.
        //
        // ONE button with the styles hung off it, rather than one button per style: the button places
        // the mark, and the arrow beside it says which kind - straight strokes to begin with, since
        // that is the one a mechanical layout uses. Whichever is chosen stays chosen, and the button
        // wears it, so the next placement needs no menu at all.
        auto* breakW = new QWidget(lenW);
        auto* bg = new QGridLayout(breakW);
        bg->setContentsMargins(0, 0, 0, 0);
        bg->setSpacing(2);
        auto* breakBtn = new QToolButton(breakW);
        breakBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        breakBtn->setIconSize(QSize(26, 26));
        breakBtn->setAutoRaise(true);
        breakBtn->setFocusPolicy(Qt::NoFocus);
        breakBtn->setPopupMode(QToolButton::MenuButtonPopup);
        auto* breakMenu = new QMenu(breakBtn);
        breakBtn->setMenu(breakMenu);
        // The icon of a style, drawn as the thing it places.
        auto styleIcon = [](bool curved) {
            QPixmap pm(28, 28);
            pm.fill(Qt::transparent);
            QPainter ip(&pm);
            ip.setRenderHint(QPainter::Antialiasing, true);
            ip.setPen(QPen(QColor("#cfe3ff"), 1.6));
            ip.drawLine(2, 14, 26, 14);
            for (int k = 0; k < 2; ++k) {
                const double x = 11.0 + k * 7.0;
                if (!curved) {
                    ip.drawLine(QPointF(x + 3, 5), QPointF(x - 3, 23));
                } else {
                    QPainterPath path(QPointF(x + 3, 5));
                    path.cubicTo(QPointF(x - 2, 9), QPointF(x + 3, 13), QPointF(x, 14));
                    path.cubicTo(QPointF(x - 3, 15), QPointF(x + 2, 19), QPointF(x - 3, 23));
                    ip.drawPath(path);
                }
            }
            ip.end();
            return QIcon(pm);
        };
        struct Style {
            const char* key;
            const char* name;
            const char* tip;
            bool curved;
        };
        static const Style kStyles[] = {
            {"straight", "Straight strokes",
             "Two straight strokes across the run - the form a mechanical layout uses.", false},
            {"curved", "S-curves",
             "The same mark drawn as two S-curves - the form used where the run itself is drawn as a "
             "curve.", true},
        };
        // Shown on the button: what the next click will place.
        auto showStyle = [this, breakBtn, styleIcon](const QString& key, const char* name) {
            editor_->breakStyle() = key;
            breakBtn->setIcon(styleIcon(key == QLatin1String("curved")));
            breakBtn->setText(QStringLiteral("Break\n%1").arg(
                key == QLatin1String("curved") ? QStringLiteral("curved") : QStringLiteral("straight")));
            breakBtn->setToolTip(
                QStringLiteral("Mark a run as longer than it is drawn: the run is interrupted and the "
                               "dimension says its real length. Click on the run - the mark lies square "
                               "across it, sized to it, and slides along it afterwards.\n\nStyle: %1")
                    .arg(QString::fromLatin1(name)));
        };
        for (const Style& s : kStyles) {
            const QString key = QString::fromLatin1(s.key);
            const char* name = s.name;
            QAction* act = breakMenu->addAction(styleIcon(s.curved), QString::fromLatin1(s.name));
            act->setToolTip(QString::fromLatin1(s.tip));
            connect(act, &QAction::triggered, this, [this, key, name, showStyle] {
                showStyle(key, name);
                editor_->setTool(ToolId::LengthBreak);
                syncBreakOptions();   // the third field is named for the style being placed
                statusBar()->showMessage(
                    QStringLiteral("Draw a line across the runs to break — the mark follows what it crosses"),
                    4000);
            });
        }
        showStyle(QStringLiteral("straight"), kStyles[0].name);   // the default
        connect(breakBtn, &QToolButton::clicked, this, [this] {
            editor_->setTool(ToolId::LengthBreak);
            statusBar()->showMessage(
                QStringLiteral("Draw a line across the runs to break — the mark follows what it crosses"),
                4000);
        });
        bg->addWidget(breakBtn, 0, 0);
        // The mark's own numbers, beside the button that places it and only while that tool is armed:
        // what a break looks like is decided as it is placed. The third field is the one that differs
        // between the styles - a straight stroke has a LENGTH, an S has a reach - so it is relabelled
        // rather than duplicated.
        {
            auto* optW = new QWidget(breakW);
            auto* og = new QGridLayout(optW);
            og->setContentsMargins(6, 0, 0, 0);
            og->setSpacing(2);
            auto field = [&](const QString& text, double lo, double hi, double step,
                             QDoubleSpinBox** out, int row) {
                auto* l = new QLabel(text, optW);
                l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
                auto* s = new QDoubleSpinBox(optW);
                s->setRange(lo, hi);
                s->setSingleStep(step);
                s->setDecimals(1);
                s->setMinimumWidth(66);
                s->setSpecialValueText(QStringLiteral("auto"));
                og->addWidget(l, row, 0);
                og->addWidget(s, row, 1);
                *out = s;
                return s;
            };
            field(QStringLiteral("Distance between"), 0.0, 500.0, 1.0, &breakGapSpin_, 0)
                ->setToolTip(QStringLiteral(
                    "How far apart the two strokes are drawn. On auto: a proportion of the reach."));
            field(QStringLiteral("Length"), 0.0, 2000.0, 1.0, &breakSizeSpin_, 1)
                ->setToolTip(QStringLiteral(
                    "How far the mark reaches across the run. On auto: as wide as the run it is placed "
                    "on, with a margin either side."));
            breakSizeLabel_ = qobject_cast<QLabel*>(og->itemAtPosition(1, 0)->widget());
            field(QStringLiteral("Angle"), -1.0, 75.0, 5.0, &breakAngleSpin_, 2)
                ->setToolTip(QStringLiteral(
                    "How far the strokes lean out of square, in degrees. On auto: the lean each style "
                    "is drawn with."));
            connect(breakGapSpin_, &QDoubleSpinBox::valueChanged, this,
                    [this](double v) { editor_->breakGap() = v; });
            connect(breakSizeSpin_, &QDoubleSpinBox::valueChanged, this,
                    [this](double v) { editor_->breakSize() = v; });
            connect(breakAngleSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
                editor_->breakAngle() = v < -0.5 ? -1.0 : v;   // the bottom of the range is "auto"
            });
            optW->setVisible(false);
            breakOptionsGroup_ = optW;
            bg->addWidget(optW, 0, 1);
        }
        connect(editor_, &Editor::toolChanged, this, [this](ToolId) { syncBreakOptions(); });
        lgAdd(breakW);
        // The twisted pair, beside it: two wires laid up round each other, drawn between the two points
        // it runs between. Straight at both ends, because that is where a pair is worked on - stripped,
        // crimped, spliced - and twisted in between, which is what the drawing is saying.
        {
            auto* tpBtn = new QToolButton(lenW);
            QAction* act = makeToolAction(toolLabel(ToolId::TwistedPair), ToolId::TwistedPair);
            tpBtn->setDefaultAction(act);
            tpBtn->setText(QStringLiteral("Twisted\npair"));
            tpBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            tpBtn->setIconSize(QSize(26, 26));
            tpBtn->setAutoRaise(true);
            tpBtn->setFocusPolicy(Qt::NoFocus);
            tpBtn->setToolTip(QStringLiteral(
                "Draw a twisted pair: click the two ends. The wires run straight at both ends and lay "
                "up round each other in between."));
            lgAdd(tpBtn);
        }
        // And the same pair routed: as many knees as the run needs, drawn like a polyline. A pair in a
        // harness rarely goes straight from one end to the other - it follows the loom round the
        // corners - and drawing it as a string of separate pairs leaves a join at every knee.
        {
            auto* trBtn = new QToolButton(lenW);
            QAction* act = makeToolAction(toolLabel(ToolId::TwistedRoute), ToolId::TwistedRoute);
            trBtn->setDefaultAction(act);
            trBtn->setText(QStringLiteral("Twisted\nroute"));
            trBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            trBtn->setIconSize(QSize(26, 26));
            trBtn->setAutoRaise(true);
            trBtn->setFocusPolicy(Qt::NoFocus);
            trBtn->setToolTip(QStringLiteral(
                "Route a twisted pair through as many knees as it needs: click each knee, Enter "
                "finishes. Straight at both ends, woven in between, all one length of pair."));
            lgAdd(trBtn);
        }
        // And a whole bundle: every wire of it laid up together, however many it fans out to. Two wires
        // is a pair, so this is the same drawing with a count on it rather than a second one.
        {
            auto* thW = new QWidget(lenW);
            auto* tg = new QHBoxLayout(thW);
            tg->setContentsMargins(0, 0, 0, 0);
            tg->setSpacing(2);
            auto* thBtn = new QToolButton(thW);
            QAction* act = makeToolAction(toolLabel(ToolId::TwistedHarness), ToolId::TwistedHarness);
            thBtn->setDefaultAction(act);
            thBtn->setText(QStringLiteral("Twisted\nharness"));
            thBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            thBtn->setIconSize(QSize(26, 26));
            thBtn->setAutoRaise(true);
            thBtn->setFocusPolicy(Qt::NoFocus);
            thBtn->setToolTip(QStringLiteral(
                "Draw a twisted harness: click the two ends and every wire of the bundle is laid up "
                "together, fanning out to straight ends. Tab pins it so the number of wires can be set "
                "with it on screen."));
            tg->addWidget(thBtn);
            // Its one setting, beside the button and only while the tool is armed.
            {
                auto* optW = new QWidget(thW);
                auto* og = new QGridLayout(optW);
                og->setContentsMargins(6, 0, 0, 0);
                og->setSpacing(2);
                auto* l = new QLabel(QStringLiteral("Wires"), optW);
                l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistWiresSpin_ = new QSpinBox(optW);
                twistWiresSpin_->setRange(2, kTwistedMaxWires);
                twistWiresSpin_->setValue(editor_->twistWires());
                twistWiresSpin_->setMinimumWidth(56);
                twistWiresSpin_->setToolTip(QStringLiteral(
                    "How many wires the bundle fans out to, 2 to %1. Two wires is a twisted pair.")
                        .arg(kTwistedMaxWires));
                og->addWidget(l, 0, 0);
                og->addWidget(twistWiresSpin_, 0, 1);
                connect(twistWiresSpin_, &QSpinBox::valueChanged, this, [this](int v) {
                    editor_->twistWires() = v;
                    canvas_->update();   // a pinned bundle re-draws with the new count
                    statusBar()->showMessage(
                        QStringLiteral("Twisted harness: %1 wires").arg(v), 2000);
                });
                optW->setVisible(false);
                twistOptionsGroup_ = optW;
                tg->addWidget(optW);
            }
            lgAdd(thW);
        }
        // The stock parts themselves, drawn at size: what a resistor across a pair or a ferrite over
        // the bundle actually takes up on the board. They belong here for the same reason the break
        // mark does - they are the furniture of this kind of drawing.
        lgAdd(makePartSymbolButton(lenW, /*schematic=*/false));
        // A LABEL on a run, and the same label covered over with clear heatshrink - which is how a label
        // that has to survive a wash or a wipe-down is actually built. One button with the two hung off it
        // rather than a button each: it places a label, and the arrow says which sort. Whichever is chosen
        // stays chosen and the button wears it, so the next one needs no menu at all.
        {
            auto* labelBtn = new QToolButton(lenW);
            labelBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            labelBtn->setIconSize(QSize(26, 26));
            labelBtn->setAutoRaise(true);
            labelBtn->setFocusPolicy(Qt::NoFocus);
            labelBtn->setPopupMode(QToolButton::MenuButtonPopup);
            labelBtn->setText(QStringLiteral("Label"));
            auto* lm = new QMenu(labelBtn);
            // Two sorts of label, each with the short name the button wears and the long one the menu
            // shows. Kept apart so neither has to carry a line break through a string.
            struct Sort { MechModelKind kind; const char* onButton; const char* inMenu;
                          const char* tip; };
            const Sort sorts[2] = {
                {MechModelKind::Label, "Label", "Label",
                 "A label on the run, as this office draws one"},
                {MechModelKind::LabelUnderHeatshrink, "Label+", "Label under clear heatshrink",
                 "A label covered over with clear heatshrink - how one that has to survive a wash or a "
                 "wipe-down is actually built"}};
            for (const Sort& sort : sorts) {
                const QIcon ic = makeMechModelIcon(sort.kind);
                QAction* a = lm->addAction(ic, QString::fromLatin1(sort.inMenu));
                a->setToolTip(QString::fromLatin1(sort.tip));
                const MechModelKind kind = sort.kind;
                const QString text = QString::fromLatin1(sort.onButton);
                const QString tip = QString::fromLatin1(sort.tip);
                connect(a, &QAction::triggered, this, [this, labelBtn, kind, ic, text, tip] {
                    labelBtn->setIcon(ic);            // the button wears what was chosen
                    labelBtn->setText(text);          // kept for the tooltip, not shown on the button
                    labelBtn->setToolTip(tip);
                    // WHICH sort, held on the button rather than read back out of its caption: the caption
                    // is not shown any more, and a choice remembered as display text is a choice that
                    // breaks the moment the display changes.
                    labelBtn->setProperty("mechKind", static_cast<int>(kind));
                    placeMechModel(kind);
                });
            }
            labelBtn->setIcon(makeMechModelIcon(sorts[0].kind));   // the first of the list, to begin with
            labelBtn->setToolTip(QString::fromLatin1(sorts[0].tip));
            labelBtn->setMenu(lm);
            labelBtn->setProperty("mechKind", static_cast<int>(sorts[0].kind));
            connect(labelBtn, &QToolButton::clicked, this, [this, labelBtn] {
                placeMechModel(static_cast<MechModelKind>(labelBtn->property("mechKind").toInt()));
            });
            lgAdd(labelBtn);
        }
        // Room for the WORDS. Two rows of buttons sized to their icons cut the labels off - "Twisted
        // route" and "Label under..." are wider than a 26-pixel icon - and a label that cannot be read is
        // worse than no label. Each column is given enough for a two-line caption, so the group is as wide
        // as it needs to be and no wider.
        for (int col = 0; col <= (specialAt - 1) / 2; ++col) lg->setColumnMinimumWidth(col, 78);
        mechBreakGroup_ = addGroupKeep(lay, QStringLiteral("Special"), lenW);
        mechGroups_ << mechBreakGroup_;
    }
    {
        // THE LABEL, once one is on: the two numbers a stroke cannot settle by itself.
        //
        // The stroke settles the WIDTH - across the runs, the dimension that has to cover the bundle -
        // because it says which runs are meant, and that is right nearly every time. It says nothing about
        // the HEIGHT, how far the label reaches along the wire: no amount of looking at the bundle tells you
        // that, and it cannot know this office's labels are 12 mm whatever they happen to cover.
        //
        // Both fields work on the preview as well as on a label already down, which is why Tab pins the
        // stroke: setting a label's size only after it is placed means placing it at the wrong size first,
        // and that is the one moment you least know what size it should be. It is a group of its own and it
        // takes the tab while it is up, because two fields among forty buttons is two fields nobody finds.
        auto* labW = new QWidget(page);
        auto* lfg = new QGridLayout(labW);
        lfg->setContentsMargins(6, 2, 6, 2);
        lfg->setSpacing(3);
        auto field = [&](const QString& text, const QString& tip, QDoubleSpinBox** out, int row) {
            auto* l = new QLabel(text, labW);
            l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
            auto* sp = new QDoubleSpinBox(labW);
            sp->setRange(0.1, 5000.0);
            sp->setSingleStep(1.0);
            sp->setDecimals(1);
            sp->setSuffix(QStringLiteral(" mm"));
            sp->setMinimumWidth(84);
            sp->setToolTip(tip);
            l->setToolTip(tip);
            lfg->addWidget(l, row, 0);
            lfg->addWidget(sp, row, 1);
            *out = sp;
        };
        field(QStringLiteral("Width"),
              QStringLiteral("ACROSS the runs — the dimension that has to cover the bundle. This is what "
                             "the stroke measures, over the outermost wire it crossed."),
              &labelWidthSpin_, 0);
        field(QStringLiteral("Height"),
              QStringLiteral("ALONG the run — how far the label reaches up the wire. No amount of looking "
                             "at the bundle tells you this, so it starts at the size labels come in."),
              &labelHeightSpin_, 1);
        connect(labelWidthSpin_, &QDoubleSpinBox::valueChanged, this,
                [this](double v) { editor_->setLabelWidth(v); });
        connect(labelHeightSpin_, &QDoubleSpinBox::valueChanged, this,
                [this](double v) { editor_->setLabelHeight(v); });
        // Done. The label stays where it is; the tab comes back.
        auto* done = new QToolButton(labW);
        done->setText(QStringLiteral("Done"));
        done->setToolButtonStyle(Qt::ToolButtonTextOnly);
        done->setAutoRaise(true);
        done->setFocusPolicy(Qt::NoFocus);
        done->setToolTip(QStringLiteral("Finish fitting the label and bring the tab back"));
        connect(done, &QToolButton::clicked, this, [this] { editor_->endLabelEdit(); });
        lfg->addWidget(done, 0, 2, 2, 1);
        labelFitGroup_ = addGroupKeep(lay, QStringLiteral("Label"), labW);
        labelFitGroup_->setVisible(false);
        connect(editor_, &Editor::labelChanged, this, [this] { syncLabelOptions(); });
    }
    {
        // The parts behind the drawing: the same bill of materials the "bom" command opens.
        auto* dataW = new QWidget(page);
        auto* dg = new QGridLayout(dataW);
        dg->setContentsMargins(0, 0, 0, 0);
        dg->setSpacing(2);
        auto* bomBtn = new QToolButton(dataW);
        bomBtn->setText(QStringLiteral("Bill of\nmaterials"));
        bomBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        bomBtn->setIconSize(QSize(26, 26));
        bomBtn->setMinimumWidth(84);
        bomBtn->setAutoRaise(true);
        bomBtn->setFocusPolicy(Qt::NoFocus);
        bomBtn->setToolTip(QStringLiteral(
            "Every connector, wire and accessory in the drawing, counted"));
        {
            // A short parts list: a heading rule and three rows.
            QPixmap pm(28, 28);
            pm.fill(Qt::transparent);
            QPainter ip(&pm);
            ip.setRenderHint(QPainter::Antialiasing, true);
            ip.setPen(QPen(QColor("#8fb7ff"), 1.6));
            ip.drawRect(4, 4, 20, 20);
            ip.drawLine(4, 10, 24, 10);
            ip.setPen(QPen(QColor("#7ee0a0"), 1.4));
            for (int i = 0; i < 3; ++i) ip.drawLine(7, 14 + i * 4, 21, 14 + i * 4);
            ip.end();
            bomBtn->setIcon(QIcon(pm));
        }
        connect(bomBtn, &QToolButton::clicked, this, [this] { showBomDialog(); });
        dg->addWidget(bomBtn, 0, 0);
        dg->addWidget(makePartsLibraryButton(dataW), 0, 1);
        // The house style sits with the library: both are what the drawings are made OF, as against
        // the windows that make a drawing.
        dg->addWidget(makeMechModelsButton(dataW), 0, 2);
        QWidget* dataGroup = addGroupKeep(lay, QStringLiteral("Data"), dataW);
        mechGroups_ << dataGroup;
        mechModelHiddenGroups_ << dataGroup;
    }
    {
        auto* anW = new QWidget(page);
        auto* ag = new QGridLayout(anW);
        ag->setContentsMargins(0, 0, 0, 0);
        ag->setSpacing(2);
        // The guided build first: it is where a harness that does not exist yet is started, and the
        // builder proper is where one that does is changed.
        ag->addWidget(makeHarnessStepsButton(anW), 0, 0);
        ag->addWidget(makeMechanicalBuilderButton(anW), 0, 1);
        ag->addWidget(makeMechanicalValidationButton(anW), 0, 2);
        QWidget* buildGroup = addGroupKeep(lay, QStringLiteral("Build and check"), anW);
        mechGroups_ << buildGroup;
        mechModelHiddenGroups_ << buildGroup;
    }
    {
        auto* tw = new QWidget(page);
        auto* tg = new QGridLayout(tw);
        tg->setContentsMargins(0, 0, 0, 0);
        tg->setSpacing(2);
        tg->addWidget(makeVoltageDropButton(tw), 0, 0);
        tg->addWidget(makeBundleDiameterButton(tw), 0, 1);
        tg->addWidget(makeThermalButton(tw), 0, 2);
        QWidget* toolsGroup = addGroupKeep(lay, QStringLiteral("Tools"), tw);
        mechGroups_ << toolsGroup;
        mechModelHiddenGroups_ << toolsGroup;
    }
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("Harness mechanical"));
}

} // namespace Edraw_Desktop
