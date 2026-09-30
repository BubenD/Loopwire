#include "app/BrickWizardDialog.h"

#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "app/UiBits.h"
#include "harness/Bom.h"
#include "model/Document.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QShowEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QApplication>
#include <limits>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QSettings>
#include <QMenu>
#include <QContextMenuEvent>
#include <QToolButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <QWheelEvent>
#include <QMimeData>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>
#include <QToolTip>
#include <QHelpEvent>
#include <QShortcut>

namespace Edraw_Desktop {

namespace {

double snapHalf(double v) { return std::round(v * 2.0) / 2.0; }

double segDistance(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = len2 > 1e-12 ? ((p - a).x() * ab.x() + (p - a).y() * ab.y()) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

QString brickTitle(const Brick& b) {
    if (!b.name.isEmpty()) return b.name;
    return b.type == BrickType::End ? partKindName(b.kind) : brickTypeName(b.type);
}

// A brick's size as it sits unturned: along its two ports, and across them.
QSizeF brickSize(const Brick& b) {
    if (b.type != BrickType::End) return {82.0, 30.0};
    return brickKindIsJoint(b.kind) ? QSizeF(90.0, 34.0) : QSizeF(96.0, 38.0);
}

// A SECTION CHANGED SIZE - folded, or pulled taller or shorter - and the column it stands in lays itself
// out again at once, so what is after it moves up or down with it rather than the space staying.
void relayColumn(QWidget* section) {
    if (!section) return;
    if (section->layout()) section->layout()->activate();
    section->updateGeometry();
    section->adjustSize();
    if (QWidget* column = section->parentWidget()) {
        if (column->layout()) {
            column->layout()->invalidate();
            column->layout()->activate();
        }
        column->updateGeometry();
        column->adjustSize();
    }
}

// A GRIP ALONG THE BOTTOM OF A SECTION: dragged, the section grows or shrinks, and everything after it
// is pushed down or pulled up with it.
class SectionGrip : public QWidget {
public:
    SectionGrip(QWidget* body, QWidget* parent) : QWidget(parent), body_(body) {
        setFixedHeight(7);
        setCursor(Qt::SizeVerCursor);
        setToolTip(QStringLiteral("Drag to make this section taller or shorter"));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(QColor(0x7a, 0x84, 0x90));
        const int c = width() / 2;
        p.drawLine(c - 14, 2, c + 14, 2);
        p.drawLine(c - 14, 4, c + 14, 4);
    }
    void mousePressEvent(QMouseEvent* e) override {
        from_ = e->globalPosition().y();
        start_ = body_->height();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!(e->buttons() & Qt::LeftButton)) return;
        body_->setFixedHeight(std::max(40, int(start_ + e->globalPosition().y() - from_)));
        relayColumn(parentWidget());
    }

private:
    QWidget* body_;
    double from_ = 0.0;
    int start_ = 0;
};

// A SECTION OF THE RIGHT-HAND PANEL: a header that folds it to one line, straight after the one
// before it; its body as tall as it needs, or - given a height - as tall as it is pulled with its grip.
QWidget* foldSection(const QString& title, QWidget* body, QVBoxLayout* into, const char* name,
                     int height = 0) {
    auto* sec = new QWidget;
    auto* lay = new QVBoxLayout(sec);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto* head = new QToolButton(sec);
    head->setObjectName(QLatin1String(name));
    head->setText(title);
    head->setCheckable(true);
    head->setChecked(true);
    head->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    head->setArrowType(Qt::DownArrow);
    head->setAutoRaise(true);
    head->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    head->setStyleSheet(QStringLiteral("QToolButton{font-weight:bold;text-align:left;}"));
    lay->addWidget(head);
    lay->addWidget(body);
    QWidget* grip = nullptr;
    if (height > 0) {
        body->setFixedHeight(height);
        grip = new SectionGrip(body, sec);
        grip->setObjectName(QLatin1String(name) + QStringLiteral("Grip"));
        lay->addWidget(grip);
    }
    QObject::connect(head, &QToolButton::toggled, sec, [sec, head, body, grip](bool open) {
        body->setVisible(open);
        if (grip) grip->setVisible(open);
        head->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        relayColumn(sec);
    });
    into->addWidget(sec);
    return sec;
}

// The column the sections stand in, top to bottom, with nothing between them.
QVBoxLayout* sectionColumn(QWidget* column) {
    auto* lay = new QVBoxLayout(column);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(3);
    return lay;
}

QWidget* scrolled(QWidget* w) {
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(w);
    return sa;
}

QString layerName(const QString& key) {
    if (key == QLatin1String("cable")) return QStringLiteral("Cable");
    if (key == QLatin1String("braid")) return QStringLiteral("Braided shield");
    if (key == QLatin1String("heatshrink")) return QStringLiteral("Heatshrink");
    return QStringLiteral("Wires");
}

} // namespace

// ==================================================================================================
// The pane
// ==================================================================================================

BrickCanvas::BrickCanvas(BrickDesign* design, QWidget* parent) : QWidget(parent), d_(design) {
    setAcceptDrops(true);
    mapButton_ = new QToolButton(this);
    mapButton_->setObjectName(QStringLiteral("brickMap"));
    mapButton_->setText(QStringLiteral("Map"));
    mapButton_->setCheckable(true);
    mapButton_->setAutoRaise(true);
    mapButton_->setFocusPolicy(Qt::NoFocus);
    mapButton_->setToolTip(QStringLiteral("All the bricks small, with the part in view outlined"));
    mapButton_->setStyleSheet(QStringLiteral("QToolButton{color:#8fb0d6;font-size:10px;padding:1px 4px;}"
                                             "QToolButton:checked{background:#2b6cb0;color:white;}"));
    connect(mapButton_, &QToolButton::toggled, this, [this](bool v) { setMinimap(v); });
    // FIT: every brick in view, zoomed to fill the pane.
    fitButton_ = new QToolButton(this);
    fitButton_->setObjectName(QStringLiteral("brickFit"));
    fitButton_->setText(QStringLiteral("Fit"));
    fitButton_->setAutoRaise(true);
    fitButton_->setFocusPolicy(Qt::NoFocus);
    fitButton_->setToolTip(QStringLiteral("Zoom so every brick is in view (the wheel zooms about the cursor)"));
    fitButton_->setStyleSheet(QStringLiteral("QToolButton{color:#8fb0d6;font-size:10px;padding:1px 4px;}"));
    connect(fitButton_, &QToolButton::clicked, this, [this] { fit(); });
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(420, 220);
}

QPointF BrickCanvas::toScreen(double col, double row) const {
    return origin_ + QPointF(col * stepX_, row * stepY_);
}

void BrickCanvas::toGrid(const QPointF& at, double& col, double& row) const {
    col = (at.x() - origin_.x()) / stepX_;
    row = (at.y() - origin_.y()) / stepY_;
}

QRectF BrickCanvas::brickRect(const Brick& b) const {
    const QPointF c = toScreen(b.col, b.row);
    QSizeF sz = brickSize(b);
    if (b.rot % 2 != 0) sz.transpose();
    return QRectF(c.x() - sz.width() / 2.0, c.y() - sz.height() / 2.0, sz.width(), sz.height());
}

// A PORT SITS WHERE ITS FACE IS ON THE PART: out on the brick's edge the way the face looks, and
// across that edge as far along as the face is along that side of the part - so a Y-split's two ways
// out are one above the other, as on the part.
QPointF BrickCanvas::portPoint(const Brick& b, int port) const {
    const BrickShape s = d_->shapeOf(b);
    const QRectF r = brickRect(b);
    if (port < 0 || port >= s.ports()) return r.center();
    const Vec2 o = s.faceOut[static_cast<size_t>(port)];
    const Vec2 m = s.faceMid[static_cast<size_t>(port)];
    const Vec2 across = o.perp();
    const double reach = std::abs(across.x) * s.half.x + std::abs(across.y) * s.half.y;
    const double t = std::clamp(reach > 1e-9 ? dot(m, across) / reach : 0.0, -1.0, 1.0);
    // TURNED AND MIRRORED AS ON THE DRAWING (R 45, Space 90, M): its connections go round the brick with it.
    const Brick shown = d_->asOnPane(b);
    const Vec2 wo = Edraw_Desktop::turnOnDrawing(shown, o).normalized();
    const Vec2 wa = Edraw_Desktop::turnOnDrawing(shown, across).normalized();
    const QPointF d(wo.x, -wo.y), a(wa.x, -wa.y);
    // Out to the edge of the brick the way the connection looks - at a slant, to its corner side.
    auto toEdge = [&r](const QPointF& u) {
        const double kx = std::abs(u.x()) > 1e-9 ? r.width() / 2.0 / std::abs(u.x()) : 1e300;
        const double ky = std::abs(u.y()) > 1e-9 ? r.height() / 2.0 / std::abs(u.y()) : 1e300;
        return std::min(kx, ky);
    };
    const QPointF at = r.center() + d * toEdge(d) + a * (t * toEdge(a) * 0.75);
    return QPointF(std::clamp(at.x(), r.left(), r.right()), std::clamp(at.y(), r.top(), r.bottom()));
}

std::pair<QString, int> BrickCanvas::portAt(const QPointF& at) const {
    std::pair<QString, int> best{QString(), -1};
    double bestD = 7.0;
    for (const Brick& b : d_->bricks)
        for (int p = 0; p < d_->portCount(b); ++p) {
            const QPointF q = portPoint(b, p);
            const double dd = std::hypot(at.x() - q.x(), at.y() - q.y());
            if (dd <= bestD) {
                bestD = dd;
                best = {b.id, p};
            }
        }
    return best;
}

QString BrickCanvas::brickAt(const QPointF& at) const {
    for (auto it = d_->bricks.rbegin(); it != d_->bricks.rend(); ++it)
        if (brickRect(*it).contains(at)) return it->id;
    return QString();
}

void BrickCanvas::setSelected(const QString& id) {
    sel_.clear();
    if (!id.isEmpty()) sel_ << id;
    selLink_ = {};
    update();
}

void BrickCanvas::setSelection(const QStringList& ids) {
    sel_ = ids;
    selLink_ = {};
    update();
}

void BrickCanvas::setMinimap(bool on) {
    minimap_ = on;
    if (mapButton_->isChecked() != on) mapButton_->setChecked(on);
    update();
}

void BrickCanvas::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    mapButton_->move(width() - mapButton_->sizeHint().width() - 6, 6);
    fitButton_->move(mapButton_->x() - fitButton_->sizeHint().width() - 4, 6);
}

void BrickCanvas::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.0;
    if (std::abs(steps) < 1e-9) {
        QWidget::wheelEvent(e);
        return;
    }
    const double next = std::clamp(zoom_ * std::pow(1.15, steps), 0.2, 4.0);
    // About the cursor: whatever is under it stays under it.
    const QPointF s = e->position();
    origin_ += s / next - s / zoom_;
    zoom_ = next;
    update();
    e->accept();
}

QRectF BrickCanvas::minimapRect() const {
    return QRectF(width() - 190.0, height() - 124.0, 180.0, 114.0);
}

QPointF BrickCanvas::viewCentreGrid() const {
    double c, r;
    toGrid(QPointF(viewSize().width() / 2.0, viewSize().height() / 2.0), c, r);
    return QPointF(c, r);
}

void BrickCanvas::centreView() {
    if (d_->bricks.empty()) {
        origin_ = QPointF(80.0, 70.0);
        update();
        return;
    }
    QRectF all;
    for (const Brick& b : d_->bricks) all = all.isNull() ? brickRect(b) : all.united(brickRect(b));
    origin_ += QPointF(viewSize().width() / 2.0, viewSize().height() / 2.0) - all.center();
    update();
}

void BrickCanvas::resetZoom() {
    const QPointF mid(width() / 2.0, height() / 2.0);
    origin_ += mid / 1.0 - mid / zoom_;
    zoom_ = 1.0;
    update();
}

void BrickCanvas::fit() {
    if (d_->bricks.empty()) {
        origin_ = QPointF(80.0, 70.0);
        update();
        return;
    }
    // Everything the bricks cover, in the pane's own units, with a margin; zoomed so that fills the pane
    // (never past double size, which is all a handful of bricks needs), then centred.
    QRectF all;
    for (const Brick& b : d_->bricks) all = all.isNull() ? brickRect(b) : all.united(brickRect(b));
    all = all.adjusted(-30, -30, 30, 30);
    if (width() > 0 && height() > 0 && all.width() > 1 && all.height() > 1)
        zoom_ = std::clamp(std::min(width() / all.width(), height() / all.height()), 0.2, 2.0);
    origin_ += QPointF(viewSize().width() / 2.0, viewSize().height() / 2.0) - all.center();
    update();
}

void BrickCanvas::deleteSelected() {
    if (!sel_.isEmpty()) {
        for (const QString& id : sel_) d_->removeBrick(id);
        sel_.clear();
        emit selectionChanged(QString());
        emit designChanged();
    } else if (!selLink_.first.isEmpty()) {
        d_->disconnect(selLink_.first, selLink_.second);
        selLink_ = {};
        emit selectionChanged(QString());
        emit designChanged();
    }
    update();
}

void BrickCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    const QString hit = brickAt(L(e->position()));
    if (!hit.isEmpty()) {
        emit brickDoubleClicked(hit);
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void BrickCanvas::keepOnDrawing(const QHash<QString, QPointF>& from) {
    // The pitch the drawing was LAST DRAWN at - scaled parts (Auto) change it, and a figure worked out afresh
    // without them moved the part the other way from the brick.
    // Worked out once as the move begins, from the drawing as it is now (Auto's scales and all).
    if (rowPitchMm_ <= 0.0) rowPitchMm_ = buildBrickHarness(*d_, d_->catalog).rowPitch;
    if (rowPitchMm_ <= 0.0) rowPitchMm_ = brickRowPitchMm(*d_);
    for (auto it = from.cbegin(); it != from.cend(); ++it) {
        Brick* b = d_->brick(it.key());
        if (!b || b->type != BrickType::End) continue;
        if (!startDy_.contains(it.key())) startDy_.insert(it.key(), b->drawDyMm);
        // Drawn at -row x pitch + dy: the row changed, dy changes the other way, and it is drawn where it was.
        b->drawDyMm = startDy_.value(it.key()) + (b->row - it.value().y()) * rowPitchMm_;
    }
}

void BrickCanvas::setDimensionMode(bool on) {
    dimMode_ = on;
    dimFirst_.clear();
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    emit message(on ? QStringLiteral("Dimension: click the first brick, then the second") : QString());
}

void BrickCanvas::moveSelection(double dCol, double dRow) {
    if (sel_.isEmpty()) return;
    QHash<QString, QPointF> from;
    // Nothing goes with them: with the drawing no longer placed by the pane, a brick is not tied to another.
    const QHash<QString, BrickFollow> follow;
    for (auto it = follow.cbegin(); it != follow.cend(); ++it)
        if (Brick* b = d_->brick(it.key())) {
            from.insert(it.key(), QPointF(b->col, b->row));
            if (it.value().x) b->col += dCol;
            if (it.value().y) b->row += dRow;
        }
    for (const QString& id : sel_)
        if (Brick* b = d_->brick(id)) {
            from.insert(id, QPointF(b->col, b->row));
            b->col += dCol;
            b->row += dRow;
        }
    startDy_.clear();
    rowPitchMm_ = 0.0;
    keepOnDrawing(from);
    emit bricksMoved(from);
    emit designChanged();
    update();
}

bool BrickCanvas::event(QEvent* e) {
    // WHAT THE MARKS SAY, held over a brick.
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const QString id = brickAt(L(QPointF(he->pos())));
        const BrickMark mk = marks_.value(id);
        QStringList said;
        if (mk.bom == 1) said << QStringLiteral("● On the BOM");
        if (mk.bom == 0) said << QStringLiteral("● Not on the BOM");
        if (mk.compat >= 0 && !mk.why.isEmpty()) said << QStringLiteral("● ") + mk.why;
        if (said.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(he->globalPos(), said.join(QLatin1Char('\n')), this);
        return true;
    }
    return QWidget::event(e);
}

void BrickCanvas::turnOnDrawing(double deg) {
    bool any = false;
    for (const QString& id : sel_)
        if (Brick* b = d_->brick(id); b && b->type == BrickType::End) {
            b->drawTurnDeg = std::fmod(b->drawTurnDeg + deg + 360.0, 360.0);
            any = true;
        }
    if (!any) {
        emit message(QStringLiteral("Pick a part to turn on the drawing"));
        return;
    }
    emit designChanged();
    emit message(QStringLiteral("Turned %1 degrees on the drawing").arg(deg));
    update();
}

void BrickCanvas::mirrorOnDrawing() {
    bool any = false;
    for (const QString& id : sel_)
        if (Brick* b = d_->brick(id); b && b->type == BrickType::End) {
            b->drawMirror = !b->drawMirror;
            any = true;
        }
    if (!any) {
        emit message(QStringLiteral("Pick a part to mirror on the drawing"));
        return;
    }
    emit designChanged();
    emit message(QStringLiteral("Mirrored on the drawing"));
    update();
}

void BrickCanvas::rotateSelection() {
    if (sel_.isEmpty()) return;
    for (const QString& id : sel_)
        if (Brick* b = d_->brick(id)) b->rot = (b->rot + 1) % 4;
    emit designChanged();
    update();
}

void BrickCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0x23, 0x2a, 0x33));
    // WHAT THIS PANE IS, in its top left corner - at the screen's size, whatever the zoom.
    {
        QFont f = font();
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0x8f, 0xb0, 0xd6));
        p.drawText(QPointF(8, 16), QStringLiteral("Brick pane"));
        p.setFont(font());
    }
    // Everything from here is in the pane's own units, drawn at the zoom.
    p.scale(zoom_, zoom_);
    p.setPen(QPen(QColor(0x3c, 0x46, 0x52), 1.6));
    double c0, r0, c1, r1;
    toGrid(QPointF(0, 0), c0, r0);
    toGrid(QPointF(viewSize().width(), viewSize().height()), c1, r1);
    for (int c = int(std::floor(c0)); c <= int(std::ceil(c1)); ++c)
        for (int r = int(std::floor(r0)); r <= int(std::ceil(r1)); ++r) p.drawPoint(toScreen(c, r));

    if (d_->bricks.empty()) {
        p.setPen(QColor(0x8a, 0x93, 0xa0));
        p.drawText(QRectF(QPointF(0, 0), viewSize()), Qt::AlignCenter,
                   QStringLiteral("Drag end parts, joints and specials here from the list on the left.\n"
                                  "Link them by dragging from a port - the dot on a brick's side - to "
                                  "another brick."));
    }

    // The links, port to port, under the bricks.
    QFont small = font();
    small.setPointSizeF(7.5);
    for (const BrickLink& l : d_->links) {
        const Brick* a = d_->brick(l.a);
        const Brick* b = d_->brick(l.b);
        if (!a || !b) continue;
        const bool picked = (selLink_.first == l.a && selLink_.second == l.b) ||
                            (selLink_.first == l.b && selLink_.second == l.a);
        const QPointF pa = portPoint(*a, d_->portOn(l.a, l.b));
        const QPointF pb = portPoint(*b, d_->portOn(l.b, l.a));
        p.setPen(QPen(picked ? QColor(0x2b, 0xd0, 0xff) : QColor(0xc9, 0xcc, 0xd1), picked ? 3.0 : 2.0));
        p.drawLine(pa, pb);
        // A fanout says how many wires each part it finishes on takes.
        const Brick* sp = brickIsSpecial(a->type) ? a : b;
        if (sp->type == BrickType::Fanout && l.wires > 0) {
            p.setFont(small);
            p.setPen(QColor(0x7e, 0xe0, 0xa0));
            p.drawText((pa + pb) / 2.0 + QPointF(3, -3), QStringLiteral("%1w").arg(l.wires));
        }
    }
    if (drag_ == Drag::Link) {
        if (const Brick* a = d_->brick(dragId_)) {
            QPen pen(QColor(0x7e, 0xe0, 0xa0), 2.0);
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawLine(dragPort_ >= 0 ? portPoint(*a, dragPort_) : brickRect(*a).center(), cursor_);
        }
    }

    // The bricks.
    for (const Brick& b : d_->bricks) {
        const QRectF r = brickRect(b);
        const bool end = b.type == BrickType::End;
        const bool joint = end && brickKindIsJoint(b.kind);
        const bool picked = sel_.contains(b.id);
        QColor fill = end ? (joint ? QColor(0x2f, 0x62, 0x5a) : QColor(0x2f, 0x4f, 0x74))
                          : QColor(0x5a, 0x43, 0x78);
        if (end && b.libPart.isEmpty()) fill = fill.darker(135);
        p.setPen(QPen(picked ? QColor(0x2b, 0xd0, 0xff) : QColor(0x9a, 0xa7, 0xb8), picked ? 2.4 : 1.2));
        p.setBrush(fill);
        if (end) p.drawRect(r);
        else {
            const double rr = std::min(r.width(), r.height()) / 2.0;
            p.drawRoundedRect(r, rr, rr);
        }
        p.setPen(QColor(0xe8, 0xea, 0xee));
        QFont f = font();
        f.setPointSizeF(8.5);
        f.setBold(true);
        p.setFont(f);
        const QRectF top(r.left() + 3, r.top() + 2, r.width() - 6, r.height() / 2.0);
        p.drawText(top, Qt::AlignCenter,
                   p.fontMetrics().elidedText(brickTitle(b), Qt::ElideRight, int(top.width())));
        f.setBold(false);
        f.setPointSizeF(7.5);
        p.setFont(f);
        QString sub;
        // ITS PART NUMBER - not the library's own id for the part ("connector-187" said nothing to anybody).
        if (end) {
            const LibraryPart* lp = d_->catalog && !b.libPart.isEmpty() ? d_->catalog->byId(b.libPart) : nullptr;
            sub = b.libPart.isEmpty() ? QStringLiteral("no part yet")
                  : !lp               ? b.libPart
                  : !lp->mfrPn.isEmpty() ? lp->mfrPn
                                         : lp->title();
        }
        else sub = b.type == BrickType::TwistedPair
                       ? QStringLiteral("%1 pr · L-%2").arg(std::max(1, b.run.pairs)).arg(b.run.lengthMm, 0, 'f', 0)
                       : QStringLiteral("%1 w · L-%2").arg(b.run.wires).arg(b.run.lengthMm, 0, 'f', 0);
        const QRectF bottom(r.left() + 3, r.center().y(), r.width() - 6, r.height() / 2.0 - 2);
        p.setPen(QColor(0xb8, 0xc0, 0xcc));
        p.drawText(bottom, Qt::AlignCenter,
                   p.fontMetrics().elidedText(sub, Qt::ElideRight, int(bottom.width())));
        // THE BILL'S MARK, top right: green on the BOM, red not on it.
        {
            const BrickMark mk = marks_.value(b.id);
            auto dot = [&p](const QPointF& at, const QColor& c) {
                p.setPen(QPen(QColor(0x14, 0x18, 0x1e), 1.0));
                p.setBrush(c);
                p.drawEllipse(at, 3.6, 3.6);
            };
            if (mk.bom >= 0)
                dot(QPointF(r.right() - 6.0, r.top() + 6.0), mk.bom ? QColor(0x4c, 0xd0, 0x7a) : QColor(0xe0, 0x4a, 0x4a));
        }
        // THE PORTS - the brick's joints - ARE ITS COMPATIBILITY MARK: a linked one filled green where the part suits
        // what it is linked to, orange where something is not known or not linked yet, red where it does not suit.
        // One with nothing linked stays empty.
        const int suits = marks_.value(b.id).compat;
        const QColor jointColour = suits == 1   ? QColor(0xf0, 0xa0, 0x30)
                             : suits == 2 ? QColor(0xe0, 0x4a, 0x4a)
                                          : QColor(0x7e, 0xe0, 0xa0);
        for (int port = 0; port < d_->portCount(b); ++port) {
            bool used = false;
            for (const QString& o : d_->linksOf(b.id))
                if (d_->portOn(b.id, o) == port) used = true;
            p.setPen(QPen(used ? jointColour : QColor(0x7e, 0xe0, 0xa0), 1.2));
            p.setBrush(used ? jointColour : QColor(0x23, 0x2a, 0x33));
            p.drawEllipse(portPoint(b, port), 4.0, 4.0);
        }
    }

    // THE MAP: every brick small, and the part in view outlined.
    if (minimap_ && !d_->bricks.empty()) {
        // The map itself sits on the screen at its own size, whatever the zoom.
        p.save();
        p.resetTransform();
        QRectF all;
        for (const Brick& b : d_->bricks) all = all.isNull() ? brickRect(b) : all.united(brickRect(b));
        all = all.united(QRectF(QPointF(0, 0), viewSize()));
        const QRectF m = minimapRect();
        p.setPen(QPen(QColor("#4a4e55"), 1));
        p.setBrush(QColor(30, 33, 38, 235));
        p.drawRect(m);
        const QRectF in = m.adjusted(6, 6, -6, -6);
        const double s = std::min(in.width() / all.width(), in.height() / all.height());
        auto map = [&](const QRectF& r) {
            return QRectF(in.center() + (r.topLeft() - all.center()) * s, r.size() * s);
        };
        for (const Brick& b : d_->bricks) {
            p.setPen(Qt::NoPen);
            p.setBrush(b.type == BrickType::End ? QColor(0x4f, 0x7f, 0xb4) : QColor(0x8a, 0x6a, 0xb8));
            p.drawRect(map(brickRect(b)));
        }
        p.setPen(QPen(QColor("#2bd0ff"), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(map(QRectF(QPointF(0, 0), viewSize())));
        p.restore();
    }

    // A ZONE being dragged out: blue left to right (wholly inside), green right to left (touching).
    if (drag_ == Drag::Zone) {
        const QRectF z = QRectF(pressAt_, cursor_).normalized();
        const bool window = cursor_.x() >= pressAt_.x();
        QColor c = window ? QColor(60, 130, 255) : QColor(60, 200, 110);
        QPen pen(c, 1.2);
        if (!window) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        c.setAlpha(45);
        p.setBrush(c);
        p.drawRect(z);
    }
}

void BrickCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    shiftAlone_ = false;
    pressAt_ = cursor_ = L(e->position());
    if (e->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        panFrom_ = origin_;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    // A CLICK ON THE MAP goes there.
    if (minimap_ && !d_->bricks.empty() && minimapRect().contains(e->position())) {
        QRectF all;
        for (const Brick& b : d_->bricks) all = all.isNull() ? brickRect(b) : all.united(brickRect(b));
        all = all.united(QRectF(QPointF(0, 0), viewSize()));
        const QRectF in = minimapRect().adjusted(6, 6, -6, -6);
        const double s = std::min(in.width() / all.width(), in.height() / all.height());
        const QPointF at = all.center() + (e->position() - in.center()) / s;   // on the pane
        origin_ += QPointF(viewSize().width() / 2.0, viewSize().height() / 2.0) - at;
        update();
        return;
    }
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    // DRAWING ORDER against another brick: the one clicked now is the other.
    if (!orderSource_.isEmpty()) {
        const QString hit = brickAt(L(e->position()));
        const QString source = orderSource_;
        orderSource_.clear();
        setCursor(Qt::ArrowCursor);
        if (!hit.isEmpty() && hit != source) emit orderPicked(source, hit, orderAbove_);
        else emit message(QStringLiteral("Drawing order left as it was"));
        update();
        return;
    }
    // PUTTING A DIMENSION ON: the first brick clicked, then the second - and the dimension goes between them.
    if (dimMode_) {
        const QString hit = brickAt(L(e->position()));
        if (hit.isEmpty()) {
            emit message(QStringLiteral("Dimension: click a brick"));
            return;
        }
        if (dimFirst_.isEmpty() || dimFirst_ == hit) {
            dimFirst_ = hit;
            sel_ = {hit};
            emit message(QStringLiteral("Dimension: now click the second brick"));
            update();
            return;
        }
        const QString first = dimFirst_;
        dimFirst_.clear();
        sel_.clear();
        emit dimensionPicked(first, hit);
        update();
        return;
    }
    // A PORT, aimed at on purpose, wins over any brick's body: a link starts there.
    const auto port = portAt(L(e->position()));
    if (!port.first.isEmpty()) {
        dragId_ = port.first;
        dragPort_ = port.second;
        drag_ = Drag::Link;
        update();
        return;
    }
    const QString hit = brickAt(L(e->position()));
    if (!hit.isEmpty()) {
        if (e->modifiers() & Qt::ControlModifier) {
            // With Ctrl, a link from whichever port faces what it is let go on.
            dragId_ = hit;
            dragPort_ = -1;
            drag_ = Drag::Link;
            update();
            return;
        }
        if (shift) {
            if (sel_.contains(hit)) sel_.removeAll(hit);
            else sel_ << hit;
            selLink_ = {};
            emit selectionChanged(selected());
            update();
            return;
        }
        if (!sel_.contains(hit)) {
            sel_ = {hit};
            selLink_ = {};
        } else {
            // The brick pressed becomes the one the panel shows, the rest still picked.
            sel_.removeAll(hit);
            sel_ << hit;
        }
        emit selectionChanged(hit);
        drag_ = Drag::Move;
        startAt_.clear();
        for (const QString& id : sel_)
            if (const Brick* b = d_->brick(id)) startAt_.insert(id, QPointF(b->col, b->row));
        // Only the bricks picked move: none is tied to another on the pane.
        follow_.clear();
        for (auto it = follow_.cbegin(); it != follow_.cend(); ++it)
            if (const Brick* b = d_->brick(it.key())) startAt_.insert(it.key(), QPointF(b->col, b->row));
        startDy_.clear();
        rowPitchMm_ = 0.0;
        update();
        return;
    }
    for (const BrickLink& l : d_->links) {
        const Brick* a = d_->brick(l.a);
        const Brick* b = d_->brick(l.b);
        if (!a || !b) continue;
        if (segDistance(L(e->position()), portPoint(*a, d_->portOn(l.a, l.b)),
                        portPoint(*b, d_->portOn(l.b, l.a))) <= 5.0) {
            sel_.clear();
            selLink_ = {l.a, l.b};
            emit selectionChanged(QString());
            emit linkPicked(l.a, l.b);
            emit message(QStringLiteral("Link picked - Delete removes it"));
            update();
            return;
        }
    }
    drag_ = Drag::Zone;
    zoneAdd_ = shift;
    update();
}

void BrickCanvas::mouseMoveEvent(QMouseEvent* e) {
    cursor_ = L(e->position());
    if (drag_ == Drag::Pan) {
        origin_ = panFrom_ + (L(e->position()) - pressAt_);
        update();
    } else if (drag_ == Drag::Move) {
        // Along the harness to ten millimetres - a column is so many millimetres of run (kBrickMmPerCol), and a
        // part dragged along says its new length; across, to the half row as before.
        const double tenMm = 10.0 / kBrickMmPerCol;
        const double dc = std::round((L(e->position()).x() - pressAt_.x()) / stepX_ / tenMm) * tenMm;
        const double dr = snapHalf((L(e->position()).y() - pressAt_.y()) / stepY_);
        bool moved = false;
        for (auto it = startAt_.begin(); it != startAt_.end(); ++it)
            if (Brick* b = d_->brick(it.key())) {
                const auto f = follow_.constFind(it.key());
                const bool followX = f == follow_.cend() || f->x, followY = f == follow_.cend() || f->y;
                const double col = it.value().x() + (followX ? dc : 0.0), row = it.value().y() + (followY ? dr : 0.0);
                if (col != b->col || row != b->row) {
                    b->col = col;
                    b->row = row;
                    moved = true;
                }
            }
        if (moved) {
            keepOnDrawing(startAt_);
            emit bricksMoved(startAt_);
            emit designChanged();
        }
        update();
    } else if (drag_ == Drag::Link || drag_ == Drag::Zone) {
        update();
    }
}

void BrickCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (drag_ == Drag::Link) {
        const QString target = brickAt(L(e->position()));
        const auto tport = portAt(L(e->position()));
        const QString to = !tport.first.isEmpty() ? tport.first : target;
        if (!to.isEmpty() && to != dragId_) {
            // Let go ON a port, that port; anywhere else on the brick, the one facing the other.
            const int toPort = tport.first == to ? tport.second : -1;
            QString why;
            if (d_->connect(dragId_, to, &why, dragPort_, toPort)) {
                emit message(QStringLiteral("Linked"));
                emit linked(dragId_, to);
                emit designChanged();
            } else {
                emit message(why);
            }
        }
    } else if (drag_ == Drag::Zone) {
        const QRectF z = QRectF(pressAt_, L(e->position())).normalized();
        if (z.width() < 3.0 && z.height() < 3.0) {
            // A click on nothing lets go of what was picked.
            if (!zoneAdd_ && (!sel_.isEmpty() || !selLink_.first.isEmpty())) {
                sel_.clear();
                selLink_ = {};
                emit selectionChanged(QString());
            }
        } else {
            const bool window = L(e->position()).x() >= pressAt_.x();
            QStringList got = zoneAdd_ ? sel_ : QStringList();
            for (const Brick& b : d_->bricks) {
                const QRectF r = brickRect(b);
                const bool in = window ? z.contains(r) : z.intersects(r);
                if (in && !got.contains(b.id)) got << b.id;
            }
            sel_ = got;
            selLink_ = {};
            emit selectionChanged(selected());
            emit message(QStringLiteral("%1 brick(s) picked").arg(sel_.size()));
        }
    }
    const bool wasMove = drag_ == Drag::Move;
    drag_ = Drag::None;
    dragId_.clear();
    update();
    if (wasMove) emit designChanged();   // the move finished: one step to undo
}

