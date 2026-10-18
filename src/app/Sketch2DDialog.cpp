#include "app/Sketch2DDialog.h"

#include "app/Chrome.h"
#include "app/Icons3D.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Loopwire {

namespace {
const char* kToolNames[6] = {"Select", "Lines", "Rectangle", "Circle", "Polygon", "Slot"};
const char* kToolKeys[6] = {"Select", "Lines", "Rect", "Circle", "Polygon", "Slot"};
const char* kToolTips[6] = {
    "Click an outline to select it, a corner to pick it; drag a corner to move it",
    "Click each corner; click the first again (or Enter, or a double click) to close it",
    "Two opposite corners",
    "Its middle, then a point on its edge",
    "Its middle, then a corner - as many sides as set",
    "The middle of one end, then of the other - as wide as set",
};

double dist(const QPointF& a, const QPointF& b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }
} // namespace

// ---- The canvas: the plane drawn flat, the sketch on it ----------------------------------------------------------

class SketchCanvas2D : public QWidget {
public:
    explicit SketchCanvas2D(Sketch2DDialog* d) : QWidget(d), d_(d) {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(520, 420);
        setObjectName(QStringLiteral("sketch2dCanvas"));
    }
    // ALL OF IT IN VIEW: the sketch and the model under it, or 40 mm round the origin.
    void fit() {
        double lo[2] = {1e18, 1e18}, hi[2] = {-1e18, -1e18};
        auto take = [&](const QPointF& p) {
            lo[0] = std::min(lo[0], p.x());
            lo[1] = std::min(lo[1], p.y());
            hi[0] = std::max(hi[0], p.x());
            hi[1] = std::max(hi[1], p.y());
        };
        for (const auto& t : d_->seen_)
            for (const QPointF& p : t) take(p);
        for (const SketchLoop& l : d_->loops_)
            for (const auto& [u, v] : l) take({u, v});
        take({0, 0});
        if (hi[0] - lo[0] < 1e-6 && hi[1] - lo[1] < 1e-6) {
            lo[0] = lo[1] = -20;
            hi[0] = hi[1] = 20;
        }
        centre_ = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2};
        scale_ = std::clamp(std::min(width() / std::max(1.0, (hi[0] - lo[0]) * 1.25), height() / std::max(1.0, (hi[1] - lo[1]) * 1.25)), 0.5, 2000.0);
        update();
    }
    QPointF toPlane(const QPointF& s) const { return {centre_.x() + (s.x() - width() / 2.0) / scale_, centre_.y() - (s.y() - height() / 2.0) / scale_}; }
    QPointF toScreen(const QPointF& p) const { return {width() / 2.0 + (p.x() - centre_.x()) * scale_, height() / 2.0 - (p.y() - centre_.y()) * scale_}; }
    double scale() const { return scale_; }
    QPointF hover() const { return hover_; }

