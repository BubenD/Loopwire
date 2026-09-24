// The Harness mechanical ribbon tab. Split out of MainWindow.cpp so each tab is one file: the ribbon was the
// largest thing in that file and the place every new feature landed. Still MainWindow members, so
// nothing about what they build or reach has changed.
#include "app/MainWindow.h"

#include <vector>

#include "app/BlockDialog.h"
#include "app/Chrome.h"
#include "app/ribbon/RibbonIcons.h"
#include "app/Editor.h"
#include "harness/Harness.h"
#include "render/CanvasWidget.h"
#include "tools/QuickSelect.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QFontMetrics>
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
        // button goes in the grid with them: eight buttons, two rows.
        //
        // The block arrow is here rather than in Special. It was put there as furniture peculiar to a
        // harness drawing, but it is not: it is a thing WRITTEN ON a drawing to say which way something
        // goes, the same as a leader or a note, and it is looked for where the rest of the writing is.
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
                     ToolId::Revision, ToolId::DetailView, ToolId::VolumeArrow},
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
            // EXCEPT THE LITTLE A ON A FIELD. It is a tool button because that is what a
            // two-letter button is made of, and this sweep turns every tool button in the cell into
            // a forty-pixel icon-only square - which took the A's letter away and made it the size
            // of a tool. It belongs to the field beside it, not to the row of icons; it is told
            // apart by the cell it sits in, the same way the sweep that hides them is.
            for (QToolButton* b : w->findChildren<QToolButton*>()) {
                if (b->parentWidget() &&
                    b->parentWidget()->objectName() == QLatin1String("autoFieldCell"))
                    continue;
                oneButton(b);
            }
            if (auto* b = qobject_cast<QToolButton*>(w)) oneButton(b);
        };
        // COLLECTED FIRST, PLACED AFTERWARDS. Which cell a button belongs in cannot be worked out while
        // they are being made, because it depends on how many there are in the end: put down as they come,
        // the last one lands in a column of its own with a hole under it, and what should be a block of
        // icons reads as a ragged tail. See the placement below.
        std::vector<QWidget*> specialCells;
        auto lgAdd = [&](QWidget* w) {
            sizeCell(w);
            specialCells.push_back(w);
            ++specialAt;
        };
        auto lgAddFirst = [&](QWidget* w) {
            sizeCell(w);
            specialCells.insert(specialCells.begin(), w);
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
            // THE FIELD SAYS WHAT IS GOING TO BE DRAWN, not the word "auto". A field reading "auto"
            // tells nobody how far apart the strokes will be or how far they lean; the figure the
            // drawing has worked out does, and the little A beside it is what says the figure is
            // the drawing's own rather than one somebody typed. See syncBreakOptions.
            auto field = [&](const QString& text, double lo, double hi, double step,
                             QDoubleSpinBox** out, QLabel** outLabel, int row,
                             std::function<void()> toAuto) {
                auto* l = new QLabel(text, optW);
                l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
                auto* s = new QDoubleSpinBox(optW);
                s->setRange(lo, hi);
                s->setSingleStep(step);
                s->setDecimals(1);
                s->setMinimumWidth(66);
                og->addWidget(l, row, 0);
                og->addWidget(autoBeside(s, std::move(toAuto)), row, 1);
                *out = s;
                *outLabel = l;
                return s;
            };
            field(QStringLiteral("Distance between"), 0.0, 500.0, 1.0, &breakGapSpin_,
                  &breakGapLabel_, 0,
                  [this] { editor_->breakGap() = 0.0; syncBreakOptions(); })
                ->setToolTip(QStringLiteral(
                    "How far apart the two strokes are drawn. Auto is a proportion of the reach, and "
                    "the field shows what that comes to."));
            field(QStringLiteral("Length"), 0.0, 2000.0, 1.0, &breakSizeSpin_, &breakSizeLabel_, 1,
                  [this] { editor_->breakSize() = 0.0; syncBreakOptions(); })
                ->setToolTip(QStringLiteral(
                    "How far the mark reaches across the run. Auto is as wide as the run it is placed "
                    "on, with a margin either side."));
            field(QStringLiteral("Angle"), -1.0, 75.0, 5.0, &breakAngleSpin_, &breakAngleLabel_, 2,
                  [this] { editor_->breakAngle() = -1.0; syncBreakOptions(); })
                ->setToolTip(QStringLiteral(
                    "How far the strokes lean out of square, in degrees. Auto is the lean each style "
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
        connect(editor_, &Editor::faceFitChanged, this, [this] { syncBreakOptions(); });
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
            thBtn->setText(QStringLiteral("Twisted\nbundle"));
            thBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            thBtn->setIconSize(QSize(26, 26));
            thBtn->setAutoRaise(true);
            thBtn->setFocusPolicy(Qt::NoFocus);
            thBtn->setToolTip(QStringLiteral(
                "Twisted bundle: click the two ends and every wire of the bundle is laid up together, "
                "fanning out to straight ends. Tab pins it so the number of wires can be set with it "
                "on screen."));
            tg->addWidget(thBtn);
            // Its one setting, beside the button and only while the tool is armed.
            {
                auto* optW = new QWidget(thW);
                auto* og = new QGridLayout(optW);
                og->setContentsMargins(6, 0, 0, 0);
                og->setSpacing(2);
                // TWO COLUMNS OF PAIRS, not four rows. A ribbon group is about two rows tall, and
                // four rows of label-and-box in one were drawn on top of each other - the four
                // labels legible down the left and the four boxes piled up in one place. Laid out
                // as two columns the whole set reads at a glance and every box can be got at.
                og->setColumnMinimumWidth(2, 10);   // a gap between the columns of settings
                og->setColumnMinimumWidth(5, 10);
                og->setColumnMinimumWidth(7, 8);
                // AND THE SETTINGS COME IN THREE SORTS, so they are drawn in three.
                //
                // What the RUN is - how many wires, which way they spread, how thick one is, how
                // tightly it may be bent - stands on its own at the left. Then the FAN, boxed: how
                // far apart the wires open, over what length, what their corners do, and what is
                // done to their ends. Then the BODY, boxed: what the stretch between the two fans is
                // made of and the figures that belong to whichever that is.
                //
                // Eighteen settings in one unbroken row read as eighteen settings. Boxed, they read
                // as three questions, and a drawing office asking one of them can see where to look.
                // EACH BOX SAYS WHAT IT IS. A border groups them and a name tells you which group
                // you are looking at - a box of eight figures with nothing on it is a box of eight
                // figures. Set small and dim, under the fields, the way a ribbon group's own name is:
                // it is there to be found rather than read every time.
                auto boxed = [optW](QGridLayout** into, const QString& name, int across) {
                    auto* box = new QFrame(optW);
                    box->setObjectName(QStringLiteral("harnessSection"));
                    box->setStyleSheet(QStringLiteral(
                        "QFrame#harnessSection{border:1px solid #3d4a5a;border-radius:4px;"
                        "background:transparent;}"));
                    auto* grid = new QGridLayout(box);
                    grid->setContentsMargins(6, 1, 6, 1);
                    grid->setSpacing(2);
                    auto* caption = new QLabel(name, box);
                    caption->setObjectName(QStringLiteral("harnessSectionName"));
                    caption->setAlignment(Qt::AlignHCenter);
                    caption->setStyleSheet(QStringLiteral(
                        "color:#8b9bb0;background:transparent;font-size:9px;border:none;"));
                    grid->addWidget(caption, 3, 0, 1, across);
                    *into = grid;
                    return box;
                };
                QGridLayout* fg = nullptr;
                QGridLayout* bg = nullptr;
                QFrame* fanBox = boxed(&fg, QStringLiteral("Fan out"), 14);
                QFrame* bodyBox = boxed(&bg, QStringLiteral("Body"), 14);
                // ONE BOX, ONE GRID. The figures for the two ends were briefly boxed apart, and two
                // frames inside a frame inside a ribbon group is three borders deep to read one
                // number out of. They are back in the box itself, ordered so that the start end and
                // the finish end stand in the same column as each other - which is what the frames
                // were for, and a column does it without drawing anything.

                // Kept, because a fanout has no body to describe: it is the fan alone, and the box
                // goes away rather than standing there offering settings it cannot use.
                twistBodyBox_ = bodyBox;
                og->addWidget(fanBox, 0, 6, 2, 1);
                og->addWidget(bodyBox, 0, 8, 2, 1);
                for (int gap : {2, 5, 8, 11}) fg->setColumnMinimumWidth(gap, 10);
                for (int gap : {2, 5, 8, 11}) bg->setColumnMinimumWidth(gap, 10);
                // THE SLACK GOES AT THE END, not into the first column. A grid with nothing told to
                // take up the spare width hands it to whichever column will have it, which put a
                // hand's breadth between the first label and its own box.
                for (int fixed = 0; fixed <= 8; ++fixed) og->setColumnStretch(fixed, 0);
                og->setColumnStretch(9, 1);
                auto* l = new QLabel(QStringLiteral("Wires"), optW);
                l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistWiresSpin_ = new QSpinBox(optW);
                twistWiresSpin_->setRange(2, kTwistedMaxWires);
                twistWiresSpin_->setValue(editor_->twistWires());
                twistWiresSpin_->setMinimumWidth(56);
                twistWiresSpin_->setToolTip(QStringLiteral(
                    "How many wires the bundle fans out to, 2 to %1. Two wires is a twisted pair.")
                        .arg(kTwistedMaxWires));
                fg->addWidget(l, 1, 0);
                fg->addWidget(twistWiresSpin_, 1, 1);
                connect(twistWiresSpin_, &QSpinBox::valueChanged, this, [this](int v) {
                    editor_->setTwistWires(v);
                    // Converge-to can never be more than there are, and past three the bundle is
                    // drawn as fewer lines - both decided in the editor, shown here.
                    syncBreakOptions();
                    canvas_->update();   // a pinned bundle re-draws with the new count
                    statusBar()->showMessage(
                        QStringLiteral("Twisted bundle: %1 wires").arg(v), 2000);
                });
                twistSpreadLabel_ = new QLabel(QStringLiteral("Fan out"), optW);
                twistSpreadLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                QLabel* sl = twistSpreadLabel_;
                twistSpreadCombo_ = new QComboBox(optW);
                // NAMED IN DRAW ORDER, and only so. Left and right are ends of the SHEET, which is
                // an answer that changes when the run is turned round - and a run drawn up the page
                // has no left end to speak of at all. Start and finish are the two clicks, which is
                // what the person drawing it has in mind and what stays true whatever it points at.
                // (A drawing made before this still reads: left and right are understood, they are
                // simply not offered.)
                twistSpreadCombo_->addItem(QStringLiteral("Start end"), QStringLiteral("start"));
                twistSpreadCombo_->addItem(QStringLiteral("Finish end"), QStringLiteral("finish"));
                twistSpreadCombo_->addItem(QStringLiteral("Both"), QStringLiteral("both"));
                twistSpreadCombo_->setCurrentIndex(2);
                twistSpreadCombo_->setMinimumWidth(84);
                twistSpreadCombo_->setToolTip(QStringLiteral(
                    "Which end of the run the wires fan out at, named by the order it was drawn in: "
                    "the end you started from, the end you finished at, or both. Either single "
                    "answer leaves the other end as body - the cable, sleeve or wires carrying on."));
                // WHICH END FANS IS A FAN-OUT SETTING, so it is in that box with the rest of them.
                // It also settles which end has wire ends to flush and to strip, which are the two
                // fields beside it there.
                fg->addWidget(sl, 0, 0);
                fg->addWidget(twistSpreadCombo_, 0, 1);
                connect(twistSpreadCombo_, &QComboBox::currentIndexChanged, this,
                        [this](int index) {
                            const QString spread =
                                twistSpreadCombo_->itemData(index).toString();
                            editor_->setTwistSpread(spread);
                            canvas_->update();   // a pinned bundle re-draws with the new spread
                            statusBar()->showMessage(
                                QStringLiteral("Twisted bundle: spread %1")
                                    .arg(twistSpreadCombo_->itemText(index).toLower()),
                                2000);
                        });
                // HOW FAR APART THE SPREAD WIRES SIT. Only the straight run uses it - a laid-up
                // bundle spreads its wires across its own width, which is what its width is for -
                // so the row is hidden for the other tools rather than sitting there doing nothing.
                // NAMED FOR WHAT IT SETS. "Spacing" could be the pitch in the bundle, the pitch in
                // the fan, or the gap between runs; it is the fan's, and the field now says so.
                twistLaneLabel_ = new QLabel(QStringLiteral("Fan pitch"), optW);
                twistLaneLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistLaneSpin_ = new QDoubleSpinBox(optW);
                twistLaneSpin_->setRange(0.0, 200.0);
                twistLaneSpin_->setDecimals(1);
                twistLaneSpin_->setSingleStep(0.5);
                twistLaneSpin_->setKeyboardTracking(false);
                twistLaneSpin_->setValue(editor_->twistLane());
                twistLaneSpin_->setMinimumWidth(64);
                twistLaneSpin_->setToolTip(QStringLiteral(
                    "How far apart the wires sit where they are spread out, centre to centre. Auto "
                    "is half again the pitch they run at packed in the run."));
                fg->addWidget(twistLaneLabel_, 0, 3);
                fg->addWidget(autoBeside(twistLaneSpin_, [this] { editor_->setTwistLane(0.0); syncBreakOptions(); }), 0, 4);
                connect(twistLaneSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
                    editor_->setTwistLane(v);
                    canvas_->update();   // a pinned run re-draws at the new spacing
                    statusBar()->showMessage(
                        v > 1e-9 ? QStringLiteral("Harness run: spread wires %1 apart").arg(v)
                                 : QStringLiteral("Harness run: spread wires at the drawing's own "
                                                  "spacing"),
                        2000);
                });
                // HOW MANY CARRY ON past the fan. A bundle is drawn with the number of lines it takes
                // to read as one, not with a line per wire: the rest run into it and stop.
                twistTrunkLabel_ = new QLabel(QStringLiteral("Converge to"), optW);
                twistTrunkLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistTrunkSpin_ = new QSpinBox(optW);
                twistTrunkSpin_->setRange(0, kTwistedMaxWires);
                twistTrunkSpin_->setSpecialValueText(QStringLiteral("All"));
                twistTrunkSpin_->setValue(editor_->twistTrunk());
                twistTrunkSpin_->setMinimumWidth(56);
                twistTrunkSpin_->setToolTip(QStringLiteral(
                    "How many wires carry on through the run once the spread ones have converged. "
                    "The rest are drawn running into it and stopping, which is how a bundle of many "
                    "wires is drawn as a run of few. All keeps every wire."));
                // IN THE BODY BOX, because it is a figure of the hook-up-wires body and of no other:
                // it says how many lines the bundle between the fans is drawn as, and a stretch
                // covered by a cable or a length of tubing draws none of them. It shares its cell
                // with the cover width, which is the same question asked of the other two sorts -
                // one of the two is on show at a time, never both.
                bg->addWidget(twistTrunkLabel_, 1, 3);
                bg->addWidget(autoBeside(twistTrunkSpin_), 1, 4);
                connect(twistTrunkSpin_, &QSpinBox::valueChanged, this, [this](int v) {
                    editor_->setTwistTrunk(v);
                    canvas_->update();   // a pinned run re-draws with the new bundle
                    statusBar()->showMessage(
                        v > 0 ? QStringLiteral("Harness run: %1 wire(s) carry on").arg(v)
                              : QStringLiteral("Harness run: every wire carries on"),
                        2000);
                });
                // HOW LONG THE FAN IS, and HOW THICK ONE WIRE IS. Both are figures the drawing
                // already knew how to read and neither had anywhere to be set from: the fan was
                // whatever the default said, and the wire width - which on a straight run is also
                // what settles how wide the bundle comes out - could only be changed by editing a
                // placed object's properties afterwards.
                twistFanLabel_ = new QLabel(QStringLiteral("Fan length"), optW);
                twistFanLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistFanSpin_ = new QDoubleSpinBox(optW);
                twistFanSpin_->setRange(0.0, 500.0);
                twistFanSpin_->setDecimals(1);
                twistFanSpin_->setSingleStep(1.0);
                twistFanSpin_->setKeyboardTracking(false);
                twistFanSpin_->setValue(editor_->twistFan());
                twistFanSpin_->setMinimumWidth(64);
                twistFanSpin_->setToolTip(QStringLiteral(
                    "How long the fan is at each end - the stretch over which the wires open out of "
                    "the bundle and settle into their lanes. Auto is the drawing's own figure."));
                fg->addWidget(twistFanLabel_, 1, 3);
                fg->addWidget(autoBeside(twistFanSpin_, [this] { editor_->setTwistFan(0.0); syncBreakOptions(); }), 1, 4);
                connect(twistFanSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
                    editor_->setTwistFan(v);
                    canvas_->update();
                    statusBar()->showMessage(
                        v > 1e-9 ? QStringLiteral("Harness run: the fan is %1 long").arg(v)
                                 : QStringLiteral("Harness run: the drawing's own fan length"),
                        2000);
                });
                twistWireLabel_ = new QLabel(QStringLiteral("Wire width"), optW);
                twistWireLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistWireSpin_ = new QDoubleSpinBox(optW);
                twistWireSpin_->setRange(0.0, 50.0);
                twistWireSpin_->setDecimals(2);
                twistWireSpin_->setSingleStep(0.1);
                twistWireSpin_->setKeyboardTracking(false);
                twistWireSpin_->setValue(editor_->twistWireWidth());
                twistWireSpin_->setMinimumWidth(64);
                twistWireSpin_->setToolTip(QStringLiteral(
                    "How thick one wire is drawn. Six wires take up more room than three, so this is "
                    "also what settles how wide the bundle comes out. Auto is the drawing's own."));
                fg->addWidget(twistWireLabel_, 0, 12);
                fg->addWidget(
                    autoBeside(twistWireSpin_, [this] { editor_->setTwistWireWidth(0.0); syncBreakOptions(); }), 0, 13);
                connect(twistWireSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
                    editor_->setTwistWireWidth(v);
                    canvas_->update();
                    statusBar()->showMessage(
                        v > 1e-9 ? QStringLiteral("Harness run: wires drawn %1 across").arg(v)
                                 : QStringLiteral("Harness run: the drawing's own wire width"),
                        2000);
                });
                // AND HOW TIGHTLY IT TURNS at a knee, past the fan. Only the routed runs have knees
                // to turn at - a run drawn between two points has none - so the field shows for those
                // and stands down for the straight one rather than sitting there doing nothing.
                twistKneeLabel_ = new QLabel(QStringLiteral("Knee radius"), optW);
                twistKneeLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                // AUTO SITS BELOW NOUGHT, so that nought can mean what it says. While Auto was
                // nought there was no way to ask for a sharp corner at all: every run was bent,
                // whether the drawing wanted it bent or not.
                twistKneeSpin_ = new QDoubleSpinBox(optW);
                twistKneeSpin_->setRange(-1.0, 500.0);
                twistKneeSpin_->setDecimals(1);
                twistKneeSpin_->setSingleStep(1.0);
                twistKneeSpin_->setKeyboardTracking(false);
                twistKneeSpin_->setValue(editor_->twistKnee());
                twistKneeSpin_->setMinimumWidth(64);
                twistKneeSpin_->setToolTip(QStringLiteral(
                    "How tightly the run turns where it bends. A loom does not kink at a corner, it "
                    "bends - and how far it may be bent is set by what is in it, not by how wide the "
                    "line is. It is the TIGHTEST radius in the bundle, so no wire bends inside it. "
                    "0 is a sharp corner; Auto is a share of the bundle width."));
                bg->addWidget(twistKneeLabel_, 0, 3);
                bg->addWidget(autoBeside(twistKneeSpin_, [this] { editor_->setTwistKnee(-1.0); syncBreakOptions(); }), 0,
                              4);
                connect(twistKneeSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
                    editor_->setTwistKnee(v);
                    canvas_->update();
                    statusBar()->showMessage(
                        v > 1e-9  ? QStringLiteral("Harness run: knees turn on a %1 radius").arg(v)
                        : v < 0.0 ? QStringLiteral("Harness run: the drawing's own knee radius")
                                  : QStringLiteral("Harness run: knees are sharp corners"),
                        2000);
                });
                // AND THE FAN'S OWN CORNERS, which is a different radius from the one above. The
                // knee is where the loom is routed round the shape of the car; this is where a wire
                // leaves the bundle for its pin, over a few millimetres rather than tens of them.
                // One field for both would be wrong for whichever of the two it was not set for.
                twistFanKneeLabel_ = new QLabel(QStringLiteral("Fan fillet"), optW);
                twistFanKneeLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistFanKneeSpin_ = new QDoubleSpinBox(optW);
                twistFanKneeSpin_->setRange(0.0, 500.0);
                twistFanKneeSpin_->setDecimals(1);
                twistFanKneeSpin_->setSingleStep(0.5);
                twistFanKneeSpin_->setKeyboardTracking(false);
                twistFanKneeSpin_->setValue(editor_->twistFanKnee());
                twistFanKneeSpin_->setMinimumWidth(64);
                twistFanKneeSpin_->setToolTip(QStringLiteral(
                    "How tightly the fan turns where a wire leaves the bundle and where it settles "
                    "into its lane. Asked for, the slant runs straight between two corners of that "
                    "radius; more than the fan can hold rounds the whole of it. 0 is sharp corners, "
                    "Auto eases it all."));
                fg->addWidget(twistFanKneeLabel_, 2, 3);
                fg->addWidget(
                    autoBeside(twistFanKneeSpin_, [this] { editor_->setTwistFanKnee(-1.0); syncBreakOptions(); }), 2, 4);
                connect(twistFanKneeSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
                    editor_->setTwistFanKnee(v);
                    canvas_->update();
                    statusBar()->showMessage(
                        v > 1e-9  ? QStringLiteral("Harness run: the fan turns on a %1 radius").arg(v)
                        : v < 0.0 ? QStringLiteral("Harness run: the fan is eased end to end")
                                  : QStringLiteral("Harness run: the fan has sharp corners"),
                        2000);
                });
                // THE TWO ENDS OF THE RUN. It leaves one thing and arrives at another, and the two
                // need not fan alike: eight ways out of a header at one end and two into a ring
                // terminal at the other is one run, not two runs that meet. Ticked - which is how
                // it starts, because nearly every run IS drawn alike at both ends - the finish end
                // repeats the start and there is one set of figures to keep. Unticked, the three
                // that describe a fan are asked again for the finish end.
                twistSidesCheck_ = new QCheckBox(QStringLiteral("Both sides same"), optW);
                twistSidesCheck_->setChecked(editor_->twistSidesSame());
                twistSidesCheck_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistSidesCheck_->setToolTip(QStringLiteral(
                    "The finish end of the run repeats the start end. Untick it to give the finish "
                    "end its own pitch, fan length and fan knee - a run out of a header into a ring "
                    "terminal fans quite differently at the two ends."));
                fg->addWidget(twistSidesCheck_, 0, 9, 1, 2);
                // WHAT FINISHES A BODY END. Which end is body is said by Spread, above - the one
                // control for the one question. A transition is a part fitted over the end of a
                // CABLE; whether a SLEEVE has recovered onto what comes out of it or been cut off
                // square is the same question asked of the other sort of cover. Neither is anything
                // to ask about bare wires, and neither arises while both ends fan.
                twistEndBootCombo_ = new QComboBox(optW);
                twistEndBootCombo_->addItem(QStringLiteral("No transition"), false);
                twistEndBootCombo_->addItem(QStringLiteral("With transition"), true);
                twistEndBootCombo_->setCurrentIndex(editor_->twistEndBoot() ? 1 : 0);
                twistEndBootCombo_->setMinimumWidth(110);
                twistEndBootCombo_->setToolTip(QStringLiteral(
                    "A transition heatshrink over the body end of the run."));
                bg->addWidget(twistEndBootCombo_, 2, 12, 1, 2);
                twistEndBootFaceCombo_ = new QComboBox(optW);
                twistEndBootFaceCombo_->addItem(QStringLiteral("Recovered end"), true);
                twistEndBootFaceCombo_->addItem(QStringLiteral("Not recovered"), false);
                twistEndBootFaceCombo_->setCurrentIndex(editor_->twistEndBootRecovered() ? 0 : 1);
                twistEndBootFaceCombo_->setMinimumWidth(110);
                twistEndBootFaceCombo_->setToolTip(QStringLiteral(
                    "How the sleeve's own end is drawn where the run does not fan: recovered, "
                    "shrunk down onto what comes out of it, or not recovered - cut off square at "
                    "the body's own width."));
                bg->addWidget(twistEndBootFaceCombo_, 2, 12, 1, 2);
                connect(twistEndBootCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
                    editor_->setTwistEndBoot(twistEndBootCombo_->itemData(i).toBool());
                    canvas_->update();
                });
                connect(twistEndBootFaceCombo_, &QComboBox::currentIndexChanged, this,
                        [this](int i) {
                            editor_->setTwistEndBootRecovered(
                                twistEndBootFaceCombo_->itemData(i).toBool());
                            canvas_->update();
                        });
                connect(twistEndBootCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
                    editor_->setTwistEndBoot(twistEndBootCombo_->itemData(i).toBool());
                    syncBreakOptions();   // the recovered/square choice only means something with one
                    canvas_->update();
                });
                connect(twistEndBootFaceCombo_, &QComboBox::currentIndexChanged, this,
                        [this](int i) {
                            editor_->setTwistEndBootRecovered(
                                twistEndBootFaceCombo_->itemData(i).toBool());
                            canvas_->update();
                        });
                auto addFinish = [&](QLabel*& label, QDoubleSpinBox*& spin, const QString& text,
                                     double lo, double value, int row, int col,
                                     const QString& tip) {
                    label = new QLabel(text, optW);
                    label->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
                    spin = new QDoubleSpinBox(optW);
                    spin->setRange(lo, 500.0);
                    spin->setDecimals(1);
                    spin->setSingleStep(1.0);
                    spin->setKeyboardTracking(false);
                    spin->setValue(value);
                    spin->setMinimumWidth(64);
                    spin->setToolTip(tip);
                    fg->addWidget(label, row, col);
                    fg->addWidget(autoBeside(spin, [this, spin, lo] { spin->setValue(lo); syncBreakOptions(); }), row,
                                  col + 1);
                };
                addFinish(twistLaneBLabel_, twistLaneBSpin_, QStringLiteral("Finish fan pitch"), 0.0,
                          editor_->twistLaneFinish(), 0, 6,
                          QStringLiteral("How far apart the wires sit where they are spread at the "
                                         "FINISH end of the run."));
                addFinish(twistFanBLabel_, twistFanBSpin_, QStringLiteral("Finish fan length"), 0.0,
                          editor_->twistFanFinish(), 1, 6,
                          QStringLiteral("How long the fan is at the FINISH end of the run."));
                addFinish(twistFanKneeBLabel_, twistFanKneeBSpin_, QStringLiteral("Finish fan fillet"),
                          0.0, editor_->twistFanKneeFinish(), 2, 6,
                          QStringLiteral("How tightly the fan turns at the FINISH end. 0 is sharp "
                                         "corners, Auto eases it all."));
                // THE FLUSH END: the straight stretch at the very end of the fan, where every wire is
                // parallel to the run again so every cut end lines up square across it. That is how
                // a fan is drawn and it was always on and always the same share of the fan - a
                // drawing that wants a stated length of parallel wire to land, or wants the wires cut
                // where they are still spreading, had no way to say either.
                twistFlushCheck_ = new QCheckBox(QStringLiteral("Flush ends"), optW);
                twistFlushCheck_->setChecked(editor_->twistFlush());
                twistFlushCheck_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistFlushCheck_->setToolTip(QStringLiteral(
                    "End the fan with a straight stretch, so every wire is parallel to the run where "
                    "it is cut and all the cut ends line up square across the fan. Unticked, the "
                    "wires are cut where they are still spreading."));
                fg->addWidget(twistFlushCheck_, 1, 9, 1, 2);
                twistFlushLabel_ = new QLabel(QStringLiteral("Flush length"), optW);
                twistFlushLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistFlushSpin_ = new QDoubleSpinBox(optW);
                twistFlushSpin_->setRange(0.0, 500.0);
                twistFlushSpin_->setDecimals(1);
                twistFlushSpin_->setSingleStep(1.0);
                twistFlushSpin_->setKeyboardTracking(false);
                twistFlushSpin_->setValue(editor_->twistFlushLen());
                twistFlushSpin_->setMinimumWidth(64);
                twistFlushSpin_->setToolTip(QStringLiteral(
                    "How long that straight stretch is - how much parallel wire there is to land. "
                    "Auto is the drawing's own share of the fan."));
                fg->addWidget(twistFlushLabel_, 2, 0);
                fg->addWidget(
                    autoBeside(twistFlushSpin_, [this] { editor_->setTwistFlushLen(0.0); syncBreakOptions(); }), 2, 1);
                connect(twistFlushCheck_, &QCheckBox::toggled, this, [this](bool on) {
                    editor_->setTwistFlush(on);
                    syncBreakOptions();   // the length comes and goes with it
                    canvas_->update();
                    statusBar()->showMessage(
                        on ? QStringLiteral("Harness run: the ends are cut square across the fan")
                           : QStringLiteral("Harness run: the ends are cut where they are spreading"),
                        2000);
                });
                connect(twistFlushSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistFlushLen(v); canvas_->update(); });
                // STRIPPED ENDS, beside the flush ones because they are the other thing done to the
                // end of a wire. A wire is landed by taking the insulation off the last few
                // millimetres of it and crimping the bare conductor into a contact, and a harness
                // drawing says how much comes off: that is the strip length, and it is a made figure
                // like the rest of these.
                twistStripCheck_ = new QCheckBox(QStringLiteral("Stripped ends"), optW);
                twistStripCheck_->setChecked(editor_->twistStrip());
                twistStripCheck_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistStripCheck_->setToolTip(QStringLiteral(
                    "Show the insulation stripped off the end of each wire, back to bare conductor - "
                    "which is how a wire is landed in a contact. A second line across each wire marks "
                    "where the insulation stops."));
                fg->addWidget(twistStripCheck_, 2, 9, 1, 2);
                twistStripLabel_ = new QLabel(QStringLiteral("Strip length"), optW);
                twistStripLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistStripSpin_ = new QDoubleSpinBox(optW);
                twistStripSpin_->setRange(0.0, 500.0);
                twistStripSpin_->setDecimals(1);
                twistStripSpin_->setSingleStep(0.5);
                twistStripSpin_->setKeyboardTracking(false);
                twistStripSpin_->setValue(editor_->twistStripLen());
                twistStripSpin_->setMinimumWidth(64);
                twistStripSpin_->setToolTip(QStringLiteral(
                    "How much insulation comes off the end of each wire. Auto is five millimetres, or "
                    "whatever the end can spare."));
                fg->addWidget(twistStripLabel_, 2, 12);
                fg->addWidget(
                    autoBeside(twistStripSpin_, [this] { editor_->setTwistStripLen(0.0); syncBreakOptions(); }), 2, 13);
                connect(twistStripCheck_, &QCheckBox::toggled, this, [this](bool on) {
                    editor_->setTwistStrip(on);
                    syncBreakOptions();   // the length comes and goes with it
                    canvas_->update();
                    statusBar()->showMessage(
                        on ? QStringLiteral("Harness run: the ends are drawn stripped")
                           : QStringLiteral("Harness run: the ends are drawn cut, and nothing else"),
                        2000);
                });
                connect(twistStripSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistStripLen(v); canvas_->update(); });
                // THE CONVERGED STRETCH AS A CABLE UNDER HEATSHRINK. A loom between its two fans is
                // a covered thing - a cable, or a sleeve over the bundle - and drawing the two or
                // three lines its wires make there says the wires are on show when they are not.
                // The FANS ARE UNTOUCHED: the wires come out of it at both ends exactly as before,
                // and the ends of the cover are drawn recovered onto what they cover, which is what
                // a shrunk end looks like.
                auto* bodyLabel = new QLabel(QStringLiteral("Body"), optW);
                bodyLabel->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistBodyCombo_ = new QComboBox(optW);
                twistBodyCombo_->addItem(QStringLiteral("Hook-up wires"),
                                         static_cast<int>(RunBody::Wires));
                twistBodyCombo_->addItem(QStringLiteral("Cable"), static_cast<int>(RunBody::Cable));
                twistBodyCombo_->addItem(QStringLiteral("Hshrink"),
                                         static_cast<int>(RunBody::Heatshrink));
                twistBodyCombo_->setMinimumWidth(96);
                twistBodyCombo_->setToolTip(QStringLiteral(
                    "What the stretch between the two fans is made of. Hook-up wires: the lines the "
                    "wires themselves make. Cable: one body cut square at each end, with a "
                    "transition heatshrink flaring out over the first of each fan - a breakout. "
                    "Heatshrink: a length of tubing over the run, shrunk onto what it grips at each "
                    "end, and no boot. The fans are unchanged by all three."));
                bg->addWidget(bodyLabel, 0, 0);
                bg->addWidget(twistBodyCombo_, 0, 1);
                connect(twistBodyCombo_, &QComboBox::currentIndexChanged, this, [this](int at) {
                    if (!twistBodyCombo_) return;
                    editor_->setTwistBodyKind(
                        static_cast<RunBody>(twistBodyCombo_->itemData(at).toInt()));
                    syncBreakOptions();   // each sort brings its own figures
                    canvas_->update();
                    statusBar()->showMessage(
                        QStringLiteral("Harness run: between the fans, %1")
                            .arg(twistBodyCombo_->itemText(at).toLower()),
                        2000);
                });
                // WHAT THE CABLE IS DRAWN TO, which only means anything while there is one - so all
                // of it comes up with the tick and goes away with it. How wide the cable is, and at
                // each end how far across its face is and over what length it comes down to that:
                // the shrunk length. A cover over a loom is shrunk onto a connector at one end and
                // onto a breakout at the other, and those are not the same size, so the two ends can
                // be described apart - though most of the time they are not, which is the tick.
                // WHICH GRID a body field belongs to is said outright: the box for the whole body,
                // or one of the two end boxes inside it. Guessing it from the column worked only
                // while every field in a column belonged to the same end.
                auto bodyField = [&](QGridLayout* into, QLabel*& label, QDoubleSpinBox*& spin,
                                     const QString& text,
                                     double lo, double value, int row, int col, const QString& tip,
                                     std::function<void()> toAuto) {
                    label = new QLabel(text, optW);
                    label->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
                    spin = new QDoubleSpinBox(optW);
                    spin->setRange(lo, 500.0);
                    spin->setDecimals(1);
                    spin->setSingleStep(0.5);
                    spin->setKeyboardTracking(false);
                    spin->setValue(value);
                    spin->setMinimumWidth(64);
                    spin->setToolTip(tip);
                    into->addWidget(label, row, col);
                    into->addWidget(autoBeside(spin, toAuto), row, col + 1);
                };
                // HOW FAR APART THE WIRES RUN IN THE BODY. Drawn as its wires, the body IS those
                // wires and how tightly they are gathered is the whole look of it: loose in a loom,
                // pulled up close in a harness. Auto is the pitch these drawings are drawn at.
                bodyField(bg, twistBodyPitchLabel_, twistBodyPitchSpin_, QStringLiteral("Wire pitch"),
                          0.0, editor_->twistBodyPitch(), 1, 0,
                          QStringLiteral("How far apart the wires run where the bundle is gathered, "
                                         "centre to centre. Auto is a wire and a half."),
                          [this] { editor_->setTwistBodyPitch(0.0); syncBreakOptions(); });
                bodyField(bg, twistBodyWidthLabel_, twistBodyWidthSpin_, QStringLiteral("Width"), 0.0,
                          editor_->twistBodyWidth(), 2, 0,
                          QStringLiteral("How wide the cable over the converged stretch is drawn. "
                                         "Auto is the bundle it covers plus a wire's width."),
                          [this] { editor_->setTwistBodyWidth(0.0); syncBreakOptions(); });
                bodyField(bg, twistBodyShrinkLabel_, twistBodyShrinkSpin_,
                          QStringLiteral("Hshrink recover length"), -1.0, editor_->twistBodyShrink(), 1, 6,
                          QStringLiteral("How far in from the end the cover takes to come down to "
                                         "its face. 0 is a square shoulder; Auto is the drawing's "
                                         "own."),
                          [this] { editor_->setTwistBodyShrink(-1.0); syncBreakOptions(); });
                bodyField(bg, twistBodyFaceLabel_, twistBodyFaceSpin_, QStringLiteral("Entry size"), 0.0,
                          editor_->twistBodyFace(), 0, 6,
                          QStringLiteral("How far across the end of the cover is - the size it has "
                                         "shrunk down to. Auto is the bundle underneath it."),
                          [this] { editor_->setTwistBodyFace(0.0); syncBreakOptions(); });
                bodyField(bg, twistBodyShrinkBLabel_, twistBodyShrinkBSpin_,
                          QStringLiteral("Finish Hshrink recover"), -1.0,
                          editor_->twistBodyShrinkFinish(), 1, 9,
                          QStringLiteral("The shrunk length at the FINISH end of the cable."),
                          [this] { editor_->setTwistBodyShrinkFinish(-1.0); syncBreakOptions(); });
                bodyField(bg, twistBodyFaceBLabel_, twistBodyFaceBSpin_, QStringLiteral("Finish entry size"),
                          0.0, editor_->twistBodyFaceFinish(), 0, 9,
                          QStringLiteral("How far across the FINISH end of the cover is."),
                          [this] { editor_->setTwistBodyFaceFinish(0.0); syncBreakOptions(); });
                // THE BOOT AT EACH END OF A CABLE: the transition heatshrink that takes the bundle
                // of wires down to the one cable. A cable's, not a sleeve's - a length of tubing
                // over the run has no transition to cover.
                bodyField(bg, twistBootLabel_, twistBootSpin_, QStringLiteral("Hshrink length"), 0.0,
                          editor_->twistBoot(), 2, 6,
                          QStringLiteral("How far the transition heatshrink at each end of the cable "
                                         "reaches back along the cable. The fan starts at its face. "
                                         "Auto is a third of the fan."),
                          [this] { editor_->setTwistBoot(0.0); syncBreakOptions(); });
                bodyField(bg, twistBootBLabel_, twistBootBSpin_, QStringLiteral("Finish Hshrink length"), 0.0,
                          editor_->twistBootFinish(), 2, 9,
                          QStringLiteral("The boot at the FINISH end of the cable."),
                          [this] { editor_->setTwistBootFinish(0.0); syncBreakOptions(); });
                connect(twistBootSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistBoot(v); canvas_->update(); });
                connect(twistBootBSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistBootFinish(v); canvas_->update(); });
                twistBodySidesCheck_ = new QCheckBox(QStringLiteral("Cable both sides"), optW);
                twistBodySidesCheck_->setChecked(editor_->twistBodySidesSame());
                twistBodySidesCheck_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistBodySidesCheck_->setToolTip(QStringLiteral(
                    "Both ends of the cable shrink alike. Untick it to give the finish end its own "
                    "shrunk length and end size - a cover is shrunk onto a connector at one end and "
                    "onto a breakout at the other."));
                bg->addWidget(twistBodySidesCheck_, 0, 12, 1, 2);
                // THE TRANSITION HSHRINK, which is a cable thing only: a length of tubing over the
                // whole run is already the transition and cannot have a second one over its own
                // end. WHICH ENDS wear one is a question a tick could not answer: a loom is not
                // symmetrical, and one end going into a connector wants the boot while the other
                // running into a splice does not. It stands under the knee radius, with the rest of
                // what the body is made of.
                twistTransitionLabel_ = new QLabel(QStringLiteral("Transition Hshrink"), optW);
                twistTransitionLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistTransitionCombo_ = new QComboBox(optW);
                twistTransitionCombo_->addItem(QStringLiteral("None"),
                                               static_cast<int>(RunEnds::None));
                twistTransitionCombo_->addItem(QStringLiteral("Start"),
                                               static_cast<int>(RunEnds::Start));
                twistTransitionCombo_->addItem(QStringLiteral("Finish"),
                                               static_cast<int>(RunEnds::Finish));
                twistTransitionCombo_->addItem(QStringLiteral("Both"),
                                               static_cast<int>(RunEnds::Both));
                twistTransitionCombo_->setMinimumWidth(72);
                twistTransitionCombo_->setToolTip(QStringLiteral(
                    "Which ends of the cable wear a boot: sat back along the cable, shrunk down "
                    "onto it where it grips, and cut square at the mouth the fan starts from. An "
                    "end without one runs straight out into its wires, and its own figures stand "
                    "down with it."));
                bg->addWidget(twistTransitionLabel_, 1, 3);
                bg->addWidget(twistTransitionCombo_, 1, 4);
                connect(twistTransitionCombo_, &QComboBox::currentIndexChanged, this, [this](int at) {
                    if (!twistTransitionCombo_) return;
                    editor_->setTwistTransitionEnds(
                        static_cast<RunEnds>(twistTransitionCombo_->itemData(at).toInt()));
                    syncBreakOptions();   // the boot figures come and go with the ends
                    canvas_->update();
                    statusBar()->showMessage(
                        QStringLiteral("Harness run: transition Hshrink on %1")
                            .arg(twistTransitionCombo_->itemText(at).toLower()),
                        2000);
                });
                // AND HOW THE GATHERED WIRES RUN, which only means anything while the body IS the
                // wires: under a cable or a sleeve there is nothing of them to see.
                twistLayLabel_ = new QLabel(QStringLiteral("Wires run"), optW);
                twistLayLabel_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistLayCombo_ = new QComboBox(optW);
                twistLayCombo_->addItem(QStringLiteral("Straight"),
                                        static_cast<int>(RunLay::Straight));
                twistLayCombo_->addItem(QStringLiteral("Twisted pairs"),
                                        static_cast<int>(RunLay::Pairs));
                twistLayCombo_->addItem(QStringLiteral("Mixed"), static_cast<int>(RunLay::Mixed));
                twistLayCombo_->setMinimumWidth(72);
                twistLayCombo_->setToolTip(QStringLiteral(
                    "How the wires run where they are gathered. Straight: side by side, as a loom "
                    "of hook-up wire is drawn. Twisted pairs: every pair of neighbouring wires laid "
                    "up together. Mixed: every other pair laid up, which is a harness carrying a few "
                    "signal pairs among its power wires."));
                bg->addWidget(twistLayLabel_, 2, 3);
                bg->addWidget(twistLayCombo_, 2, 4);
                connect(twistLayCombo_, &QComboBox::currentIndexChanged, this, [this](int at) {
                    if (!twistLayCombo_) return;
                    editor_->setTwistLay(static_cast<RunLay>(twistLayCombo_->itemData(at).toInt()));
                    canvas_->update();
                    statusBar()->showMessage(
                        QStringLiteral("Harness run: the gathered wires run %1")
                            .arg(twistLayCombo_->itemText(at).toLower()),
                        2000);
                });
                twistBodyOverCableCheck_ = new QCheckBox(QStringLiteral("Over the cable"), optW);
                twistBodyOverCableCheck_->setChecked(editor_->twistBodyOverCable());
                twistBodyOverCableCheck_->setStyleSheet(
                    QStringLiteral("color:#e6e9ef;background:transparent;"));
                twistBodyOverCableCheck_->setToolTip(QStringLiteral(
                    "Which of the two sorts of tubing. Unticked, one sleeve runs ALONG the whole "
                    "converged stretch and is the body itself. Ticked, the body is a cable and the "
                    "heatshrink is a piece fitted OVER each of its ends: standing proud of the "
                    "cable, stepping onto it at the body end and shrunk down toward the fan."));
                bg->addWidget(twistBodyOverCableCheck_, 1, 12, 1, 2);
                connect(twistBodyOverCableCheck_, &QCheckBox::toggled, this, [this](bool on) {
                    editor_->setTwistBodyOverCable(on);
                    syncBreakOptions();
                    canvas_->update();
                    statusBar()->showMessage(
                        on ? QStringLiteral("Harness run: heatshrink over the cable, at each end")
                           : QStringLiteral("Harness run: heatshrink along the whole run"),
                        2000);
                });
                connect(twistBodySidesCheck_, &QCheckBox::toggled, this, [this](bool on) {
                    editor_->setTwistBodySidesSame(on);
                    syncBreakOptions();
                    canvas_->update();
                    statusBar()->showMessage(
                        on ? QStringLiteral("Harness run: both ends of the cable shrink alike")
                           : QStringLiteral("Harness run: each end of the cable shrinks its own way"),
                        2000);
                });
                connect(twistBodyPitchSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) {
                            editor_->setTwistBodyPitch(v);
                            syncBreakOptions();
                            canvas_->update();
                        });
                connect(twistBodyWidthSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistBodyWidth(v); canvas_->update(); });
                connect(twistBodyFaceSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistBodyFace(v); canvas_->update(); });
                connect(twistBodyShrinkSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistBodyShrink(v); canvas_->update(); });
                connect(twistBodyFaceBSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
                    editor_->setTwistBodyFaceFinish(v);
                    canvas_->update();
                });
                connect(twistBodyShrinkBSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) {
                            editor_->setTwistBodyShrinkFinish(v);
                            canvas_->update();
                        });
                connect(twistSidesCheck_, &QCheckBox::toggled, this, [this](bool on) {
                    editor_->setTwistSidesSame(on);
                    syncBreakOptions();   // the finish end's fields come and go with it
                    canvas_->update();
                    statusBar()->showMessage(
                        on ? QStringLiteral("Harness run: both ends fan alike")
                           : QStringLiteral("Harness run: each end fans its own way"),
                        2000);
                });
                connect(twistLaneBSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistLaneFinish(v); canvas_->update(); });
                connect(twistFanBSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistFanFinish(v); canvas_->update(); });
                connect(twistFanKneeBSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) { editor_->setTwistFanKneeFinish(v); canvas_->update(); });
                // A BOX THAT DOES NOT CHANGE SIZE.
                //
                // Which fields a box shows depends on the sort of body picked and on which ticks
                // are set, and a grid gives a hidden widget no room at all - so unticking Flush
                // ends took the strip in by the width of a field and everything to the right of it
                // jumped sideways. A ribbon is a place on the screen you learn: a field that moves
                // because a different field went away has to be found again every time.
                //
                // So a field hidden by a TICK keeps its room, and a field the sort of body in the
                // box has no use for at all gives its room up - which is settled field by field in
                // syncBreakOptions, because only that knows which sort is in the box. What is done
                // here is the standing furniture: the labels and the wire settings, which are
                // either there for a bundle tool or not there at all.
                optW->setVisible(false);
                twistOptionsGroup_ = optW;
                tg->addWidget(optW);
            }
            lgAddFirst(thW);
        }
        // And that bundle ROUTED: the same wire count as Wires harness, but through as many knees
        // as the loom needs rather than straight between two points - the same relationship Twisted
        // route bears to Twisted pair. Shares twistOptionsGroup_ (the Wires field), which shows for
        // either bundle tool (see syncBreakOptions()).
        {
            auto* thrBtn = new QToolButton(lenW);
            QAction* act =
                makeToolAction(toolLabel(ToolId::TwistedHarnessRoute), ToolId::TwistedHarnessRoute);
            thrBtn->setDefaultAction(act);
            thrBtn->setText(QStringLiteral("Twisted\nbundle run"));
            thrBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            thrBtn->setIconSize(QSize(26, 26));
            thrBtn->setAutoRaise(true);
            thrBtn->setFocusPolicy(Qt::NoFocus);
            thrBtn->setToolTip(QStringLiteral(
                "Twisted bundle run: click each knee, Enter finishes. The whole bundle is laid up "
                "together, fanning out to straight ends. Tab pins it so the number of wires can be "
                "set with it on screen."));
            lgAdd(thrBtn);
        }
        // THE SAME BUNDLE RUNNING STRAIGHT, which is how most of a loom is drawn: the wires lie side
        // by side the whole way and are only laid up where the drawing says they are twisted. One
        // object with the laid-up bundle, so a run can be made either afterwards from its properties
        // - and the same ribbon, the same Tab, the same wire count. What it adds is the spacing the
        // wires are spread out at, which a laid-up bundle has no use for.
        {
            auto* wrBtn = new QToolButton(lenW);
            wrBtn->setDefaultAction(makeToolAction(toolLabel(ToolId::WiresRun), ToolId::WiresRun));
            wrBtn->setText(QStringLiteral("Wires\nrun"));
            wrBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            wrBtn->setIconSize(QSize(26, 26));
            wrBtn->setAutoRaise(true);
            wrBtn->setFocusPolicy(Qt::NoFocus);
            wrBtn->setToolTip(QStringLiteral(
                "Harness run: click each knee, Enter finishes. The wires run straight - side by side "
                "the whole way rather than laid up - and spread out at whichever end the ribbon "
                "says, at the spacing it says. Tab pins it so all of that can be set with the run "
                "on screen."));
            lgAdd(wrBtn);
        }
        // AND THE FAN ON ITS OWN. A drawing often wants the break-out without the run: wires coming
        // out of something drawn elsewhere, or out of nothing at all. Same object, same fan settings
        // on the ribbon - what it leaves out is the second fan.
        {
            auto* foBtn = new QToolButton(lenW);
            foBtn->setDefaultAction(makeToolAction(toolLabel(ToolId::Fanout), ToolId::Fanout));
            foBtn->setText(QStringLiteral("Fanout"));
            foBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            foBtn->setIconSize(QSize(26, 26));
            foBtn->setAutoRaise(true);
            foBtn->setFocusPolicy(Qt::NoFocus);
            foBtn->setToolTip(QStringLiteral(
                "Fanout: click where the bundle is and again where the wires are landed. The wires "
                "fan out at the first end and the bundle simply stops at the other - every fan "
                "setting on the ribbon applies, and it is edited afterwards like any harness run."));
            lgAdd(foBtn);
        }
        // AND TAKING WHAT IS DRAWN TO WHERE IT HAS TO GO. Click the run, click the wall it lands
        // on, press Enter: the same sort of object is laid between them, in as few bends as that
        // takes, arriving square to that wall. It belongs in Special with the rest of the furniture
        // of a mechanical drawing - it is a way of drawing a run, not a way of editing one.
        {
            // The button and its two settings side by side, the settings shown only while it is
            // armed - the same shape as the break mark beside it.
            auto* arW = new QWidget(lenW);
            auto* arG = new QGridLayout(arW);
            arG->setContentsMargins(0, 0, 0, 0);
            arG->setSpacing(2);
            auto* arBtn = new QToolButton(arW);
            arBtn->setDefaultAction(
                makeToolAction(toolLabel(ToolId::AutoRoute), ToolId::AutoRoute));
            arBtn->setText(QStringLiteral("Auto\nroute"));
            arBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            arBtn->setIconSize(QSize(26, 26));
            arBtn->setAutoRaise(true);
            arBtn->setFocusPolicy(Qt::NoFocus);
            arBtn->setToolTip(QStringLiteral(
                "Auto route: click the run, cable, sleeve or wire to be routed - it lights orange - "
                "then click the wall it is to arrive at, which lights green. Enter lays another of "
                "the same sort of object between them, in as few bends as it takes, arriving square "
                "to that wall. Esc takes the picks back."));
            arG->addWidget(arBtn, 0, 0);
            {
                auto* optW = new QWidget(arW);
                auto* og = new QGridLayout(optW);
                og->setContentsMargins(6, 0, 0, 0);
                og->setSpacing(2);
                auto field = [&](const QString& text, double lo, double hi, double step,
                                 QDoubleSpinBox** out, int row, std::function<void()> toAuto,
                                 const QString& tip) {
                    auto* l = new QLabel(text, optW);
                    l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
                    auto* sp = new QDoubleSpinBox(optW);
                    sp->setRange(lo, hi);
                    sp->setDecimals(1);
                    sp->setSingleStep(step);
                    sp->setKeyboardTracking(false);
                    sp->setMinimumWidth(64);
                    sp->setToolTip(tip);
                    og->addWidget(l, row, 0);
                    og->addWidget(autoBeside(sp, std::move(toAuto)), row, 1);
                    *out = sp;
                };
                // THE RADIUS EVERY KNEE OF THE ROUTE IS ROUNDED ON. Auto suits what is being
                // routed: a harness keeps its own, a cable or sleeve is rounded to its width.
                field(QStringLiteral("Fillet"), 0.0, 500.0, 1.0, &autoRouteFilletSpin_, 0,
                      [this] { editor_->setAutoRouteFillet(-1.0); syncBreakOptions(); },
                      QStringLiteral("The radius the route's knees are rounded on. Auto suits what "
                                     "is being routed: a harness keeps its own knee radius, a cable or "
                                     "sleeve is rounded in proportion to its width, a bare line keeps "
                                     "a sharp corner."));
                // AND THE ANGLE A RUN CHANGES LEVEL AT when it has to move across to meet the wall.
                field(QStringLiteral("Level shift"), 1.0, 90.0, 5.0, &autoRouteShiftSpin_, 1,
                      [this] { editor_->setAutoRouteShift(-1.0); syncBreakOptions(); },
                      QStringLiteral("The angle, in degrees, a run crosses at when it has to move up "
                                     "or down to meet the wall. Auto is 45 - never square where there "
                                     "is room for a slant, because a run that steps sideways square "
                                     "reads as two runs."));
                connect(autoRouteFilletSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) {
                            editor_->setAutoRouteFillet(v);
                            syncBreakOptions();
                            canvas_->update();
                        });
                connect(autoRouteShiftSpin_, &QDoubleSpinBox::valueChanged, this,
                        [this](double v) {
                            editor_->setAutoRouteShift(v);
                            syncBreakOptions();
                            canvas_->update();   // the ghosted route re-lays at the new angle
                        });
                optW->setVisible(false);
                autoRouteOptionsGroup_ = optW;
                arG->addWidget(optW, 0, 1);
            }
            lgAdd(arW);
        }
        // The stock parts themselves, drawn at size: what a resistor across a pair or a ferrite over
        // the bundle actually takes up on the board. They belong here for the same reason the break
        // mark does - they are the furniture of this kind of drawing.
        lgAdd(makePartSymbolButton(lenW, /*schematic=*/false));
        // HEATSHRINK OVER THE RUN. The Special group had every other piece of furniture a mechanical
        // drawing needs - the break mark, the twisted bundles, the stock parts, the label - and no
        // heatshrink, which is the one that goes on nearly every harness. It was reachable only by
        // describing a step in the wizard or by letting the drawing put one over a splice on its own
        // account, so a sleeve wanted anywhere else had to be drawn by hand.
        //
        // Placed as the recovered shape: pulled in at both ends onto what it grips, standing off over
        // the middle - see builtInHeatshrink, which draws the same thing the renderer does so a placed
        // sleeve and a generated one cannot look different on the same sheet.
        {
            // ROUTED, NOT STAMPED - the same shape of tool as Twisted harness route.
            //
            // A sleeve covers a STRETCH of run, and where that stretch bends the sleeve bends with it,
            // so it is drawn by clicking along the run rather than dropped at one size and nudged into
            // place. A fixed-length stamp cannot say a sleeve round a corner at all.
            auto* hsBtn = new QToolButton(lenW);
            hsBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            hsBtn->setIconSize(QSize(26, 26));
            hsBtn->setAutoRaise(true);
            hsBtn->setFocusPolicy(Qt::NoFocus);
            hsBtn->setPopupMode(QToolButton::MenuButtonPopup);
            QAction* hsAct = makeToolAction(toolLabel(ToolId::HeatshrinkRoute),
                                            ToolId::HeatshrinkRoute);
            // THE ICON COMES FROM THE ACTION, not set on the button afterwards - a QToolButton with a
            // default action re-takes its icon whenever the action changes, so one assigned to the
            // button is in a fight it eventually loses.
            hsAct->setIcon(makeToolIcon(ToolId::HeatshrinkRoute, Chrome::ink()));
            hsBtn->setDefaultAction(hsAct);
            hsBtn->setText(QStringLiteral("Heatshrink"));
            auto* hm = new QMenu(hsBtn);
            struct Sort { ShrinkRouteMode mode; int icon; const char* onButton; const char* inMenu;
                          const char* tip; };
            const Sort sorts[4] = {
                {ShrinkRouteMode::Sleeve, 0, "Heatshrink", "Heatshrink over the run",
                 "Heatshrink over the run: click along the stretch it covers, Enter finishes. Drawn "
                 "recovered - pulled in at both ends onto what it grips, and following the knees in "
                 "between."},
                {ShrinkRouteMode::Cable, 1, "Cable", "Cable over the run",
                 "A cable running along the run under a length of heatshrink: one width from end to "
                 "end, cut square across each end rather than recovered onto what is under it."},
                {ShrinkRouteMode::CableEnds, 2, "Cable ends",
                 "Cable over the run with heatshrink on ends",
                 "Cable over the run with heatshrink on its ends: the cable shows between the two "
                 "sleeves, each split line can be dragged, and each end keeps its own face and "
                 "shrunk length."},
                {ShrinkRouteMode::Transition, 3, "Transition", "Transition heatshrink",
                 "Transition heatshrink: draw a line across the parallel lines it covers; the sleeve "
                 "is fitted at their closest end, using the longest line when their lengths differ."}};
            for (const Sort& sort : sorts) {
                QAction* a = hm->addAction(
                    makeToolIcon(ToolId::HeatshrinkRoute, Chrome::ink(), sort.icon),
                    QString::fromLatin1(sort.inMenu));
                a->setToolTip(QString::fromLatin1(sort.tip));
                const ShrinkRouteMode mode = sort.mode;
                const int icon = sort.icon;
                const QString text = QString::fromLatin1(sort.onButton);
                const QString tip = QString::fromLatin1(sort.tip);
                connect(a, &QAction::triggered, this,
                        [this, hsBtn, hsAct, mode, icon, text, tip] {
                    hsBtn->setText(text);
                    hsBtn->setToolTip(tip);
                    // WHICH sort, held on the button rather than read back out of its caption: a
                    // choice remembered as display text breaks the moment the display changes.
                    hsBtn->setProperty("shrinkMode", static_cast<int>(mode));
                    hsAct->setIcon(makeToolIcon(ToolId::HeatshrinkRoute,
                                                Chrome::ink(), icon));
                    if (editor_) editor_->setShrinkRouteMode(mode);
                    hsAct->trigger();   // picks the tool, and checks it on the ribbon
                });
            }
            hsBtn->setToolTip(QString::fromLatin1(sorts[0].tip));
            hsBtn->setMenu(hm);
            hsBtn->setProperty("shrinkMode", static_cast<int>(ShrinkRouteMode::Sleeve));
            // Pressing the button itself draws whichever sort it is wearing. The default action sets
            // the tool; this only has to say which sort of sleeve that tool is to draw.
            connect(hsAct, &QAction::triggered, this, [this, hsBtn] {
                if (editor_)
                    editor_->setShrinkRouteMode(static_cast<ShrinkRouteMode>(
                        hsBtn->property("shrinkMode").toInt()));
            });
            lgAdd(hsBtn);
        }
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
        // ABOUT WHAT IT TURNS. A released mechanical drawing says this constantly and the sheet could
        // not draw it: an axis, with the swept angle about it. It belongs in Special for the same reason
        // the break mark does - it is a convention of this kind of drawing rather than geometry somebody
        // is modelling. (The block arrow that used to sit beside it is in Annotation now: it is writing
        // on the drawing, not a convention of it.)
        {
            auto* aaBtn = new QToolButton(lenW);
            aaBtn->setDefaultAction(makeToolAction(toolLabel(ToolId::AxisAngle), ToolId::AxisAngle));
            aaBtn->setToolTip(QStringLiteral(
                "The axis and the angle swept about it: click where the axis is, then where the sweep "
                "starts. Centre lines mark the axis and an arc carries the arrow round it, with the "
                "limits written at its ends - one object, so the arc cannot end up on the sheet "
                "without its centre. The sweep, the limits and the arrow are all on its properties."));
            aaBtn->setAutoRaise(true);
            aaBtn->setFocusPolicy(Qt::NoFocus);
            lgAdd(aaBtn);
        }
        // TWO EQUAL ROWS. As many columns as it takes to halve them, filled along the top row and then
        // along the bottom - the way a row of pictures is read - so the two rows are the same length and the
        // group is a block rather than a comb. An odd one out goes on the TOP row: the eye starts there.
        //
        // And no column wider than the icon in it. Each column used to reserve 78 pixels, which was room for
        // a two-line caption under the button - and the captions have since gone (the name is on hover, like
        // every other icon-only group on the ribbon), so all that width was doing was pushing the icons
        // apart. Seven 40-pixel icons spread over four 78-pixel columns is what made these rows look ragged
        // whatever order they went in.
        {
            const int n = static_cast<int>(specialCells.size());
            const int cols = (n + 1) / 2;   // 7 buttons -> 4 columns: four on top, three under them
            for (int i = 0; i < n; ++i)
                lg->addWidget(specialCells[static_cast<size_t>(i)], i / cols, i % cols);
            for (int col = 0; col < cols; ++col)
                lg->setColumnMinimumWidth(col, kSpecialCell.width());
        }
        mechSpecialCells_.clear();
        for (QWidget* w : specialCells) mechSpecialCells_ << w;
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
              QStringLiteral("How far the label reaches ACROSS the sheet, left to right. On a bundle drawn "
                             "across the sheet that is its length along the wire; on one drawn up the sheet "
                             "it is its reach across the bundle."),
              &labelWidthSpin_, 0);
        field(QStringLiteral("Height"),
              QStringLiteral("How far the label reaches UP the sheet. Whichever of its two dimensions is "
                             "lying that way where it sits — so what is typed is what is seen."),
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
        tg->addWidget(makeCalculatorButton(tw), 0, 3);
        QWidget* toolsGroup = addGroupKeep(lay, QStringLiteral("Tools"), tw);
        mechGroups_ << toolsGroup;
        mechModelHiddenGroups_ << toolsGroup;
    }
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("Harness mechanical"));
}

} // namespace Edraw_Desktop