void BrickCanvas::contextMenuEvent(QContextMenuEvent* e) {
    // The brick under the pointer is what the menu is for - picked, if it was not already.
    const QString hit = brickAt(L(QPointF(e->pos())));
    if (!hit.isEmpty() && !sel_.contains(hit)) {
        sel_ = {hit};
        selLink_ = {};
        emit selectionChanged(hit);
        update();
    }
    QMenu menu(this);
    fillContextMenu(menu);
    menu.exec(e->globalPos());
}

void BrickCanvas::fillContextMenu(QMenu& menu) {
    if (sel_.isEmpty()) {
        menu.addAction(QStringLiteral("Fit everything in view"), this, [this] { fit(); });
        menu.addAction(minimap_ ? QStringLiteral("Hide the map") : QStringLiteral("Show the map"), this,
                       [this] { setMinimap(!minimap_); });
        return;
    }
    // OPEN IN LIBRARY: the part of the brick right-clicked - an end's part, or one of a run's layers.
    if (const Brick* b = d_->brick(sel_.front())) {
        if (b->type == BrickType::End) {
            QAction* a = menu.addAction(QStringLiteral("Open in library"), this, [this, id = b->libPart] { emit openInLibrary(id); });
            a->setEnabled(!b->libPart.isEmpty());
        } else if (brickIsSpecial(b->type)) {
            QMenu* lib = menu.addMenu(QStringLiteral("Open in library"));
            for (const QString& layer : b->run.stack) {
                const QString id = b->run.stackParts.value(layer);
                if (id.isEmpty()) continue;
                lib->addAction(QStringLiteral("%1 - %2").arg(layer, id), this, [this, id] { emit openInLibrary(id); });
            }
            lib->setEnabled(!lib->actions().isEmpty());
        }
        menu.addSeparator();
    }
    menu.addAction(QStringLiteral("Rotate a quarter"), this, [this] { rotateSelection(); });
    menu.addAction(QStringLiteral("Turn on the drawing 45°\tR"), this, [this] { turnOnDrawing(45.0); });
    menu.addAction(QStringLiteral("Turn on the drawing 90°\tSpace"), this, [this] { turnOnDrawing(90.0); });
    menu.addAction(QStringLiteral("Mirror on the drawing\tM"), this, [this] { mirrorOnDrawing(); });
    QMenu* move = menu.addMenu(QStringLiteral("Move"));
    move->addAction(QStringLiteral("Left\t←"), this, [this] { moveSelection(-1.0, 0.0); });
    move->addAction(QStringLiteral("Right\t→"), this, [this] { moveSelection(1.0, 0.0); });
    move->addAction(QStringLiteral("Up\t↑"), this, [this] { moveSelection(0.0, -1.0); });
    move->addAction(QStringLiteral("Down\t↓"), this, [this] { moveSelection(0.0, 1.0); });
    bool fans = false;
    for (const QString& id : sel_)
        if (const Brick* b = d_->brick(id); b && b->type == BrickType::Fanout) fans = true;
    if (fans)
        menu.addAction(QStringLiteral("Turn the fan round\tShift"), this, [this] {
            for (const QString& id : sel_)
                if (Brick* b = d_->brick(id); b && b->type == BrickType::Fanout)
                    b->run.fanSide = 1 - d_->fanoutSpreadSide(*b);
            emit designChanged();
            update();
        });
    // DRAWING ORDER: over everything, under everything, or over / under one other brick clicked next.
    {
        QMenu* order = menu.addMenu(QStringLiteral("Draw order"));
        auto zRange = [this](int& lo, int& hi) {
            lo = hi = 0;
            for (const Brick& b : d_->bricks) {
                lo = std::min(lo, b.z);
                hi = std::max(hi, b.z);
            }
        };
        order->addAction(QStringLiteral("Bring to front"), this, [this, zRange] {
            int lo, hi;
            zRange(lo, hi);
            for (const QString& id : sel_)
                if (Brick* b = d_->brick(id)) b->z = ++hi;
            emit designChanged();
        });
        order->addAction(QStringLiteral("Send to back"), this, [this, zRange] {
            int lo, hi;
            zRange(lo, hi);
            for (const QString& id : sel_)
                if (Brick* b = d_->brick(id)) b->z = --lo;
            emit designChanged();
        });
        auto against = [this](bool above) {
            if (sel_.isEmpty()) return;
            orderSource_ = sel_.back();
            orderAbove_ = above;
            setCursor(Qt::PointingHandCursor);
            emit message(above ? QStringLiteral("Click the brick to draw it above")
                               : QStringLiteral("Click the brick to draw it under"));
        };
        order->addAction(QStringLiteral("Above another brick…"), this, [against] { against(true); });
        order->addAction(QStringLiteral("Under another brick…"), this, [against] { against(false); });
    }
    menu.addAction(QStringLiteral("Duplicate"), this, [this] { duplicateSelection(); });
    menu.addAction(QStringLiteral("Unlink"), this, [this] {
        for (const QString& id : sel_)
            for (const QString& o : d_->linksOf(id)) d_->disconnect(id, o);
        emit designChanged();
        update();
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Delete\tDel"), this, [this] { deleteSelected(); });
}

namespace {
constexpr char kBrickClipMime[] = "application/x-edraw-bricks";
} // namespace

void BrickCanvas::copySelection() {
    if (sel_.isEmpty()) return;
    // The picked bricks and the links between them, as a little design of their own.
    BrickDesign part;
    for (const QString& id : sel_)
        if (const Brick* b = d_->brick(id)) part.bricks.push_back(*b);
    for (const BrickLink& l : d_->links)
        if (sel_.contains(l.a) && sel_.contains(l.b)) part.links.push_back(l);
    const QByteArray json = QJsonDocument(part.toJson()).toJson(QJsonDocument::Compact);
    auto* mime = new QMimeData;
    mime->setData(QLatin1String(kBrickClipMime), json);
    mime->setText(QStringLiteral("%1 brick(s)").arg(part.bricks.size()));
    QGuiApplication::clipboard()->setMimeData(mime);
    emit message(QStringLiteral("Copied %1 brick(s) - Ctrl+V to put them down").arg(part.bricks.size()));
}

void BrickCanvas::pasteClipboard() {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QLatin1String(kBrickClipMime))) return;
    const BrickDesign part = BrickDesign::fromJson(QJsonDocument::fromJson(mime->data(QLatin1String(kBrickClipMime))).object());
    if (part.bricks.empty()) return;
    // Put down beside where they were, a step down and across, each a new brick with the same part and
    // settings; the links between them come too.
    QHash<QString, QString> newId;
    QStringList made;
    for (const Brick& src : part.bricks) {
        const QString nid = d_->addBrick(src.type, src.kind, src.col + 1.0, src.row + 1.0);
        Brick* nb = d_->brick(nid);
        if (!nb) continue;
        *nb = src;
        nb->id = nid;
        nb->col += 1.0;
        nb->row += 1.0;
        newId.insert(src.id, nid);
        made << nid;
    }
    for (const BrickLink& l : part.links) {
        if (!newId.contains(l.a) || !newId.contains(l.b)) continue;
        d_->connect(newId.value(l.a), newId.value(l.b), nullptr, l.portA, l.portB);
        for (BrickLink& nl : d_->links)
            if (nl.a == newId.value(l.a) && nl.b == newId.value(l.b)) nl.wires = l.wires;
    }
    if (made.isEmpty()) return;
    sel_ = made;
    emit selectionChanged(selected());
    emit designChanged();
    emit message(QStringLiteral("Pasted %1 brick(s)").arg(made.size()));
    update();
}

void BrickCanvas::duplicateSelection() {
    QStringList made;
    for (const QString& id : sel_) {
        const Brick* b = d_->brick(id);
        if (!b) continue;
        const Brick copy = *b;
        const QString nid = d_->addBrick(copy.type, copy.kind, copy.col, copy.row + 1.0);
        Brick* nb = d_->brick(nid);
        *nb = copy;
        nb->id = nid;
        nb->row += 1.0;
        made << nid;
    }
    if (made.isEmpty()) return;
    sel_ = made;
    emit selectionChanged(selected());
    emit designChanged();
    update();
}

// SHIFT PRESSED AND LET GO ON ITS OWN turns the picked fanouts round: the spread goes to the other
// end. Shift with a click is adding to what is picked, and does not.
void BrickCanvas::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Shift && !e->isAutoRepeat() && shiftAlone_) {
        shiftAlone_ = false;
        bool turned = false;
        for (const QString& id : sel_)
            if (Brick* b = d_->brick(id); b && b->type == BrickType::Fanout) {
                b->run.fanSide = 1 - d_->fanoutSpreadSide(*b);
                turned = true;
            }
        if (turned) {
            emit message(QStringLiteral("Fanout turned round - it spreads at the other end"));
            emit designChanged();
            update();
        }
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void BrickCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Shift && !e->isAutoRepeat()) {
        shiftAlone_ = true;
        return;
    }
    shiftAlone_ = false;
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    // ON THE DRAWING: R turns what is picked 45 degrees, Space 90, M mirrors it - the bricks on the pane stay as they are.
    if (e->key() == Qt::Key_R && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        turnOnDrawing(45.0);
        return;
    }
    if (e->key() == Qt::Key_Space) {
        turnOnDrawing(90.0);
        return;
    }
    if (e->key() == Qt::Key_M && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        mirrorOnDrawing();
        return;
    }
    if (e->matches(QKeySequence::Copy)) {
        copySelection();
        return;
    }
    if (e->matches(QKeySequence::Paste)) {
        pasteClipboard();
        return;
    }
    double dc = 0.0, dr = 0.0;
    if (e->key() == Qt::Key_Left) dc = -1.0;
    else if (e->key() == Qt::Key_Right) dc = 1.0;
    else if (e->key() == Qt::Key_Up) dr = -1.0;
    else if (e->key() == Qt::Key_Down) dr = 1.0;
    if ((dc != 0.0 || dr != 0.0) && !sel_.isEmpty()) {
        const double step = (e->modifiers() & Qt::ShiftModifier) ? 0.5 : 1.0;
        moveSelection(dc * step, dr * step);
        return;
    }
    QWidget::keyPressEvent(e);
}

void BrickCanvas::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(QString::fromLatin1(kBrickMime))) e->acceptProposedAction();
}

void BrickCanvas::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat(QString::fromLatin1(kBrickMime))) e->acceptProposedAction();
}

void BrickCanvas::dropEvent(QDropEvent* e) {
    const QString what = QString::fromUtf8(e->mimeData()->data(QString::fromLatin1(kBrickMime)));
    const QStringList parts = what.split(QLatin1Char('|'));
    if (parts.size() < 2) return;
    double col, row;
    toGrid(L(e->position()), col, row);
    const QString id = d_->addBrick(brickTypeFromKey(parts[0]), partKindFromKey(parts[1]),
                                    std::round(col), snapHalf(row));
    if (parts.value(2) == QLatin1String("bridge"))
        if (Brick* b = d_->brick(id)) makeBridgeWire(*b);
    e->acceptProposedAction();
    setSelected(id);
    emit selectionChanged(id);
    emit designChanged();
}

// ==================================================================================================
// The palette
// ==================================================================================================

BrickPalette::BrickPalette(QWidget* parent) : QListWidget(parent) {
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    auto header = [this](const QString& text) {
        auto* it = new QListWidgetItem(text, this);
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
        it->setFlags(Qt::ItemIsEnabled);
    };
    auto brick = [this](const QString& text, const QString& data) {
        auto* it = new QListWidgetItem(QStringLiteral("   ") + text, this);
        it->setData(Qt::UserRole, data);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    };
    header(QStringLiteral("End parts"));
    for (PartKind k : brickEndKinds())
        brick(partKindName(k), brickTypeKey(BrickType::End) + QLatin1Char('|') + partKindKey(k));
    header(QStringLiteral("Joints"));
    for (PartKind k : brickJointKinds())
        brick(partKindName(k), brickTypeKey(BrickType::End) + QLatin1Char('|') + partKindKey(k));
    header(QStringLiteral("Specials"));
    for (BrickType t : {BrickType::Fanout, BrickType::Run, BrickType::TwistedPair,
                        BrickType::TwistedBundle})
        brick(brickTypeName(t), brickTypeKey(t) + QLatin1Char('|') + partKindKey(PartKind::Wire));
    // A BRIDGE WIRE: takes the wires it is told from the cable it is joined to, the rest going on to its parts.
    brick(QStringLiteral("Bridge wire"),
          brickTypeKey(BrickType::Run) + QLatin1Char('|') + partKindKey(PartKind::Wire) + QStringLiteral("|bridge"));
}

QStringList BrickPalette::mimeTypes() const { return {QString::fromLatin1(kBrickMime)}; }

QMimeData* BrickPalette::mimeData(const QList<QListWidgetItem*>& items) const {
    auto* m = new QMimeData;
    if (!items.isEmpty())
        m->setData(QString::fromLatin1(kBrickMime), items.front()->data(Qt::UserRole).toString().toUtf8());
    return m;
}

// ==================================================================================================
// The wizard
// ==================================================================================================

BrickWizardDialog::BrickWizardDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Mechanical harness wizard"));
    resize(1500, 880);
    catalog_.openRemembered();
    design_.catalog = &catalog_;
    buildUi();
    wheelScrollsNotFields(this);   // rolling the wheel down the panel changes no figure
    loadPanel();
    refreshPreview();
}

Document* BrickWizardDialog::document() const { return editor_ ? editor_->document() : nullptr; }

void BrickWizardDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // THE HARNESSES KEPT IN THE DRAWING: which one this is, and saving, opening and deleting them.
    auto* keep = new QHBoxLayout;
    keep->addWidget(new QLabel(QStringLiteral("Harness"), this));
    harnessName_ = new QLineEdit(this);
    harnessName_->setObjectName(QStringLiteral("brickHarnessName"));
    harnessName_->setPlaceholderText(QStringLiteral("W1"));
    harnessName_->setMaximumWidth(200);
    keep->addWidget(harnessName_);
    auto* saveBtn = new QPushButton(QStringLiteral("Save to drawing"), this);
    saveBtn->setObjectName(QStringLiteral("brickSave"));
    keep->addWidget(saveBtn);
    keep->addSpacing(12);
    keep->addWidget(new QLabel(QStringLiteral("Saved in this drawing"), this));
    saved_ = new QComboBox(this);
    saved_->setObjectName(QStringLiteral("brickSaved"));
    saved_->setMinimumContentsLength(16);
    keep->addWidget(saved_);
    auto* openBtn = new QPushButton(QStringLiteral("Open"), this);
    openBtn->setObjectName(QStringLiteral("brickOpen"));
    keep->addWidget(openBtn);
    auto* newBtn = new QPushButton(QStringLiteral("New"), this);
    newBtn->setObjectName(QStringLiteral("brickNew"));
    keep->addWidget(newBtn);
    auto* delBtn = new QPushButton(QStringLiteral("Delete saved"), this);
    delBtn->setObjectName(QStringLiteral("brickDeleteSaved"));
    keep->addWidget(delBtn);
    keep->addSpacing(18);
    keep->addWidget(new QLabel(QStringLiteral("BOM"), this));
    bomAtCombo_ = new QComboBox(this);
    bomAtCombo_->setObjectName(QStringLiteral("brickBomAt"));
    bomAtCombo_->addItems({QStringLiteral("Left, a tab"), QStringLiteral("Left, bottom half"),
                           QStringLiteral("Middle, bottom"), QStringLiteral("Middle, top"),
                           QStringLiteral("Right, bottom")});
    keep->addWidget(bomAtCombo_);
    connect(bomAtCombo_, &QComboBox::currentIndexChanged, this, [this](int i) { placeBom(i); });
    keep->addStretch(1);
    root->addLayout(keep);
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        QString name = harnessName_->text().trimmed();
        if (name.isEmpty()) {
            name = QInputDialog::getText(this, QStringLiteral("Save harness"), QStringLiteral("Name"),
                                         QLineEdit::Normal, QStringLiteral("W1"));
            if (name.trimmed().isEmpty()) return;
        }
        saveToDrawing(name.trimmed());
    });
    connect(openBtn, &QPushButton::clicked, this, [this] {
        if (saved_->currentIndex() >= 0) loadFromDrawing(saved_->currentText());
    });
    connect(newBtn, &QPushButton::clicked, this, [this] {
        design_.bricks.clear();
        design_.links.clear();
        currentName_.clear();
        harnessName_->clear();
        canvas_->setSelection({});
        liveInit_ = false;   // the last harness's parts stay on the bill: a new one starts counting afresh
        designChanged();
        status_->setText(QStringLiteral("A new harness"));
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        if (saved_->currentIndex() >= 0) deleteFromDrawing(saved_->currentText());
    });

    // THE BAR: turning and moving what is picked, arranging the whole, and where dimensions go from.
    auto* bar = new QHBoxLayout;
    auto button = [&](const char* name, const QString& text, const QString& tip, auto fn) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QLatin1String(name));
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, fn);
        bar->addWidget(b);
        return b;
    };
    button("brickRotate", QStringLiteral("⟳ Rotate"),
           QStringLiteral("Turn the brick a quarter. On the drawing only: R turns 45°, Space 90°, M mirrors"),
           [this] { rotateSelected(); });
    for (auto [name, text, dc, dr] : {std::tuple<const char*, const char*, double, double>{"brickLeft", "←", -1, 0},
                                      {"brickRight", "→", 1, 0},
                                      {"brickUp", "↑", 0, -1},
                                      {"brickDown", "↓", 0, 1}}) {
        auto* b = button(name, QString::fromUtf8(text), QStringLiteral("Move what is picked a grid step"),
                         [this, dc = dc, dr = dr] { moveSelected(dc, dr); });
        b->setFixedWidth(34);
    }
    button("brickDelete", QStringLiteral("Delete"), QStringLiteral("Delete what is picked"),
           [this] { canvas_->deleteSelected(); });
    // SYMMETRIC, in x and y, the bricks and the drawing alike: the parts laid out either side of the axis,
    // each column the same step apart, and both panes brought round to show the whole of it.
    button("brickArrange", QStringLiteral("Symmetric"),
           QStringLiteral("Lay the bricks and the drawing out symmetrically in x and y: the most connected "
                          "part on the axis, the rest evenly either side, every brick turned to face what it "
                          "is linked to - then both panes fitted to show it all"),
           [this] { arrange(); });
    // THE BRICKS TURN WITH THE DRAWING, or not: R and Space turn the part on the drawing, and - ticked - its brick's
    // connections round it on the pane too.
    tieTurn_ = new QCheckBox(QStringLiteral("Bricks turn with drawing"), this);
    tieTurn_->setObjectName(QStringLiteral("brickTieTurn"));
    tieTurn_->setToolTip(QStringLiteral("Ticked, R (45°) and Space (90°) turn the brick's connections on this pane as "
                                        "well as the part on the drawing. Unticked, only the drawing turns."));
    tieTurn_->setChecked(design_.tieBrickTurn);
    connect(tieTurn_, &QCheckBox::toggled, this, [this](bool on) {
        design_.tieBrickTurn = on;
        canvas_->update();
        designChanged();
    });
    bar->addWidget(tieTurn_);
    // A DIMENSION BY HAND: pressed, click two bricks - it goes between them, above them or below.
    dimTool_ = new QToolButton(this);
    dimTool_->setObjectName(QStringLiteral("brickDimTool"));
    dimTool_->setText(QStringLiteral("Dimension"));
    dimTool_->setCheckable(true);
    dimTool_->setToolTip(QStringLiteral("Put a dimension on: press, then click two bricks"));
    // A BUTTON THAT LOOKS LIKE ONE: plain text in the bar was not seen to be something to press.
    dimTool_->setText(QStringLiteral("Dimension: click 2 bricks"));
    dimTool_->setStyleSheet(QStringLiteral("QToolButton{padding:3px 10px;border:1px solid #5a6b7d;border-radius:3px;"
                                           "background:#2c3947;color:#e6e9ef;}"
                                           "QToolButton:hover{background:#34475a;}"
                                           "QToolButton:checked{background:#2b6cb0;color:white;border-color:#2b6cb0;}"));
    connect(dimTool_, &QToolButton::toggled, this, [this](bool on) { canvas_->setDimensionMode(on); });
    // THE DIMENSIONS ARE ALL IN THE DIMENSION WIZARD (its button at the bottom): the pick, the side, the points -
    // none of them on this bar. These stay, out of sight, as what the wizard works.
    dimTool_->hide();
    dimAllSide_ = new QComboBox(this);
    dimAllSide_->setObjectName(QStringLiteral("brickDimSide"));
    dimAllSide_->addItems({QStringLiteral("Above"), QStringLiteral("Below")});
    connect(dimAllSide_, &QComboBox::activated, this, [this](int i) { setAllDimsSide(i == 1); });
    dimAllSide_->hide();
    dimFrom_ = new QComboBox(this);
    dimFrom_->setObjectName(QStringLiteral("brickDimFrom"));
    dimFrom_->addItems({QStringLiteral("Near point"), QStringLiteral("Far point"), QStringLiteral("Mid point")});
    dimFrom_->hide();
    dimTo_ = new QComboBox(this);
    dimTo_->setObjectName(QStringLiteral("brickDimTo"));
    dimTo_->addItems({QStringLiteral("Near point"), QStringLiteral("Far point"), QStringLiteral("Mid point")});
    dimTo_->hide();
    for (QComboBox* c : {dimFrom_, dimTo_})
        connect(c, &QComboBox::currentIndexChanged, this, [this](int) {
            design_.dimFromFar = dimFrom_->currentIndex() == 1;
            design_.dimToFar = dimTo_->currentIndex() == 1;
            design_.dimFromMid = dimFrom_->currentIndex() == 2;   // halfway between near and far
            design_.dimToMid = dimTo_->currentIndex() == 2;
            refreshPreview();
        });
    bar->addStretch(1);
    root->addLayout(bar);

    auto* split = new QSplitter(Qt::Horizontal, this);
    mainSplit_ = split;
    root->addWidget(split, 1);

    // THE LEFT: the bricks and the parts list - and the bill, as a tab or under them.
    leftSplit_ = new QSplitter(Qt::Vertical, split);
    leftTabs_ = new QTabWidget(leftSplit_);
    QTabWidget* leftTabs = leftTabs_;
    palette_ = new BrickPalette(leftTabs);
    palette_->setObjectName(QStringLiteral("brickPalette"));
    leftTabs->addTab(palette_, QStringLiteral("Bricks"));
    connect(palette_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const QString what = it->data(Qt::UserRole).toString();
        if (!what.isEmpty()) addFromPalette(what);
    });
    auto* partsPage = new QWidget(leftTabs);
    auto* pl = new QVBoxLayout(partsPage);
    partsFilter_ = new QLineEdit(partsPage);
    partsFilter_->setPlaceholderText(QStringLiteral("Filter by P/N, description, kind…"));
    pl->addWidget(partsFilter_);
    parts_ = new QTreeWidget(partsPage);
    parts_->setObjectName(QStringLiteral("brickParts"));
    parts_->setHeaderLabels({QStringLiteral("P/N"), QStringLiteral("Description"), QStringLiteral("Kind"),
                             QStringLiteral("Manufacturer"), QStringLiteral("Drawing"),
                             QStringLiteral("Entry faces"), QStringLiteral("Near / far")});
    parts_->setRootIsDecorated(false);
    parts_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    parts_->header()->setStretchLastSection(false);
    parts_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    pl->addWidget(parts_, 1);
    auto* partsRow = new QHBoxLayout;
    auto* assign = new QPushButton(QStringLiteral("Assign to picked brick"), partsPage);
    assign->setObjectName(QStringLiteral("brickPartsAssign"));
    assign->setToolTip(QStringLiteral("Make the picked end part this library part, and put it on the BOM "
                                      "(double-click does the same)"));
    partsRow->addWidget(assign);
    auto* addParts = new QPushButton(QStringLiteral("Add to BOM"), partsPage);
    addParts->setObjectName(QStringLiteral("brickPartsToBom"));
    partsRow->addWidget(addParts);
    pl->addLayout(partsRow);
    leftTabs->addTab(partsPage, QStringLiteral("Parts list"));
    connect(partsFilter_, &QLineEdit::textChanged, this, [this](const QString&) { fillPartsList(); });
    connect(addParts, &QPushButton::clicked, this, [this] {
        int added = 0;
        for (QTreeWidgetItem* it : parts_->selectedItems())
            if (addPartToBom(it->data(0, Qt::UserRole).toString())) ++added;
        status_->setText(QStringLiteral("%1 part(s) added to the BOM").arg(added));
        loadPanel();
        refreshPreview();
    });
    connect(assign, &QPushButton::clicked, this, [this] {
        if (QTreeWidgetItem* it = parts_->currentItem()) assignPart(it->data(0, Qt::UserRole).toString());
    });
    connect(parts_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        assignPart(it->data(0, Qt::UserRole).toString());
    });
    fillPartsList();

    // THE MIDDLE: the low-detail pane above the preview.
    middle_ = new QSplitter(Qt::Vertical, split);
    canvas_ = new BrickCanvas(&design_, middle_);
    canvas_->setObjectName(QStringLiteral("brickCanvas"));
    preview_ = new DrawingPreview(QStringLiteral("Preview"), middle_);
    preview_->setEmptyText(QStringLiteral("The drawing appears here as the bricks are linked"));
    preview_->setMinimapAvailable(true);
    preview_->setKeepView(true);   // choosing a part must not make the drawing jump
    preview_->installEventFilter(this);   // Ctrl + wheel over a part scales it
    preview_->setFitButtonShown(true);
    preview_->setMouseTracking(true);     // the hand over a part's grab point
    // ENLARGE: the drawing takes the whole window, the pane kept small above it; pressed again, back as it was.
    enlargeBtn_ = new QToolButton(preview_);
    enlargeBtn_->setObjectName(QStringLiteral("brickEnlarge"));
    enlargeBtn_->setCheckable(true);
    enlargeBtn_->setAutoRaise(true);
    enlargeBtn_->setFocusPolicy(Qt::NoFocus);
    enlargeBtn_->setText(QStringLiteral("Enlarge"));
    enlargeBtn_->setToolTip(QStringLiteral("The drawing across the whole window, the bricks small above it"));
    enlargeBtn_->setStyleSheet(QStringLiteral("QToolButton{color:#8fb0d6;font-size:10px;padding:1px 4px;}"
                                              "QToolButton:checked{color:#ffffff;background:#2b6cb0;}"));
    connect(enlargeBtn_, &QToolButton::toggled, this, [this](bool on) { setDrawingEnlarged(on); });
    enlargeBtn_->show();
    // THE BILL, LIVE: every line of the drawing's bill of materials as it stands, the parts this
    // harness uses in bold - so what adding a part did is seen as it is done. Where it sits is chosen.
    bom_ = new QTableWidget(0, 5, this);
    bom_->horizontalHeader()->setHighlightSections(false);   // rows picked do not bold the headings
    bom_->setObjectName(QStringLiteral("brickBom"));
    bom_->setHorizontalHeaderLabels({QStringLiteral("Item"), QStringLiteral("Qty"), QStringLiteral("Unit"),
                                     QStringLiteral("P/N"), QStringLiteral("Description")});
    bom_->horizontalHeader()->setStretchLastSection(true);
    bom_->verticalHeader()->setVisible(false);
    // THE QUANTITY CAN BE CHANGED: a double-click on it (only on lines of the bill itself, not those there because
    // their part is on the drawing).
    bom_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    connect(bom_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (!it || it->column() != 1) return;
        const int row = it->row();
        Document* doc = document();
        if (!doc || row < 0 || row >= static_cast<int>(bomShown_.size())) return;
        bool ok = false;
        const double qty = it->text().trimmed().toDouble(&ok);
        if (!ok || qty < 0.0) {
            refreshBom();
            return;
        }
        BomTable table;
        table.fromJson(doc->bom());
        for (BomRow& r : table.rows)
            if (bomSameThing(r, bomShown_[static_cast<size_t>(row)])) r.qty = qty;
        doc->bom() = table.toJson();
        doc->setDirty(true);
        emit bomChanged();
        refreshBom();
        status_->setText(QStringLiteral("Item %1: quantity %2").arg(row + 1).arg(qty));
    });
    bom_->setSelectionMode(QAbstractItemView::SingleSelection);
    bom_->setSelectionBehavior(QAbstractItemView::SelectRows);
    // DRAGGED TO A NEW PLACE: press on a line and drag it up or down - a white line shows where it will go.
    bom_->viewport()->installEventFilter(this);
    bom_->setToolTip(QStringLiteral("Drag a line up or down to change the order - the item numbers and the "
                                    "balloons follow"));
    bomDropLine_ = new QWidget(bom_->viewport());
    bomDropLine_->setObjectName(QStringLiteral("brickBomDropLine"));
    bomDropLine_->setStyleSheet(QStringLiteral("background:#ffffff;"));
    bomDropLine_->setAttribute(Qt::WA_TransparentForMouseEvents);
    bomDropLine_->hide();
    // The bill and what is done to it, moved together wherever it is put.
    bomBox_ = new QWidget(this);
    bomBox_->setObjectName(QStringLiteral("brickBomBox"));
    {
        auto* bl = new QVBoxLayout(bomBox_);
        bl->setContentsMargins(0, 0, 0, 0);
        bom_->setParent(bomBox_);
        bl->addWidget(bom_, 1);
        auto* row = new QHBoxLayout;
        auto* rm = new QPushButton(QStringLiteral("Remove from BOM"), bomBox_);
        rm->setObjectName(QStringLiteral("brickBomRemove"));
        rm->setToolTip(QStringLiteral("Take the picked line off the drawing's bill"));
        row->addStretch(1);
        row->addWidget(rm);
        bl->addLayout(row);
        connect(rm, &QPushButton::clicked, this, [this] { removeFromBom(bom_->currentRow()); });
    }
    connect(canvas_, &BrickCanvas::selectionChanged, this, [this](const QString& now) {
        // Matching a part to another: the brick just clicked is the other one.
        if (!matchSource_.isEmpty() && !now.isEmpty() && now != matchSource_) {
            const QString source = matchSource_;
            matchSource_.clear();
            if (partScaleMatch_) {
                const QSignalBlocker hush(partScaleMatch_);
                partScaleMatch_->setChecked(false);
            }
            const bool done = scalePartToMatch(source, now);
            canvas_->setSelected(source);
            loadPanel();
            refreshPreview();
            if (!done) status_->setText(QStringLiteral("That brick has no entry face to match"));
            return;
        }
        loadPanel();
        refreshPreview();
    });
    connect(canvas_, &BrickCanvas::linkPicked, this, [this](const QString&, const QString&) { loadPanel(); });
    // THE PANE SAYS WHAT IS JOINED TO WHAT, not how long anything is: bricks moved on it change no length. A
    // length is typed into the run, or set by dragging its part on the drawing (the preview).
    // LINKED TO A SPECIAL whose other end is on a part: put at the special's length from it.
    connect(canvas_, &BrickCanvas::linked, this, [this](const QString& a, const QString& b) {
        const Brick* ba = design_.brick(a);
        const Brick* bb = design_.brick(b);
        if (!ba || !bb) return;
        const Brick* sp = brickIsSpecial(ba->type) ? ba : bb;
        const Brick* en = sp == ba ? bb : ba;
        if (!brickIsSpecial(sp->type) || en->type != BrickType::End) return;
        if (!brickIsTied(*en)) return;   // a joint stands alone
        // The pane no longer places the drawing, so a special is not tied to its part there: each brick stays
        // where it is put.
    });
    // ABOVE OR UNDER ANOTHER BRICK: just over it (or just under it), everything between moved along to make room.
    connect(canvas_, &BrickCanvas::orderPicked, this, [this](const QString& source, const QString& other, bool above) {
        Brick* s = design_.brick(source);
        const Brick* o = design_.brick(other);
        if (!s || !o) return;
        const int at = above ? o->z + 1 : o->z;
        for (Brick& b : design_.bricks)
            if (b.id != source && b.z >= at) ++b.z;
        s->z = at;
        designChanged();
        status_->setText(QStringLiteral("%1 drawn %2 %3").arg(brickTitle(*s), above ? QStringLiteral("above")
                                                                                      : QStringLiteral("under"),
                                                              brickTitle(*o)));
    });
    connect(canvas_, &BrickCanvas::openInLibrary, this, &BrickWizardDialog::openInLibraryRequested);
    connect(canvas_, &BrickCanvas::dimensionPicked, this, [this](const QString& a, const QString& b) {
        // Which side is the part's own (its Dimensions setting): above unless it says below.
        design_.dims.push_back({a, b, design_.brick(a) && design_.brick(a)->dimBelow});
        designChanged();
        status_->setText(QStringLiteral("Dimension put on between %1 and %2 - click two more for another")
                             .arg(brickTitle(*design_.brick(a)), brickTitle(*design_.brick(b))));
    });
    // UNDO AND REDO, Ctrl+Z and Ctrl+Y (and Ctrl+Shift+Z).
    for (const auto& [keys, fn] : {std::pair<QKeySequence, void (BrickWizardDialog::*)()>{QKeySequence::Undo,
                                                                                            &BrickWizardDialog::undo},
                                   {QKeySequence(Qt::CTRL | Qt::Key_Y), &BrickWizardDialog::redo},
                                   {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), &BrickWizardDialog::redo}}) {
        auto* sc = new QShortcut(keys, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    }
    // DOUBLE-CLICK A BRICK: its part brought up on the drawing - half the pane, in the middle.
    connect(canvas_, &BrickCanvas::brickDoubleClicked, this, [this](const QString& id) {
        Bounds b;
        for (const Entity& e : lastEnts_)
            if (e.meta.value(QStringLiteral("brickId")).toString() == id && e.type != EntityType::Dimension)
                b.expand(e.bounds());
        selectBrick(id);   // first: picking it rebuilds the preview, which would put the old view back
        if (b.valid) preview_->zoomToBounds(b, 0.5);
    });
    connect(canvas_, &BrickCanvas::designChanged, this, &BrickWizardDialog::designChanged);
    connect(canvas_, &BrickCanvas::message, this, [this](const QString& t) { status_->setText(t); });

    // THE RIGHT: what the picked brick is, in sections that fold and pull taller or shorter.
    rightSplit_ = new QSplitter(Qt::Vertical, split);
    panel_ = new QStackedWidget(rightSplit_);
    panel_->setObjectName(QStringLiteral("brickPanel"));
    noneText_ = new QLabel(QStringLiteral("Pick a brick to set what it is."), panel_);
    noneText_->setWordWrap(true);
    noneText_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    panel_->addWidget(noneText_);
    panel_->addWidget(buildEndPage());
    panel_->addWidget(buildRunPage());
    panel_->addWidget(buildLinkPage());

    split->addWidget(leftSplit_);
    split->addWidget(middle_);
    split->addWidget(rightSplit_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({400, 700, 440});

    auto* bottom = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("brickStatus"));
    bottom->addWidget(status_, 1);
    auto* dimsBtn = new QPushButton(QStringLiteral("Dimensions…"), this);
    dimsBtn->setObjectName(QStringLiteral("brickDimensions"));
    dimsBtn->setToolTip(QStringLiteral("The dimension wizard: every dimension of the harness in one place"));
    connect(dimsBtn, &QPushButton::clicked, this, [this] { openDimensionWizard(); });
    bottom->addWidget(dimsBtn);
    auto* placeBtn = new QPushButton(QStringLiteral("Place the drawing"), this);
    placeBtn->setObjectName(QStringLiteral("brickPlace"));
    connect(placeBtn, &QPushButton::clicked, this, [this] { place(); });
    bottom->addWidget(placeBtn);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    bottom->addWidget(close);
    root->addLayout(bottom);

    // NO BUTTON HERE IS PRESSED BY ENTER. A dialog's buttons are all "default" to Qt, and Enter in a
    // text field presses the first of them - which was Rotate: typing a label's name and pressing
    // Enter turned the harness round.
    for (QPushButton* b : findChildren<QPushButton*>()) {
        b->setAutoDefault(false);
        b->setDefault(false);
    }
    refreshSaved();
    {
        const int at = std::clamp(QSettings().value(QStringLiteral("brickWizard/bomAt"), 1).toInt(), 0, 4);
        const QSignalBlocker block(bomAtCombo_);
        bomAtCombo_->setCurrentIndex(at);
        placeBom(at);
    }
    refreshBom();
}

QWidget* BrickWizardDialog::buildEndPage() {
    auto* column = new QWidget;
    column->setObjectName(QStringLiteral("brickEndSections"));
    QVBoxLayout* cols = sectionColumn(column);
    auto* page = new QWidget;
    auto* f = new QFormLayout(page);
    endKind_ = new QLabel(page);
    f->addRow(QStringLiteral("Part"), endKind_);
    part_ = new QComboBox(page);
    part_->setObjectName(QStringLiteral("brickPart"));
    // Wide enough to read the whole P/N and description, in the field and in the list it opens.
    part_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    part_->setMinimumContentsLength(34);
    part_->view()->setMinimumWidth(640);
    f->addRow(QStringLiteral("Library part"), part_);
    name_ = new QLineEdit(page);
    name_->setObjectName(QStringLiteral("brickName"));
    name_->setPlaceholderText(QStringLiteral("X1"));
    // THE DESIGNATOR LINE: the name, how tall it is written, and an eye for whether it is written on the
    // drawing at all - off until it is asked for.
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(3);
        row->addWidget(name_, 1);
        designatorHeight_ = new QDoubleSpinBox(page);
        designatorHeight_->setObjectName(QStringLiteral("brickDesignatorHeight"));
        designatorHeight_->setRange(0.5, 50.0);
        designatorHeight_->setSingleStep(0.5);
        designatorHeight_->setDecimals(1);
        designatorHeight_->setSuffix(QStringLiteral(" mm"));
        designatorHeight_->setValue(3.5);
        designatorHeight_->setToolTip(QStringLiteral("How tall the designator is written on the drawing"));
        row->addWidget(designatorHeight_);
        showDesignator_ = new QToolButton(page);
        showDesignator_->setObjectName(QStringLiteral("brickShowDesignator"));
        showDesignator_->setCheckable(true);
        showDesignator_->setChecked(false);
        showDesignator_->setText(QStringLiteral("👁"));
        showDesignator_->setToolTip(QStringLiteral("Write this designator on the drawing (lit) or not"));
        showDesignator_->setStyleSheet(QStringLiteral("QToolButton{padding:1px 5px;}"
                                                      "QToolButton:checked{background:#2b6cb0;color:white;}"));
        row->addWidget(showDesignator_);
        // THE SAME HEIGHT FOR EVERY DESIGNATOR on the drawing.
        auto* all = new QToolButton(page);
        all->setObjectName(QStringLiteral("brickDesignatorHeightAll"));
        all->setText(QStringLiteral("All"));
        all->setToolTip(QStringLiteral("Every designator on the drawing this tall"));
        connect(all, &QToolButton::clicked, this, [this] {
            const double h = designatorHeight_->value();
            for (Brick& b : design_.bricks)
                if (b.type == BrickType::End) b.designatorHeightMm = h;
            refreshPreview();
            status_->setText(QStringLiteral("Every designator is %1 mm tall").arg(h, 0, 'f', 1));
        });
        row->addWidget(all);
        f->addRow(QStringLiteral("Designator"), row);
    }
    balloon_ = new QCheckBox(QStringLiteral("Balloon"), page);
    balloon_->setObjectName(QStringLiteral("brickBalloon"));
    f->addRow(QString(), balloon_);
    // HOW BIG THE PART IS DRAWN. Its faces grow and shrink with it, so the wires and cables come in where
    // its faces now are. "A" works it out: as big as the bundle coming in wants, so the wires run straight
    // in. Ctrl + the wheel over a part in the preview does the same by hand.
    {
        partScale_ = new QDoubleSpinBox(page);
        partScale_->setObjectName(QStringLiteral("brickPartScale"));
        partScale_->setRange(10.0, 1000.0);
        partScale_->setDecimals(0);
        partScale_->setSingleStep(5.0);
        partScale_->setSuffix(QStringLiteral(" %"));
        partScale_->setToolTip(QStringLiteral("How big the part is drawn - Ctrl + wheel over it in the preview "
                                              "does the same"));
        partScaleAuto_ = new QToolButton(page);
        partScaleAuto_->setObjectName(QStringLiteral("brickPartScaleAuto"));
        partScaleAuto_->setText(QStringLiteral("A"));
        partScaleAuto_->setCheckable(true);
        partScaleAuto_->setToolTip(QStringLiteral("Auto: as big as the wires and cables coming in want, so they "
                                                  "run in with the fewest bends"));
        partScaleAuto_->setStyleSheet(QStringLiteral("QToolButton{padding:1px 5px;font-weight:bold;}"
                                                     "QToolButton:checked{background:#2b6cb0;color:white;}"));
        partScaleMatch_ = new QToolButton(page);
        partScaleMatch_->setObjectName(QStringLiteral("brickPartScaleMatch"));
        partScaleMatch_->setText(QStringLiteral("Match…"));
        partScaleMatch_->setCheckable(true);
        partScaleMatch_->setToolTip(QStringLiteral("Scale this part to another: press, then click the other brick - "
                                                   "this part's entry face is made as long as that one's nearest"));
        partScaleMatch_->setStyleSheet(QStringLiteral("QToolButton{padding:1px 5px;}"
                                                      "QToolButton:checked{background:#2b6cb0;color:white;}"));
        connect(partScaleMatch_, &QToolButton::toggled, this, [this](bool on) {
            matchSource_ = on ? canvas_->selected() : QString();
            if (on && matchSource_.isEmpty()) {
                const QSignalBlocker hush(partScaleMatch_);
                partScaleMatch_->setChecked(false);
                return;
            }
            status_->setText(on ? QStringLiteral("Click the brick to scale this part to") : QString());
        });
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(partScale_, 1);
        row->addWidget(partScaleAuto_);
        row->addWidget(partScaleMatch_);
        f->addRow(QStringLiteral("Part scale"), row);
        connect(partScale_, &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (partScale_->signalsBlocked()) return;
            // A figure typed in takes it over from Auto.
            if (partScaleAuto_->isChecked()) {
                const QSignalBlocker hush(partScaleAuto_);
                partScaleAuto_->setChecked(false);
            }
            writeEnd();
        });
        connect(partScaleAuto_, &QToolButton::toggled, this, [this](bool) { writeEnd(); });
    }
    bomState_ = new QLabel(page);
    bomState_->setObjectName(QStringLiteral("brickBomState"));
    bomState_->setWordWrap(true);
    bomAdd_ = new QPushButton(QStringLiteral("Add to BOM"), page);
    bomAdd_->setObjectName(QStringLiteral("brickBomAdd"));
    auto* bomRow = new QHBoxLayout;
    bomRow->addWidget(bomState_, 1);
    bomRow->addWidget(bomAdd_);
    f->addRow(QStringLiteral("BOM"), bomRow);
    // IS IT COMPLETE: a connector without its contacts says so, and offers the contacts tied to it.
    {
        auto* row = new QHBoxLayout;
        compat_ = new QLabel(page);
        compat_->setObjectName(QStringLiteral("brickCompat"));
        compat_->setWordWrap(true);
        compat_->setStyleSheet(QStringLiteral("color:#f0b35a;"));
        row->addWidget(compat_, 1);
        compatAdd_ = new QPushButton(QStringLiteral("Add contacts"), page);
        compatAdd_->setObjectName(QStringLiteral("brickCompatAdd"));
        compatAdd_->setToolTip(QStringLiteral("Put the contact tied to this connector on the bill, one per way"));
        row->addWidget(compatAdd_);
        f->addRow(QString(), row);
        connect(compatAdd_, &QPushButton::clicked, this, [this] {
            const Brick* b = design_.brick(canvas_->selected());
            const LibraryPart* p = b ? catalog_.byId(b->libPart) : nullptr;
            if (!p) return;
            const QStringList tied = tiedContactsOf(catalog_.merged(), p->id);
            if (tied.isEmpty()) return;
            if (addPartToBom(tied.front(), p->wayCount()))
                status_->setText(QStringLiteral("%1 contact(s) on the bill for %2").arg(p->wayCount()).arg(p->title()));
            loadPanel();
        });
    }
    // WHICH SIDE ITS DIMENSIONS GO.
    partDimSide_ = new QComboBox(page);
    partDimSide_->setObjectName(QStringLiteral("brickPartDimSide"));
    partDimSide_->addItems({QStringLiteral("Above the part"), QStringLiteral("Below the part")});
    partDimSide_->setToolTip(QStringLiteral("Where the dimensions put on from this part go"));
    f->addRow(QStringLiteral("Dimensions"), partDimSide_);
    connect(partDimSide_, &QComboBox::currentIndexChanged, this, [this](int i) {
        Brick* b = design_.brick(canvas_->selected());
        if (!b || b->type != BrickType::End) return;
        b->dimBelow = i == 1;
        // Its dimensions go that side now.
        for (BrickDim& dm : design_.dims)
            if (dm.a == b->id) dm.below = b->dimBelow;
        refreshPreview();
    });
    params_ = new QTableWidget(0, 2, page);
    params_->setObjectName(QStringLiteral("brickParams"));
    params_->setHorizontalHeaderLabels({QStringLiteral("Parameter"), QStringLiteral("Value")});
    params_->horizontalHeader()->setStretchLastSection(true);
    params_->verticalHeader()->setVisible(false);
    params_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    params_->setMinimumHeight(30);
    foldSection(QStringLiteral("Part"), page, cols, "brickFoldPart");
    foldSection(QStringLiteral("Parameters"), params_, cols, "brickFoldParams", 260);
    cols->addStretch(1);
    connect(part_, &QComboBox::currentIndexChanged, this, [this](int) {
        writeEnd();
        loadPanel();
        // Said at once when the part chosen is not complete on its own (a connector without its contacts).
        if (compat_ && !compat_->text().isEmpty()) status_->setText(compat_->text());
    });
    connect(name_, &QLineEdit::textEdited, this, [this](const QString&) { writeEnd(); });
    connect(balloon_, &QCheckBox::toggled, this, [this](bool) { writeEnd(); });
    connect(showDesignator_, &QToolButton::toggled, this, [this](bool on) {
        designatorHeight_->setEnabled(on);
        writeEnd();
    });
    connect(designatorHeight_, &QDoubleSpinBox::valueChanged, this, [this](double) { writeEnd(); });
    connect(bomAdd_, &QPushButton::clicked, this, [this] {
        const Brick* b = design_.brick(canvas_->selected());
        if (!b || b->libPart.isEmpty()) return;
        if (addPartToBom(b->libPart)) status_->setText(QStringLiteral("Added to the BOM"));
        loadPanel();
        refreshPreview();
    });
    return scrolled(column);
}