protected:
    void resizeEvent(QResizeEvent*) override {
        if (!fitted_) {
            fitted_ = true;
            fit();
        }
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(250, 251, 253));
        p.setRenderHint(QPainter::Antialiasing, false);
        // THE GRID: its lines every step, and every tenth darker - fewer where they would crowd.
        const double grid = d_->gridBox_->currentData().toDouble();
        double step = std::max(0.01, grid);
        while (step * scale_ < 8.0) step *= 5.0;
        const QPointF a = toPlane({0, static_cast<double>(height())}), b = toPlane({static_cast<double>(width()), 0});
        for (int axis = 0; axis < 2; ++axis) {
            const double from = std::floor((axis == 0 ? a.x() : a.y()) / step), to = std::ceil((axis == 0 ? b.x() : b.y()) / step);
            for (double k = from; k <= to; ++k) {
                const bool major = std::fmod(std::abs(k), 10.0) < 0.5;
                p.setPen(QPen(major ? QColor(214, 220, 230) : QColor(234, 238, 244), 1));
                if (axis == 0) {
                    const double x = toScreen({k * step, 0}).x();
                    p.drawLine(QPointF(x, 0), QPointF(x, height()));
                } else {
                    const double y = toScreen({0, k * step}).y();
                    p.drawLine(QPointF(0, y), QPointF(width(), y));
                }
            }
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        // THE MODEL SEEN THROUGH THE PLANE, faint; where the plane cuts it, dark.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(150, 160, 175, 28));
        for (const auto& t : d_->seen_) {
            const QPointF pts[3] = {toScreen(t[0]), toScreen(t[1]), toScreen(t[2])};
            p.drawPolygon(pts, 3);
        }
        p.setPen(QPen(QColor(40, 70, 120), 1.6));
        for (const auto& [s, e] : d_->section_) p.drawLine(toScreen(s), toScreen(e));
        // ITS TWO AXES through the origin: the first red, the second green - and the one it is turned round, dashed.
        const QPointF o = toScreen({0, 0});
        const bool revolve = d_->finish_->currentIndex() == 1;
        for (int axis = 0; axis < 2; ++axis) {
            QPen pen(axis == 0 ? QColor(200, 60, 60) : QColor(40, 150, 70), revolve && d_->axis_->currentIndex() == axis ? 2.0 : 1.0);
            if (revolve && d_->axis_->currentIndex() == axis) pen.setStyle(Qt::DashDotLine);
            p.setPen(pen);
            if (axis == 0) p.drawLine(QPointF(0, o.y()), QPointF(width(), o.y()));
            else p.drawLine(QPointF(o.x(), 0), QPointF(o.x(), height()));
        }
        // THE OUTLINES: an outline a solid filled, a hole in it not; the one selected orange.
        const std::vector<SketchRegion> regions = sketchRegions(d_->loops_);
        for (const SketchRegion& r : regions) {
            QPainterPath path;
            path.setFillRule(Qt::OddEvenFill);
            auto add = [&](const SketchLoop& l) {
                QPolygonF poly;
                for (const auto& [u, v] : l) poly << toScreen({u, v});
                path.addPolygon(poly);
                path.closeSubpath();
            };
            add(r.outer);
            for (const SketchLoop& h : r.holes) add(h);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(80, 150, 230, 60));
            p.drawPath(path);
        }
        for (size_t i = 0; i < d_->loops_.size(); ++i) {
            const SketchLoop& l = d_->loops_[i];
            const bool sel = static_cast<int>(i) == d_->selLoop_;
            QPolygonF poly;
            for (const auto& [u, v] : l) poly << toScreen({u, v});
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(sel ? QColor(235, 130, 30) : QColor(30, 40, 60), sel ? 2.2 : 1.6));
            p.drawPolygon(poly);
            for (int k = 0; k < poly.size(); ++k) {
                const bool corner = sel && k == d_->selCorner_;
                p.setPen(Qt::NoPen);
                p.setBrush(corner ? QColor(235, 90, 30) : sel ? QColor(235, 130, 30) : QColor(30, 40, 60));
                const double r = corner ? 4.0 : 2.2;
                p.drawRect(QRectF(poly[k].x() - r, poly[k].y() - r, 2 * r, 2 * r));
            }
        }
        // BEING DRAWN: the lines so far and on to the pointer; a shape from its first click to the pointer.
        p.setPen(QPen(QColor(20, 140, 220), 1.6, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        QString measure;
        if (d_->tool_ == Sketch2DDialog::Lines && !d_->drawing_.empty()) {
            QPolygonF poly;
            for (const auto& [u, v] : d_->drawing_) poly << toScreen({u, v});
            poly << toScreen(hover_);
            p.drawPolyline(poly);
            const QPointF last(d_->drawing_.back().first, d_->drawing_.back().second);
            measure = QStringLiteral("%1 mm").arg(dist(last, hover_), 0, 'f', 2);
        } else if (d_->first_ && d_->tool_ != Sketch2DDialog::Select) {
            const SketchLoop l = d_->shapeFrom(*d_->first_, hover_);
            QPolygonF poly;
            for (const auto& [u, v] : l) poly << toScreen({u, v});
            p.drawPolygon(poly);
            const double du = std::abs(hover_.x() - d_->first_->x()), dv = std::abs(hover_.y() - d_->first_->y());
            if (d_->tool_ == Sketch2DDialog::Rectangle) measure = QStringLiteral("%1 x %2 mm").arg(du, 0, 'f', 2).arg(dv, 0, 'f', 2);
            else if (d_->tool_ == Sketch2DDialog::Slot) measure = QStringLiteral("%1 mm between the ends").arg(dist(*d_->first_, hover_), 0, 'f', 2);
            else measure = QStringLiteral("Ø %1 mm").arg(2.0 * dist(*d_->first_, hover_), 0, 'f', 2);
        }
        // THE POINTER: where it is, snapped, and what is being drawn measures.
        const QPointF h = toScreen(hover_);
        p.setPen(QPen(QColor(20, 140, 220), 1));
        p.drawLine(h - QPointF(6, 0), h + QPointF(6, 0));
        p.drawLine(h - QPointF(0, 6), h + QPointF(0, 6));
        p.setPen(QColor(50, 60, 75));
        p.drawText(QRectF(8, height() - 22, width() - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1, %2 mm   %3").arg(hover_.x(), 0, 'f', 2).arg(hover_.y(), 0, 'f', 2).arg(measure));
    }
    void mousePressEvent(QMouseEvent* e) override {
        setFocus();
        if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
            panning_ = true;
            last_ = e->position();
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        const QPointF at = d_->snapped(toPlane(e->position()), 8.0 / scale_);
        if (d_->tool_ == Sketch2DDialog::Select) {
            // A CORNER under the pointer picked, to drag; else the outline under it.
            pressCorner_ = -1;
            for (size_t i = 0; i < d_->loops_.size(); ++i)
                for (size_t k = 0; k < d_->loops_[i].size(); ++k)
                    if (dist(toScreen({d_->loops_[i][k].first, d_->loops_[i][k].second}), e->position()) < 7.0) {
                        d_->selectLoop(static_cast<int>(i), static_cast<int>(k));
                        pressCorner_ = static_cast<int>(k);
                        d_->remember();
                        return;
                    }
            const QPointF q = toPlane(e->position());
            int best = -1;
            double bestArea = 1e300;
            for (size_t i = 0; i < d_->loops_.size(); ++i)
                if (d_->loops_[i].size() >= 3 && loopContains(d_->loops_[i], q.x(), q.y()) && std::abs(loopArea(d_->loops_[i])) < bestArea) {
                    bestArea = std::abs(loopArea(d_->loops_[i]));
                    best = static_cast<int>(i);
                }
            d_->selectLoop(best);
            return;
        }
        d_->clickAt(at.x(), at.y());
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && d_->tool_ == Sketch2DDialog::Lines) d_->closeLoop();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (panning_) {
            const QPointF dd = e->position() - last_;
            last_ = e->position();
            centre_ -= QPointF(dd.x() / scale_, -dd.y() / scale_);
            update();
            return;
        }
        hover_ = d_->snapped(toPlane(e->position()), 8.0 / scale_);
        if (pressCorner_ >= 0 && (e->buttons() & Qt::LeftButton) && d_->selLoop_ >= 0) {
            SketchLoop& l = d_->loops_[static_cast<size_t>(d_->selLoop_)];
            if (pressCorner_ < static_cast<int>(l.size())) l[static_cast<size_t>(pressCorner_)] = {hover_.x(), hover_.y()};
            d_->fillPoints();
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        panning_ = false;
        if (pressCorner_ >= 0) d_->changed();
        pressCorner_ = -1;
    }
    void wheelEvent(QWheelEvent* e) override {
        const QPointF before = toPlane(e->position());
        scale_ = std::clamp(scale_ * (e->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2), 0.2, 5000.0);
        const QPointF after = toPlane(e->position());
        centre_ += before - after;
        update();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) d_->closeLoop();
        else if (e->key() == Qt::Key_Escape) d_->cancelDrawing();
        else if (e->key() == Qt::Key_Delete && d_->selLoop_ >= 0) d_->deleteLoop(d_->selLoop_);
        else if (e->matches(QKeySequence::Undo)) d_->undo();
        else QWidget::keyPressEvent(e);
    }

private:
    Sketch2DDialog* d_;
    QPointF centre_{0, 0};
    double scale_ = 10.0;
    QPointF hover_{0, 0};
    QPointF last_;
    bool panning_ = false;
    bool fitted_ = false;
    int pressCorner_ = -1;
};

// ---- The dialog ----------------------------------------------------------------------------------------------

Sketch2DDialog::Sketch2DDialog(const std::vector<ModelItem3D>& model, const QString& targetName, QWidget* parent)
    : QDialog(parent), model_(model), targetName_(targetName) {
    setWindowTitle(QStringLiteral("Sketch"));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    resize(1180, 760);
    auto* lay = new QVBoxLayout(this);

    // ---- THE PLANE, AND THE TOOLS ----
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("<b>Plane</b>"), this));
    planeBox_ = new QComboBox(this);
    planeBox_->setObjectName(QStringLiteral("sketch2dPlane"));
    planeBox_->addItems({QStringLiteral("Top (X, Y)"), QStringLiteral("Front (X, Z)"), QStringLiteral("Side (Y, Z)")});
    planeBox_->setToolTip(QStringLiteral("The plane the sketch is drawn on - or, from \"Sketch on face\", the face clicked"));
    top->addWidget(planeBox_);
    top->addWidget(new QLabel(QStringLiteral("offset"), this));
    atBox_ = new QDoubleSpinBox(this);
    atBox_->setObjectName(QStringLiteral("sketch2dAt"));
    atBox_->setRange(-10000, 10000);
    atBox_->setDecimals(2);
    atBox_->setSuffix(QStringLiteral(" mm"));
    atBox_->setToolTip(QStringLiteral("How far the plane is from the origin, along the way it is given depth"));
    top->addWidget(atBox_);
    connect(planeBox_, &QComboBox::activated, this, [this](int i) {
        if (i < 3) setPlane(i, atBox_->value());
    });
    connect(atBox_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (filling_) return;
        if (onFace_) {
            // ALONG THE FACE'S NORMAL: the plane moved off the face (or into it).
            const V3 n = turnPoint({1, 0, 0}, faceTurn_);
            facePoint_ = facePoint_ + n * (v - at_);
        }
        at_ = v;
        refresh();
    });
    top->addSpacing(16);
    for (int t = 0; t < 6; ++t) {
        auto* b = new QToolButton(this);
        b->setText(QString::fromLatin1(kToolNames[t]));
        b->setObjectName(QStringLiteral("sketch2dTool%1").arg(QLatin1String(kToolKeys[t])));
        b->setToolTip(QString::fromLatin1(kToolTips[t]));
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        connect(b, &QToolButton::clicked, this, [this, t] { setTool(static_cast<Tool>(t)); });
        top->addWidget(b);
        toolButtons_[t] = b;
    }
    top->addWidget(new QLabel(QStringLiteral("sides"), this));
    sidesBox_ = new QSpinBox(this);
    sidesBox_->setObjectName(QStringLiteral("sketch2dSides"));
    sidesBox_->setRange(3, 64);
    sidesBox_->setValue(6);
    top->addWidget(sidesBox_);
    top->addWidget(new QLabel(QStringLiteral("slot width"), this));
    slotWidth_ = new QDoubleSpinBox(this);
    slotWidth_->setObjectName(QStringLiteral("sketch2dSlotWidth"));
    slotWidth_->setRange(0.05, 10000);
    slotWidth_->setValue(2.0);
    slotWidth_->setSuffix(QStringLiteral(" mm"));
    top->addWidget(slotWidth_);
    top->addStretch(1);
    lay->addLayout(top);

    auto* second = new QHBoxLayout;
    second->addWidget(new QLabel(QStringLiteral("Grid"), this));
    gridBox_ = new QComboBox(this);
    gridBox_->setObjectName(QStringLiteral("sketch2dGrid"));
    for (double g : {0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0}) gridBox_->addItem(QStringLiteral("%1 mm").arg(g), g);
    gridBox_->setCurrentIndex(3);
    connect(gridBox_, &QComboBox::currentIndexChanged, this, [this] { canvas_->update(); });
    second->addWidget(gridBox_);
    snapBox_ = new QCheckBox(QStringLiteral("Snap to the grid, corners and the model's edges"), this);
    snapBox_->setObjectName(QStringLiteral("sketch2dSnap"));
    snapBox_->setChecked(true);
    second->addWidget(snapBox_);
    second->addSpacing(16);
    second->addWidget(new QLabel(QStringLiteral("Point"), this));
    typedU_ = new QDoubleSpinBox(this);
    typedV_ = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* s : {typedU_, typedV_}) {
        s->setRange(-10000, 10000);
        s->setDecimals(3);
        s->setSuffix(QStringLiteral(" mm"));
        second->addWidget(s);
    }
    typedU_->setObjectName(QStringLiteral("sketch2dTypedU"));
    typedV_->setObjectName(QStringLiteral("sketch2dTypedV"));
    auto* addPoint = new QPushButton(QStringLiteral("Add point"), this);
    addPoint->setObjectName(QStringLiteral("sketch2dAddPoint"));
    addPoint->setToolTip(QStringLiteral("A click at the point typed - as the tool takes it"));
    connect(addPoint, &QPushButton::clicked, this, [this] { clickAt(typedU_->value(), typedV_->value()); });
    second->addWidget(addPoint);
    auto* close = new QPushButton(QStringLiteral("Close outline"), this);
    close->setObjectName(QStringLiteral("sketch2dClose"));
    connect(close, &QPushButton::clicked, this, [this] { closeLoop(); });
    second->addWidget(close);
    auto* undoB = new QPushButton(QStringLiteral("Undo"), this);
    connect(undoB, &QPushButton::clicked, this, [this] { undo(); });
    second->addWidget(undoB);
    auto* fitB = new QPushButton(QStringLiteral("Fit"), this);
    connect(fitB, &QPushButton::clicked, this, [this] { canvas_->fit(); });
    second->addWidget(fitB);
    second->addStretch(1);
    lay->addLayout(second);

    // ---- THE CANVAS, AND THE OUTLINE SELECTED ----
    auto* split = new QSplitter(Qt::Horizontal, this);
    canvas_ = new SketchCanvas2D(this);
    split->addWidget(canvas_);
    auto* side = new QWidget(split);
    auto* sl = new QVBoxLayout(side);
    sl->setContentsMargins(0, 0, 0, 0);
    auto* selBox = new QGroupBox(QStringLiteral("The outline selected"), side);
    auto* sf = new QVBoxLayout(selBox);
    points_ = new QTableWidget(0, 2, selBox);
    points_->setObjectName(QStringLiteral("sketch2dPoints"));
    points_->setHorizontalHeaderLabels({QStringLiteral("First (mm)"), QStringLiteral("Second (mm)")});
    points_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(points_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (!filling_ && selLoop_ >= 0) {
            selCorner_ = row;
            canvas_->update();
        }
    });
    connect(points_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (filling_ || selLoop_ < 0) return;
        const SketchLoop& l = loops_[static_cast<size_t>(selLoop_)];
        const int row = it->row();
        if (row < 0 || row >= static_cast<int>(l.size())) return;
        bool ok = false;
        const double value = it->text().toDouble(&ok);
        if (!ok) {
            fillPoints();
            return;
        }
        auto [u, v] = l[static_cast<size_t>(row)];
        (it->column() == 0 ? u : v) = value;
        movePoint(selLoop_, row, u, v);
    });
    sf->addWidget(points_, 1);
    auto* cornerRow = new QHBoxLayout;
    cornerRow->addWidget(new QLabel(QStringLiteral("Round the corner"), selBox));
    radius_ = new QDoubleSpinBox(selBox);
    radius_->setObjectName(QStringLiteral("sketch2dRadius"));
    radius_->setRange(0.01, 10000);
    radius_->setValue(1.0);
    radius_->setSuffix(QStringLiteral(" mm"));
    cornerRow->addWidget(radius_);
    auto* round = new QPushButton(QStringLiteral("Round"), selBox);
    round->setObjectName(QStringLiteral("sketch2dRound"));
    round->setToolTip(QStringLiteral("The corner picked (click it with Select, or its row) rounded to this radius"));
    connect(round, &QPushButton::clicked, this, [this] { roundCorner(selLoop_, selCorner_, radius_->value()); });
    cornerRow->addWidget(round);
    sf->addLayout(cornerRow);
    auto* loopRow = new QHBoxLayout;
    auto* mirrorU = new QPushButton(QStringLiteral("Mirror across first axis"), selBox);
    mirrorU->setObjectName(QStringLiteral("sketch2dMirrorU"));
    connect(mirrorU, &QPushButton::clicked, this, [this] { mirrorLoop(selLoop_, 0); });
    auto* mirrorV = new QPushButton(QStringLiteral("Mirror across second axis"), selBox);
    mirrorV->setObjectName(QStringLiteral("sketch2dMirrorV"));
    connect(mirrorV, &QPushButton::clicked, this, [this] { mirrorLoop(selLoop_, 1); });
    auto* del = new QPushButton(QStringLiteral("Delete"), selBox);
    del->setObjectName(QStringLiteral("sketch2dDelete"));
    connect(del, &QPushButton::clicked, this, [this] { deleteLoop(selLoop_); });
    loopRow->addWidget(mirrorU);
    loopRow->addWidget(mirrorV);
    loopRow->addWidget(del);
    sf->addLayout(loopRow);
    sl->addWidget(selBox, 1);

    // ---- WHAT IS MADE OF IT ----
    auto* makeBox = new QGroupBox(QStringLiteral("Make a solid of it"), side);
    auto* mf = new QFormLayout(makeBox);
    finish_ = new QComboBox(makeBox);
    finish_->setObjectName(QStringLiteral("sketch2dFinish"));
    finish_->addItems({QStringLiteral("Extrude - given depth"), QStringLiteral("Revolve - turned round an axis")});
    mf->addRow(QStringLiteral("How"), finish_);
    depthLabel_ = new QLabel(QStringLiteral("Depth"), makeBox);
    depth_ = new QDoubleSpinBox(makeBox);
    depth_->setObjectName(QStringLiteral("sketch2dDepth"));
    depth_->setRange(-10000, 10000);
    depth_->setDecimals(2);
    depth_->setValue(5.0);
    depth_->setSuffix(QStringLiteral(" mm"));
    depth_->setToolTip(QStringLiteral("Toward you as you look at the plane - or, below nothing, away from you"));
    mf->addRow(depthLabel_, depth_);
    both_ = new QCheckBox(QStringLiteral("Both ways - half each side of the plane"), makeBox);
    both_->setObjectName(QStringLiteral("sketch2dBoth"));
    mf->addRow(QString(), both_);
    angleLabel_ = new QLabel(QStringLiteral("Angle"), makeBox);
    angle_ = new QDoubleSpinBox(makeBox);
    angle_->setObjectName(QStringLiteral("sketch2dAngle"));
    angle_->setRange(0.5, 360);
    angle_->setValue(360);
    angle_->setSuffix(QStringLiteral("°"));
    mf->addRow(angleLabel_, angle_);
    axisLabel_ = new QLabel(QStringLiteral("Round"), makeBox);
    axis_ = new QComboBox(makeBox);
    axis_->setObjectName(QStringLiteral("sketch2dAxis"));
    axis_->addItems({QStringLiteral("the first axis (red)"), QStringLiteral("the second axis (green)")});
    connect(axis_, &QComboBox::currentIndexChanged, this, [this] { canvas_->update(); });
    mf->addRow(axisLabel_, axis_);
    combine_ = new QComboBox(makeBox);
    combine_->setObjectName(QStringLiteral("sketch2dCombine"));
    combine_->addItem(QStringLiteral("A new solid"), 0);
    if (!targetName_.isEmpty()) {
        combine_->addItem(QStringLiteral("Added to %1").arg(targetName_), 1);
        combine_->addItem(QStringLiteral("Cut out of %1").arg(targetName_), 2);
        combine_->addItem(QStringLiteral("Kept where it meets %1").arg(targetName_), 3);
    }
    combine_->addItem(QStringLiteral("Cut out of every solid"), 4);
    mf->addRow(QStringLiteral("As"), combine_);
    connect(finish_, &QComboBox::currentIndexChanged, this, [this] { finishChanged(); });
    sl->addWidget(makeBox);
    split->addWidget(side);
    split->setStretchFactor(0, 1);
    split->setSizes({820, 340});
    lay->addWidget(split, 1);

    said_ = new QLabel(this);
    said_->setObjectName(QStringLiteral("sketch2dSaid"));
    said_->setWordWrap(true);
    lay->addWidget(said_);
    auto* bb = new QDialogButtonBox(this);
    auto* ok = bb->addButton(QStringLiteral("Make the solid"), QDialogButtonBox::AcceptRole);
    ok->setObjectName(QStringLiteral("sketch2dOk"));
    ok->setIcon(icon3D(QStringLiteral("extrude"), Chrome::ink()));
    bb->addButton(QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        if (sketchRegions(loops_).empty()) {
            said_->setText(QStringLiteral("Draw a closed outline first - three corners at least."));
            return;
        }
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    // Enter closes an outline - it does not make the solid.
    ok->setAutoDefault(false);
    ok->setDefault(false);

    finishChanged();
    setTool(Lines);
    setPlane(0, 0.0);
}

