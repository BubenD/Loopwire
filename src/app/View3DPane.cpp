#include "app/View3DPane.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QSlider>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

View3DPane::View3DPane(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("view3d"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    auto* bar = new QHBoxLayout;
    bar->setSpacing(3);
    source_ = new QComboBox(this);
    source_->setObjectName(QStringLiteral("view3dSource"));
    source_->addItems({QStringLiteral("The drawing"), QStringLiteral("The harness wizard")});
    source_->setToolTip(QStringLiteral("Which harness to show: the one placed on the sheet, or the one open in the harness wizard"));
    connect(source_, &QComboBox::activated, this, [this](int) { rebuild(true); });
    bar->addWidget(source_);
    // A PART WITH NO 3D MODEL: a box, or a solid made from its 2D drawing.
    look_ = new QComboBox(this);
    look_->setObjectName(QStringLiteral("view3dPartLook"));
    look_->addItems({QStringLiteral("Parts as boxes"), QStringLiteral("Parts turned from the drawing"),
                     QStringLiteral("Parts extruded from the drawing")});
    look_->setCurrentIndex(1);
    look_->setToolTip(QStringLiteral("A part with no 3D model attached: a box as big as it is drawn, or a solid made from "
                                     "its 2D drawing - turned round the way it goes in (a round part), or given depth (a "
                                     "square one)"));
    connect(look_, &QComboBox::activated, this, [this](int i) {
        partLook_ = i == 0 ? PartLook::Box : i == 1 ? PartLook::Revolved : PartLook::Extruded;
        rebuild(false);
    });
    bar->addWidget(look_);
    auto button = [this](QBoxLayout* row, const char* name, const QString& text, const QString& tip, auto fn) {
        auto* b = new QToolButton(this);
        b->setObjectName(QLatin1String(name));
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        row->addWidget(b);
        return b;
    };
    button(bar, "view3dRefresh", QStringLiteral("Refresh"), QStringLiteral("Build it again from the drawing"), [this] { rebuild(false); });
    button(bar, "view3dFit", QStringLiteral("Fit"), QStringLiteral("Everything shown in view (or double-click)"), [this] { canvas_->fit(); });
    button(bar, "view3dIso", QStringLiteral("Iso"), QStringLiteral("At an angle"), [this] { canvas_->viewFrom(QStringLiteral("iso")); });
    button(bar, "view3dTop", QStringLiteral("Top"), QStringLiteral("From above"), [this] { canvas_->viewFrom(QStringLiteral("top")); });
    button(bar, "view3dFront", QStringLiteral("Front"), QStringLiteral("From the front"), [this] { canvas_->viewFrom(QStringLiteral("front")); });
    button(bar, "view3dSide", QStringLiteral("Side"), QStringLiteral("From the side"), [this] { canvas_->viewFrom(QStringLiteral("side")); });
    auto* wire = new QCheckBox(QStringLiteral("Wireframe"), this);
    wire->setObjectName(QStringLiteral("view3dWireframe"));
    connect(wire, &QCheckBox::toggled, this, [this](bool on) { canvas_->setWireframe(on); });
    bar->addWidget(wire);
    bar->addStretch(1);
    lay->addLayout(bar);

    // THE ENVIRONMENT'S ROW: the model the harness is routed in, how it shows, and a section through it.
    envRow_ = new QWidget(this);
    auto* env = new QHBoxLayout(envRow_);
    env->setContentsMargins(0, 0, 0, 0);
    env->setSpacing(3);
    auto* envLabel = new QLabel(QStringLiteral("Environment:"), envRow_);
    env->addWidget(envLabel);
    envLoad_ = button(env, "view3dEnvLoad", QStringLiteral("Load..."),
                      QStringLiteral("Load the CAD model the harness is routed in - a chassis, a panel, an enclosure - from "
                                     "a STEP, STL or OBJ file"),
                      [this] {
                          const QString dir = QSettings().value(QStringLiteral("view3d/environmentDir")).toString();
                          const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load an environment"), dir,
                                                                            environment3DFileFilter());
                          if (path.isEmpty()) return;
                          QSettings().setValue(QStringLiteral("view3d/environmentDir"), QFileInfo(path).absolutePath());
                          loadEnvironment(path);
                      });
    envClear_ = button(env, "view3dEnvClear", QStringLiteral("Clear"), QStringLiteral("Take the environment away"),
                       [this] { clearEnvironment(); });
    envShown_ = new QCheckBox(QStringLiteral("Show"), envRow_);
    envShown_->setObjectName(QStringLiteral("view3dEnvShown"));
    envShown_->setChecked(true);
    envShown_->setToolTip(QStringLiteral("Show the environment round the harness"));
    connect(envShown_, &QCheckBox::toggled, this, [this](bool on) { canvas_->setEnvironmentShown(on); });
    env->addWidget(envShown_);
    auto* opacityLabel = new QLabel(QStringLiteral("Opacity"), envRow_);
    env->addWidget(opacityLabel);
    envOpacity_ = new QSlider(Qt::Horizontal, envRow_);
    envOpacity_->setObjectName(QStringLiteral("view3dEnvOpacity"));
    envOpacity_->setRange(10, 100);
    envOpacity_->setValue(100);
    envOpacity_->setMaximumWidth(90);
    envOpacity_->setToolTip(QStringLiteral("How solid the environment is - less, to see the harness through it"));
    connect(envOpacity_, &QSlider::valueChanged, this, [this](int v) { canvas_->setEnvironmentOpacity(v / 100.0); });
    env->addWidget(envOpacity_);
    harnessShown_ = new QCheckBox(QStringLiteral("Harness"), envRow_);
    harnessShown_->setObjectName(QStringLiteral("view3dHarnessShown"));
    harnessShown_->setChecked(true);
    harnessShown_->setToolTip(QStringLiteral("Show the harness - off, to look at the environment alone"));
    connect(harnessShown_, &QCheckBox::toggled, this, [this](bool on) { canvas_->setHarnessShown(on); });
    env->addWidget(harnessShown_);
    section_ = new QComboBox(envRow_);
    section_->setObjectName(QStringLiteral("view3dSection"));
    section_->addItems({QStringLiteral("No section"), QStringLiteral("Section X"), QStringLiteral("Section Y"),
                        QStringLiteral("Section Z")});
    section_->setToolTip(QStringLiteral("Cut the environment open across X, Y or Z, to see inside it - the harness is not cut"));
    connect(section_, &QComboBox::activated, this, [this](int) { applySection(); });
    env->addWidget(section_);
    sectionAt_ = new QSlider(Qt::Horizontal, envRow_);
    sectionAt_->setObjectName(QStringLiteral("view3dSectionAt"));
    sectionAt_->setRange(0, 1000);
    sectionAt_->setValue(500);
    sectionAt_->setMaximumWidth(120);
    sectionAt_->setToolTip(QStringLiteral("Where the section cuts: everything beyond it is cut away"));
    connect(sectionAt_, &QSlider::valueChanged, this, [this](int) { applySection(); });
    env->addWidget(sectionAt_);
    env->addStretch(1);
    lay->addWidget(envRow_);

    canvas_ = new View3DCanvas(this);
    canvas_->setObjectName(QStringLiteral("view3dCanvas"));
    lay->addWidget(canvas_, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("view3dStatus"));
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(status_);
    canvas_->setScene(&scene_);
    connect(canvas_, &View3DCanvas::picked, this, [this](const Pick3D& pick) { showPick(pick); });
    applySection();
}