QWidget* BrickWizardDialog::buildRunPage() {
    auto* column = new QWidget;
    column->setObjectName(QStringLiteral("brickRunSections"));
    QVBoxLayout* cols = sectionColumn(column);
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    runForm_ = form;
    runKind_ = new QLabel(page);
    form->addRow(QStringLiteral("Special"), runKind_);
    auto spin = [page](const char* name, double lo, double hi, double step, const QString& suffix,
                       const QString& special = QString()) {
        auto* s = new QDoubleSpinBox(page);
        s->setObjectName(QLatin1String(name));
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(1);
        s->setSuffix(suffix);
        if (!special.isEmpty()) s->setSpecialValueText(special);
        return s;
    };
    wires_ = new QSpinBox(page);
    wires_->setObjectName(QStringLiteral("brickWires"));
    wires_->setRange(1, 64);
    form->addRow(QStringLiteral("Wires"), wires_);
    // A TWISTED PAIR: how many pairs (two wires each).
    pairs_ = new QSpinBox(page);
    pairs_->setObjectName(QStringLiteral("brickPairs"));
    pairs_->setRange(1, 32);
    pairs_->setToolTip(QStringLiteral("How many twisted pairs - two wires each"));
    form->addRow(QStringLiteral("Pairs"), pairs_);
    connect(pairs_, &QSpinBox::valueChanged, this, [this](int) {
        writeRun();
        loadPanel();
    });
    length_ = spin("brickLength", 0.0, 100000.0, 10.0, QStringLiteral(" mm"));   // down to nought: parts face to face
    form->addRow(QStringLiteral("Length"), length_);
    // BRIDGE WIRES: which wires of the cable, fanout or heatshrink this one is joined to it takes - "1-3,5" -
    // and so how many wires it has and what each one's net is.
    {
        auto* host = new QWidget(page);
        auto* v = new QVBoxLayout(host);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(1);
        bridge_ = new QComboBox(host);
        bridge_->setObjectName(QStringLiteral("brickBridgeWires"));
        bridge_->setEditable(true);
        bridge_->setInsertPolicy(QComboBox::NoInsert);
        bridge_->setToolTip(QStringLiteral(
            "Which wire of the cable this bridge wire takes: pick one of the cable's wires (by its net, where it "
            "has one) or its braided shield - or type several, \"1-3,5\"."));
        // SEVERAL OF THE CABLE'S WIRES: ticked one by one from a list of them.
        auto* pickRow = new QHBoxLayout;
        pickRow->setContentsMargins(0, 0, 0, 0);
        pickRow->addWidget(bridge_, 1);
        bridgeMany_ = new QToolButton(host);
        bridgeMany_->setObjectName(QStringLiteral("brickBridgeMany"));
        bridgeMany_->setText(QStringLiteral("Wires…"));
        bridgeMany_->setToolTip(QStringLiteral("Take several of the cable's wires: tick each one"));
        bridgeMany_->setPopupMode(QToolButton::InstantPopup);
        auto* manyMenu = new QMenu(bridgeMany_);
        bridgeMany_->setMenu(manyMenu);
        connect(manyMenu, &QMenu::aboutToShow, this, [this, manyMenu] {
            manyMenu->clear();
            const Brick* b = design_.brick(canvas_->selected());
            const Brick* from = b ? design_.brick(joinedSpecial(design_, b->id)) : nullptr;
            if (!b || !from) {
                manyMenu->addAction(QStringLiteral("Link it to a cable first"))->setEnabled(false);
                return;
            }
            const QList<int> taken = parseWireList(b->run.bridgeWires, from->run.wires);
            for (int w = 1; w <= from->run.wires; ++w) {
                const QString net = from->run.nets.value(w - 1);
                QAction* a = manyMenu->addAction(net.isEmpty() ? QStringLiteral("Wire %1").arg(w)
                                                               : QStringLiteral("Wire %1 - %2").arg(w).arg(net));
                a->setCheckable(true);
                a->setChecked(taken.contains(w));
                connect(a, &QAction::toggled, this, [this, w](bool on) {
                    Brick* bb = design_.brick(canvas_->selected());
                    const Brick* src = bb ? design_.brick(joinedSpecial(design_, bb->id)) : nullptr;
                    if (!bb || !src) return;
                    QList<int> now = parseWireList(bb->run.bridgeWires, src->run.wires);
                    if (on && !now.contains(w)) now << w;
                    if (!on) now.removeAll(w);
                    if (now.isEmpty()) return;   // a bridge wire takes at least one
                    std::sort(now.begin(), now.end());
                    QStringList said;
                    for (int x : now) said << QString::number(x);
                    bridge_->setEditText(said.join(QLatin1Char(',')));
                    writeRun();
                    loadPanel();
                });
            }
        });
        pickRow->addWidget(bridgeMany_);
        v->addLayout(pickRow);
        bridgeFrom_ = new QLabel(host);
        bridgeFrom_->setObjectName(QStringLiteral("brickBridgeFrom"));
        bridgeFrom_->setStyleSheet(QStringLiteral("color:#8fa3b8;"));
        v->addWidget(bridgeFrom_);
        form->addRow(QStringLiteral("Bridge wires"), host);
        connect(bridge_, &QComboBox::activated, this, [this](int) {
            writeRun();
            loadPanel();
        });
        connect(bridge_->lineEdit(), &QLineEdit::editingFinished, this, [this] {
            writeRun();
            loadPanel();
        });
    }
    wireWidth_ = spin("brickWireWidth", 0.2, 20.0, 0.1, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Wire width"), wireWidth_);
    // How many lines the wires converge to where they are gathered.
    trunk_ = spin("brickTrunk", 1.0, 12.0, 1.0, QStringLiteral(" wires"));
    trunk_->setDecimals(0);
    // A FIGURE THE DRAWING WORKS OUT WHEN LEFT ALONE: its actual size in the field, and "A" beside it
    // - lit while it is left alone. Typing a figure takes it over; "A" gives it back.
    auto withAuto = [this, page](QDoubleSpinBox* s, const char* name) {
        auto* row = new QWidget(page);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(3);
        h->addWidget(s, 1);
        auto* a = new QToolButton(row);
        a->setObjectName(QLatin1String(name));
        a->setText(QStringLiteral("A"));
        a->setCheckable(true);
        a->setToolTip(QStringLiteral("Auto: the drawing works it out - lit while it does"));
        a->setStyleSheet(QStringLiteral("QToolButton{padding:1px 5px;font-weight:bold;}"
                                        "QToolButton:checked{background:#2b6cb0;color:white;}"));
        h->addWidget(a);
        autoOf_.insert(s, a);
        connect(a, &QToolButton::toggled, this, [this](bool) {
            writeRun();
            showActuals();
        });
        return row;
    };
    // A FAN OR NONE, beside the "A": lit, the wires spread across the face of the part at that end; off,
    // they go into it as the bundle they are, and the length is not asked for.
    auto withFan = [this](QWidget* row, QDoubleSpinBox* s, const char* name) {
        auto* f = new QToolButton(row);
        f->setObjectName(QLatin1String(name));
        f->setText(QStringLiteral("Fan"));
        f->setCheckable(true);
        f->setChecked(true);
        f->setToolTip(QStringLiteral("A fan at this end: lit, the wires spread across the part's face - "
                                     "off, they go in as the bundle"));
        f->setStyleSheet(QStringLiteral("QToolButton{padding:1px 4px;}"
                                        "QToolButton:checked{background:#2f855a;color:white;}"));
        row->layout()->addWidget(f);
        connect(f, &QToolButton::toggled, this, [this, s](bool on) {
            s->setEnabled(on);
            if (QToolButton* a = autoOf_.value(s)) a->setEnabled(on);
            writeRun();
            showActuals();
        });
        return f;
    };
    fanStart_ = spin("brickFanStart", 0.5, 1000.0, 1.0, QStringLiteral(" mm"));
    {
        QWidget* row = withAuto(fanStart_, "brickFanStartAuto");
        fanStartOn_ = withFan(row, fanStart_, "brickFanStartOn");
        form->addRow(QStringLiteral("Fan length, start"), row);
    }
    fanFinish_ = spin("brickFanFinish", 0.5, 1000.0, 1.0, QStringLiteral(" mm"));
    {
        QWidget* row = withAuto(fanFinish_, "brickFanFinishAuto");
        fanFinishOn_ = withFan(row, fanFinish_, "brickFanFinishOn");
        form->addRow(QStringLiteral("Fan length, finish"), row);
    }
    knee_ = spin("brickKnee", 0.0, 1000.0, 1.0, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Bend radius, body"), withAuto(knee_, "brickKneeAuto"));
    fanKnee_ = spin("brickFanKnee", 0.0, 1000.0, 0.5, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Bend radius, fans"), withAuto(fanKnee_, "brickFanKneeAuto"));
    form->addRow(QStringLiteral("Converge to"), withAuto(trunk_, "brickTrunkAuto"));
    fanScale_ = spin("brickFanScale", 5.0, 100.0, 5.0, QStringLiteral(" %"));
    form->addRow(QStringLiteral("Fans across the face"), fanScale_);
    bodyScale_ = spin("brickBodyScale", 5.0, 100.0, 5.0, QStringLiteral(" %"));
    form->addRow(QStringLiteral("Body across the face"), bodyScale_);
    bodyWidth_ = spin("brickBodyWidth", 0.0, 1000.0, 0.5, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Body width"), withAuto(bodyWidth_, "brickBodyWidthAuto"));
    transitions_ = new QComboBox(page);
    transitions_->setObjectName(QStringLiteral("brickTransitions"));
    transitions_->addItems({QStringLiteral("None"), QStringLiteral("Start"), QStringLiteral("Finish"),
                            QStringLiteral("Both")});
    form->addRow(QStringLiteral("Transition heatshrink"), transitions_);
    // ITS WIDTH, which cannot be less than the cable it goes over: turned down past that, the field stops at
    // the cable's width.
    transitionWidth_ = spin("brickTransitionWidth", 0.0, 1000.0, 0.5, QStringLiteral(" mm"));
    transitionWidth_->setToolTip(QStringLiteral("How wide the transition heatshrink is - never less than the cable"));
    form->addRow(QStringLiteral("Transition width"), withAuto(transitionWidth_, "brickTransitionWidthAuto"));
    for (QDoubleSpinBox* s : {length_, wireWidth_, fanStart_, fanFinish_, knee_, fanKnee_, fanScale_,
                              bodyScale_, bodyWidth_, trunk_, transitionWidth_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this, s](double) {
            // A figure typed in takes it over from Auto.
            if (QToolButton* a = autoOf_.value(s)) {
                const QSignalBlocker block(a);
                a->setChecked(false);
            }
            writeRun();
        });
    foldSection(QStringLiteral("Run"), page, cols, "brickFoldRun");
    connect(wires_, &QSpinBox::valueChanged, this, [this](int) {
        writeRun();
        loadLabels();   // the wires a label can go on
        loadNets();
    });
    connect(transitions_, &QComboBox::currentIndexChanged, this, [this](int i) {
        transitionWidth_->setEnabled(i != 0);
        autoOf_.value(transitionWidth_)->setEnabled(i != 0);
        writeRun();
        showActuals();
    });

    // THE WIRES AND THEIR NETS, numbered from the top down as the labels are. A net is picked from
    // the schematic's - those landing on the parts this run is linked to - or typed in.
    auto* netBox = new QGroupBox(QStringLiteral("Wires and nets"), page);
    auto* nv = new QVBoxLayout(netBox);
    nets_ = new QTableWidget(0, 2, netBox);
    nets_->setObjectName(QStringLiteral("brickNets"));
    nets_->setHorizontalHeaderLabels({QStringLiteral("Wire"), QStringLiteral("Net")});
    nets_->horizontalHeader()->setStretchLastSection(true);
    nets_->verticalHeader()->setVisible(false);
    nets_->setMinimumHeight(30);
    nv->addWidget(nets_);
    netBox->setTitle(QString());
    foldSection(QStringLiteral("Wires and nets"), netBox, cols, "brickFoldNets", 150);

    // THE STACK, from the middle out; the top of the list is the outermost, and only it is seen.
    auto* stackBox = new QGroupBox(QStringLiteral("Stack — from the inside out"), page);
    auto* sv = new QVBoxLayout(stackBox);
    stack_ = new QListWidget(stackBox);
    stack_->setObjectName(QStringLiteral("brickStack"));
    stack_->setMinimumHeight(30);
    sv->addWidget(stack_);
    auto* sb = new QHBoxLayout;
    auto* addLayer = new QComboBox(stackBox);
    addLayer->setObjectName(QStringLiteral("brickStackAdd"));
    addLayer->addItem(QStringLiteral("Add…"), QString());
    addLayer->addItem(QStringLiteral("Cable"), QStringLiteral("cable"));
    addLayer->addItem(QStringLiteral("Braided shield"), QStringLiteral("braid"));
    addLayer->addItem(QStringLiteral("Heatshrink"), QStringLiteral("heatshrink"));
    sb->addWidget(addLayer);
    auto layerBtn = [&](const char* name, const QString& text) {
        auto* b = new QPushButton(text, stackBox);
        b->setObjectName(QLatin1String(name));
        sb->addWidget(b);
        return b;
    };
    auto* outBtn = layerBtn("brickStackOut", QStringLiteral("Outward"));
    auto* inBtn = layerBtn("brickStackIn", QStringLiteral("Inward"));
    auto* rmBtn = layerBtn("brickStackRemove", QStringLiteral("Remove"));
    sv->addLayout(sb);
    // THE PICKED LAYER'S PART NUMBER: from the parts list (parts of that layer's kind), or the same as the
    // cables next along have.
    {
        auto* pr = new QHBoxLayout;
        pr->addWidget(new QLabel(QStringLiteral("P/N"), stackBox));
        layerPart_ = new QComboBox(stackBox);
        layerPart_->setObjectName(QStringLiteral("brickLayerPart"));
        layerPart_->setToolTip(QStringLiteral("The part number of the layer picked above, from the parts list"));
        pr->addWidget(layerPart_, 1);
        auto* same = new QPushButton(QStringLiteral("As neighbours"), stackBox);
        same->setObjectName(QStringLiteral("brickLayerPartNeighbours"));
        same->setToolTip(QStringLiteral("Every layer the same part as on the cables next along (parts between "
                                        "them do not count)"));
        pr->addWidget(same);
        sv->addLayout(pr);
        connect(stack_, &QListWidget::currentRowChanged, this, [this](int) { loadLayerPart(); });
        connect(layerPart_, &QComboBox::activated, this, [this](int i) {
            Brick* b = design_.brick(canvas_->selected());
            const int row = stack_->currentRow();
            if (!b || !brickIsSpecial(b->type) || row < 0 || row >= b->run.stack.size()) return;
            const QString id = layerPart_->itemData(i).toString();
            b->run.stackParts.insert(b->run.stack[row], id);
            // On the bill, once.
            if (!id.isEmpty() && bomItemOf(id) == 0) addPartToBom(id);
            loadPanel();
            refreshPreview();
        });
        connect(same, &QPushButton::clicked, this, [this] {
            const int n = layerPartsFromNeighbours(canvas_->selected());
            loadPanel();
            refreshPreview();
            status_->setText(n > 0 ? QStringLiteral("%1 layer(s) given the part the cables next along have").arg(n)
                                   : QStringLiteral("No cable next along has a part for these layers"));
        });
    }
    stackBox->setTitle(QString());
    foldSection(QStringLiteral("Stack — from the inside out"), stackBox, cols, "brickFoldStack", 150);
    auto editStack = [this](auto fn) {
        Brick* b = design_.brick(canvas_->selected());
        if (!b || !brickIsSpecial(b->type)) return;
        fn(b->run.stack);
        if (b->run.stack.isEmpty() || b->run.stack.front() != QLatin1String("wires")) {
            b->run.stack.removeAll(QStringLiteral("wires"));
            b->run.stack.prepend(QStringLiteral("wires"));
        }
        loadPanel();
        refreshPreview();
    };
    connect(addLayer, &QComboBox::activated, this, [this, addLayer, editStack](int i) {
        const QString key = addLayer->itemData(i).toString();
        addLayer->setCurrentIndex(0);
        if (key.isEmpty()) return;
        editStack([&](QStringList& s) {
            if (!s.contains(key)) s << key;
        });
    });
    // The list is the stack as it goes on, the wires first and the outermost last: row r is layer r.
    connect(outBtn, &QPushButton::clicked, this, [this, editStack] {
        const int i = stack_->currentRow();
        bool moved = false;
        editStack([&](QStringList& s) {
            if (i >= 1 && i + 1 < s.size()) {
                s.swapItemsAt(i, i + 1);
                moved = true;
            }
        });
        stack_->setCurrentRow(moved ? i + 1 : i);
    });
    connect(inBtn, &QPushButton::clicked, this, [this, editStack] {
        const int i = stack_->currentRow();
        bool moved = false;
        editStack([&](QStringList& s) {
            if (i >= 2 && i < s.size()) {
                s.swapItemsAt(i, i - 1);
                moved = true;
            }
        });
        stack_->setCurrentRow(moved ? i - 1 : i);
    });
    connect(rmBtn, &QPushButton::clicked, this, [this, editStack] {
        const int i = stack_->currentRow();
        editStack([&](QStringList& s) {
            if (i >= 1 && i < s.size()) s.removeAt(i);
        });
    });

    // DIMENSIONS.
    auto* dims = new QGroupBox(QStringLiteral("Dimensions"), page);
    auto* dv = new QGridLayout(dims);
    dimTotal_ = new QCheckBox(QStringLiteral("Whole length"), dims);
    dimTotal_->setObjectName(QStringLiteral("brickDimTotal"));
    dimFans_ = new QCheckBox(QStringLiteral("Fans only"), dims);
    dimFans_->setObjectName(QStringLiteral("brickDimFans"));
    dimBetween_ = new QCheckBox(QStringLiteral("Between fans"), dims);
    dimBetween_->setObjectName(QStringLiteral("brickDimBetween"));
    auto sayEdit = [dims](const char* name) {
        auto* e = new QLineEdit(dims);
        e->setObjectName(QLatin1String(name));
        e->setPlaceholderText(QStringLiteral("the measurement"));
        e->setToolTip(QStringLiteral("What the dimension says on the drawing - the real length is kept "
                                     "with it"));
        return e;
    };
    dimTextTotal_ = sayEdit("brickDimTextTotal");
    dimTextFans_ = sayEdit("brickDimTextFans");
    dimTextBetween_ = sayEdit("brickDimTextBetween");
    // EACH ONE ABOVE OR BELOW the run, as picked beside it.
    auto sideBox = [dims](const char* name) {
        auto* s = new QComboBox(dims);
        s->setObjectName(QLatin1String(name));
        s->addItems({QStringLiteral("Above"), QStringLiteral("Below")});
        s->setToolTip(QStringLiteral("Which side of the run this dimension goes"));
        return s;
    };
    dimSideTotal_ = sideBox("brickDimSideTotal");
    dimSideFans_ = sideBox("brickDimSideFans");
    dimSideBetween_ = sideBox("brickDimSideBetween");
    int dr = 0;
    for (auto [c, e, sb] : {std::tuple<QCheckBox*, QLineEdit*, QComboBox*>{dimTotal_, dimTextTotal_, dimSideTotal_},
                            {dimFans_, dimTextFans_, dimSideFans_},
                            {dimBetween_, dimTextBetween_, dimSideBetween_}}) {
        dv->addWidget(c, dr, 0);
        dv->addWidget(e, dr, 1);
        dv->addWidget(sb, dr, 2);
        ++dr;
        connect(c, &QCheckBox::toggled, this, [this](bool) { writeRun(); });
        connect(e, &QLineEdit::textEdited, this, [this](const QString&) { writeRun(); });
        connect(sb, &QComboBox::currentIndexChanged, this, [this](int) { writeRun(); });
    }
    // HOW THE WHOLE LENGTH IS DRAWN: linear along the harness, or at the angle between its points -
    // tracing the run round its bends, if asked - or single-ended.
    dimLinear_ = new QCheckBox(QStringLiteral("Linear"), dims);
    dimLinear_->setObjectName(QStringLiteral("brickDimLinear"));
    dimLinear_->setChecked(true);
    dimTrace_ = new QCheckBox(QStringLiteral("Trace the run"), dims);
    dimTrace_->setObjectName(QStringLiteral("brickDimTrace"));
    dimTrace_->setToolTip(QStringLiteral("Follow the harness round every bend, knee radius and all"));
    dimSingle_ = new QCheckBox(QStringLiteral("Single end"), dims);
    dimSingle_->setObjectName(QStringLiteral("brickDimSingle"));
    dimSingle_->setToolTip(QStringLiteral("From its start: a short line, one arrow, the figure over it"));
    auto* styleRow = new QHBoxLayout;
    styleRow->addWidget(dimLinear_);
    styleRow->addWidget(dimTrace_);
    styleRow->addWidget(dimSingle_);
    styleRow->addStretch(1);
    dv->addLayout(styleRow, dr, 0, 1, 3);
    for (QCheckBox* c : {dimLinear_, dimTrace_, dimSingle_})
        connect(c, &QCheckBox::toggled, this, [this](bool) {
            dimTrace_->setVisible(!dimLinear_->isChecked());
            writeRun();
        });
    dims->setTitle(QString());
    foldSection(QStringLiteral("Dimensions"), dims, cols, "brickFoldDims");

    // LABELS.
    auto* labBox = new QGroupBox(QStringLiteral("Labels"), page);
    auto* lv = new QVBoxLayout(labBox);
    labels_ = new QTableWidget(0, 7, labBox);
    labels_->setObjectName(QStringLiteral("brickLabels"));
    labels_->setHorizontalHeaderLabels({QStringLiteral("On"), QStringLiteral("From"),
                                        QStringLiteral("Distance"), QStringLiteral("Length"),
                                        QStringLiteral("Show on drawing"), QStringLiteral("Clear heatshrink"),
                                        QStringLiteral("Text")});
    labels_->horizontalHeader()->setStretchLastSection(true);
    labels_->verticalHeader()->setVisible(false);
    labels_->setMinimumHeight(30);
    lv->addWidget(labels_);
    auto* lb = new QHBoxLayout;
    auto* addLab = new QPushButton(QStringLiteral("Add label"), labBox);
    addLab->setObjectName(QStringLiteral("brickLabelAdd"));
    auto* rmLab = new QPushButton(QStringLiteral("Remove"), labBox);
    rmLab->setObjectName(QStringLiteral("brickLabelRemove"));
    lb->addWidget(addLab);
    lb->addWidget(rmLab);
    lb->addStretch(1);
    lv->addLayout(lb);
    labBox->setTitle(QString());
    foldSection(QStringLiteral("Labels"), labBox, cols, "brickFoldLabels", 190);
    cols->addStretch(1);
    connect(addLab, &QPushButton::clicked, this, [this] {
        Brick* b = design_.brick(canvas_->selected());
        if (!b || !brickIsSpecial(b->type)) return;
        b->run.labels.push_back(BrickLabel{});
        loadLabels();
        refreshPreview();
    });
    connect(rmLab, &QPushButton::clicked, this, [this] {
        Brick* b = design_.brick(canvas_->selected());
        const int r = labels_->currentRow();
        if (!b || r < 0 || r >= static_cast<int>(b->run.labels.size())) return;
        b->run.labels.erase(b->run.labels.begin() + r);
        loadLabels();
        refreshPreview();
    });
    return scrolled(column);
}

QWidget* BrickWizardDialog::buildLinkPage() {
    auto* page = new QWidget(panel_);
    auto* f = new QFormLayout(page);
    linkText_ = new QLabel(page);
    linkText_->setWordWrap(true);
    f->addRow(linkText_);
    linkWires_ = new QSpinBox(page);
    linkWires_->setObjectName(QStringLiteral("brickLinkWires"));
    linkWires_->setRange(0, 64);
    linkWires_->setSpecialValueText(QStringLiteral("An even share"));
    f->addRow(QStringLiteral("Wires to this part"), linkWires_);
    connect(linkWires_, &QSpinBox::valueChanged, this, [this](int n) {
        const auto l = canvas_->selectedLink();
        if (BrickLink* link = design_.link(l.first, l.second)) {
            link->wires = n;
            canvas_->update();
            refreshPreview();
        }
    });
    return page;
}

void BrickWizardDialog::fillPartsList() {
    parts_->clear();
    const QString filter = partsFilter_->text().trimmed();
    const PartsLibrary all = catalog_.merged();
    for (PartKind k : allPartKinds())
        for (const LibraryPart* p : all.ofKind(k)) {
            const QStringList cols{p->mfrPn, p->description, partKindName(p->kind), p->mfrName};
            if (!filter.isEmpty() && !cols.join(QLatin1Char(' ')).contains(filter, Qt::CaseInsensitive))
                continue;
            // WHAT THE WIZARD CAN SET IT DOWN BY: a drawing, its entry faces, its near and far points.
            const bool line = partDrawingConnectionLine(p->mechanical) >= 0;
            const QString faces = !p->outputs.empty() ? QString::number(p->outputs.size())
                                  : line               ? QStringLiteral("line")
                                                       : QStringLiteral("—");
            QStringList row = cols;
            row << (p->hasMechanical() ? QStringLiteral("✔") : QStringLiteral("—")) << faces
                << (p->hasNearPoint && p->hasFarPoint ? QStringLiteral("✔") : QStringLiteral("—"));
            auto* it = new QTreeWidgetItem(parts_, row);
            it->setData(0, Qt::UserRole, p->id);
            it->setToolTip(1, p->description);
            it->setToolTip(4, QStringLiteral("Has a mechanical drawing"));
            it->setToolTip(5, QStringLiteral("Entry faces marked (ports the brick gets)"));
            it->setToolTip(6, QStringLiteral("Near and far points marked (for dimensions)"));
        }
}

QString BrickWizardDialog::addFromPalette(const QString& what) {
    const QStringList parts = what.split(QLatin1Char('|'));
    if (parts.size() < 2) return QString();
    // IN VIEW: the middle of what the pane shows, or the nearest free place below it.
    const QPointF c = canvas_->viewCentreGrid();
    const double col = std::round(c.x());
    double row = std::round(c.y() * 2.0) / 2.0;
    auto taken = [&](double r) {
        for (const Brick& b : design_.bricks)
            if (std::abs(b.col - col) < 0.6 && std::abs(b.row - r) < 0.6) return true;
        return false;
    };
    for (int i = 0; i < 40 && taken(row); ++i) row += 1.0;
    const QString id = design_.addBrick(brickTypeFromKey(parts[0]), partKindFromKey(parts[1]), col, row);
    if (parts.value(2) == QLatin1String("bridge"))
        if (Brick* b = design_.brick(id)) makeBridgeWire(*b);
    selectBrick(id);
    designChanged();
    return id;
}

void BrickWizardDialog::selectBrick(const QString& id) {
    canvas_->setSelected(id);
    loadPanel();
    refreshPreview();
}

void BrickWizardDialog::moveSelected(double dCol, double dRow) {
    if (canvas_->selection().isEmpty()) {
        status_->setText(QStringLiteral("Pick a brick to move"));
        return;
    }
    canvas_->moveSelection(dCol, dRow);
}

void BrickWizardDialog::rotateSelected() {
    if (canvas_->selection().isEmpty()) {
        status_->setText(QStringLiteral("Pick a brick to turn"));
        return;
    }
    canvas_->rotateSelection();
}

void BrickWizardDialog::arrange() {
    // SYMMETRIC ON THE DRAWING: the parts set up or down on it where a symmetric layout puts them - the bricks on the
    // pane are left exactly where they were put.
    symmetricOnDrawing(design_, canvas_ ? lastRowPitch() : 0.0);
    designChanged();
    preview_->fitToView();
    status_->setText(QStringLiteral("The drawing laid out symmetrically - the bricks left where they are"));
}

double BrickWizardDialog::lastRowPitch() const {
    return buildBrickHarness(design_, &catalog_).rowPitch;
}

std::vector<Entity> BrickWizardDialog::drawing() const {
    return buildBrickHarness(design_, &catalog_,
                             [this](const QString& id) { return bomItemOf(id); })
        .entities;
}

void BrickWizardDialog::setAllDimsSide(bool below) {
    setAllDimensionsBelow(design_, below);
    designChanged();
    status_->setText(below ? QStringLiteral("Every dimension below the objects")
                           : QStringLiteral("Every dimension above the objects"));
}

void BrickWizardDialog::openDimensionWizard() {
    if (!dimWizard_) {
        dimWizard_ = new QDialog(this);
        dimWizard_->setObjectName(QStringLiteral("brickDimWizard"));
        dimWizard_->setWindowTitle(QStringLiteral("Dimensions"));
        dimWizard_->setModal(false);
        dimWizard_->resize(720, 560);
        auto* lay = new QVBoxLayout(dimWizard_);
        // WHAT IS MEASURED: only the runs between two parts - a cable, a fanout, wires - never a joint or a part on
        // the way; and each end from its near point, far point or middle (a joint from its face toward the other end).
        auto* note = new QLabel(QStringLiteral("A dimension measures only the cables, fanouts and wires between its two "
                                               "bricks - Y-splits, heatshrinks and other parts on the way add nothing. "
                                               "A joint at an end is measured from its face nearest the other end. On "
                                               "the placed drawing every dimension says its figure as a text override."),
                                dimWizard_);
        note->setWordWrap(true);
        lay->addWidget(note);
        auto* top = new QHBoxLayout;
        top->addWidget(new QLabel(QStringLiteral("All dimensions"), dimWizard_));
        dimWizSide_ = new QComboBox(dimWizard_);
        dimWizSide_->setObjectName(QStringLiteral("dimWizSide"));
        dimWizSide_->addItems({QStringLiteral("Above the objects"), QStringLiteral("Below the objects")});
        connect(dimWizSide_, &QComboBox::activated, this, [this](int i) { setAllDimsSide(i == 1); });
        top->addWidget(dimWizSide_);
        top->addSpacing(16);
        top->addWidget(new QLabel(QStringLiteral("From"), dimWizard_));
        dimWizFrom_ = new QComboBox(dimWizard_);
        dimWizFrom_->setObjectName(QStringLiteral("dimWizFrom"));
        dimWizFrom_->addItems({QStringLiteral("Near point"), QStringLiteral("Far point"), QStringLiteral("Mid point")});
        top->addWidget(dimWizFrom_);
        top->addWidget(new QLabel(QStringLiteral("to"), dimWizard_));
        dimWizTo_ = new QComboBox(dimWizard_);
        dimWizTo_->setObjectName(QStringLiteral("dimWizTo"));
        dimWizTo_->addItems({QStringLiteral("Near point"), QStringLiteral("Far point"), QStringLiteral("Mid point")});
        top->addWidget(dimWizTo_);
        // The same as the bar's: the bar's box is set, and it sets the design.
        connect(dimWizFrom_, &QComboBox::activated, this, [this](int i) { dimFrom_->setCurrentIndex(i); });
        connect(dimWizTo_, &QComboBox::activated, this, [this](int i) { dimTo_->setCurrentIndex(i); });
        top->addStretch(1);
        lay->addLayout(top);

        auto* runsBox = new QGroupBox(QStringLiteral("Runs"), dimWizard_);
        auto* rl = new QVBoxLayout(runsBox);
        dimRuns_ = new QTableWidget(0, 6, runsBox);
        dimRuns_->setObjectName(QStringLiteral("dimWizRuns"));
        dimRuns_->setHorizontalHeaderLabels({QStringLiteral("Run"), QStringLiteral("Length"),
                                             QStringLiteral("Whole length"), QStringLiteral("Fans"),
                                             QStringLiteral("Between fans"), QStringLiteral("Side")});
        dimRuns_->verticalHeader()->setVisible(false);
        dimRuns_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        dimRuns_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        rl->addWidget(dimRuns_);
        lay->addWidget(runsBox, 1);

        auto* handBox = new QGroupBox(QStringLiteral("Between two bricks"), dimWizard_);
        auto* hl = new QVBoxLayout(handBox);
        dimHand_ = new QTableWidget(0, 4, handBox);
        dimHand_->setObjectName(QStringLiteral("dimWizHand"));
        dimHand_->setHorizontalHeaderLabels(
            {QStringLiteral("From"), QStringLiteral("To"), QStringLiteral("Measures"), QStringLiteral("Side")});
        dimHand_->verticalHeader()->setVisible(false);
        dimHand_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        dimHand_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        dimHand_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        dimHand_->setSelectionBehavior(QAbstractItemView::SelectRows);
        hl->addWidget(dimHand_);
        auto* hb = new QHBoxLayout;
        // Pressed, it stays pressed: every two bricks clicked get a dimension, till it is pressed again.
        auto* add = new QPushButton(QStringLiteral("Add: click 2 bricks"), handBox);
        add->setObjectName(QStringLiteral("dimWizAdd"));
        add->setCheckable(true);
        add->setChecked(dimTool_ && dimTool_->isChecked());
        connect(add, &QPushButton::toggled, this, [this](bool on) {
            if (dimTool_) dimTool_->setChecked(on);
            status_->setText(on ? QStringLiteral("Click two bricks on the pane to dimension between them")
                                : QStringLiteral("Dimensioning off"));
        });
        connect(dimTool_, &QToolButton::toggled, add, [add](bool on) {
            const QSignalBlocker hush(add);
            add->setChecked(on);
        });
        hb->addWidget(add);
        auto* del = new QPushButton(QStringLiteral("Delete"), handBox);
        del->setObjectName(QStringLiteral("dimWizDelete"));
        connect(del, &QPushButton::clicked, this, [this] {
            const int row = dimHand_->currentRow();
            if (row < 0 || row >= static_cast<int>(design_.dims.size())) return;
            design_.dims.erase(design_.dims.begin() + row);
            designChanged();
        });
        hb->addWidget(del);
        auto* clear = new QPushButton(QStringLiteral("Clear all"), handBox);
        connect(clear, &QPushButton::clicked, this, [this] {
            design_.dims.clear();
            designChanged();
        });
        hb->addWidget(clear);
        hb->addStretch(1);
        hl->addLayout(hb);
        lay->addWidget(handBox, 1);

        auto* close = new QPushButton(QStringLiteral("Close"), dimWizard_);
        close->setAutoDefault(false);
        connect(close, &QPushButton::clicked, dimWizard_, &QDialog::hide);
        auto* cl = new QHBoxLayout;
        cl->addStretch(1);
        cl->addWidget(close);
        lay->addLayout(cl);
        for (QPushButton* b : dimWizard_->findChildren<QPushButton*>()) b->setAutoDefault(false);
    }
    refreshDimensionWizard();
    dimWizard_->show();
    dimWizard_->raise();
    dimWizard_->activateWindow();
}

void BrickWizardDialog::refreshDimensionWizard() {
    if (!dimWizard_) return;
    {
        const QSignalBlocker a(dimWizSide_), b(dimWizFrom_), c(dimWizTo_);
        dimWizSide_->setCurrentIndex(allDimensionsBelow(design_) ? 1 : 0);
        dimWizFrom_->setCurrentIndex(design_.dimFromMid ? 2 : design_.dimFromFar ? 1 : 0);
        dimWizTo_->setCurrentIndex(design_.dimToMid ? 2 : design_.dimToFar ? 1 : 0);
    }
    // A change made from a box in the table rebuilds the table - later, never under the box's own signal.
    auto later = [this](std::function<void()> fn) {
        QTimer::singleShot(0, this, [this, fn] {
            fn();
            designChanged();
        });
    };
    // THE RUNS: each special, its length, which of its dimensions are on, and their side.
    std::vector<const Brick*> runs;
    for (const Brick& b : design_.bricks)
        if (brickIsSpecial(b.type)) runs.push_back(&b);
    dimRuns_->setRowCount(static_cast<int>(runs.size()));
    for (int r = 0; r < static_cast<int>(runs.size()); ++r) {
        const Brick& b = *runs[static_cast<size_t>(r)];
        const QString id = b.id;
        dimRuns_->setItem(r, 0, new QTableWidgetItem(b.name.isEmpty() ? brickTitle(b) : b.name));
        dimRuns_->setItem(r, 1, new QTableWidgetItem(QStringLiteral("%1 mm").arg(b.run.lengthMm, 0, 'f', 1)));
        int col = 2;
        for (int which = 0; which < 3; ++which, ++col) {
            auto* box = new QCheckBox(dimRuns_);
            box->setChecked(which == 0 ? b.run.dimTotal : which == 1 ? b.run.dimFans : b.run.dimBetween);
            connect(box, &QCheckBox::toggled, this, [this, id, which, later](bool on) {
                later([this, id, which, on] {
                    if (Brick* s = design_.brick(id)) (which == 0 ? s->run.dimTotal : which == 1 ? s->run.dimFans
                                                                                                 : s->run.dimBetween) = on;
                });
            });
            dimRuns_->setCellWidget(r, col, box);
        }
        auto* side = new QComboBox(dimRuns_);
        side->addItems({QStringLiteral("Above"), QStringLiteral("Below")});
        side->setCurrentIndex(b.run.dimTotalBelow && b.run.dimFansBelow && b.run.dimBetweenBelow ? 1 : 0);
        connect(side, &QComboBox::activated, this, [this, id, later](int i) {
            later([this, id, i] {
                if (Brick* s = design_.brick(id)) s->run.dimTotalBelow = s->run.dimFansBelow = s->run.dimBetweenBelow = i == 1;
            });
        });
        dimRuns_->setCellWidget(r, 5, side);
    }
    // BETWEEN TWO BRICKS: what each measures, as the drawing works it out.
    QHash<int, double> measured;
    for (const Entity& e : lastEnts_)
        if (e.meta.value(QStringLiteral("brickDim")).toString() == QLatin1String("manual"))
            measured.insert(e.meta.value(QStringLiteral("brickDimIndex")).toInt(), e.meta.value(QStringLiteral("brickMeasured")).toDouble());
    dimHand_->setRowCount(static_cast<int>(design_.dims.size()));
    for (int r = 0; r < static_cast<int>(design_.dims.size()); ++r) {
        const BrickDim& dm = design_.dims[static_cast<size_t>(r)];
        auto title = [this](const QString& id) {
            const Brick* b = design_.brick(id);
            return !b ? id : !b->name.isEmpty() ? b->name : brickTitle(*b);
        };
        dimHand_->setItem(r, 0, new QTableWidgetItem(title(dm.a)));
        dimHand_->setItem(r, 1, new QTableWidgetItem(title(dm.b)));
        dimHand_->setItem(r, 2, new QTableWidgetItem(measured.contains(r) ? QStringLiteral("%1 mm").arg(measured.value(r), 0, 'f', 1)
                                                                          : QStringLiteral("-")));
        auto* side = new QComboBox(dimHand_);
        side->addItems({QStringLiteral("Above"), QStringLiteral("Below")});
        side->setCurrentIndex(dm.below ? 1 : 0);
        connect(side, &QComboBox::activated, this, [this, r, later](int i) {
            later([this, r, i] {
                if (r < static_cast<int>(design_.dims.size())) design_.dims[static_cast<size_t>(r)].below = i == 1;
            });
        });
        dimHand_->setCellWidget(r, 3, side);
    }
}

void BrickWizardDialog::designChanged() {
    if (dimAllSide_) {
        const QSignalBlocker hush(dimAllSide_);
        dimAllSide_->setCurrentIndex(allDimensionsBelow(design_) ? 1 : 0);
    }
    // A harness opened, a new one, an undo: the tick as that design says.
    if (tieTurn_ && tieTurn_->isChecked() != design_.tieBrickTurn) {
        const QSignalBlocker hush(tieTurn_);
        tieTurn_->setChecked(design_.tieBrickTurn);
    }
    canvas_->update();
    loadPanel();
    refreshPreview();
}

void BrickWizardDialog::loadPanel() {
    const auto link = canvas_->selectedLink();
    if (!link.first.isEmpty()) {
        const Brick* a = design_.brick(link.first);
        const Brick* b = design_.brick(link.second);
        const Brick* sp = a && brickIsSpecial(a->type) ? a : b;
        const Brick* en = sp == a ? b : a;
        const bool splits = sp && (sp->type == BrickType::Fanout ||
                                   (sp->type == BrickType::Run && design_.finishesOf(sp->id).size() > 1));
        if (sp && en && splits && design_.portOn(sp->id, en->id) == kPortRight) {
            panel_->setCurrentIndex(3);
            const QSignalBlocker block(linkWires_);
            linkText_->setText(QStringLiteral("%1 to %2.").arg(brickTitle(*sp), brickTitle(*en)));
            linkWires_->setMaximum(sp->run.wires);
            linkWires_->setValue(design_.link(link.first, link.second)->wires);
            return;
        }
        noneText_->setText(QStringLiteral("A link. Delete removes it."));
        panel_->setCurrentIndex(0);
        return;
    }
    const QStringList sel = canvas_->selection();
    if (sel.size() > 1) {
        noneText_->setText(QStringLiteral("%1 bricks picked. Move them with the arrows, drag them, or "
                                          "turn them with Rotate. %2 is shown in the preview.")
                               .arg(sel.size())
                               .arg(design_.brick(sel.back()) ? brickTitle(*design_.brick(sel.back()))
                                                              : QString()));
        panel_->setCurrentIndex(0);
        return;
    }
    const Brick* b = design_.brick(canvas_->selected());
    if (!b) {
        noneText_->setText(QStringLiteral("Pick a brick to set what it is."));
        panel_->setCurrentIndex(0);
        return;
    }
    if (b->type == BrickType::End) {
        panel_->setCurrentIndex(1);
        const QSignalBlocker b1(part_), b2(name_), b3(balloon_), b4(showDesignator_), b5(designatorHeight_);
        showDesignator_->setChecked(b->showDesignator);
        designatorHeight_->setValue(b->designatorHeightMm);
        designatorHeight_->setEnabled(b->showDesignator);
        endKind_->setText(partKindName(b->kind) +
                          (brickKindIsJoint(b->kind) ? QStringLiteral(" (a joint: a face each side)")
                                                     : QString()));
        part_->clear();
        part_->addItem(QStringLiteral("(none yet)"), QString());
        for (const LibraryPart* p : catalog_.ofKind(b->kind)) {
            part_->addItem(p->title(), p->id);
            part_->setItemData(part_->count() - 1, p->title(), Qt::ToolTipRole);
        }
        const int at = part_->findData(b->libPart);
        part_->setCurrentIndex(at >= 0 ? at : 0);
        if (name_->text() != b->name) name_->setText(b->name);
        balloon_->setChecked(b->balloon);
        if (partScale_) {
            const QSignalBlocker a(partScale_), c(partScaleAuto_);
            partScaleAuto_->setChecked(b->autoScale);
            // On Auto, the figure the drawing came to; otherwise the brick's own.
            const double k = b->autoScale ? drawnScales_.value(b->id, b->scale) : b->scale;
            partScale_->setValue(k * 100.0);
        }
        // THE PART'S OWN FIGURES, straight out of the library.
        params_->setRowCount(0);
        const LibraryPart* p = b->libPart.isEmpty() ? nullptr : catalog_.byId(b->libPart);
        auto row = [this](const QString& k, const QString& v) {
            if (v.isEmpty()) return;
            const int r = params_->rowCount();
            params_->insertRow(r);
            params_->setItem(r, 0, new QTableWidgetItem(k));
            params_->setItem(r, 1, new QTableWidgetItem(v));
        };
        if (p) {
            row(QStringLiteral("Mfr P/N"), p->mfrPn);
            row(QStringLiteral("Manufacturer"), p->mfrName);
            row(QStringLiteral("Internal P/N"), p->internalPn);
            row(QStringLiteral("Description"), p->description);
            for (auto it = p->params.begin(); it != p->params.end(); ++it) row(it.key(), it.value());
        }
        // THE BILL: marked when it is already on it.
        const int item = p ? bomItemOf(p->id) : 0;
        bomAdd_->setEnabled(p != nullptr && document() != nullptr);
        checkCompatibility(b);
        {
            const QSignalBlocker hush(partDimSide_);
            partDimSide_->setCurrentIndex(b->dimBelow ? 1 : 0);
        }
        bomState_->setText(!p      ? QStringLiteral("Choose a library part first")
                           : item  ? QStringLiteral("✔ In the BOM as item %1").arg(item)
                                   : QStringLiteral("⚠ Not in the BOM"));
        // Not on the bill: said in red, so it is not missed.
        bomState_->setStyleSheet(p && !item ? QStringLiteral("color:#ff5b5b;font-weight:bold;") : QString());
        return;
    }
    panel_->setCurrentIndex(2);
    const QSignalBlocker s1(wires_), s2(length_), s3(wireWidth_), s4(fanStart_), s5(fanFinish_),
        s6(knee_), s7(fanKnee_), s8(fanScale_), s9(transitions_), s10(bodyScale_), s11(bodyWidth_),
        s12(dimTotal_), s13(dimFans_), s14(dimBetween_), s15(transitionWidth_);
    const BrickRunSpec& r = b->run;
    runKind_->setText(brickTypeName(b->type));
    wires_->setValue(r.wires);
    if (pairs_) {
        const QSignalBlocker hush(pairs_);
        pairs_->setValue(std::max(1, r.pairs));
        if (runForm_) runForm_->setRowVisible(pairs_, b->type == BrickType::TwistedPair);
    }
    {
        // Bridge wires: only for a special joined to another one, and then they decide the wire count.
        // Only on a BRIDGE WIRE: a cable's own panel has no such thing.
        const bool bridge = brickIsBridge(*b);
        if (runForm_) runForm_->setRowVisible(bridge_->parentWidget(), bridge);
        if (bridge) runKind_->setText(QStringLiteral("Bridge wire"));
        const Brick* from = design_.brick(joinedSpecial(design_, b->id));
        const QSignalBlocker hush(bridge_);
        bridge_->clear();
        if (from) {
            // The cable's wires, by net where they have one - and its shield, when it has a braid.
            for (int w = 1; w <= from->run.wires; ++w) {
                const QString net = from->run.nets.value(w - 1);
                bridge_->addItem(net.isEmpty() ? QStringLiteral("Wire %1").arg(w)
                                               : QStringLiteral("Wire %1 - %2").arg(w).arg(net),
                                 QString::number(w));
            }
            if (from->run.wires > 1)
                bridge_->addItem(QStringLiteral("All %1 wires").arg(from->run.wires),
                                 QStringLiteral("1-%1").arg(from->run.wires));
            if (from->run.stack.contains(QStringLiteral("braid")))
                bridge_->addItem(QStringLiteral("Shield (braid)"), QString::fromLatin1(kBridgeShield));
        }
        const int at = bridge_->findData(r.bridgeWires.trimmed());
        if (at >= 0) bridge_->setCurrentIndex(at);
        else bridge_->setEditText(r.bridgeWires);
        bridge_->setEnabled(from != nullptr);
        const bool shield = r.bridgeWires.trimmed() == QLatin1String(kBridgeShield);
        const QList<int> take = from && !shield ? parseWireList(r.bridgeWires, from->run.wires) : QList<int>();
        bridgeFrom_->setText(!from     ? QStringLiteral("link it to a cable or fanout to pick its wire")
                             : shield ? QStringLiteral("the shield of %1").arg(brickTitle(*from))
                             : take.isEmpty()
                                 ? QStringLiteral("from %1 (%2 wires)").arg(brickTitle(*from)).arg(from->run.wires)
                                 : QStringLiteral("%1 of %2's %3 wires").arg(take.size()).arg(brickTitle(*from))
                                       .arg(from->run.wires));
    }
    wires_->setEnabled(b->type != BrickType::TwistedPair && (r.bridgeWires.trimmed().isEmpty() ||
                                                            !design_.brick(joinedSpecial(design_, b->id))));
    trunk_->setEnabled(b->type == BrickType::Fanout || b->type == BrickType::Run);
    trunk_->setMaximum(std::max(1, r.wires));
    length_->setValue(r.lengthMm);
    wireWidth_->setValue(r.wireWidthMm);
    // Each Auto figure: "A" lit when left alone, the field its actual size then (showActuals).
    for (auto [spinBox, value, isAuto] : {std::tuple<QDoubleSpinBox*, double, bool>{fanStart_, r.fanStartMm, r.fanStartMm < 0.0},
                                          {fanFinish_, r.fanFinishMm, r.fanFinishMm < 0.0},
                                          {knee_, r.kneeRadiusMm, r.kneeRadiusMm < 0.0},
                                          {fanKnee_, r.fanKneeMm, r.fanKneeMm < 0.0},
                                          {bodyWidth_, r.bodyWidthMm, r.bodyWidthMm <= 1e-9},
                                          {transitionWidth_, r.transitionWidthMm, r.transitionWidthMm <= 1e-9},
                                          {trunk_, double(r.trunkWires), r.trunkWires <= 0}}) {
        QToolButton* a = autoOf_.value(spinBox);
        const QSignalBlocker ba(a);
        a->setChecked(isAuto);
        if (!isAuto) spinBox->setValue(value);
    }
    for (auto [on, spinBox, value] : {std::tuple<QToolButton*, QDoubleSpinBox*, double>{fanStartOn_, fanStart_, r.fanStartMm},
                                      {fanFinishOn_, fanFinish_, r.fanFinishMm}}) {
        const bool fan = value != 0.0;
        const QSignalBlocker bo(on);
        on->setChecked(fan);
        spinBox->setEnabled(fan);
        autoOf_.value(spinBox)->setEnabled(fan);
    }
    {
        const QSignalBlocker d1(dimLinear_), d2(dimTrace_), d3(dimSingle_);
        dimLinear_->setChecked(r.dimLinear);
        dimTrace_->setChecked(r.dimTrace);
        dimSingle_->setChecked(r.dimSingle);
        dimTrace_->setVisible(!r.dimLinear);
    }
    fanScale_->setValue(r.fanScalePct);
    bodyScale_->setValue(r.bodyScalePct);
    transitions_->setCurrentIndex(r.transitions == RunEnds::Start    ? 1
                                  : r.transitions == RunEnds::Finish ? 2
                                  : r.transitions == RunEnds::Both   ? 3
                                                                     : 0);
    transitionWidth_->setEnabled(r.transitions != RunEnds::None);
    autoOf_.value(transitionWidth_)->setEnabled(r.transitions != RunEnds::None);
    dimTotal_->setChecked(r.dimTotal);
    for (auto [sb, below] : {std::pair<QComboBox*, bool>{dimSideTotal_, r.dimTotalBelow}, {dimSideFans_, r.dimFansBelow},
                             {dimSideBetween_, r.dimBetweenBelow}}) {
        const QSignalBlocker hush(sb);
        sb->setCurrentIndex(below ? 1 : 0);
    }
    dimFans_->setChecked(r.dimFans);
    dimBetween_->setChecked(r.dimBetween);
    for (auto [e, t] : {std::pair<QLineEdit*, QString>{dimTextTotal_, r.dimTextTotal}, {dimTextFans_, r.dimTextFans},
                        {dimTextBetween_, r.dimTextBetween}}) {
        const QSignalBlocker block(e);
        if (e->text() != t) e->setText(t);
    }
    const int keep = stack_->currentRow();
    stack_->clear();
    for (int i = 0; i < r.stack.size(); ++i) {
        // With its part number, where it has one.
        QString text = layerName(r.stack[i]);
        if (const LibraryPart* lp = catalog_.byId(r.stackParts.value(r.stack[i])))
            text += QStringLiteral("  -  ") + (lp->mfrPn.isEmpty() ? lp->title() : lp->mfrPn);
        auto* it = new QListWidgetItem(text, stack_);
        if (i == r.stack.size() - 1) {
            QFont f = it->font();
            f.setBold(true);
            it->setFont(f);
            it->setText(it->text() + QStringLiteral("   (seen)"));
        }
    }
    stack_->setCurrentRow(std::clamp(keep, 0, stack_->count() - 1));
    loadLayerPart();
    loadLabels();
    loadNets();
    showActuals();
}

void BrickWizardDialog::showActuals() {
    const Brick* b = design_.brick(canvas_->selected());
    if (!b || !brickIsSpecial(b->type) || !actual_.contains(b->id)) return;
    const BrickActual a = actual_.value(b->id);
    // The transition cannot be turned down below the cable it goes over: the field stops there.
    {
        const QSignalBlocker block(transitionWidth_);
        transitionWidth_->setMinimum(std::max(0.0, a.bodyWidth));
        if (!autoOf_.value(transitionWidth_)->isChecked())
            transitionWidth_->setValue(std::max(transitionWidth_->value(), a.bodyWidth));
    }
    for (auto [spinBox, value] : {std::pair<QDoubleSpinBox*, double>{fanStart_, a.fanStart},
                                  {transitionWidth_, a.transitionWidth},
                                  {fanFinish_, a.fanFinish},
                                  {knee_, a.knee},
                                  {fanKnee_, a.fanKnee},
                                  {bodyWidth_, a.bodyWidth},
                                  {trunk_, a.trunk}}) {
        QToolButton* btn = autoOf_.value(spinBox);
        if (!btn || !btn->isChecked()) continue;
        const QSignalBlocker block(spinBox);
        spinBox->setValue(value);
    }
}

void BrickWizardDialog::loadLabels() {
    const Brick* b = design_.brick(canvas_->selected());
    const QSignalBlocker block(labels_);
    labels_->setRowCount(0);
    if (!b || !brickIsSpecial(b->type)) return;
    const QString id = b->id;
    for (int i = 0; i < static_cast<int>(b->run.labels.size()); ++i) {
        const BrickLabel& l = b->run.labels[i];
        labels_->insertRow(i);
        auto* on = new QComboBox(labels_);
        on->addItem(QStringLiteral("Whole harness"), -1);
        for (int w = 0; w < b->run.wires; ++w) on->addItem(QStringLiteral("Wire %1").arg(w + 1), w);
        on->setCurrentIndex(std::max(0, on->findData(l.wire)));
        auto* from = new QComboBox(labels_);
        from->addItems({QStringLiteral("Start"), QStringLiteral("Finish")});
        from->setCurrentIndex(l.fromFinish ? 1 : 0);
        auto* dist = new QDoubleSpinBox(labels_);
        dist->setRange(0.0, 100000.0);
        dist->setDecimals(1);
        dist->setSuffix(QStringLiteral(" mm"));
        dist->setValue(l.distanceMm);
        auto* len = new QDoubleSpinBox(labels_);
        len->setRange(0.0, 1000.0);
        len->setDecimals(1);
        len->setSuffix(QStringLiteral(" mm"));
        len->setSpecialValueText(QStringLiteral("Auto"));
        len->setToolTip(QStringLiteral("How long the label is - kept off a bend either way"));
        len->setValue(l.lengthMm);
        auto* shown = new QCheckBox(labels_);
        shown->setToolTip(QStringLiteral("Call the label's text out with a leader on the drawing"));
        shown->setChecked(l.leader);
        auto* shrink = new QCheckBox(labels_);
        shrink->setToolTip(QStringLiteral("A clear heatshrink over the label"));
        shrink->setChecked(l.clearShrink);
        auto* text = new QLineEdit(l.text, labels_);
        text->setPlaceholderText(QStringLiteral("the wire's net"));
        labels_->setCellWidget(i, 0, on);
        labels_->setCellWidget(i, 1, from);
        labels_->setCellWidget(i, 2, dist);
        labels_->setCellWidget(i, 3, len);
        labels_->setCellWidget(i, 4, shown);
        labels_->setCellWidget(i, 5, shrink);
        labels_->setCellWidget(i, 6, text);
        auto write = [this, id, i, on, from, dist, len, shown, shrink, text] {
            Brick* br = design_.brick(id);
            if (!br || i >= static_cast<int>(br->run.labels.size())) return;
            BrickLabel& lab = br->run.labels[i];
            lab.wire = on->currentData().toInt();
            lab.fromFinish = from->currentIndex() == 1;
            lab.distanceMm = dist->value();
            lab.text = text->text();
            lab.lengthMm = len->value();
            lab.leader = shown->isChecked();
            lab.clearShrink = shrink->isChecked();
            refreshPreview();
        };
        connect(on, &QComboBox::currentIndexChanged, this, write);
        connect(from, &QComboBox::currentIndexChanged, this, write);
        connect(dist, &QDoubleSpinBox::valueChanged, this, write);
        connect(text, &QLineEdit::textEdited, this, write);
        connect(len, &QDoubleSpinBox::valueChanged, this, write);
        connect(shown, &QCheckBox::toggled, this, write);
        connect(shrink, &QCheckBox::toggled, this, write);
    }
}

void BrickWizardDialog::writeEnd() {
    Brick* b = design_.brick(canvas_->selected());
    if (!b || b->type != BrickType::End) return;
    const QString chosen = part_->currentData().toString();
    const bool newPart = chosen != b->libPart;
    b->libPart = chosen;
    // A PART WHOSE SIZE IS LEFT TO THE DRAWING comes in on Auto: made the drawing's usual entry size.
    if (newPart && partScaleAuto_) {
        const LibraryPart* p = chosen.isEmpty() ? nullptr : catalog_.byId(chosen);
        if (p && partSizeMode(*p) == PartSizeMode::Auto) {
            const QSignalBlocker hush(partScaleAuto_);
            partScaleAuto_->setChecked(true);
        }
    }
    b->name = name_->text();
    b->balloon = balloon_->isChecked();
    b->showDesignator = showDesignator_->isChecked();
    b->designatorHeightMm = designatorHeight_->value();
    if (partScale_) {
        b->autoScale = partScaleAuto_->isChecked();
        if (!b->autoScale) b->scale = std::clamp(partScale_->value() / 100.0, 0.1, 10.0);
    }
    canvas_->update();
    refreshPreview();
    // On Auto, the figure it came to.
    if (partScale_ && b->autoScale) {
        const QSignalBlocker hush(partScale_);
        partScale_->setValue(drawnScales_.value(b->id, b->scale) * 100.0);
    }
}

bool BrickWizardDialog::scalePartToMatch(const QString& id, const QString& other) {
    Brick* b = design_.brick(id);
    if (!b || b->type != BrickType::End || id == other) return false;
    if (lastFaces_.isEmpty()) refreshPreview();
    // The two faces nearest each other: this part's, and the other's.
    const BrickFace* mine = nullptr;
    const BrickFace* theirs = nullptr;
    double best = 1e300;
    for (auto a = lastFaces_.cbegin(); a != lastFaces_.cend(); ++a) {
        if (!a.key().startsWith(id + QLatin1Char(':'))) continue;
        for (auto o = lastFaces_.cbegin(); o != lastFaces_.cend(); ++o) {
            if (!o.key().startsWith(other + QLatin1Char(':'))) continue;
            const double d = distance(a.value().centre, o.value().centre);
            if (d < best) {
                best = d;
                mine = &a.value();
                theirs = &o.value();
            }
        }
    }
    if (!mine || !theirs || mine->lengthMm <= 1e-6 || theirs->lengthMm <= 1e-6) return false;
    const double now = b->autoScale ? drawnScales_.value(b->id, b->scale) : b->scale;
    b->autoScale = false;
    // A FERRULE (or a crimp terminal, a contact, a terminal) MATCHED TO ANY END PART is half ITS BODY in size - as
    // big across as half the other part is, the way it is drawn: it goes on one wire, not the whole of the part.
    // Its entry face is no measure of that: a circular connector's is its small rear thread.
    const Brick* ob = design_.brick(other);
    const bool small = (b->kind == PartKind::Ferrule || b->kind == PartKind::Crimp || b->kind == PartKind::ContactPin ||
                        b->kind == PartKind::Terminal) &&
                       ob && ob->type == BrickType::End;
    if (small) {
        // How big each is across the way its wires come in, as drawn: its outline, not its dimensions or its words.
        auto across = [this](const QString& brickId, const Vec2& out) {
            Bounds box;
            for (const Entity& e : lastEnts_) {
                if (e.meta.value(QStringLiteral("brickId")).toString() != brickId) continue;
                const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
                if (role == QLatin1String("dim") || role == QLatin1String("designator") || role == QLatin1String("label") ||
                    e.type == EntityType::Text || e.type == EntityType::Dimension || e.type == EntityType::Wipeout ||
                    e.meta.value(QStringLiteral("balloon")).toBool())
                    continue;
                box.expand(e.bounds());
            }
            if (!box.valid) return 0.0;
            return std::abs(out.x) * box.height() + std::abs(out.y) * box.width();
        };
        const double mineAcross = across(id, mine->out), theirsAcross = across(other, theirs->out);
        if (mineAcross > 1e-6 && theirsAcross > 1e-6) {
            b->scale = std::clamp(now * theirsAcross * 0.5 / mineAcross, 0.05, 10.0);
            status_->setText(QStringLiteral("Scaled to %1 % - half the size of %2 (%3 mm across)")
                                 .arg(qRound(b->scale * 100.0))
                                 .arg(brickTitle(*ob))
                                 .arg(theirsAcross * 0.5, 0, 'f', 1));
            return true;
        }
    }
    b->scale = std::clamp(now * theirs->lengthMm / mine->lengthMm, 0.1, 10.0);
    status_->setText(QStringLiteral("Scaled to %1 % - its entry as long as the other's (%2 mm)")
                         .arg(qRound(b->scale * 100.0))
                         .arg(theirs->lengthMm, 0, 'f', 1));
    return true;
}

// CTRL + THE WHEEL OVER A PART IN THE PREVIEW scales that part - and the wires meet it where its faces now
// are. The wheel on its own still zooms the preview.
int BrickWizardDialog::bomGapAt(const QPoint& at) const {
    const int rows = bom_->rowCount();
    const int r = bom_->rowAt(at.y());
    if (r < 0) return at.y() < 0 ? 0 : rows;
    const int top = bom_->rowViewportPosition(r);
    return at.y() < top + bom_->rowHeight(r) / 2 ? r : r + 1;
}

bool BrickWizardDialog::moveBomRow(int from, int gap) {
    Document* doc = document();
    const int shown = static_cast<int>(bomShown_.size());
    if (!doc || from < 0 || from >= shown || gap < 0 || gap > shown) return false;
    const int to = gap > from ? gap - 1 : gap;
    if (to == from) return false;
    BomTable table;
    table.fromJson(doc->bom());
    // Only a line of the bill itself can be moved; one there because its part is on the drawing follows it.
    bool ours = false;
    for (const BomRow& r : table.rows) ours = ours || bomSameThing(r, bomShown_[static_cast<size_t>(from)]);
    if (!ours) {
        status_->setText(QStringLiteral("That line is there because its part is on the drawing - its place follows it"));
        return false;
    }
    std::vector<BomRow> order = bomShown_;
    const BomRow moved = order[static_cast<size_t>(from)];
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, moved);
    auto rank = [&order](const BomRow& r) {
        for (size_t i = 0; i < order.size(); ++i)
            if (bomSameThing(order[i], r)) return static_cast<int>(i);
        return std::numeric_limits<int>::max();
    };
    std::stable_sort(table.rows.begin(), table.rows.end(),
                     [&rank](const BomRow& x, const BomRow& y) { return rank(x) < rank(y); });
    doc->bom() = table.toJson();
    doc->setDirty(true);
    emit bomChanged();
    refreshBom();
    refreshPreview();   // the balloons, renumbered
    if (to < bom_->rowCount()) bom_->selectRow(to);
    status_->setText(QStringLiteral("Item %1 is now item %2 - the balloons follow").arg(from + 1).arg(to + 1));
    return true;
}

bool BrickWizardDialog::eventFilter(QObject* o, QEvent* e) {
    // A PART DRAGGED ON THE DRAWING: pressed on an end part in the preview and moved, its run's length follows -
    // along, and across (farther off, more cable) - and it goes up or down on the drawing with it.
    if (o == preview_) {
        if (e->type() == QEvent::Resize || e->type() == QEvent::Show) placeEnlargeButton();
        // The hand over a part's grab point, so it is seen it can be taken hold of.
        if (e->type() == QEvent::MouseMove && partDrag_.isEmpty() &&
            !(static_cast<QMouseEvent*>(e)->buttons() & Qt::LeftButton)) {
            const QString over = previewPartAt(static_cast<QMouseEvent*>(e)->position());
            preview_->setCursor(over.isEmpty() ? Qt::ArrowCursor : Qt::OpenHandCursor);
            if (over != hoverPart_) {
                hoverPart_ = over;
                showGrabRings();
            }
        }
        // CTRL ON AN END OF A RUN - a cable's, a heatshrink's, a fanout's, loose or on a part: dragged, the run is
        // shortened or lengthened, its length following the drag along the way that end points.
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton && (me->modifiers() & Qt::ControlModifier)) {
                const Vec2 at = preview_->worldAt(me->position());
                const double reach = std::max(1.5, handleMm_ * 1.5);
                double best = reach;
                for (const Entity& en : lastEnts_) {
                    const Brick* s = design_.brick(en.meta.value(QStringLiteral("brickId")).toString());
                    if (!s || !brickIsSpecial(s->type) || en.points.size() < 2) continue;
                    if (en.type != EntityType::TwistedPair &&
                        !(en.type == EntityType::Polyline && en.meta.value(QStringLiteral("double")).toBool()))
                        continue;
                    const size_t n = en.points.size();
                    for (bool last : {false, true}) {
                        const Vec2 end = last ? en.points[n - 1] : en.points[0];
                        const double dd = distance(at, end);
                        if (dd >= best) continue;
                        best = dd;
                        runDrag_ = s->id;
                        runDragDir_ = (last ? en.points[n - 1] - en.points[n - 2] : en.points[0] - en.points[1]).normalized();
                        runDragLen0_ = s->run.lengthMm;
                        runDragFrom_ = at;
                    }
                }
                if (!runDrag_.isEmpty()) {
                    selectBrick(runDrag_);
                    return true;
                }
            }
        } else if (e->type() == QEvent::MouseMove && !runDrag_.isEmpty()) {
            const Vec2 now = preview_->worldAt(static_cast<QMouseEvent*>(e)->position());
            if (Brick* s = design_.brick(runDrag_)) {
                s->run.lengthMm = std::max(0.0, std::round(runDragLen0_ + dot(now - runDragFrom_, runDragDir_)));
                canvas_->update();
                refreshPreview();
                status_->setText(QStringLiteral("%1: %2 mm").arg(brickTitle(*s)).arg(s->run.lengthMm, 0, 'f', 0));
            }
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease && !runDrag_.isEmpty()) {
            runDrag_.clear();
            recordHistory();
            loadPanel();
            return true;
        }
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton && me->modifiers() == Qt::NoModifier) {
                const QString hit = previewPartAt(me->position());
                if (!hit.isEmpty()) {
                    partDrag_ = hit;
                    partDragFrom_ = preview_->worldAt(me->position());
                    partDragDesign_ = design_;
                    partDragBuilt_.faces = lastFaces_;
                    selectBrick(hit);
                    return true;
                }
            }
        } else if (e->type() == QEvent::MouseMove && !partDrag_.isEmpty()) {
            auto* me = static_cast<QMouseEvent*>(e);
            const Vec2 now = preview_->worldAt(me->position());
            // From where it started, each time: the design as it was, moved by the whole drag so far.
            const QString keep = partDrag_;
            design_ = partDragDesign_;
            design_.catalog = &catalog_;
            const Vec2 d = now - partDragFrom_;
            const double dx = std::round(d.x), dy = std::round(d.y);
            const QString sid = dragPartOnDrawing(design_, partDragBuilt_, keep, dx, dy);
            canvas_->update();
            refreshPreview();
            if (const Brick* s = design_.brick(sid))
                status_->setText(QStringLiteral("%1: %2 mm").arg(brickTitle(*s)).arg(s->run.lengthMm, 0, 'f', 0));
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease && !partDrag_.isEmpty()) {
            partDrag_.clear();
            recordHistory();
            loadPanel();
            return true;
        }
    }
    if (bom_ && o == bom_->viewport()) {
        switch (e->type()) {
            case QEvent::MouseButtonPress: {
                auto* me = static_cast<QMouseEvent*>(e);
                if (me->button() == Qt::LeftButton) {
                    bomDragFrom_ = bom_->rowAt(me->position().toPoint().y());
                    bomPressAt_ = me->position().toPoint();
                    bomDragging_ = false;
                }
                break;
            }
            case QEvent::MouseMove: {
                auto* me = static_cast<QMouseEvent*>(e);
                if (bomDragFrom_ < 0 || !(me->buttons() & Qt::LeftButton)) break;
                const QPoint at = me->position().toPoint();
                if (!bomDragging_ && (at - bomPressAt_).manhattanLength() < QApplication::startDragDistance()) break;
                bomDragging_ = true;
                const int gap = bomGapAt(at);
                const int rows = bom_->rowCount();
                const int y = gap < rows ? bom_->rowViewportPosition(gap)
                                         : (rows > 0 ? bom_->rowViewportPosition(rows - 1) + bom_->rowHeight(rows - 1) : 0);
                bomDropLine_->setGeometry(0, std::max(0, y - 1), bom_->viewport()->width(), 2);
                bomDropLine_->show();
                bomDropLine_->raise();
                return true;
            }
            case QEvent::MouseButtonRelease: {
                auto* me = static_cast<QMouseEvent*>(e);
                const bool was = bomDragging_;
                const int from = bomDragFrom_;
                bomDragging_ = false;
                bomDragFrom_ = -1;
                bomDropLine_->hide();
                if (was && me->button() == Qt::LeftButton) {
                    moveBomRow(from, bomGapAt(me->position().toPoint()));
                    return true;
                }
                break;
            }
            default: break;
        }
    }
    if (o == preview_ && e->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(e);
        if (we->modifiers() & Qt::ControlModifier) {
            const Vec2 at = preview_->worldAt(we->position());
            QString hit;
            double best = 1e300;
            for (const Entity& en : preview_->entities()) {
                const QString id = en.meta.value(QStringLiteral("brickId")).toString();
                const Brick* b = design_.brick(id);
                if (!b || b->type != BrickType::End) continue;
                const Bounds bb = en.bounds();
                if (!bb.valid || at.x < bb.min.x || at.x > bb.max.x || at.y < bb.min.y || at.y > bb.max.y) continue;
                const double area = std::max(1e-9, bb.width() * bb.height());
                if (area < best) {
                    best = area;
                    hit = id;
                }
            }
            if (Brick* b = design_.brick(hit)) {
                const double steps = we->angleDelta().y() / 120.0;
                const double from = b->autoScale ? drawnScales_.value(b->id, b->scale) : b->scale;
                b->autoScale = false;
                b->scale = std::clamp(from * std::pow(1.1, steps), 0.1, 10.0);
                canvas_->setSelected(b->id);
                loadPanel();
                refreshPreview();
                status_->setText(QStringLiteral("%1 drawn at %2 %").arg(b->name.isEmpty() ? partKindName(b->kind) : b->name)
                                     .arg(qRound(b->scale * 100.0)));
                return true;
            }
        }
    }
    return QDialog::eventFilter(o, e);
}