void Sketch2DDialog::finishChanged() {
    const bool revolve = finish_->currentIndex() == 1;
    for (QWidget* w : {static_cast<QWidget*>(depth_), static_cast<QWidget*>(depthLabel_), static_cast<QWidget*>(both_)}) w->setVisible(!revolve);
    for (QWidget* w : {static_cast<QWidget*>(angle_), static_cast<QWidget*>(angleLabel_), static_cast<QWidget*>(axis_), static_cast<QWidget*>(axisLabel_)})
        w->setVisible(revolve);
    canvas_->update();
}

void Sketch2DDialog::setPlane(int plane, double at) {
    plane_ = std::clamp(plane, 0, 2);
    at_ = at;
    onFace_ = false;
    {
        const QSignalBlocker hold(planeBox_);
        while (planeBox_->count() > 3) planeBox_->removeItem(3);
        planeBox_->setCurrentIndex(plane_);
    }
    filling_ = true;
    atBox_->setValue(at);
    filling_ = false;
    refresh();
    canvas_->fit();
}

void Sketch2DDialog::setFace(const V3& point, const V3& normal) {
    plane_ = 2;
    at_ = 0.0;
    onFace_ = true;
    facePoint_ = point;
    faceTurn_ = turnAlong(normal);
    {
        const QSignalBlocker hold(planeBox_);
        while (planeBox_->count() > 3) planeBox_->removeItem(3);
        planeBox_->addItem(QStringLiteral("On the face clicked"));
        planeBox_->setCurrentIndex(3);
    }
    filling_ = true;
    atBox_->setValue(0.0);
    filling_ = false;
    refresh();
    canvas_->fit();
}

