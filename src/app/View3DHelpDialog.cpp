#include "app/View3DHelpDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMovie>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace Edraw_Desktop {

namespace {

struct Topic {
    const char* title;
    const char* gif;   // empty: words only
    const char* steps;
};

// WHAT EACH RECORDING SHOWS, written out - for reading at one's own pace, and for what a picture cannot say. In the
// order a newcomer needs them: what the view is for and the whole of it once, then each part of it on its own.
const Topic kTopics[] = {
    {"1. Getting started",
     ":/help3d/start.gif",
     "<p>The 3D view routes a harness through the real thing it goes in - a chassis, a panel, an enclosure - so each "
     "cable's length is read off the path it really takes, not guessed.</p>"
     "<p><b>The whole of it, in four steps:</b></p>"
     "<ol>"
     "<li><b>The model.</b> <i>Environment: Load...</i> and choose its STEP, STL or OBJ file. (To try it first, press "
     "<i>Open the example</i> below - a plate with two boxes and a rib on it.)</li>"
     "<li><b>The connectors.</b> Choose <i>Connector</i> and click the face each one sits on.</li>"
     "<li><b>The cable.</b> Choose <i>Route</i>, click the first connector, click the places it runs over (each a "
     "corner), and click the second connector.</li>"
     "<li><b>The length.</b> It is in the table on the right. <i>Lengths to harness</i> gives it to the harness.</li>"
     "</ol>"
     "<p>The topics that follow take each step on its own. <i>Full screen</i> (or F11) gives the view the whole screen "
     "while you work; F11 or <i>Back</i> comes back.</p>"},
    {"2. Turning the view, all the way round",
     ":/help3d/view.gif",
     "<p><b>With the mouse:</b> drag with the left button to turn (left and right) and tilt (up and down) - all the way "
     "round, over the top and underneath. Hold <i>Shift</i> and drag to roll. Drag with the right or middle button to "
     "move the view; the wheel zooms. Double-click (with <i>Select</i>) fits everything in.</p>"
     "<p><b>By the numbers:</b> <i>Turn</i>, <i>Tilt</i> and <i>Roll</i> in the top row are the same three turns in "
     "degrees, each going all the way round (-180&deg; to 180&deg;, and on). Their arrows step by 15&deg;; type any "
     "figure.</p>"
     "<p><b>Back to a known view:</b> <i>Iso</i>, <i>Top</i>, <i>Front</i>, <i>Side</i> and <i>Fit</i>.</p>"
     "<p><b>The wheel</b> in the picture's top right corner does it by clicks: <i>Turn</i> left and right, "
     "<i>Tilt</i> up and down, <i>Roll</i> either way, <i>Zoom</i> in and out, <i>Fit</i>, <i>Iso</i>, and "
     "<i>Lock</i>. Each click turns it by a step - 15&deg;, or what <i>Options</i> (the cog, or the wheel's middle) "
     "says: 5&deg;, 15&deg;, 45&deg; or 90&deg;. <i>Wheel</i> in the top row shows or hides it.</p>"},
    {"3. Where a click lands - the faces, the ground, the section",
     ":/help3d/plane.gif",
     "<p>Every click lands on the face of the model under the pointer. That point, and the way the face looks, is what "
     "a connector, a branch, a clip or a corner of a cable is put on.</p>"
     "<p>Where there is no model under the pointer, the click lands on the <b>ground</b>: the plane the drawing lies in "
     "(Z = 0).</p>"
     "<p>To reach a face inside the model, cut it open with the <b>section</b>: <i>Section X</i>, <i>Y</i> or "
     "<i>Z</i>, and the slider beside it moves the cut. A click goes through what is cut away to the first face left. "
     "The see-through slider lets you look through the model instead.</p>"},
    {"4. Routing up and down - the work plane",
     ":/help3d/workplane.gif",
     "<p>On its own a click lands on a face - so a cable lies on the model. A real cable also runs <b>through the "
     "air</b>: up a riser, across a gap, along a frame at some height. For that, choose a <b>work plane</b> in "
     "<i>Clicks land on:</i></p>"
     "<ul>"
     "<li><b>a plane across Z (level)</b> - every click lands at that <b>height</b>. Set the height in <i>at</i>.</li>"
     "<li><b>a plane across X</b> or <b>across Y</b> - upright planes, for laying a cable <b>up or down</b>: its points "
     "go wherever on that wall of air you click, high or low.</li>"
     "</ul>"
     "<p>The plane is drawn see-through, so you see where your clicks will land. <i>Through the point picked</i> puts it "
     "through the node selected or the point last picked on the model. You can change the plane in the middle of laying "
     "a cable - lay a few corners high, then go back to <i>the model's faces</i> and click the connector to finish.</p>"},
    {"5. Connectors - putting them, turning them",
     ":/help3d/connector.gif",
     "<p><b>Putting one:</b> choose <i>Connector</i> and click the face it sits on. It points out of that face - its "
     "cable leaves it that way, straight for the <i>Lead</i> length before it may turn. With the harness's parts "
     "listed on the right, choose a part first and the connector is that part's (click again elsewhere to move it).</p>"
     "<p><b>Turning one:</b> select it (<i>Select</i>, click it). The <i>Connector</i> box appears on the right:</p>"
     "<ul>"
     "<li><i>Points</i> - which way it points: out of its face, or along an axis (+X, -X, +Y, -Y, up, down).</li>"
     "<li><i>Turned</i> and <i>Tilted</i> - point it any way at all: round the upright, and up or down from level.</li>"
     "<li><i>Spun</i> - round its own axis: how its rectangular housing lies. <b>R</b> in the picture spins it a "
     "quarter turn.</li>"
     "</ul>"
     "<p>The cable follows: it leaves the connector the new way.</p>"},
    {"6. Route a cable from connector to connector",
     ":/help3d/route.gif",
     "<ol>"
     "<li>Choose <b>Route</b> and click the first connector.</li>"
     "<li>Click the places the cable runs over - each click is a corner of it. A cable follows the pointer to show "
     "where it would go next.</li>"
     "<li>Click the other connector: the cable is laid, and its length is in the table.</li>"
     "</ol>"
     "<p><i>Esc</i> gives up the cable being laid; <i>Backspace</i> takes back its last corner. A double-click ends it at "
     "a new branch instead of a connector. A cable can also start or end at a branch or a clip.</p>"},
    {"7. Bend the cable",
     ":/help3d/bend.gif",
     "<p><b>A cable bends at the corners you click</b> while laying it - each one rounded on the bend radius, as a real "
     "cable bends, rather than turning on a point.</p>"
     "<p><b>Bend R</b> is the tightest it may bend. <i>Auto</i> is six times its diameter; type a radius to bend wider "
     "or tighter. <b>&Oslash;</b> is the diameter (the table sets one cable's own). A corner with no room for its bend "
     "is counted under the table.</p>"
     "<p><b>Changing your mind:</b> while laying, <i>Backspace</i> takes back the last corner. Once laid, <i>Undo</i> "
     "(Ctrl+Z) takes a change back. To take a laid cable another way, <i>Lock view</i> and drag it: a corner is "
     "put where you take hold of it, and moves with the pointer (topic 10). Or delete it and lay it again.</p>"},
    {"8. Branch - where a cable divides",
     ":/help3d/branch.gif",
     "<p>A <b>branch</b> is a point where a cable divides - one bundle coming in, two or more going out, as at a "
     "breakout or a Y. It is drawn as a small yellow block.</p>"
     "<p><b>To make one:</b> choose <i>Branch</i> and click on a laid cable. The cable is divided there into two, each "
     "with its own length. Then choose <i>Route</i>, click the branch, and lay the new cable from it to the next "
     "connector.</p>"
     "<p>A branch can also be put on a face on its own and cables laid to and from it, and a double-click while "
     "laying ends a cable at a new branch.</p>"},
    {"9. Clip - where a cable is held",
     ":/help3d/clip.gif",
     "<p>A <b>clip</b> is a point where a cable is held to the structure - a P-clip, a cable tie on a mount, a "
     "saddle. It is drawn as a small green block.</p>"
     "<p><b>To put one:</b> choose <i>Clip</i> and click on a laid cable where it is held. The cable is divided there, "
     "so each piece's length - from the connector to the clip, from clip to clip - is known. A clip can also be put on "
     "a face first, and cables laid through it with <i>Route</i>.</p>"},
    {"10. Lock view - moving cables and connectors",
     ":/help3d/lock.gif",
     "<p><b>Lock view</b> (top row, or the padlock on the wheel) holds the view still: the mouse no longer turns, "
     "moves or zooms it. Instead a <b>drag moves what is under the pointer</b> - across the view and up and down it, "
     "at the depth it is:</p>"
     "<ul>"
     "<li><b>a cable</b> - taken hold of anywhere along it, a corner is put there and moved: the cable bends round "
     "it. Taken by a corner it has, that corner moves.</li>"
     "<li><b>a connector, a branch or a clip</b> - it moves, and its cables with it.</li>"
     "</ul>"
     "<p>Each drag is one change: <i>Undo</i> takes it back. To move something the other way - nearer or further - "
     "unlock, turn the view, lock again and drag. <i>View locked</i> again, or the padlock, unlocks.</p>"},
    {"11. Full screen",
     "",
     "<p><i>Full screen</i> (top right of the view), or <b>F11</b>, opens the 3D view in a window of its own, filling "
     "the screen - with every row and the panel on the right, so nothing is lost. It is a window still: drag it by its "
     "title bar, make it smaller, or put it on another screen. <b>F11</b>, <i>Back</i> or closing the window "
     "returns it to where it was.</p>"},
    {"12. What every command does",
     "",
     "<p><b>The top row - what is shown, and the view</b></p><dl>"
     "<dt><i>The drawing / The harness wizard</i></dt><dd>Which harness to show: the one on the sheet, or the one open in "
     "the wizard.</dd>"
     "<dt><i>Parts as boxes / turned / extruded</i></dt><dd>How a part with no 3D model is made from its drawing.</dd>"
     "<dt><i>Refresh</i></dt><dd>Build it again from the drawing now (it is built again by itself as the drawing "
     "changes).</dd>"
     "<dt><i>Fit, Iso, Top, Front, Side</i></dt><dd>Everything in view; seen at an angle, from above, from the front, "
     "from the side.</dd>"
     "<dt><i>Wireframe</i></dt><dd>Edges only - to see through to what is behind.</dd>"
     "<dt><i>Turn, Tilt, Roll</i></dt><dd>The view's three turns in degrees, each all the way round.</dd>"
     "<dt><i>Wheel</i></dt><dd>Show or hide the navigation wheel.</dd>"
     "<dt><i>Full screen</i></dt><dd>The view in a window of its own, filling the screen.</dd>"
     "<dt><i>Lock view</i></dt><dd>Hold the view still, so a drag moves a cable, a corner or a connector.</dd>"
     "</dl><p><b>Environment - the model routed in</b></p><dl>"
     "<dt><i>Load...</i></dt><dd>Read the model (STEP, STL, OBJ); a bar shows how far the reading has got.</dd>"
     "<dt><i>Clear</i></dt><dd>Take the model away.</dd>"
     "<dt><i>Show</i></dt><dd>Show or hide the model.</dd>"
     "<dt><i>Opacity</i></dt><dd>How solid the model is drawn - to see through it.</dd>"
     "<dt><i>Harness</i></dt><dd>Show or hide the harness from the drawing.</dd>"
     "<dt><i>Section X / Y / Z</i> and its slider</dt><dd>Cut the model open across that axis; the slider moves the "
     "cut.</dd>"
     "</dl><p><b>Route - laying the cables</b></p><dl>"
     "<dt><i>Select</i></dt><dd>Pick a point of the model, or select a node or a cable (to turn a connector, or "
     "delete it with the Delete key).</dd>"
     "<dt><i>Connector</i></dt><dd>Put a connector on the face clicked.</dd>"
     "<dt><i>Branch</i></dt><dd>Divide a cable where it is clicked - where cables part or meet.</dd>"
     "<dt><i>Clip</i></dt><dd>Put a clip on a cable where it is held to the structure.</dd>"
     "<dt><i>Route</i></dt><dd>Lay a cable: the node it starts from, the corners, the node it ends at.</dd>"
     "<dt><i>Delete</i></dt><dd>Take away the node or cable clicked.</dd>"
     "<dt><i>&Oslash;</i></dt><dd>A cable's diameter (the table sets one cable's own).</dd>"
     "<dt><i>Bend R</i></dt><dd>The tightest a cable may bend; Auto, six times its diameter.</dd>"
     "<dt><i>Lead</i></dt><dd>How far a cable comes straight out of a connector before it may turn.</dd>"
     "<dt><i>Undo</i></dt><dd>Take back the last change to the route (Ctrl+Z).</dd>"
     "<dt><i>Help</i></dt><dd>This.</dd>"
     "<dt><i>Clicks land on / at / Through the point picked</i></dt><dd>Where a click lands: the model's faces, or a "
     "level or upright plane at the place given - to lay a cable through the air.</dd>"
     "</dl><p><b>The panel on the right</b></p><dl>"
     "<dt>The parts list</dt><dd>The harness's parts: choose one, and the Connector tool puts that part.</dd>"
     "<dt><i>Link / Unlink</i></dt><dd>Make the node selected that part's, or no part's.</dd>"
     "<dt><i>Lengths to harness</i></dt><dd>Give each cable of the harness its length along the route.</dd>"
     "<dt><i>Connector: Points, Turned, Tilted, Spun</i></dt><dd>Which way the connector selected points, and how it "
     "is turned round its own axis.</dd>"
     "<dt>The table</dt><dd>Each cable, its length, and its diameter (type one in to set it).</dd>"
     "</dl><p><b>Keys</b></p><dl>"
     "<dt>Esc</dt><dd>Give up the cable being laid.</dd>"
     "<dt>Backspace</dt><dd>Take back the last corner of the cable being laid.</dd>"
     "<dt>Delete</dt><dd>Delete the node or cable selected.</dd>"
     "<dt>R</dt><dd>Spin the connector selected a quarter turn.</dd>"
     "<dt>Ctrl+Z</dt><dd>Undo.</dd>"
     "<dt>F11</dt><dd>Full screen, and back.</dd>"
     "</dl>"},
    {"13. Words used here",
     "",
     "<dl>"
     "<dt><b>Environment</b></dt><dd>The CAD model the harness is routed in (STEP, STL or OBJ).</dd>"
     "<dt><b>Node</b></dt><dd>A point of the route: a connector, a branch or a clip.</dd>"
     "<dt><b>Connector</b></dt><dd>An end of the route, on a face of the model; it points the way its cable leaves "
     "it.</dd>"
     "<dt><b>Branch</b></dt><dd>Where a cable divides into two or more.</dd>"
     "<dt><b>Clip</b></dt><dd>Where a cable is held to the structure.</dd>"
     "<dt><b>Bundle / cable</b></dt><dd>What runs between two nodes, through the corners clicked; its length is in the "
     "table.</dd>"
     "<dt><b>Corner</b></dt><dd>A point clicked while laying; the cable bends there.</dd>"
     "<dt><b>&Oslash;</b></dt><dd>The cable's diameter.</dd>"
     "<dt><b>Bend R</b></dt><dd>The tightest the cable may bend (Auto: six times its diameter).</dd>"
     "<dt><b>Lead</b></dt><dd>How far a cable comes straight out of a connector before it may turn.</dd>"
     "<dt><b>Work plane</b></dt><dd>A level or upright plane clicks land on, for laying a cable through the air.</dd>"
     "<dt><b>Section</b></dt><dd>A cut through the model, to see and click inside it.</dd>"
     "</dl>"},
};

} // namespace