View3DPane::~View3DPane() {
    // A model still being read is waited for: the reading has the pane's results to hand over.
    if (loading_) loading_->wait();
}

void View3DPane::setSources(std::function<std::vector<Entity>()> drawing, std::function<std::vector<Entity>()> wizard) {
    drawing_ = std::move(drawing);
    wizard_ = std::move(wizard);
}

void View3DPane::setModelLookup(std::function<QString(const QString&)> modelOf) { modelOf_ = std::move(modelOf); }

void View3DPane::setSourceChoice(bool shown) { source_->setVisible(shown); }

void View3DPane::setEnvironmentChoice(bool shown) { envRow_->setVisible(shown); }

void View3DPane::setPinsLookup(std::function<PartPins(const QString&)> pinsOf) { pinsOf_ = std::move(pinsOf); }

void View3DPane::setPartLook(PartLook look) {
    partLook_ = look;
    look_->setCurrentIndex(look == PartLook::Box ? 0 : look == PartLook::Revolved ? 1 : 2);
    rebuild(false);
}

void View3DPane::setHighlight(const QStringList& bricks) { canvas_->setHighlight(bricks); }

void View3DCanvas::setHighlight(const QStringList& bricks) {
    lit_ = bricks;
    update();
}

void View3DPane::rebuild(bool fit) {
    const bool fromWizard = source_->currentIndex() == 1;
    std::vector<Entity> ents = fromWizard ? (wizard_ ? wizard_() : std::vector<Entity>{})
                                          : (drawing_ ? drawing_() : std::vector<Entity>{});
    // NOTHING PLACED ON THE SHEET YET: the wizard's harness, if one is open.
    bool fell = false;
    if (!fromWizard && wizard_) {
        bool any = false;
        for (const Entity& e : ents) any = any || e.type == EntityType::TwistedPair;
        if (!any) {
            ents = wizard_();
            fell = true;
        }
    }
    scene_ = buildHarness3D(ents, modelOf_, partLook_, pinsOf_);
    canvas_->setScene(&scene_);
    if (fit) canvas_->fit();
    canvas_->update();
    QString said = scene_.runs == 0 && scene_.parts == 0
                       ? QStringLiteral("No harness to show - place one from the harness wizard, or open one in it.")
                       : QStringLiteral("%1 part(s), %2 with a 3D model, the rest %5; %3 run(s)%4.")
                             .arg(scene_.parts)
                             .arg(scene_.modelled)
                             .arg(scene_.runs)
                             .arg(fell ? QStringLiteral(" - from the harness wizard, nothing being placed on the sheet") : QString())
                             .arg(partLook_ == PartLook::Box        ? QStringLiteral("as boxes")
                                  : partLook_ == PartLook::Revolved ? QStringLiteral("turned from their drawings")
                                                                    : QStringLiteral("extruded from their drawings"));
    if (!scene_.notes.isEmpty()) said += QLatin1Char('\n') + scene_.notes.join(QLatin1Char('\n'));
    sceneSaid_ = said;
    sayStatus();
}