void Sketch2DDialog::setStart(const Result& s) {
    // A FACE'S PLANE is the side's, turned: what is turned is drawn as it was, in its own plane.
    if (std::abs(s.turnDeg.x) + std::abs(s.turnDeg.y) + std::abs(s.turnDeg.z) > 1e-9 || (s.plane == 2 && (s.at.y != 0 || s.at.z != 0))) {
        onFace_ = true;
        plane_ = 2;
        facePoint_ = s.at;
        faceTurn_ = s.turnDeg;
        at_ = 0.0;
        const QSignalBlocker hold(planeBox_);
        while (planeBox_->count() > 3) planeBox_->removeItem(3);
        planeBox_->addItem(QStringLiteral("On the face clicked"));
        planeBox_->setCurrentIndex(3);
        filling_ = true;
        atBox_->setValue(0.0);
        filling_ = false;
    } else {
        V3 u, v, w;
        sketchAxes(s.plane, u, v, w);
        setPlane(s.plane, dot3(s.at, w));
    }
    loops_ = s.loops;
    finish_->setCurrentIndex(s.revolve ? 1 : 0);
    depth_->setValue(s.depth);
    both_->setChecked(s.both);
    angle_->setValue(s.angle);
    axis_->setCurrentIndex(s.axis);
    const int want = s.cutAll ? 4 : static_cast<int>(s.op);
    combine_->setCurrentIndex(std::max(0, combine_->findData(want)));
    undo_.clear();
    refresh();
    changed();
    canvas_->fit();
}