QStringList View3DHelpDialog::recordings() {
    QStringList out;
    for (const Topic& t : kTopics)
        if (*t.gif) out << QString::fromLatin1(t.gif);
    return out;
}

View3DHelpDialog::View3DHelpDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("3D view - how to"));
    resize(1240, 820);
    auto* lay = new QHBoxLayout(this);
    auto* left = new QVBoxLayout;
    topics_ = new QListWidget(this);
    topics_->setObjectName(QStringLiteral("help3dTopics"));
    for (const Topic& t : kTopics) topics_->addItem(QString::fromLatin1(t.title));
    topics_->setFixedWidth(290);
    left->addWidget(topics_, 1);
    // TO TRY IT: the example the recordings use, loaded into the view.
    auto* example = new QPushButton(QStringLiteral("Open the example"), this);
    example->setObjectName(QStringLiteral("help3dExample"));
    example->setToolTip(QStringLiteral("Load the example model into the 3D view - a plate with two boxes and a rib - to try "
                                       "each step on"));
    connect(example, &QPushButton::clicked, this, [this] { emit exampleRequested(); });
    left->addWidget(example);
    lay->addLayout(left);

    auto* right = new QVBoxLayout;
    picture_ = new QLabel(this);
    picture_->setObjectName(QStringLiteral("help3dPicture"));
    picture_->setAlignment(Qt::AlignCenter);
    picture_->setMinimumSize(640, 420);
    picture_->setStyleSheet(QStringLiteral("QLabel{background:#1e2228;color:#8fa2b8;}"));
    right->addWidget(picture_, 3);
    auto* controls = new QHBoxLayout;
    pause_ = new QToolButton(this);
    pause_->setObjectName(QStringLiteral("help3dPause"));
    pause_->setText(QStringLiteral("Pause"));
    pause_->setCheckable(true);
    controls->addWidget(pause_);
    again_ = new QToolButton(this);
    again_->setObjectName(QStringLiteral("help3dRestart"));
    again_->setText(QStringLiteral("From the start"));
    controls->addWidget(again_);
    controls->addStretch(1);
    right->addLayout(controls);
    steps_ = new QLabel(this);
    steps_->setObjectName(QStringLiteral("help3dSteps"));
    steps_->setWordWrap(true);
    steps_->setTextFormat(Qt::RichText);
    steps_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(steps_);
    right->addWidget(scroll, 2);
    lay->addLayout(right, 1);

    movie_ = new QMovie(this);
    movie_->setCacheMode(QMovie::CacheAll);
    connect(topics_, &QListWidget::currentRowChanged, this, [this](int row) { showTopic(row); });
    connect(pause_, &QToolButton::toggled, this, [this](bool on) {
        movie_->setPaused(on);
        pause_->setText(on ? QStringLiteral("Play") : QStringLiteral("Pause"));
    });
    connect(again_, &QToolButton::clicked, this, [this] {
        movie_->stop();
        movie_->start();
        if (pause_->isChecked()) pause_->setChecked(false);
    });
    topics_->setCurrentRow(0);
}

int View3DHelpDialog::topicCount() const { return static_cast<int>(std::size(kTopics)); }

int View3DHelpDialog::currentTopic() const { return topics_->currentRow(); }

void View3DHelpDialog::showTopic(int index) {
    if (index < 0 || index >= topicCount()) return;
    if (topics_->currentRow() != index) {
        topics_->setCurrentRow(index);   // comes back here
        return;
    }
    const Topic& t = kTopics[index];
    movie_->stop();
    const bool recorded = *t.gif;
    pause_->setVisible(recorded);
    again_->setVisible(recorded);
    if (recorded) {
        movie_->setFileName(QString::fromLatin1(t.gif));
        picture_->setMovie(movie_);
        movie_->start();
        if (pause_->isChecked()) pause_->setChecked(false);
    } else {
        picture_->setMovie(nullptr);
        picture_->setText(QString::fromLatin1(t.title).section(QLatin1Char(' '), 1));
    }
    steps_->setText(QString::fromUtf8(t.steps));
}

} // namespace Edraw_Desktop