void View3DPane::sayStatus() {
    QStringList lines{sceneSaid_};
    if (loading_)
        lines << QStringLiteral("Environment: reading %1...").arg(loadingName_);
    else if (!env_.parts.empty() || !env_.how.isEmpty())
        lines << QStringLiteral("Environment: %1").arg(env_.how);
    if (!pickSaid_.isEmpty()) lines << pickSaid_;
    status_->setText(lines.join(QLatin1Char('\n')));
}

// ---- The environment ------------------------------------------------------------------------------

bool View3DPane::loadEnvironment(const QString& path, bool wait) {
    if (loading_) return false;
    auto env = std::make_shared<Environment3D>();
    auto index = std::make_shared<RayIndex3D>();
    auto work = [path, env, index] {
        *env = loadEnvironment3D(path);
        std::vector<const Mesh3D*> meshes;
        for (const EnvPart3D& p : env->parts) meshes.push_back(&p.mesh);
        index->build(meshes);
    };
    if (wait) {
        work();
        takeEnvironment(std::move(*env), index);
        return !env_.empty();
    }
    // READ IN THE BACKGROUND: a big assembly takes a while, and the drawing is not to stop while it does.
    loadingName_ = QFileInfo(path).fileName();
    envLoad_->setEnabled(false);
    loading_ = QThread::create(work);
    connect(loading_, &QThread::finished, this, [this, env, index] {
        loading_->deleteLater();
        loading_ = nullptr;
        envLoad_->setEnabled(true);
        takeEnvironment(std::move(*env), index);
    });
    loading_->start();
    sayStatus();
    return true;
}