Sketch2DDialog::Result Sketch2DDialog::result() const {
    Result r;
    r.plane = plane_;
    if (onFace_) {
        r.at = facePoint_;
        r.turnDeg = faceTurn_;
    } else {
        V3 u, v, w;
        sketchAxes(plane_, u, v, w);
        r.at = w * at_;
    }
    r.loops = loops_;
    r.revolve = finish_->currentIndex() == 1;
    r.depth = depth_->value();
    r.both = both_->isChecked();
    r.angle = angle_->value();
    r.axis = axis_->currentIndex();
    const int c = combine_->currentData().toInt();
    r.cutAll = c == 4;
    r.op = c >= 1 && c <= 3 ? static_cast<BodyOp>(c) : BodyOp::None;
    return r;
}

void Sketch2DDialog::refresh() {
    // EACH POINT OF THE MODEL IN THE PLANE'S OWN TERMS: along its two directions, and how far off it.
    V3 u, v, w;
    sketchAxes(plane_, u, v, w);
    auto local = [&](const V3& p) {
        if (onFace_) {
            const V3 q = turnPointBack(p - facePoint_, faceTurn_);
            return V3{dot3(q, u), dot3(q, v), dot3(q, w)};
        }
        return V3{dot3(p, u), dot3(p, v), dot3(p, w) - at_};
    };
    seen_.clear();
    section_.clear();
    for (const ModelItem3D& it : model_) {
        if (it.alpha < 0.99) continue;
        std::vector<V3> pts;
        pts.reserve(it.mesh.v.size());
        for (const V3& p : it.mesh.v) pts.push_back(local(p));
        for (const auto& t : it.mesh.f) {
            const V3 &a = pts[static_cast<size_t>(t[0])], &b = pts[static_cast<size_t>(t[1])], &c = pts[static_cast<size_t>(t[2])];
            seen_.push_back({QPointF(a.x, a.y), QPointF(b.x, b.y), QPointF(c.x, c.y)});
            // WHERE THE PLANE CUTS IT: the two edges that cross it.
            std::vector<QPointF> cross;
            const V3* corner[3] = {&a, &b, &c};
            for (int k = 0; k < 3; ++k) {
                const V3& p = *corner[k];
                const V3& q = *corner[(k + 1) % 3];
                if ((p.z > 0) != (q.z > 0) && std::abs(p.z - q.z) > 1e-12) {
                    const double s = p.z / (p.z - q.z);
                    cross.emplace_back(p.x + (q.x - p.x) * s, p.y + (q.y - p.y) * s);
                }
            }
            if (cross.size() == 2 && dist(cross[0], cross[1]) > 1e-9) section_.emplace_back(cross[0], cross[1]);
        }
    }
    canvas_->update();
}

