#include "app/MechModelCanvasDialog.h"

#include "app/Editor.h"
#include "harness/PartsLibrary.h"
#include "render/CanvasWidget.h"
#include "tools/EntityTransforms.h"
#include "tools/ToolId.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace Loopwire {

namespace {

// The guides that show the two ends which must not stretch. Drawn on a layer of their own so they can
// be told from the model, and thrown away when the drawing is taken.
constexpr auto kGuideLayer = "«guide»";

} // namespace

MechModelCanvasDialog::MechModelCanvasDialog(const MechModel& model, QWidget* parent)
    : QDialog(parent), model_(model) {
    setWindowTitle(QStringLiteral("%1 — draw the model").arg(mechModelName(model.kind)));
    setModal(true);
    buildUi();

    // What is there already, to be changed rather than redrawn.
    doc_.entities().clear();
    for (const Entity& e : partDrawingEntities(model_.drawing)) doc_.entities().push_back(e);
    refreshGuides();
    if (canvas_) canvas_->fitView();
    refreshMeasure();
}

MechModelCanvasDialog::~MechModelCanvasDialog() = default;

void MechModelCanvasDialog::buildUi() {
    auto* v = new QVBoxLayout(this);

    hint_ = new QLabel;
    hint_->setWordWrap(true);
    hint_->setText(QStringLiteral(
        "Draw it along +x, at the size it is in millimetres. The dashed lines are the two ends that "
        "must not stretch — draw up to them. Everything the canvas can do is here: snapping, the grid, "
        "zoom, undo."));
    v->addWidget(hint_);

    // The tools. Enough to draw any of these pieces; the canvas does the rest.
    auto* strip = new QHBoxLayout;
    struct ToolBtn {
        const char* label;
        ToolId tool;
        const char* tip;
    };
    const ToolBtn tools[] = {
        {"Select", ToolId::Select, "Pick and move what is drawn"},
        {"Line", ToolId::Polyline, "A line through every point clicked"},
        {"Rectangle", ToolId::Rect, "A rectangle"},
        {"Circle", ToolId::Circle, "A circle"},
        {"Arc", ToolId::Arc, "An arc"},
    };
    for (const ToolBtn& t : tools) {
        auto* b = new QToolButton;
        b->setText(QString::fromLatin1(t.label));
        b->setToolTip(QString::fromLatin1(t.tip));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        const ToolId id = t.tool;
        connect(b, &QToolButton::clicked, this, [this, id] {
            if (editor_) editor_->setTool(id);
        });
        if (t.tool == ToolId::Polyline) b->setChecked(true);
        strip->addWidget(b);
    }
    strip->addSpacing(12);
    auto* undo = new QToolButton;
    undo->setText(QStringLiteral("Undo"));
    connect(undo, &QToolButton::clicked, this, [this] {
        if (editor_) editor_->undo();
    });
    strip->addWidget(undo);
    auto* del = new QToolButton;
    del->setText(QStringLiteral("Delete"));
    del->setToolTip(QStringLiteral("Delete what is selected"));
    connect(del, &QToolButton::clicked, this, [this] {
        if (editor_) editor_->deleteSelected();
    });
    strip->addWidget(del);
    auto* fit = new QToolButton;
    fit->setText(QStringLiteral("Fit"));
    connect(fit, &QToolButton::clicked, this, [this] {
        if (canvas_) canvas_->fitView();
    });
    strip->addWidget(fit);
    strip->addStretch(1);
    v->addLayout(strip);

    canvas_ = new CanvasWidget(this);
    canvas_->setMinimumSize(760, 420);
    editor_ = new Editor(this);
    canvas_->setEditor(editor_);
    canvas_->setDocument(&doc_);
    editor_->setDocument(&doc_);
    editor_->setTool(ToolId::Polyline);
    // A canvas of its own, and grey rather than the drawing's own dark blue, so it is never mistaken
    // for the sheet - what is drawn here is a piece of vocabulary, not part of a harness.
    canvas_->setBackgroundColor(QColor("#3a3d42"));
    canvas_->setGridVisible(true);
    canvas_->setGridSize(5.0);
    connect(editor_, &Editor::changed, this, [this] { refreshMeasure(); });
    v->addWidget(canvas_, 1);

    measure_ = new QLabel;
    measure_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    v->addWidget(measure_);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* keep = new QPushButton(QStringLiteral("Keep"));
    keep->setDefault(true);
    connect(keep, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(keep);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    row->addWidget(cancel);
    v->addLayout(row);

    resize(900, 620);
}

void MechModelCanvasDialog::refreshGuides() {
    // Away with the old ones, then the two ends as dashed lines at the head and tail distances. They
    // are drawn from the length the model says it is - or from what is on the canvas when it says
    // nothing - so they follow the drawing rather than sitting where they were first put.
    auto& ents = doc_.entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(),
                              [](const Entity& e) { return e.layer == QLatin1String(kGuideLayer); }),
               ents.end());
    if (!mechModelStretches(model_.kind)) return;
    const double len = drawnLengthMm() > 0.0 ? drawnLengthMm() : mechModelDrawnLength(model_);
    if (len <= 0.0) return;
    Bounds b;
    for (const Entity& e : ents) {
        const Bounds eb = e.bounds();
        if (eb.valid) {
            b.expand(eb.min);
            b.expand(eb.max);
        }
    }
    const double half = b.valid ? std::max(4.0, (b.max.y - b.min.y)) : 8.0;
    const double x0 = b.valid ? b.min.x : 0.0;
    for (double at : {model_.headMm, len - model_.tailMm}) {
        if (at <= 0.0 || at >= len) continue;
        Entity g;
        g.id = QStringLiteral("guide%1").arg(at, 0, 'f', 2);
        g.type = EntityType::Line;
        g.layer = QLatin1String(kGuideLayer);
        g.a = {x0 + at, -half};
        g.b = {x0 + at, half};
        g.color = QStringLiteral("#e6b45a");
        g.meta.insert(QStringLiteral("lineType"), QStringLiteral("dashed"));
        ents.push_back(g);
    }
}