void View3DPane::takeEnvironment(Environment3D env, std::shared_ptr<RayIndex3D> index) {
    env_ = std::move(env);
    envIndex_ = std::move(index);
    pickSaid_.clear();
    canvas_->clearPick();
    canvas_->setEnvironment(&env_, envIndex_.get());
    applySection();
    V3 lo, hi;
    if (env_.bounds(lo, hi)) canvas_->fitBox(lo, hi);
    sayStatus();
    emit environmentChanged();
}

void View3DPane::clearEnvironment() {
    if (loading_) return;
    env_ = Environment3D();
    envIndex_.reset();
    pickSaid_.clear();
    canvas_->clearPick();
    canvas_->setEnvironment(nullptr, nullptr);
    applySection();
    sayStatus();
    emit environmentChanged();
}

void View3DPane::applySection() {
    Section3D s;
    const int axis = section_->currentIndex() - 1;
    sectionAt_->setEnabled(axis >= 0);
    V3 lo, hi;
    if (axis >= 0 && env_.bounds(lo, hi)) {
        s.on = true;
        s.normal = axis == 0 ? V3{1, 0, 0} : axis == 1 ? V3{0, 1, 0} : V3{0, 0, 1};
        const double a = dot3(lo, s.normal), b = dot3(hi, s.normal);
        s.offset = a + (b - a) * sectionAt_->value() / 1000.0;
    }
    canvas_->setSection(s);
}

void View3DPane::showPick(const Pick3D& pick) {
    if (!pick.hit) {
        pickSaid_.clear();
    } else {
        QString name = pick.part >= 0 && pick.part < static_cast<int>(env_.parts.size())
                           ? env_.parts[static_cast<size_t>(pick.part)].name
                           : QString();
        if (name.isEmpty()) name = QStringLiteral("solid %1").arg(pick.part + 1);
        pickSaid_ = QStringLiteral("Picked %1 at %2, %3, %4 mm - its face looking %5, %6, %7")
                        .arg(name)
                        .arg(pick.point.x, 0, 'f', 1)
                        .arg(pick.point.y, 0, 'f', 1)
                        .arg(pick.point.z, 0, 'f', 1)
                        .arg(pick.normal.x, 0, 'f', 2)
                        .arg(pick.normal.y, 0, 'f', 2)
                        .arg(pick.normal.z, 0, 'f', 2);
    }
    sayStatus();
}

// ---- The picture ----------------------------------------------------------------------------------

View3DCanvas::View3DCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 160);
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral("Left drag: turn - right or middle drag: move - wheel: zoom - double-click: fit - click on the "
                              "environment: pick a point on it"));
}

void View3DCanvas::setScene(const Harness3DScene* scene) { scene_ = scene; }

void View3DCanvas::setEnvironment(const Environment3D* env, const RayIndex3D* index) {
    env_ = env;
    envIndex_ = index;
    update();
}

void View3DCanvas::setEnvironmentShown(bool on) {
    envShown_ = on;
    update();
}

void View3DCanvas::setEnvironmentOpacity(double alpha) {
    envAlpha_ = std::clamp(alpha, 0.05, 1.0);
    update();
}

void View3DCanvas::setHarnessShown(bool on) {
    harnessShown_ = on;
    update();
}

void View3DCanvas::setSection(const Section3D& section) {
    section_ = section;
    update();
}

void View3DCanvas::setUseGpu(bool on) {
    useGpu_ = on;
    update();
}

QString View3DCanvas::rendererSaid() const {
    if (lastOnGpu_) return QStringLiteral("graphics card");
    if (!useGpu_) return QStringLiteral("software");
    return gl_ && !gl_->why().isEmpty() ? QStringLiteral("software (%1)").arg(gl_->why()) : QStringLiteral("software");
}