QPointF Sketch2DDialog::snapped(const QPointF& p, double reach) const {
    if (!snapBox_->isChecked()) return p;
    QPointF best = p;
    double nearest = reach;
    auto take = [&](const QPointF& q) {
        const double d = dist(p, q);
        if (d < nearest) {
            nearest = d;
            best = q;
        }
    };
    for (const SketchLoop& l : loops_)
        for (const auto& [u, v] : l) take({u, v});
    for (const auto& [u, v] : drawing_) take({u, v});
    for (const auto& [a, b] : section_) {
        take(a);
        take(b);
    }
    take({0, 0});
    if (nearest < reach) return best;
    const double g = gridBox_->currentData().toDouble();
    return {std::round(p.x() / g) * g, std::round(p.y() / g) * g};
}

void Sketch2DDialog::setTool(Tool tool) {
    cancelDrawing();
    tool_ = tool;
    for (int t = 0; t < 6; ++t) {
        const QSignalBlocker hold(toolButtons_[t]);
        toolButtons_[t]->setChecked(t == tool);
    }
    canvas_->setCursor(tool == Select ? Qt::ArrowCursor : Qt::CrossCursor);
    said_->setText(QString::fromLatin1(kToolTips[tool]) + QStringLiteral(".  Drag with the right or middle button to pan; the wheel zooms."));
}