void BrickWizardDialog::writeRun() {
    Brick* b = design_.brick(canvas_->selected());
    if (!b || !brickIsSpecial(b->type)) return;
    BrickRunSpec& r = b->run;
    if (b->type == BrickType::TwistedPair && pairs_) r.pairs = pairs_->value();
    r.wires = b->type == BrickType::TwistedPair ? 2 * std::max(1, r.pairs) : wires_->value();
    const double oldLength = r.lengthMm;
    r.lengthMm = length_->value();
    // THE LENGTH CHANGED: the part at its finish, and all beyond it, pulled in or pushed out to suit.
    (void)oldLength;   // the drawing follows the length; the pane is left as it is
    if (bridge_ && brickIsBridge(*b)) {
        const QString said = bridgeValue();
        if (!said.isEmpty()) r.bridgeWires = said;   // a bridge wire is always one: never emptied into a cable
        if (syncBridgeWires(design_) > 0) {
            const QSignalBlocker hush(wires_);
            wires_->setValue(r.wires);
        }
    }
    r.wireWidthMm = wireWidth_->value();
    auto manual = [this](QDoubleSpinBox* s) { return !autoOf_.value(s)->isChecked(); };
    r.fanStartMm = !fanStartOn_->isChecked() ? 0.0 : manual(fanStart_) ? fanStart_->value() : -1.0;
    r.fanFinishMm = !fanFinishOn_->isChecked() ? 0.0 : manual(fanFinish_) ? fanFinish_->value() : -1.0;
    r.kneeRadiusMm = manual(knee_) ? knee_->value() : -1.0;
    r.fanKneeMm = manual(fanKnee_) ? fanKnee_->value() : -1.0;
    r.fanScalePct = fanScale_->value();
    r.bodyScalePct = bodyScale_->value();
    r.bodyWidthMm = manual(bodyWidth_) ? bodyWidth_->value() : 0.0;
    r.transitionWidthMm =
        manual(transitionWidth_)
            ? std::max(transitionWidth_->value(), actual_.contains(b->id) ? actual_.value(b->id).bodyWidth : 0.0)
            : 0.0;
    r.trunkWires = manual(trunk_) ? static_cast<int>(std::lround(trunk_->value())) : 0;
    r.dimLinear = dimLinear_->isChecked();
    r.dimTrace = dimTrace_->isChecked();
    r.dimSingle = dimSingle_->isChecked();
    r.transitions = transitions_->currentIndex() == 1   ? RunEnds::Start
                    : transitions_->currentIndex() == 2 ? RunEnds::Finish
                    : transitions_->currentIndex() == 3 ? RunEnds::Both
                                                        : RunEnds::None;
    r.dimTotal = dimTotal_->isChecked();
    r.dimTotalBelow = dimSideTotal_->currentIndex() == 1;
    r.dimFansBelow = dimSideFans_->currentIndex() == 1;
    r.dimBetweenBelow = dimSideBetween_->currentIndex() == 1;
    r.dimFans = dimFans_->isChecked();
    r.dimBetween = dimBetween_->isChecked();
    r.dimTextTotal = dimTextTotal_->text().trimmed();
    r.dimTextFans = dimTextFans_->text().trimmed();
    r.dimTextBetween = dimTextBetween_->text().trimmed();
    canvas_->update();
    refreshPreview();
}