void MechModelCanvasDialog::refreshMeasure() {
    if (!measure_) return;
    const double len = drawnLengthMm();
    const double across = drawnAcrossMm();
    int objects = 0;
    for (const Entity& e : doc_.entities())
        if (e.layer != QLatin1String(kGuideLayer)) ++objects;
    measure_->setText(
        objects == 0
            ? QStringLiteral("Nothing drawn yet")
            : QStringLiteral("%1 object(s), %2 mm long and %3 mm across as drawn")
                  .arg(objects)
                  .arg(len, 0, 'f', 1)
                  .arg(across, 0, 'f', 1));
}

QJsonArray MechModelCanvasDialog::drawing() const {
    // What is on the canvas, less the guides, moved so it starts at x = 0 and is centred across. A
    // model is stored that way so the head and tail figures mean the same thing whatever corner of the
    // canvas it happened to be drawn in.
    std::vector<Entity> keep;
    Bounds b;
    for (const Entity& e : doc_.entities()) {
        if (e.layer == QLatin1String(kGuideLayer)) continue;
        keep.push_back(e);
        const Bounds eb = e.bounds();
        if (eb.valid) {
            b.expand(eb.min);
            b.expand(eb.max);
        }
    }
    if (keep.empty() || !b.valid) return QJsonArray();
    const Vec2 shift{-b.min.x, -(b.min.y + b.max.y) / 2.0};
    for (Entity& e : keep) {
        translateEntity(e, shift);
        e.layer = QStringLiteral("0");
    }
    return partDrawingFromEntities(keep);
}

double MechModelCanvasDialog::drawnLengthMm() const {
    Bounds b;
    for (const Entity& e : doc_.entities()) {
        if (e.layer == QLatin1String(kGuideLayer)) continue;
        const Bounds eb = e.bounds();
        if (eb.valid) {
            b.expand(eb.min);
            b.expand(eb.max);
        }
    }
    return b.valid ? b.max.x - b.min.x : 0.0;
}

double MechModelCanvasDialog::drawnAcrossMm() const {
    Bounds b;
    for (const Entity& e : doc_.entities()) {
        if (e.layer == QLatin1String(kGuideLayer)) continue;
        const Bounds eb = e.bounds();
        if (eb.valid) {
            b.expand(eb.min);
            b.expand(eb.max);
        }
    }
    return b.valid ? b.max.y - b.min.y : 0.0;
}

void MechModelCanvasDialog::useTool(int toolId) {
    if (editor_) editor_->setTool(static_cast<ToolId>(toolId));
}

} // namespace Loopwire