SketchLoop Sketch2DDialog::shapeFrom(const QPointF& a, const QPointF& b) const {
    SketchLoop l;
    if (tool_ == Rectangle) {
        l = {{a.x(), a.y()}, {b.x(), a.y()}, {b.x(), b.y()}, {a.x(), b.y()}};
    } else if (tool_ == Circle) {
        const double r = dist(a, b);
        for (int k = 0; k < 48; ++k) l.emplace_back(a.x() + r * std::cos(k * M_PI / 24.0), a.y() + r * std::sin(k * M_PI / 24.0));
    } else if (tool_ == Polygon) {
        const double r = dist(a, b), start = std::atan2(b.y() - a.y(), b.x() - a.x());
        const int n = sidesBox_->value();
        for (int k = 0; k < n; ++k) l.emplace_back(a.x() + r * std::cos(start + 2.0 * M_PI * k / n), a.y() + r * std::sin(start + 2.0 * M_PI * k / n));
    } else if (tool_ == Slot) {
        // TWO HALF CIRCLES round the two middles, joined by straight sides.
        const double r = slotWidth_->value() / 2.0, len = dist(a, b);
        const double dir = len > 1e-9 ? std::atan2(b.y() - a.y(), b.x() - a.x()) : 0.0;
        for (int k = 0; k <= 16; ++k) {
            const double t = dir - M_PI / 2 + M_PI * k / 16;
            l.emplace_back(b.x() + r * std::cos(t), b.y() + r * std::sin(t));
        }
        for (int k = 0; k <= 16; ++k) {
            const double t = dir + M_PI / 2 + M_PI * k / 16;
            l.emplace_back(a.x() + r * std::cos(t), a.y() + r * std::sin(t));
        }
    }
    return l;
}

void Sketch2DDialog::clickAt(double u, double v) {
    const QPointF at(u, v);
    if (tool_ == Select) return;
    if (tool_ == Lines) {
        // ON THE FIRST CORNER AGAIN: the outline closed.
        if (drawing_.size() >= 3 && dist(at, {drawing_.front().first, drawing_.front().second}) < 1e-6) {
            closeLoop();
            return;
        }
        if (!drawing_.empty() && dist(at, {drawing_.back().first, drawing_.back().second}) < 1e-9) return;
        drawing_.emplace_back(u, v);
        said_->setText(QStringLiteral("%1 corner(s) - click the first again, or Enter, to close it.").arg(drawing_.size()));
        canvas_->update();
        return;
    }
    if (!first_) {
        first_ = at;
        canvas_->update();
        return;
    }
    const QPointF a = *first_;
    first_.reset();
    if (dist(a, at) < 1e-9) return;
    addLoop(shapeFrom(a, at));
}

bool Sketch2DDialog::closeLoop() {
    if (tool_ != Lines || drawing_.size() < 3) return false;
    SketchLoop l = drawing_;
    drawing_.clear();
    addLoop(std::move(l));
    return true;
}

void Sketch2DDialog::cancelDrawing() {
    drawing_.clear();
    first_.reset();
    if (canvas_) canvas_->update();
}

void Sketch2DDialog::addLoop(SketchLoop loop) {
    if (loop.size() < 3 || std::abs(loopArea(loop)) < 1e-9) {
        said_->setText(QStringLiteral("That outline has no area."));
        return;
    }
    remember();
    loops_.push_back(std::move(loop));
    selLoop_ = static_cast<int>(loops_.size()) - 1;
    selCorner_ = -1;
    changed();
}

void Sketch2DDialog::setLoops(const std::vector<SketchLoop>& loops) {
    remember();
    loops_ = loops;
    selLoop_ = loops_.empty() ? -1 : 0;
    changed();
}

void Sketch2DDialog::remember() {
    undo_.push_back(loops_);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
}

void Sketch2DDialog::undo() {
    if (!drawing_.empty()) {
        drawing_.pop_back();
        canvas_->update();
        return;
    }
    if (undo_.empty()) return;
    loops_ = undo_.back();
    undo_.pop_back();
    if (selLoop_ >= static_cast<int>(loops_.size())) selLoop_ = -1;
    changed();
}

