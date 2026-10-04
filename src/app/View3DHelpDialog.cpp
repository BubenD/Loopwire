#include "app/View3DHelpDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMovie>
#include <QToolButton>
#include <QVBoxLayout>

namespace Edraw_Desktop {

namespace {

struct Topic {
    const char* title;
    const char* gif;
    const char* steps;
};

// WHAT EACH RECORDING SHOWS, written out - for reading at one's own pace, and for what a picture cannot say.
const Topic kTopics[] = {
    {"The view and the plane",
     ":/help3d/plane.gif",
     "<p><b>Turning, moving, zooming.</b> Drag with the left button to turn the view, with the right (or the middle) "
     "button to move it; the wheel zooms. <i>Fit</i>, <i>Iso</i>, <i>Top</i>, <i>Front</i> and <i>Side</i> bring it "
     "back to a view you know.</p>"
     "<p><b>Where a click lands.</b> Every click lands on the face of the model under the pointer - that point, and "
     "the way the face looks, is what a connector, a branch or a corner of a cable is put on.</p>"
     "<p><b>The ground plane.</b> Where there is no model under the pointer, the click lands on the ground: the "
     "plane the drawing lies in (Z = 0). A cable can be laid over it like over any face.</p>"
     "<p><b>The section plane.</b> To reach a face inside the model, cut it open: <i>Section X</i>, <i>Y</i> or "
     "<i>Z</i>, and the slider beside it moves the plane. A click goes through what is cut away to the first face "
     "left standing. The see-through slider lets the model be looked through instead.</p>"},
    {"Route a cable from connector to connector",
     ":/help3d/route.gif",
     "<ol>"
     "<li>Load the model the harness goes in: <i>Environment: Load...</i> (STEP, STL or OBJ).</li>"
     "<li>Choose <b>Connector</b> and click the face each connector sits on. Its cable leaves it along the face, "
     "straight out for the <i>Lead</i> length. With the harness's parts listed on the right, choose a part first "
     "to put that part's connector.</li>"
     "<li>Choose <b>Route</b> and click the first connector.</li>"
     "<li>Click the faces the cable runs over - each click is a corner of it. A cable follows the pointer to show "
     "where it would go next.</li>"
     "<li>Click the other connector: the cable is laid, and its length is in the table.</li>"
     "</ol>"
     "<p><i>Esc</i> gives up the cable being laid; <i>Backspace</i> takes back its last corner. A double-click on a "
     "face ends it at a new branch there. <i>Lengths to harness</i> gives each cable of the harness its length "
     "along the route.</p>"},
    {"Bend the cable",
     ":/help3d/bend.gif",
     "<p><b>A cable bends at the corners you click</b> while laying it - each corner is rounded on the bend "
     "radius, so the cable runs as a real one would rather than turning on a point.</p>"
     "<p><b>Bend R</b> is the tightest a cable may be bent. <i>Auto</i> is six times its diameter; type a radius "
     "to bend wider (or tighter). <b>&Oslash;</b> is the cable's diameter - the table sets one cable's own. A corner "
     "with no room for its bend is counted under the table.</p>"
     "<p><b>Changing your mind.</b> While laying, <i>Backspace</i> takes back the last corner. Once laid, "
     "<i>Undo</i> (Ctrl+Z) takes the change back. A laid cable's corners cannot be dragged: to take it another "
     "way, delete it (the <i>Delete</i> tool, or select it and press Delete) and lay it again - or put a "
     "<i>Branch</i> or a <i>Clip</i> on it to divide it where it should be held.</p>"},
};

} // namespace

QStringList View3DHelpDialog::recordings() {
    QStringList out;
    for (const Topic& t : kTopics) out << QString::fromLatin1(t.gif);
    return out;
}

View3DHelpDialog::View3DHelpDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("3D view - how to"));
    resize(1180, 760);
    auto* lay = new QHBoxLayout(this);
    topics_ = new QListWidget(this);
    topics_->setObjectName(QStringLiteral("help3dTopics"));
    for (const Topic& t : kTopics) topics_->addItem(QString::fromLatin1(t.title));
    topics_->setFixedWidth(240);
    lay->addWidget(topics_);

    auto* right = new QVBoxLayout;
    picture_ = new QLabel(this);
    picture_->setObjectName(QStringLiteral("help3dPicture"));
    picture_->setAlignment(Qt::AlignCenter);
    picture_->setMinimumSize(640, 420);
    picture_->setStyleSheet(QStringLiteral("QLabel{background:#1e2228;}"));
    right->addWidget(picture_, 1);
    auto* controls = new QHBoxLayout;
    pause_ = new QToolButton(this);
    pause_->setObjectName(QStringLiteral("help3dPause"));
    pause_->setText(QStringLiteral("Pause"));
    pause_->setCheckable(true);
    controls->addWidget(pause_);
    auto* again = new QToolButton(this);
    again->setObjectName(QStringLiteral("help3dRestart"));
    again->setText(QStringLiteral("From the start"));
    controls->addWidget(again);
    controls->addStretch(1);
    right->addLayout(controls);
    steps_ = new QLabel(this);
    steps_->setObjectName(QStringLiteral("help3dSteps"));
    steps_->setWordWrap(true);
    steps_->setTextFormat(Qt::RichText);
    steps_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(steps_);
    lay->addLayout(right, 1);

    movie_ = new QMovie(this);
    movie_->setCacheMode(QMovie::CacheAll);
    picture_->setMovie(movie_);
    connect(topics_, &QListWidget::currentRowChanged, this, [this](int row) { showTopic(row); });
    connect(pause_, &QToolButton::toggled, this, [this](bool on) {
        movie_->setPaused(on);
        pause_->setText(on ? QStringLiteral("Play") : QStringLiteral("Pause"));
    });
    connect(again, &QToolButton::clicked, this, [this] {
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
    movie_->setFileName(QString::fromLatin1(t.gif));
    movie_->start();
    if (pause_->isChecked()) pause_->setChecked(false);
    steps_->setText(QString::fromUtf8(t.steps));
}

} // namespace Edraw_Desktop