void View3DCanvas::fitBox(const V3& lo, const V3& hi) {
    cam_.fit(lo, hi, static_cast<double>(std::max(1, width())) / std::max(1, height()));
    update();
}

void View3DCanvas::fit() {
    V3 lo, hi;
    bool any = false;
    auto take = [&](const V3& a, const V3& b) {
        lo = any ? V3{std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)} : a;
        hi = any ? V3{std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)} : b;
        any = true;
    };
    if (scene_ && harnessShown_)
        for (const Harness3DItem& it : scene_->items) {
            V3 a, b;
            if (it.mesh.bounds(a, b)) take(a, b);
        }
    V3 a, b;
    if (env_ && envShown_ && env_->bounds(a, b)) take(a, b);
    if (!any) {
        lo = {-50, -50, -50};
        hi = {50, 50, 50};
    }
    fitBox(lo, hi);
}

void View3DCanvas::viewFrom(const QString& which) {
    if (which == QLatin1String("top")) {
        cam_.yawDeg = -90.0;
        cam_.pitchDeg = 89.0;
    } else if (which == QLatin1String("front")) {
        cam_.yawDeg = -90.0;
        cam_.pitchDeg = 0.0;
    } else if (which == QLatin1String("side")) {
        cam_.yawDeg = 0.0;
        cam_.pitchDeg = 0.0;
    } else {
        cam_.yawDeg = -55.0;
        cam_.pitchDeg = 35.0;
    }
    fit();
}

void View3DCanvas::setWireframe(bool on) {
    wire_ = on;
    update();
}

Pick3D View3DCanvas::pickAt(const QPointF& at) {
    Pick3D pick;
    if (env_ && envIndex_ && envShown_ && !envIndex_->empty()) {
        V3 origin, dir;
        cam_.rayAt(at.x(), at.y(), width(), height(), origin, dir);
        // ON PAST WHAT THE SECTION CUTS AWAY: the first face left standing.
        for (int tries = 0; tries < 256; ++tries) {
            pick = envIndex_->cast(origin, dir);
            if (!pick.hit || !section_.cuts(pick.point)) break;
            origin = pick.point + dir * std::max(1e-6, pick.distance * 1e-7);
            pick = Pick3D();
        }
    }
    pick_ = pick;
    remark();
    update();
    emit picked(pick_);
    return pick_;
}

void View3DCanvas::clearPick() {
    pick_ = Pick3D();
    marker_ = Mesh3D();
    update();
}

// THE MARK ON A PICKED POINT: a small block on it and a pin standing out along its face's normal.
void View3DCanvas::remark() {
    marker_ = Mesh3D();
    if (!pick_.hit) return;
    V3 lo, hi;
    double size = 2.0;
    if (env_ && env_->bounds(lo, hi)) size = std::max(1e-3, (hi - lo).length() * 0.006);
    const V3 p = pick_.point;
    marker_ = boxMesh(p - V3{size, size, size} * 0.6, p + V3{size, size, size} * 0.6);
    marker_.append(tubeMesh({p, p + pick_.normal * (size * 6.0)}, size * 0.25, 10, true));
}

std::vector<DrawItem3D> View3DCanvas::drawItems() const {
    std::vector<DrawItem3D> items;
    if (scene_ && harnessShown_)
        for (const Harness3DItem& it : scene_->items) {
            // PICKED: lit in the pane's highlight colour, the part still read through it.
            QColor c = it.color;
            if (!it.brick.isEmpty() && lit_.contains(it.brick))
                c = QColor((c.red() + 2 * 43) / 3, (c.green() + 2 * 208) / 3, (c.blue() + 2 * 255) / 3);
            items.push_back({&it.mesh, c});
        }
    if (env_ && envShown_)
        for (size_t i = 0; i < env_->parts.size(); ++i) {
            const EnvPart3D& part = env_->parts[i];
            QColor c = part.color.isValid() ? part.color : QColor(168, 174, 184);
            // The solid last picked, lit.
            if (pick_.hit && pick_.part == static_cast<int>(i))
                c = QColor((c.red() + 43) / 2, (c.green() + 208) / 2, (c.blue() + 255) / 2);
            items.push_back({&part.mesh, c, envAlpha_, true});
        }
    if (!marker_.empty()) items.push_back({&marker_, QColor(255, 196, 40)});
    return items;
}