void Sketch2DDialog::selectLoop(int loop, int corner) {
    selLoop_ = loop >= 0 && loop < static_cast<int>(loops_.size()) ? loop : -1;
    selCorner_ = selLoop_ >= 0 ? corner : -1;
    fillPoints();
    canvas_->update();
}

void Sketch2DDialog::changed() {
    fillPoints();
    const std::vector<SketchRegion> regions = sketchRegions(loops_);
    int holes = 0;
    for (const SketchRegion& r : regions) holes += static_cast<int>(r.holes.size());
    said_->setText(QStringLiteral("%1 outline(s): %2 solid(s), %3 hole(s).").arg(loops_.size()).arg(regions.size()).arg(holes));
    canvas_->update();
}

void Sketch2DDialog::fillPoints() {
    filling_ = true;
    points_->setRowCount(0);
    if (selLoop_ >= 0) {
        const SketchLoop& l = loops_[static_cast<size_t>(selLoop_)];
        points_->setRowCount(static_cast<int>(l.size()));
        for (size_t k = 0; k < l.size(); ++k) {
            points_->setItem(static_cast<int>(k), 0, new QTableWidgetItem(QString::number(l[k].first, 'f', 3)));
            points_->setItem(static_cast<int>(k), 1, new QTableWidgetItem(QString::number(l[k].second, 'f', 3)));
        }
        if (selCorner_ >= 0) points_->setCurrentCell(selCorner_, 0);
    }
    filling_ = false;
}

bool Sketch2DDialog::roundCorner(int loop, int corner, double radius) {
    if (loop < 0 || loop >= static_cast<int>(loops_.size()) || radius <= 0.0) return false;
    SketchLoop& l = loops_[static_cast<size_t>(loop)];
    const int n = static_cast<int>(l.size());
    if (corner < 0 || corner >= n || n < 3) {
        said_->setText(QStringLiteral("Pick a corner to round: click it with Select, or choose its row."));
        return false;
    }
    const QPointF p(l[static_cast<size_t>(corner)].first, l[static_cast<size_t>(corner)].second);
    const QPointF a(l[static_cast<size_t>((corner + n - 1) % n)].first, l[static_cast<size_t>((corner + n - 1) % n)].second);
    const QPointF b(l[static_cast<size_t>((corner + 1) % n)].first, l[static_cast<size_t>((corner + 1) % n)].second);
    const double la = dist(a, p), lb = dist(b, p);
    if (la < 1e-9 || lb < 1e-9) return false;
    const QPointF e1 = (a - p) / la, e2 = (b - p) / lb;
    const double cosT = std::clamp(e1.x() * e2.x() + e1.y() * e2.y(), -1.0, 1.0), theta = std::acos(cosT);
    if (theta < 1e-3 || theta > M_PI - 1e-3) {
        said_->setText(QStringLiteral("That corner is straight - there is nothing to round."));
        return false;
    }
    // AS FAR BACK ALONG EACH SIDE AS THE RADIUS WANTS - no further than nearly half of the shorter side.
    double back = radius / std::tan(theta / 2.0);
    back = std::min(back, 0.49 * std::min(la, lb));
    const double r = back * std::tan(theta / 2.0);
    const QPointF t1 = p + e1 * back, t2 = p + e2 * back;
    QPointF bis = e1 + e2;
    bis /= std::hypot(bis.x(), bis.y());
    const QPointF c = p + bis * (r / std::sin(theta / 2.0));
    const double a1 = std::atan2(t1.y() - c.y(), t1.x() - c.x());
    double sweep = std::remainder(std::atan2(t2.y() - c.y(), t2.x() - c.x()) - a1, 2.0 * M_PI);
    SketchLoop arc;
    const int steps = 8;
    for (int k = 0; k <= steps; ++k) {
        const double t = a1 + sweep * k / steps;
        arc.emplace_back(c.x() + r * std::cos(t), c.y() + r * std::sin(t));
    }
    remember();
    l.erase(l.begin() + corner);
    l.insert(l.begin() + corner, arc.begin(), arc.end());
    selCorner_ = -1;
    changed();
    return true;
}

void Sketch2DDialog::deleteLoop(int loop) {
    if (loop < 0 || loop >= static_cast<int>(loops_.size())) return;
    remember();
    loops_.erase(loops_.begin() + loop);
    selLoop_ = -1;
    selCorner_ = -1;
    changed();
}

void Sketch2DDialog::mirrorLoop(int loop, int axis) {
    if (loop < 0 || loop >= static_cast<int>(loops_.size())) return;
    SketchLoop m = loops_[static_cast<size_t>(loop)];
    for (auto& [u, v] : m) (axis == 0 ? v : u) = -(axis == 0 ? v : u);
    std::reverse(m.begin(), m.end());
    addLoop(std::move(m));
}

void Sketch2DDialog::movePoint(int loop, int corner, double u, double v) {
    if (loop < 0 || loop >= static_cast<int>(loops_.size())) return;
    SketchLoop& l = loops_[static_cast<size_t>(loop)];
    if (corner < 0 || corner >= static_cast<int>(l.size())) return;
    remember();
    l[static_cast<size_t>(corner)] = {u, v};
    selCorner_ = corner;
    changed();
}

} // namespace Loopwire