void BrickWizardDialog::loadLayerPart() {
    if (!layerPart_) return;
    const QSignalBlocker hush(layerPart_);
    layerPart_->clear();
    const Brick* b = design_.brick(canvas_->selected());
    const int row = stack_ ? stack_->currentRow() : -1;
    if (!b || !brickIsSpecial(b->type) || row < 0 || row >= b->run.stack.size()) {
        layerPart_->setEnabled(false);
        return;
    }
    const QString layer = b->run.stack[row];
    const PartKind kind = layer == QLatin1String("cable")        ? PartKind::Cable
                          : layer == QLatin1String("braid")      ? PartKind::ShieldBraid
                          : layer == QLatin1String("heatshrink") ? PartKind::Heatshrink
                                                                 : PartKind::Wire;
    layerPart_->addItem(QStringLiteral("(none)"), QString());
    for (const LibraryPart* p : catalog_.merged().ofKind(kind))
        layerPart_->addItem(p->mfrPn.isEmpty() ? p->title() : p->mfrPn + QStringLiteral(" - ") + p->title(), p->id);
    const int at = layerPart_->findData(b->run.stackParts.value(layer));
    layerPart_->setCurrentIndex(at >= 0 ? at : 0);
    layerPart_->setEnabled(true);
}

int BrickWizardDialog::layerPartsFromNeighbours(const QString& special) {
    Brick* s = design_.brick(special);
    if (!s || !brickIsSpecial(s->type)) return 0;
    // The specials next along: joined to it, or on the other side of a part it is linked to.
    QStringList near;
    for (const QString& o : design_.linksOf(special)) {
        const Brick* ob = design_.brick(o);
        if (!ob) continue;
        if (brickIsSpecial(ob->type)) near << o;
        else
            for (const QString& o2 : design_.linksOf(o))
                if (o2 != special)
                    if (const Brick* b2 = design_.brick(o2); b2 && brickIsSpecial(b2->type)) near << o2;
    }
    int given = 0;
    for (const QString& layer : s->run.stack) {
        if (!s->run.stackParts.value(layer).isEmpty()) continue;
        for (const QString& n : near)
            if (const Brick* nb = design_.brick(n); nb && !nb->run.stackParts.value(layer).isEmpty()) {
                s->run.stackParts.insert(layer, nb->run.stackParts.value(layer));
                ++given;
                break;
            }
    }
    return given;
}