QImage View3DCanvas::render(const QSize& size) const {
    QImage img(size.expandedTo(QSize(1, 1)), QImage::Format_RGB32);
    const std::vector<DrawItem3D> items = drawItems();
    const QColor background(30, 36, 44);
    if (useGpu_) {
        if (!gl_) gl_ = std::make_unique<GlRenderer3D>();
        if (gl_->render(img, items, cam_, wire_, background, section_)) {
            lastOnGpu_ = true;
            return img;
        }
    }
    lastOnGpu_ = false;
    renderScene3D(img, items, cam_, wire_, background, section_);
    return img;
}

void View3DCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawImage(0, 0, render(size()));
    // THE AXES, small in the corner: which way is which as it is turned.
    V3 right, up, forward;
    cam_.axes(right, up, forward);
    const QPointF o(34, height() - 34);
    const struct {
        V3 d;
        QColor c;
        const char* n;
    } ax[3] = {{{1, 0, 0}, QColor(230, 90, 90), "X"}, {{0, 1, 0}, QColor(90, 210, 110), "Y"}, {{0, 0, 1}, QColor(90, 140, 240), "Z"}};
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& a : ax) {
        const QPointF tip = o + QPointF(dot3(a.d, right), -dot3(a.d, up)) * 22.0;
        p.setPen(QPen(a.c, 2.0));
        p.drawLine(o, tip);
        p.drawText(tip + QPointF(3, 4), QString::fromLatin1(a.n));
    }
}

void View3DCanvas::mousePressEvent(QMouseEvent* e) {
    dragging_ = e->button();
    last_ = e->position().toPoint();
    pressedAt_ = last_;
    moved_ = false;
}

void View3DCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_ == Qt::NoButton) return;
    const QPoint now = e->position().toPoint();
    if ((now - pressedAt_).manhattanLength() > 3) moved_ = true;
    if (!moved_) return;
    const QPoint d = now - last_;
    last_ = now;
    if (dragging_ == Qt::LeftButton) {
        // TURNED: round the upright, and up and down - never over the top.
        cam_.yawDeg -= d.x() * 0.4;
        cam_.pitchDeg = std::clamp(cam_.pitchDeg + d.y() * 0.4, -89.0, 89.0);
    } else {
        // MOVED: the point it looks at, across the picture as far as the pointer went.
        V3 right, up, forward;
        cam_.axes(right, up, forward);
        const double perPixel = 2.0 * cam_.distance * std::tan(cam_.fovDeg * M_PI / 360.0) / std::max(1, height());
        cam_.target = cam_.target - right * (d.x() * perPixel) + up * (d.y() * perPixel);
    }
    update();
}

void View3DCanvas::mouseReleaseEvent(QMouseEvent* e) {
    // A CLICK, NOT A DRAG: what is under the pointer picked.
    if (dragging_ == Qt::LeftButton && !moved_ && e->button() == Qt::LeftButton && env_) pickAt(e->position());
    dragging_ = Qt::NoButton;
}

void View3DCanvas::mouseDoubleClickEvent(QMouseEvent*) { fit(); }

void View3DCanvas::wheelEvent(QWheelEvent* e) {
    // ZOOMED: nearer or further, a step a notch.
    const double notches = e->angleDelta().y() / 120.0;
    cam_.distance = std::clamp(cam_.distance * std::pow(0.85, notches), 1.0, 1e7);
    update();
}

} // namespace Edraw_Desktop