void BrickWizardDialog::checkCompatibility(const Brick* b) {
    if (!compat_) return;
    QString said;
    bool canAdd = false;
    const LibraryPart* p = b && !b->libPart.isEmpty() ? catalog_.byId(b->libPart) : nullptr;
    if (p && p->kind == PartKind::Connector && !partIncludesPins(*p) && !partIsWired(*p)) {
        // Bought without its contacts: some contact has to be on the bill for it.
        const PartsLibrary merged = catalog_.merged();
        const QStringList tied = tiedContactsOf(merged, p->id);
        bool onBill = false;
        for (const QString& id : tied) onBill = onBill || bomItemOf(id) > 0;
        if (!onBill) {
            QStringList names;
            for (const QString& id : tied)
                if (const LibraryPart* t = merged.byId(id)) names << (t->mfrPn.isEmpty() ? t->title() : t->mfrPn);
            said = tied.isEmpty()
                       ? QStringLiteral("This connector needs contacts - none are tied to it; tie them in the parts library")
                       : QStringLiteral("This connector needs contacts - tied: %1").arg(names.join(QStringLiteral(", ")));
            canAdd = !tied.isEmpty();
        }
    }
    compat_->setText(said.isEmpty() ? QString() : QStringLiteral("⚠ ") + said);
    compat_->setVisible(!said.isEmpty());
    compatAdd_->setVisible(canAdd);
}

void BrickWizardDialog::showGrabRings() {
    // THE GRAB POINT, on the preview only and only where it is wanted: the part picked, and the part the pointer is
    // over - a ring on every part all the time read as connection points drawn on the parts.
    std::vector<Entity> ents = lastEnts_;
    QStringList want = canvas_->selection();
    if (!hoverPart_.isEmpty() && !want.contains(hoverPart_)) want << hoverPart_;
    for (const QString& id : want) {
        const Brick* b = design_.brick(id);
        if (!b || b->type != BrickType::End) continue;
        const auto f = lastFaces_.constFind(b->id + QStringLiteral(":0"));
        if (f == lastFaces_.cend()) continue;
        Entity ring;
        ring.type = EntityType::Circle;
        ring.center = f->centre;
        ring.a = f->centre;
        ring.radius = handleMm_;
        ring.color = QStringLiteral("#5aa0ff");
        ring.meta.insert(QLatin1String(kPreviewMarkKey), true);
        ring.meta.insert(QStringLiteral("brickHandle"), b->id);
        ents.push_back(ring);
    }
    preview_->setEntities(ents);
}

QString BrickWizardDialog::bridgeValue() const {
    if (!bridge_) return QString();
    const QString text = bridge_->currentText().trimmed();
    const int at = bridge_->findText(text);
    if (at >= 0 && bridge_->itemData(at).isValid()) return bridge_->itemData(at).toString();
    return text;
}

bool BrickWizardDialog::drawingEnlarged() const { return enlargeBtn_ && enlargeBtn_->isChecked(); }

void BrickWizardDialog::placeEnlargeButton() {
    if (!enlargeBtn_) return;
    // Beside Fit, to its left.
    int right = preview_->width() - 6;
    if (auto* fit = preview_->findChild<QToolButton*>(QStringLiteral("previewFit"))) right = fit->x() - 4;
    enlargeBtn_->adjustSize();
    enlargeBtn_->move(right - enlargeBtn_->width(), 4 + 14);
    enlargeBtn_->raise();
}

void BrickWizardDialog::setDrawingEnlarged(bool on) {
    if (!mainSplit_ || !middle_) return;
    if (enlargeBtn_ && enlargeBtn_->isChecked() != on) {
        const QSignalBlocker hush(enlargeBtn_);
        enlargeBtn_->setChecked(on);
    }
    if (on) {
        savedMainSizes_ = mainSplit_->sizes();
        savedMiddleSizes_ = middle_->sizes();
        leftSplit_->hide();
        rightSplit_->hide();
        const int tall = std::max(400, middle_->height());
        middle_->setSizes({std::min(170, tall / 5), tall - std::min(170, tall / 5)});
        enlargeBtn_->setText(QStringLiteral("Exit enlarged"));
        status_->setText(QStringLiteral("The drawing enlarged - Exit enlarged goes back"));
    } else {
        leftSplit_->show();
        rightSplit_->show();
        if (!savedMainSizes_.isEmpty()) mainSplit_->setSizes(savedMainSizes_);
        if (!savedMiddleSizes_.isEmpty()) middle_->setSizes(savedMiddleSizes_);
        enlargeBtn_->setText(QStringLiteral("Enlarge"));
    }
    placeEnlargeButton();
    canvas_->fit();
}

QString BrickWizardDialog::previewPartAt(const QPointF& at) const {
    const Vec2 w = preview_->worldAt(at);
    // ITS GRAB POINT first - the blue ring drawn on every end part - then anywhere on the part.
    {
        QString near;
        double best = 1e300;
        const double reach = handleMm_ * 2.5;
        for (const Brick& b : design_.bricks) {
            if (b.type != BrickType::End) continue;
            const auto f = lastFaces_.constFind(b.id + QStringLiteral(":0"));
            if (f == lastFaces_.cend()) continue;
            const double d = distance(f->centre, w);
            if (d < reach && d < best) {
                best = d;
                near = b.id;
            }
        }
        if (!near.isEmpty()) return near;
    }
    QString hit;
    double best = 1e300;
    for (const Entity& en : preview_->entities()) {
        const QString id = en.meta.value(QStringLiteral("brickId")).toString();
        const Brick* b = design_.brick(id);
        if (!b || b->type != BrickType::End) continue;
        const Bounds bb = en.bounds();
        if (!bb.valid || w.x < bb.min.x || w.x > bb.max.x || w.y < bb.min.y || w.y > bb.max.y) continue;
        const double area = (bb.max.x - bb.min.x) * (bb.max.y - bb.min.y);
        if (area < best) {
            best = area;
            hit = id;
        }
    }
    return hit;
}

QHash<QString, int> BrickWizardDialog::partCounts() const {
    QHash<QString, int> n;
    for (const Brick& b : design_.bricks)
        if (b.type == BrickType::End && !b.libPart.isEmpty()) ++n[b.libPart];
    return n;
}

void BrickWizardDialog::syncLiveBom() {
    Document* doc = document();
    if (!doc) return;
    const QHash<QString, int> now = partCounts();
    if (!liveInit_) {
        liveCounts_ = now;
        liveInit_ = true;
        return;
    }
    if (now == liveCounts_) return;
    // THE BILL KEPT WITH THE BRICKS: a part given to a brick goes on it, one more brick of it is one more on it,
    // a part changed or a brick deleted comes off it (the line gone at nought) - and a connector's contacts go on
    // and come off with it, one per way.
    QSet<QString> ids;
    for (auto it = now.cbegin(); it != now.cend(); ++it) ids.insert(it.key());
    for (auto it = liveCounts_.cbegin(); it != liveCounts_.cend(); ++it) ids.insert(it.key());
    BomTable table;
    table.fromJson(doc->bom());
    const PartsLibrary merged = catalog_.merged();
    QList<const LibraryPart*> needContacts;
    bool changed = false;
    for (const QString& id : ids) {
        const int delta = now.value(id) - liveCounts_.value(id);
        if (delta == 0) continue;
        const LibraryPart* p = merged.byId(id);
        if (!p) continue;
        auto adjust = [&](const QString& partId, double by, const LibraryPart* part) {
            for (size_t k = 0; k < table.rows.size(); ++k)
                if (table.rows[k].partId == partId) {
                    table.rows[k].qty += by;
                    if (table.rows[k].qty <= 1e-9) table.rows.erase(table.rows.begin() + static_cast<long>(k));
                    changed = true;
                    return true;
                }
            if (by > 0.0 && part) {
                BomRow row = bomRowFromPart(*part, by);
                row.rowId = bomNextRowId(table.rows);
                table.rows.push_back(row);
                changed = true;
                return true;
            }
            return false;
        };
        adjust(id, delta, p);
        // Its contacts, as many ways more or fewer.
        if (p->kind == PartKind::Connector && !partIncludesPins(*p) && !partIsWired(*p)) {
            bool had = false;
            for (const QString& c : tiedContactsOf(merged, id))
                if (adjust(c, static_cast<double>(delta) * p->wayCount(), nullptr)) {
                    had = true;
                    break;
                }
            if (!had && delta > 0) needContacts << p;
        }
    }
    liveCounts_ = now;
    if (changed) {
        doc->bom() = table.toJson();
        doc->setDirty(true);
        emit bomChanged();
        refreshBom();
    }
    for (const LibraryPart* p : needContacts) addContactsFor(*p);   // which one, asked when several are tied
}

void BrickWizardDialog::recordHistory() {
    if (restoring_ || (canvas_ && canvas_->dragging()) || !partDrag_.isEmpty() || !runDrag_.isEmpty()) return;   // a drag is one step
    const QByteArray now = QJsonDocument(design_.toJson()).toJson(QJsonDocument::Compact);
    if (historyAt_ >= 0 && historyAt_ < static_cast<int>(history_.size()) && history_[historyAt_] == now) return;
    history_.resize(static_cast<size_t>(historyAt_ + 1));
    history_.push_back(now);
    if (history_.size() > 300) history_.erase(history_.begin());
    historyAt_ = static_cast<int>(history_.size()) - 1;
}

void BrickWizardDialog::restoreHistory(int at) {
    if (at < 0 || at >= static_cast<int>(history_.size())) return;
    restoring_ = true;
    const double fan = design_.faceFanPct;
    design_ = BrickDesign::fromJson(QJsonDocument::fromJson(history_[static_cast<size_t>(at)]).object());
    design_.catalog = &catalog_;
    design_.faceFanPct = fan;
    historyAt_ = at;
    QStringList keep;
    for (const QString& id : canvas_->selection())
        if (design_.brick(id)) keep << id;
    canvas_->setSelection(keep);
    designChanged();
    restoring_ = false;
}

void BrickWizardDialog::undo() {
    if (!canUndo()) {
        status_->setText(QStringLiteral("Nothing to undo"));
        return;
    }
    restoreHistory(historyAt_ - 1);
    status_->setText(QStringLiteral("Undone"));
}

void BrickWizardDialog::redo() {
    if (!canRedo()) {
        status_->setText(QStringLiteral("Nothing to redo"));
        return;
    }
    restoreHistory(historyAt_ + 1);
    status_->setText(QStringLiteral("Redone"));
}

void BrickWizardDialog::refreshPreview() {
    if (editor_) design_.faceFanPct = editor_->faceFanScale();
    syncBridgeWires(design_);   // wires taken from a joined special follow it
    recordHistory();
    if (partDrag_.isEmpty()) syncLiveBom();
    const BrickBuild built = buildBrickHarness(design_, &catalog_,
                                               [this](const QString& id) { return bomItemOf(id); });
    actual_ = built.actual;
    drawnScales_ = built.scales;
    lastFaces_ = built.faces;
    std::vector<Entity> ents = built.entities;
    const QStringList sel = canvas_->selection();
    for (Entity& e : ents)
        if (sel.contains(e.meta.value(QStringLiteral("brickId")).toString()))
            e.meta.insert(QLatin1String(kPreviewSelectedKey), true);
    // Big enough to see and to take hold of whatever size the drawing is: a share of the drawing, not a fixed 2 mm.
    Bounds whole;
    for (const Entity& e : built.entities) whole.expand(e.bounds());
    handleMm_ = whole.valid ? std::clamp(std::max(whole.width(), whole.height()) * 0.012, 1.5, 12.0) : 2.0;
    canvas_->setDrawingRowPitch(built.rowPitch);
    lastEnts_ = ents;
    showGrabRings();
    refreshBom();
    showActuals();
    // The dimension wizard, open, kept to the drawing - once the change has settled.
    if (dimWizard_ && dimWizard_->isVisible() && !dimWizQueued_) {
        dimWizQueued_ = true;
        QTimer::singleShot(0, this, [this] {
            dimWizQueued_ = false;
            refreshDimensionWizard();
        });
    }
}

void BrickWizardDialog::placeBom(int where) {
    if (!bomBox_ || where < 0 || where > 4) return;
    // Out of wherever it was.
    if (const int t = leftTabs_->indexOf(bomBox_); t >= 0) leftTabs_->removeTab(t);
    switch (where) {
    case 0: leftTabs_->addTab(bomBox_, QStringLiteral("BOM")); break;
    case 1: {
        // THE BOTTOM HALF of the left: the bricks and parts above, the bill below.
        leftSplit_->addWidget(bomBox_);
        const int h = std::max(400, leftSplit_->height());
        leftSplit_->setSizes({h / 2, h / 2});
        break;
    }
    case 2: middle_->addWidget(bomBox_); break;
    case 3: middle_->insertWidget(0, bomBox_); break;
    default: rightSplit_->addWidget(bomBox_); break;
    }
    bomBox_->show();
    bomAt_ = where;
    QSettings().setValue(QStringLiteral("brickWizard/bomAt"), where);
}

bool BrickWizardDialog::removeFromBom(int shownRow) {
    Document* doc = document();
    if (!doc || shownRow < 0 || shownRow >= static_cast<int>(bomShown_.size())) {
        status_->setText(QStringLiteral("Pick a line of the BOM to remove"));
        return false;
    }
    const BomRow target = bomShown_[static_cast<size_t>(shownRow)];
    BomTable table;
    table.fromJson(doc->bom());
    const size_t was = table.rows.size();
    table.rows.erase(std::remove_if(table.rows.begin(), table.rows.end(),
                                    [&](const BomRow& r) { return bomSameThing(r, target); }),
                     table.rows.end());
    if (table.rows.size() == was) {
        status_->setText(QStringLiteral("That line is there because the part is on the drawing - remove the "
                                        "part from the drawing to take it off"));
        return false;
    }
    doc->bom() = table.toJson();
    doc->setDirty(true);
    emit bomChanged();
    refreshBom();
    loadPanel();
    refreshPreview();
    status_->setText(QStringLiteral("%1 taken off the BOM").arg(target.mfrPn.isEmpty() ? target.description
                                                                                        : target.mfrPn));
    return true;
}

bool BrickWizardDialog::assignPart(const QString& partId) {
    Brick* b = design_.brick(canvas_->selected());
    const LibraryPart* p = catalog_.byId(partId);
    if (!b || b->type != BrickType::End || !p) {
        status_->setText(QStringLiteral("Pick an end part on the pane first"));
        return false;
    }
    // The brick becomes that part - and that part's kind.
    b->kind = p->kind;
    b->libPart = partId;
    canvas_->update();
    refreshPreview();   // the live bill puts it on (see syncLiveBom)
    loadPanel();
    status_->setText(QStringLiteral("%1 is %2, and on the BOM").arg(brickTitle(*b), p->title()));
    return true;
}

void BrickWizardDialog::keyPressEvent(QKeyEvent* e) {
    // Enter finishes typing, and nothing else: it does not press a button or close the window.
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        e->accept();
        return;
    }
    // NOR DOES ESC. It lets go of what is picked; the window closes by Close, not by a key pressed to get out of
    // something else - a harness half built was lost to it.
    if (e->key() == Qt::Key_Escape) {
        canvas_->setSelection({});
        loadPanel();
        refreshPreview();
        e->accept();
        return;
    }
    QDialog::keyPressEvent(e);
}

void BrickWizardDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    refreshSaved();   // the drawing may have changed while it was away
    refreshBom();
}

namespace {
// A conductor in square millimetres, from a gauge in AWG or in mm2.
double gaugeAreaMm2(double v, bool awg) {
    if (v <= 0.0) return 0.0;
    if (!awg) return v;
    const double dmm = 0.127 * std::pow(92.0, (36.0 - v) / 39.0);
    return M_PI * dmm * dmm / 4.0;
}
bool gaugeIsAwg(const QString& mode) { return !mode.contains(QLatin1String("mm"), Qt::CaseInsensitive); }
// Every figure in a gauge as written - "20-24", "24 to 20", "0.5" - as conductor areas.
QList<double> gaugeAreas(const QString& text, bool awg) {
    QList<double> out;
    static const QRegularExpression num(QStringLiteral("\\d+(?:\\.\\d+)?"));
    for (auto it = num.globalMatch(text); it.hasNext();)
        if (const double a = gaugeAreaMm2(it.next().captured(0).toDouble(), awg); a > 0.0) out << a;
    return out;
}
struct GaugeRange {
    double lo = 0.0, hi = 0.0;
    QString said;
};
// A range from its figures (one figure is a range of one), a little either side for rounding.
std::optional<GaugeRange> rangeOf(const QList<double>& areas, const QString& said) {
    if (areas.isEmpty()) return std::nullopt;
    const auto [lo, hi] = std::minmax_element(areas.begin(), areas.end());
    return GaugeRange{*lo * 0.97, *hi * 1.03, said};
}
} // namespace

BrickMark BrickWizardDialog::compatibilityOf(const Brick& b) const {
    BrickMark m;
    const PartsLibrary merged = catalog_.merged();
    auto partOf = [&merged](const QString& idOrPn) -> const LibraryPart* {
        if (idOrPn.isEmpty()) return nullptr;
        if (const LibraryPart* p = merged.byId(idOrPn)) return p;
        for (const LibraryPart& p : merged.parts)
            if (p.mfrPn == idOrPn) return &p;
        return nullptr;
    };
    // A size range of a part - "entry" read from entryMin / entryMax; one of them alone is both ends.
    auto rangeMm = [](const LibraryPart& q, const QString& key, double& lo, double& hi) {
        bool okLo = false, okHi = false;
        lo = q.value(key + QStringLiteral("Min")).toDouble(&okLo);
        hi = q.value(key + QStringLiteral("Max")).toDouble(&okHi);
        if (!okLo && !okHi) {
            bool one = false;
            lo = hi = q.value(key).toDouble(&one);
            return one && lo > 0.0;
        }
        if (!okLo) lo = hi;
        if (!okHi) hi = lo;
        if (lo > hi) std::swap(lo, hi);
        return hi > 0.0;
    };
    // THE STACK OF A RUN, from the inside out: the bundle its wires make (D), the cable over them, the braid over
    // that, the heatshrink over all - each layer's part checked to fit over what is inside it, and the size the run
    // comes to at the outside.
    struct Bundle {
        double d = 0.0;      // across the outside, mm; 0 = not known
        int level = -1;      // as a mark's compat
        QString why;
    };
    auto bundleOf = [&](const Brick& s) {
        Bundle out;
        auto say = [&out](int level, const QString& why) {
            if (level > out.level) {
                out.level = level;
                out.why = why;
            }
        };
        const BrickRunSpec& r = s.run;
        const QString name = s.name.isEmpty() ? brickTitle(s) : s.name;
        const int wires = s.type == BrickType::TwistedPair ? 2 * std::max(1, r.pairs) : std::max(1, r.wires);
        double d = 0.0;
        for (const QString& layer : r.stack) {
            const LibraryPart* lp = partOf(r.stackParts.value(layer));
            const QString lname = lp ? (lp->mfrPn.isEmpty() ? lp->title() : lp->mfrPn) : layer;
            if (layer == QLatin1String("wires")) {
                // THE D-BUNDLE: so many wires of their diameter, packed round.
                const double od = lp ? lp->value(QStringLiteral("od")).toDouble() : 0.0;
                if (od > 0.0) d = od * std::sqrt(wires / 0.7);
                else say(1, QStringLiteral("%1: the wires' diameter is not known").arg(name));
            } else if (layer == QLatin1String("cable")) {
                // A cable is its own outside, whatever is in it.
                const double od = lp ? lp->value(QStringLiteral("od")).toDouble() : 0.0;
                if (od > 0.0) {
                    if (d > 0.0 && od < d * 0.95)
                        say(2, QStringLiteral("%1: cable %2 (%3 mm) is smaller than its wires (%4 mm)")
                                   .arg(name, lname).arg(od, 0, 'f', 1).arg(d, 0, 'f', 1));
                    d = od;
                } else if (lp) {
                    say(1, QStringLiteral("%1: the diameter of cable %2 is not known").arg(name, lname));
                }
            } else if (layer == QLatin1String("braid")) {
                double lo = 0.0, hi = 0.0;
                if (!lp) {
                    if (d > 0.0) d += 1.0;
                    continue;
                }
                if (!rangeMm(*lp, QStringLiteral("diameter"), lo, hi)) {
                    say(1, QStringLiteral("%1: the size of braid %2 is not known").arg(name, lname));
                } else if (d > 0.0 && (d < lo * 0.95 || d > hi * 1.05)) {
                    say(2, QStringLiteral("%1: braid %2 is for %3-%4 mm, over %5 mm").arg(name, lname).arg(lo, 0, 'f', 1)
                               .arg(hi, 0, 'f', 1).arg(d, 0, 'f', 1));
                }
                if (d > 0.0) d += 1.0;   // the braid over it, about half a millimetre a side
            } else if (layer == QLatin1String("heatshrink")) {
                double lo = 0.0, hi = 0.0;
                const double wall = lp ? std::max(0.0, lp->value(QStringLiteral("wall")).toDouble()) : 0.0;
                if (!lp) {
                    continue;
                }
                if (!rangeMm(*lp, QStringLiteral("entry"), lo, hi)) {
                    say(1, QStringLiteral("%1: the size of heatshrink %2 is not known").arg(name, lname));
                } else if (d > 0.0 && d > hi * 1.02) {
                    say(2, QStringLiteral("%1: heatshrink %2 opens to %3 mm - too small for %4 mm").arg(name, lname)
                               .arg(hi, 0, 'f', 1).arg(d, 0, 'f', 1));
                } else if (d > 0.0 && d < lo * 0.98) {
                    say(2, QStringLiteral("%1: heatshrink %2 shrinks only to %3 mm - loose on %4 mm").arg(name, lname)
                               .arg(lo, 0, 'f', 1).arg(d, 0, 'f', 1));
                }
                if (d > 0.0) d += 2.0 * (wall > 0.0 ? wall : 0.5);
            }
        }
        out.d = d;
        return out;
    };
    if (brickIsSpecial(b.type)) {
        // A run: on the bill when every layer given a part number is.
        bool any = false, all = true;
        for (auto it = b.run.stackParts.cbegin(); it != b.run.stackParts.cend(); ++it) {
            const LibraryPart* p = partOf(it.value());
            if (!p) continue;
            any = true;
            all = all && bomItemOf(p->id) > 0;
        }
        if (any) m.bom = all ? 1 : 0;
        // AND WHETHER ITS STACK FITS TOGETHER: each layer over what is inside it. Checked once any layer has a part.
        if (any) {
            const Bundle bu = bundleOf(b);
            m.compat = std::max(0, bu.level);
            m.why = bu.level > 0 ? bu.why
                                 : bu.d > 0.0 ? QStringLiteral("Its layers fit one over another - %1 mm across").arg(bu.d, 0, 'f', 1)
                                              : QStringLiteral("Its layers fit one over another");
        }
        return m;
    }
    if (b.type != BrickType::End) return m;
    const LibraryPart* p = partOf(b.libPart);
    if (p) m.bom = bomItemOf(p->id) > 0 ? 1 : 0;
    // The worst thing found is what the mark says.
    auto say = [&m](int level, const QString& why) {
        if (level > m.compat) {
            m.compat = level;
            m.why = why;
        }
    };
    if (b.libPart.isEmpty()) {
        say(1, QStringLiteral("No part chosen yet"));
        return m;
    }
    if (!p) {
        say(1, QStringLiteral("Its part is not in the library"));
        return m;
    }
    // WHAT COMES INTO IT: the wires of each run linked to it - how many, and how big.
    struct In {
        int wires = 0;
        double area = 0.0;
        QString from;
    };
    QList<In> ins;
    int used = 0;
    const int ports = design_.portCount(b);
    QSet<int> portsUsed;
    for (const QString& o : design_.linksOf(b.id)) {
        portsUsed.insert(design_.portOn(b.id, o));
        ++used;
        const Brick* s = design_.brick(o);
        if (!s || !brickIsSpecial(s->type)) continue;
        const BrickLink* l = design_.link(b.id, o);
        int w = l && l->wires > 0 ? l->wires : 0;
        if (w == 0) {
            int parts = 0;
            for (const QString& e : design_.sideOf(o, design_.portOn(o, b.id)))
                if (const Brick* eb = design_.brick(e); eb && eb->type == BrickType::End) ++parts;
            const int all = s->type == BrickType::TwistedPair ? 2 * std::max(1, s->run.pairs) : std::max(1, s->run.wires);
            w = std::max(1, all / std::max(1, parts));
        }
        In in;
        in.wires = w;
        in.from = s->name.isEmpty() ? brickTitle(*s) : s->name;
        if (const LibraryPart* wp = partOf(s->run.stackParts.value(QStringLiteral("wires")))) {
            const QList<double> a = gaugeAreas(wp->value(QStringLiteral("gauge")), gaugeIsAwg(wp->value(QStringLiteral("gaugeMode"))));
            if (!a.isEmpty()) in.area = a.front();
        }
        ins << in;
    }
    if (used == 0) {
        say(1, QStringLiteral("Not linked to anything yet"));
        return m;
    }
    if (portsUsed.size() < ports) say(1, QStringLiteral("Not every side of it is linked yet"));
    // WHAT GOES INTO EACH OF ITS ENTRIES: the whole bundle of the run there - the D-bundle, braid, heatshrink over
    // it - must go into that entry of a Y-split (its main side or a leg), a heatshrink, a boot, a backshell, a gland;
    // and into a connector's cable entry where the library gives one.
    {
        const bool sized = p->kind == PartKind::YSplit || p->kind == PartKind::Heatshrink ||
                           p->kind == PartKind::HeatshrinkBoot || p->kind == PartKind::Backshell ||
                           p->kind == PartKind::Grommet || p->kind == PartKind::CableGland ||
                           p->kind == PartKind::Overmould;
        for (const QString& o : design_.linksOf(b.id)) {
            const Brick* s = design_.brick(o);
            if (!s || !brickIsSpecial(s->type)) continue;
            const int port = design_.portOn(b.id, o);
            const QString key = p->kind == PartKind::YSplit ? (port <= 0 ? QStringLiteral("main")
                                                                         : QStringLiteral("leg%1").arg(port))
                                : p->kind == PartKind::Heatshrink ? QStringLiteral("entry")
                                                                  : QStringLiteral("cableEntry");
            const QString where = p->kind == PartKind::YSplit ? (port <= 0 ? QStringLiteral("its main side")
                                                                           : QStringLiteral("leg %1").arg(port))
                                                              : QStringLiteral("its entry");
            const QString sname = s->name.isEmpty() ? brickTitle(*s) : s->name;
            double lo = 0.0, hi = 0.0;
            if (!rangeMm(*p, key, lo, hi)) {
                if (sized) say(1, QStringLiteral("The size of %1 is not in the library").arg(where));
                continue;
            }
            const Bundle bu = bundleOf(*s);
            if (bu.d <= 0.0) {
                say(1, QStringLiteral("How big %1 is across is not known - give its layers part numbers").arg(sname));
                continue;
            }
            if (bu.d > hi * 1.05)
                say(2, QStringLiteral("%1 (%2 mm across) does not go into %3 (%4-%5 mm)").arg(sname).arg(bu.d, 0, 'f', 1)
                           .arg(where).arg(lo, 0, 'f', 1).arg(hi, 0, 'f', 1));
            else if (bu.d < lo * 0.95)
                say(2, QStringLiteral("%1 (%2 mm across) is loose in %3 (%4-%5 mm)").arg(sname).arg(bu.d, 0, 'f', 1)
                           .arg(where).arg(lo, 0, 'f', 1).arg(hi, 0, 'f', 1));
        }
    }
    int total = 0;
    for (const In& in : ins) total += in.wires;
    // WHAT IT TAKES: the gauge ranges its wires must fall in.
    QList<GaugeRange> takes;
    bool takesKnown = true;
    auto rangeFromPart = [&](const LibraryPart& q) {
        const bool awg = gaugeIsAwg(q.value(QStringLiteral("gaugeMode")));
        QList<double> a = gaugeAreas(q.value(QStringLiteral("gaugeMin")), awg) +
                          gaugeAreas(q.value(QStringLiteral("gaugeMax")), awg);
        if (a.isEmpty()) a = gaugeAreas(q.value(QStringLiteral("gauge")), awg);
        // A FERRULE BY ITS ENTRY SIZE - "0.5 mm2 (20 AWG)": the first figure, in the unit it is written in.
        if (a.isEmpty() && q.kind == PartKind::Ferrule) {
            const QString size = q.value(QStringLiteral("size"));
            const QList<double> all = gaugeAreas(size, !size.contains(QLatin1String("mm"), Qt::CaseInsensitive));
            if (!all.isEmpty()) a = {all.front()};
        }
        const QString unit = awg ? QStringLiteral(" AWG") : QStringLiteral(" mm²");
        const QString said = (q.mfrPn.isEmpty() ? q.title() : q.mfrPn) + QStringLiteral(" ") +
                             QStringList{q.value(QStringLiteral("gaugeMin")), q.value(QStringLiteral("gaugeMax"))}.join(QLatin1Char('-')) + unit;
        return rangeOf(a, said);
    };
    if (p->kind == PartKind::Connector) {
        const int ways = p->wayCount();
        if (ways > 0 && total > ways)
            say(2, QStringLiteral("%1 wires into a %2-way connector").arg(total).arg(ways));
        if (partIsWired(*p)) {
            takesKnown = false;   // it comes with its own wires: nothing to size
        } else if (partIncludesPins(*p)) {
            // WITH ITS PINS: the wires must suit the pins it comes with.
            for (const QString& g : partPinGauges(*p)) {
                if (g.isEmpty()) continue;
                const bool awg = gaugeIsAwg(g);
                if (auto r = rangeOf(gaugeAreas(g, awg), g)) takes << *r;
            }
            if (takes.isEmpty()) say(1, QStringLiteral("The size of its pins is not in the library"));
        } else {
            // WITHOUT ITS PINS: its contacts must be on the bill, and the wires must suit them.
            QList<const LibraryPart*> onBill;
            for (const QString& id : tiedContactsOf(merged, p->id))
                if (bomItemOf(id) > 0)
                    if (const LibraryPart* c = merged.byId(id)) onBill << c;
            if (onBill.isEmpty()) {
                say(1, QStringLiteral("Its contacts are not on the BOM yet"));
                takesKnown = false;
            }
            for (const LibraryPart* c : onBill)
                if (auto r = rangeFromPart(*c)) takes << *r;
            if (!onBill.isEmpty() && takes.isEmpty()) say(1, QStringLiteral("The size of its contacts is not in the library"));
        }
    } else if (p->kind == PartKind::Ferrule || p->kind == PartKind::Crimp || p->kind == PartKind::ContactPin ||
               p->kind == PartKind::Splice || p->kind == PartKind::Terminal) {
        if (auto r = rangeFromPart(*p)) takes << *r;
        else say(1, QStringLiteral("Its wire size is not in the library"));
    } else {
        takesKnown = false;   // a Y-split, a heatshrink, a boot: sized to the bundle, not to a wire
    }
    // THE WIRES AGAINST WHAT IT TAKES.
    if (takesKnown && !takes.isEmpty()) {
        for (const In& in : ins) {
            if (in.area <= 0.0) {
                say(1, QStringLiteral("The wire size of %1 is not known - give its wires a part number").arg(in.from));
                continue;
            }
            bool fits = false;
            for (const GaugeRange& r : takes) fits = fits || (in.area >= r.lo && in.area <= r.hi);
            if (!fits) {
                QStringList said;
                for (const GaugeRange& r : takes) said << r.said;
                say(2, QStringLiteral("The wires of %1 (%2 mm²) do not suit %3").arg(in.from).arg(in.area, 0, 'f', 2)
                           .arg(said.join(QStringLiteral(", "))));
            }
        }
    }
    if (m.compat < 0) {
        m.compat = 0;
        m.why = QStringLiteral("Suits what it is linked to");
    }
    return m;
}

void BrickWizardDialog::refreshBrickMarks() {
    if (!canvas_) return;
    QHash<QString, BrickMark> marks;
    for (const Brick& b : design_.bricks) marks.insert(b.id, compatibilityOf(b));
    canvas_->setMarks(marks);
}

void BrickWizardDialog::refreshBom() {
    if (!bom_) return;
    refreshBrickMarks();   // the dots on the bricks follow the bill
    const QSignalBlocker hush(bom_);   // filling it is not somebody typing a quantity
    bom_->setRowCount(0);
    const Document* doc = document();
    if (!doc) return;
    BomTable table;
    table.fromJson(doc->bom());
    std::vector<BomRow> rows = table.rows;
    if (table.config.includeDrawing) {
        const std::vector<BomRow> drawn = bomFromDrawing(*doc, catalog_.merged());
        rows.insert(rows.end(), drawn.begin(), drawn.end());
    }
    if (table.config.unify) rows = bomUnify(rows);
    bomShown_ = rows;
    QSet<QString> used;
    for (const Brick& b : design_.bricks)
        if (!b.libPart.isEmpty()) used.insert(b.libPart);
    for (size_t i = 0; i < rows.size(); ++i) {
        const BomRow& r = rows[i];
        const int at = bom_->rowCount();
        bom_->insertRow(at);
        const QStringList cells{QString::number(i + 1), QString::number(r.qty), r.unit,
                                r.mfrPn.isEmpty() ? r.internalPn : r.mfrPn, r.description};
        for (int c = 0; c < cells.size(); ++c) {
            auto* it = new QTableWidgetItem(cells[c]);
            // Only the quantity of a line of the bill itself may be typed over.
            const bool editable = c == 1 && !r.fromDrawing;
            it->setFlags(editable ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable)
                                  : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
            if (editable) it->setToolTip(QStringLiteral("Double-click to change the quantity"));
            if (used.contains(r.partId)) {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
            }
            bom_->setItem(at, c, it);
        }
    }
}

QStringList BrickWizardDialog::schematicNets(const QStringList& designators) const {
    QStringList nets;
    const Document* doc = document();
    if (!doc || designators.isEmpty()) return nets;
    std::vector<Vec2> pins;
    for (const Entity& e : doc->entities()) {
        if (e.type != EntityType::Connector) continue;
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        if (!designators.contains(spec.designator, Qt::CaseInsensitive)) continue;
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        pins.insert(pins.end(), g.pinEnd.begin(), g.pinEnd.end());
    }
    // A wire is on a pin where one of its ends is on the pin's end.
    auto onPin = [&pins](const Vec2& p) {
        for (const Vec2& q : pins)
            if (distance(p, q) < 1e-3) return true;
        return false;
    };
    for (const Entity& w : doc->entities()) {
        if (w.type != EntityType::Wire || w.points.size() < 2) continue;
        if (!onPin(w.points.front()) && !onPin(w.points.back())) continue;
        QString net = w.meta.value(QStringLiteral("netName")).toString();
        if (net.isEmpty()) net = w.meta.value(QStringLiteral("wireLabel")).toString();
        if (!net.isEmpty() && !nets.contains(net)) nets << net;
    }
    nets.sort(Qt::CaseInsensitive);
    return nets;
}

void BrickWizardDialog::loadNets() {
    const Brick* b = design_.brick(canvas_->selected());
    const QSignalBlocker block(nets_);
    nets_->setRowCount(0);
    if (!b || !brickIsSpecial(b->type)) return;
    // What the schematic has on the parts this run is linked to, by their designators.
    QStringList designators;
    for (const QString& e : design_.endsOf(b->id))
        if (const Brick* eb = design_.brick(e); eb && !eb->name.isEmpty()) designators << eb->name;
    const QStringList offered = schematicNets(designators);
    const QString id = b->id;
    for (int w = 0; w < b->run.wires; ++w) {
        nets_->insertRow(w);
        nets_->setItem(w, 0, new QTableWidgetItem(QString::number(w + 1)));
        nets_->item(w, 0)->setFlags(Qt::ItemIsEnabled);
        auto* net = new QComboBox(nets_);
        net->setEditable(true);
        net->addItem(QString());
        net->addItems(offered);
        net->setCurrentText(w < b->run.nets.size() ? b->run.nets[w] : QString());
        nets_->setCellWidget(w, 1, net);
        connect(net, &QComboBox::currentTextChanged, this, [this, id, w](const QString& t) {
            Brick* br = design_.brick(id);
            if (!br) return;
            while (br->run.nets.size() <= w) br->run.nets << QString();
            br->run.nets[w] = t.trimmed();
            refreshPreview();
        });
    }
}

QStringList BrickWizardDialog::savedNames() const {
    const Document* doc = document();
    if (!doc) return {};
    QStringList names = doc->wizards().value(QStringLiteral("harnesses")).toObject().keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

void BrickWizardDialog::refreshSaved() {
    if (!saved_) return;
    const QSignalBlocker block(saved_);
    const QString keep = saved_->currentText();
    saved_->clear();
    saved_->addItems(savedNames());
    const int at = saved_->findText(!currentName_.isEmpty() ? currentName_ : keep);
    if (at >= 0) saved_->setCurrentIndex(at);
}

bool BrickWizardDialog::saveToDrawing(const QString& name) {
    Document* doc = document();
    if (!doc || name.trimmed().isEmpty()) return false;
    QJsonObject all = doc->wizards();
    QJsonObject harnesses = all.value(QStringLiteral("harnesses")).toObject();
    harnesses.insert(name.trimmed(), design_.toJson());
    all.insert(QStringLiteral("harnesses"), harnesses);
    doc->wizards() = all;
    doc->setDirty(true);
    currentName_ = name.trimmed();
    harnessName_->setText(currentName_);
    refreshSaved();
    status_->setText(QStringLiteral("Saved \"%1\" in the drawing").arg(currentName_));
    return true;
}

bool BrickWizardDialog::loadFromDrawing(const QString& name) {
    const Document* doc = document();
    if (!doc) return false;
    const QJsonObject harnesses = doc->wizards().value(QStringLiteral("harnesses")).toObject();
    if (!harnesses.contains(name)) return false;
    design_ = BrickDesign::fromJson(harnesses.value(name).toObject());
    design_.catalog = &catalog_;
    {
        const QSignalBlocker b1(dimFrom_), b2(dimTo_);
        dimFrom_->setCurrentIndex(design_.dimFromMid ? 2 : design_.dimFromFar ? 1 : 0);
        dimTo_->setCurrentIndex(design_.dimToMid ? 2 : design_.dimToFar ? 1 : 0);
    }
    currentName_ = name;
    harnessName_->setText(name);
    canvas_->setSelection({});
    canvas_->centreView();
    liveInit_ = false;   // an opened harness brings its own bill: counted from here, not added again
    designChanged();
    refreshSaved();
    status_->setText(QStringLiteral("Opened \"%1\"").arg(name));
    return true;
}

bool BrickWizardDialog::deleteFromDrawing(const QString& name) {
    Document* doc = document();
    if (!doc) return false;
    QJsonObject all = doc->wizards();
    QJsonObject harnesses = all.value(QStringLiteral("harnesses")).toObject();
    if (!harnesses.contains(name)) return false;
    harnesses.remove(name);
    all.insert(QStringLiteral("harnesses"), harnesses);
    doc->wizards() = all;
    doc->setDirty(true);
    if (currentName_ == name) currentName_.clear();
    refreshSaved();
    status_->setText(QStringLiteral("Deleted \"%1\" from the drawing").arg(name));
    return true;
}

int BrickWizardDialog::bomItemOf(const QString& partId) const {
    const Document* doc = document();
    const LibraryPart* p = catalog_.byId(partId);
    if (!doc || !p) return 0;
    return bomItemLookup(*doc, catalog_.merged())(partId, p->kind, p->description);
}

bool BrickWizardDialog::addPartToBom(const QString& partId, double qty) {
    Document* doc = document();
    const LibraryPart* p = catalog_.byId(partId);
    if (!doc || !p) return false;
    BomTable table;
    table.fromJson(doc->bom());
    std::vector<BomRow> have = table.rows;
    if (table.config.includeDrawing) {
        const std::vector<BomRow> drawn = bomFromDrawing(*doc, catalog_.merged());
        have.insert(have.end(), drawn.begin(), drawn.end());
    }
    have = bomUnify(have);
    BomRow row = bomRowFromPart(*p, qty);
    const std::vector<int> already = bomAlreadyListed(have, {row});
    if (!already.empty() && already.front() >= 0) {
        const QString q = QStringLiteral("%1 is already in the BOM as item %2.\n\nAdd it again?")
                              .arg(p->title())
                              .arg(already.front() + 1);
        const bool yes = ask_ ? ask_(q)
                              : QMessageBox::question(this, QStringLiteral("Already in the BOM"), q) ==
                                    QMessageBox::Yes;
        if (!yes) return false;
    }
    row.rowId = bomNextRowId(table.rows);
    table.rows.push_back(row);
    doc->bom() = table.toJson();
    doc->setDirty(true);
    emit bomChanged();
    refreshBom();
    addContactsFor(*p);
    return true;
}

void BrickWizardDialog::addContactsFor(const LibraryPart& connector) {
    if (connector.kind != PartKind::Connector || partIncludesPins(connector) || partIsWired(connector)) return;
    const PartsLibrary merged = catalog_.merged();
    const QStringList tied = tiedContactsOf(merged, connector.id);
    // One of them already on the bill: nothing more to add.
    for (const QString& id : tied)
        if (bomItemOf(id) > 0) return;
    if (tied.isEmpty()) {
        status_->setText(QStringLiteral("%1 needs contacts - none are tied to it; tie them in the parts library")
                             .arg(connector.title()));
        return;
    }
    QString pick = tied.front();
    // SEVERAL: which one - asked.
    if (tied.size() > 1) {
        if (chooseContact_) {
            pick = chooseContact_(tied);
        } else if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
            pick = tied.front();   // no screen to ask on: the first, the one the connector names
        } else {
            QStringList names;
            for (const QString& id : tied) {
                const LibraryPart* c = merged.byId(id);
                names << (c ? (c->mfrPn.isEmpty() ? c->title() : c->mfrPn + QStringLiteral(" - ") + c->description) : id);
            }
            names << QStringLiteral("(no contacts now)");
            bool ok = false;
            const QString chosen = QInputDialog::getItem(
                this, QStringLiteral("Contacts for %1").arg(connector.title()),
                QStringLiteral("%1 is bought without its contacts. Which contact goes on the bill with it (%2 of them)?")
                    .arg(connector.title())
                    .arg(connector.wayCount()),
                names, 0, false, &ok);
            const int at = names.indexOf(chosen);
            pick = ok && at >= 0 && at < tied.size() ? tied[at] : QString();
        }
    }
    if (pick.isEmpty()) return;
    const LibraryPart* contact = merged.byId(pick);
    if (!contact) return;
    Document* doc = document();
    if (!doc) return;
    BomTable table;
    table.fromJson(doc->bom());
    BomRow row = bomRowFromPart(*contact, connector.wayCount());
    row.rowId = bomNextRowId(table.rows);
    table.rows.push_back(row);
    doc->bom() = table.toJson();
    doc->setDirty(true);
    emit bomChanged();
    refreshBom();
    status_->setText(QStringLiteral("%1 on the bill, with %2 x %3").arg(connector.title()).arg(connector.wayCount())
                         .arg(contact->mfrPn.isEmpty() ? contact->title() : contact->mfrPn));
}

void BrickWizardDialog::place() {
    std::vector<Entity> ents = drawing();
    if (ents.empty() || !editor_) {
        status_->setText(QStringLiteral("Nothing to place yet"));
        return;
    }
    // WHAT EACH OBJECT IS PART OF goes down with it, so Alt over any line of a part on the sheet finds it: an end's
    // library part on every object of that end (its outline, its mask, its designator), and a run's layers (already
    // on it, stackParts) - with one copy mark per brick, so the whole of the part lights up together.
    QHash<QString, QString> copyOf;
    for (Entity& e : ents) {
        const QString bid = e.meta.value(QStringLiteral("brickId")).toString();
        if (const Brick* b = design_.brick(bid)) {
            if (!copyOf.contains(bid)) copyOf.insert(bid, QUuid::createUuid().toString(QUuid::WithoutBraces));
            e.meta.insert(QStringLiteral("partCopy"), copyOf.value(bid));
            if (b->type == BrickType::End && !b->libPart.isEmpty() && !e.meta.contains(QStringLiteral("libPart")))
                e.meta.insert(QStringLiteral("libPart"), b->libPart);
            if (brickIsSpecial(b->type) && !b->run.stackParts.isEmpty() && !e.meta.contains(QStringLiteral("stackParts"))) {
                QJsonArray ids;
                for (const QString& layer : b->run.stack)
                    if (!b->run.stackParts.value(layer).isEmpty()) ids.append(b->run.stackParts.value(layer));
                if (!ids.isEmpty()) e.meta.insert(QStringLiteral("stackParts"), ids);
            }
        }
        e.meta.remove(QStringLiteral("brickId"));
        e.meta.remove(QStringLiteral("brickBranch"));
    }
    editor_->placeEntities(ents);
    status_->setText(QStringLiteral("%1 object(s) ready — click on the sheet to put them down")
                         .arg(ents.size()));
    hide();
    auto* placing = new QObject(this);
    const auto back = [this, placing] {
        if (editor_ && editor_->placingPaste()) return;
        placing->deleteLater();
        show();
        raise();
        activateWindow();
    };
    connect(editor_, &Editor::changed, placing, back);
    connect(editor_, &Editor::overlayChanged, placing, back);
}

} // namespace Edraw_Desktop
