#include "app/BrickWizardDialog.h"

#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "harness/Bom.h"
#include "model/Document.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
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
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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
    const Vec2 wo = turnQuarters(o, b.rot), wa = turnQuarters(across, b.rot);
    const QPointF d(wo.x, -wo.y), a(wa.x, -wa.y);
    const bool sideways = std::abs(d.x()) > 0.5;
    const double halfAlong = sideways ? r.width() / 2.0 : r.height() / 2.0;
    const double halfAcross = sideways ? r.height() / 2.0 : r.width() / 2.0;
    return r.center() + d * halfAlong + a * (t * halfAcross * 0.75);
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

void BrickCanvas::fit() {
    if (d_->bricks.empty()) {
        origin_ = QPointF(80.0, 70.0);
        update();
        return;
    }
    double c0 = 1e9, c1 = -1e9, r0 = 1e9, r1 = -1e9;
    for (const Brick& b : d_->bricks) {
        c0 = std::min(c0, b.col);
        c1 = std::max(c1, b.col);
        r0 = std::min(r0, b.row);
        r1 = std::max(r1, b.row);
    }
    const QPointF mid((c0 + c1) / 2.0 * stepX_, (r0 + r1) / 2.0 * stepY_);
    origin_ = QPointF(width() / 2.0, height() / 2.0) - mid;
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

void BrickCanvas::moveSelection(double dCol, double dRow) {
    if (sel_.isEmpty()) return;
    for (const QString& id : sel_)
        if (Brick* b = d_->brick(id)) {
            b->col += dCol;
            b->row += dRow;
        }
    emit designChanged();
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
    p.setPen(QPen(QColor(0x3c, 0x46, 0x52), 1.6));
    double c0, r0, c1, r1;
    toGrid(QPointF(0, 0), c0, r0);
    toGrid(QPointF(width(), height()), c1, r1);
    for (int c = int(std::floor(c0)); c <= int(std::ceil(c1)); ++c)
        for (int r = int(std::floor(r0)); r <= int(std::ceil(r1)); ++r) p.drawPoint(toScreen(c, r));

    if (d_->bricks.empty()) {
        p.setPen(QColor(0x8a, 0x93, 0xa0));
        p.drawText(rect(), Qt::AlignCenter,
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
        if (end) sub = b.libPart.isEmpty() ? QStringLiteral("no part yet") : b.libPart;
        else sub = QStringLiteral("%1 w · %2").arg(b.run.wires).arg(b.run.lengthMm, 0, 'f', 0);
        const QRectF bottom(r.left() + 3, r.center().y(), r.width() - 6, r.height() / 2.0 - 2);
        p.setPen(QColor(0xb8, 0xc0, 0xcc));
        p.drawText(bottom, Qt::AlignCenter,
                   p.fontMetrics().elidedText(sub, Qt::ElideRight, int(bottom.width())));
        // THE TWO PORTS: filled where something is linked.
        for (int port = 0; port < d_->portCount(b); ++port) {
            bool used = false;
            for (const QString& o : d_->linksOf(b.id))
                if (d_->portOn(b.id, o) == port) used = true;
            p.setPen(QPen(QColor(0x7e, 0xe0, 0xa0), 1.2));
            p.setBrush(used ? QColor(0x7e, 0xe0, 0xa0) : QColor(0x23, 0x2a, 0x33));
            p.drawEllipse(portPoint(b, port), 4.0, 4.0);
        }
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
    pressAt_ = cursor_ = e->position();
    if (e->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        panFrom_ = origin_;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    // A PORT, aimed at on purpose, wins over any brick's body: a link starts there.
    const auto port = portAt(e->position());
    if (!port.first.isEmpty()) {
        dragId_ = port.first;
        dragPort_ = port.second;
        drag_ = Drag::Link;
        update();
        return;
    }
    const QString hit = brickAt(e->position());
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
        update();
        return;
    }
    for (const BrickLink& l : d_->links) {
        const Brick* a = d_->brick(l.a);
        const Brick* b = d_->brick(l.b);
        if (!a || !b) continue;
        if (segDistance(e->position(), portPoint(*a, d_->portOn(l.a, l.b)),
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
    cursor_ = e->position();
    if (drag_ == Drag::Pan) {
        origin_ = panFrom_ + (e->position() - pressAt_);
        update();
    } else if (drag_ == Drag::Move) {
        const double dc = snapHalf((e->position().x() - pressAt_.x()) / stepX_);
        const double dr = snapHalf((e->position().y() - pressAt_.y()) / stepY_);
        bool moved = false;
        for (auto it = startAt_.begin(); it != startAt_.end(); ++it)
            if (Brick* b = d_->brick(it.key())) {
                const double col = it.value().x() + dc, row = it.value().y() + dr;
                if (col != b->col || row != b->row) {
                    b->col = col;
                    b->row = row;
                    moved = true;
                }
            }
        if (moved) emit designChanged();
        update();
    } else if (drag_ == Drag::Link || drag_ == Drag::Zone) {
        update();
    }
}

void BrickCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (drag_ == Drag::Link) {
        const QString target = brickAt(e->position());
        const auto tport = portAt(e->position());
        const QString to = !tport.first.isEmpty() ? tport.first : target;
        if (!to.isEmpty() && to != dragId_) {
            // Let go ON a port, that port; anywhere else on the brick, the one facing the other.
            const int toPort = tport.first == to ? tport.second : -1;
            QString why;
            if (d_->connect(dragId_, to, &why, dragPort_, toPort)) {
                emit message(QStringLiteral("Linked"));
                emit designChanged();
            } else {
                emit message(why);
            }
        }
    } else if (drag_ == Drag::Zone) {
        const QRectF z = QRectF(pressAt_, e->position()).normalized();
        if (z.width() < 3.0 && z.height() < 3.0) {
            // A click on nothing lets go of what was picked.
            if (!zoneAdd_ && (!sel_.isEmpty() || !selLink_.first.isEmpty())) {
                sel_.clear();
                selLink_ = {};
                emit selectionChanged(QString());
            }
        } else {
            const bool window = e->position().x() >= pressAt_.x();
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
    drag_ = Drag::None;
    dragId_.clear();
    update();
}

void BrickCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    if (e->key() == Qt::Key_R) {
        rotateSelection();
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
    if (parts.size() != 2) return;
    double col, row;
    toGrid(e->position(), col, row);
    const QString id = d_->addBrick(brickTypeFromKey(parts[0]), partKindFromKey(parts[1]),
                                    std::round(col), snapHalf(row));
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
    loadPanel();
    refreshPreview();
}

Document* BrickWizardDialog::document() const { return editor_ ? editor_->document() : nullptr; }

void BrickWizardDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

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
    button("brickRotate", QStringLiteral("⟳ Rotate"), QStringLiteral("Turn what is picked a quarter (R)"),
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
    button("brickArrange", QStringLiteral("Arrange"),
           QStringLiteral("Lay the end parts out symmetrically: the most connected on the axis, the "
                          "rest either side, every brick turned to face what it is linked to"),
           [this] { arrange(); });
    bar->addSpacing(18);
    bar->addWidget(new QLabel(QStringLiteral("Dimensions from"), this));
    dimFrom_ = new QComboBox(this);
    dimFrom_->setObjectName(QStringLiteral("brickDimFrom"));
    dimFrom_->addItems({QStringLiteral("Near point"), QStringLiteral("Far point")});
    bar->addWidget(dimFrom_);
    bar->addWidget(new QLabel(QStringLiteral("to"), this));
    dimTo_ = new QComboBox(this);
    dimTo_->setObjectName(QStringLiteral("brickDimTo"));
    dimTo_->addItems({QStringLiteral("Near point"), QStringLiteral("Far point")});
    bar->addWidget(dimTo_);
    for (QComboBox* c : {dimFrom_, dimTo_})
        connect(c, &QComboBox::currentIndexChanged, this, [this](int) {
            design_.dimFromFar = dimFrom_->currentIndex() == 1;
            design_.dimToFar = dimTo_->currentIndex() == 1;
            refreshPreview();
        });
    bar->addStretch(1);
    root->addLayout(bar);

    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // THE BRICKS, AND THE PARTS LIST.
    auto* leftTabs = new QTabWidget(split);
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
                             QStringLiteral("Manufacturer")});
    parts_->setRootIsDecorated(false);
    parts_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    parts_->header()->setStretchLastSection(false);
    parts_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    pl->addWidget(parts_, 1);
    auto* addParts = new QPushButton(QStringLiteral("Add to BOM"), partsPage);
    addParts->setObjectName(QStringLiteral("brickPartsToBom"));
    pl->addWidget(addParts);
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
    fillPartsList();

    // THE LOW-DETAIL PANE ABOVE THE PREVIEW.
    auto* middle = new QSplitter(Qt::Vertical, split);
    canvas_ = new BrickCanvas(&design_, middle);
    canvas_->setObjectName(QStringLiteral("brickCanvas"));
    preview_ = new DrawingPreview(QStringLiteral("Preview"), middle);
    preview_->setEmptyText(QStringLiteral("The drawing appears here as the bricks are linked"));
    middle->setStretchFactor(0, 1);
    middle->setStretchFactor(1, 1);
    connect(canvas_, &BrickCanvas::selectionChanged, this, [this](const QString&) {
        loadPanel();
        refreshPreview();
    });
    connect(canvas_, &BrickCanvas::linkPicked, this, [this](const QString&, const QString&) { loadPanel(); });
    connect(canvas_, &BrickCanvas::designChanged, this, &BrickWizardDialog::designChanged);
    connect(canvas_, &BrickCanvas::message, this, [this](const QString& t) { status_->setText(t); });

    // WHAT THE PICKED BRICK IS.
    auto* rightScroll = new QScrollArea(split);
    rightScroll->setWidgetResizable(true);
    panel_ = new QStackedWidget(rightScroll);
    panel_->setObjectName(QStringLiteral("brickPanel"));
    rightScroll->setWidget(panel_);
    noneText_ = new QLabel(QStringLiteral("Pick a brick to set what it is."), panel_);
    noneText_->setWordWrap(true);
    noneText_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    panel_->addWidget(noneText_);
    panel_->addWidget(buildEndPage());
    panel_->addWidget(buildRunPage());
    panel_->addWidget(buildLinkPage());

    split->addWidget(leftTabs);
    split->addWidget(middle);
    split->addWidget(rightScroll);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({400, 700, 420});

    auto* bottom = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("brickStatus"));
    bottom->addWidget(status_, 1);
    auto* placeBtn = new QPushButton(QStringLiteral("Place the drawing"), this);
    placeBtn->setObjectName(QStringLiteral("brickPlace"));
    connect(placeBtn, &QPushButton::clicked, this, [this] { place(); });
    bottom->addWidget(placeBtn);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    bottom->addWidget(close);
    root->addLayout(bottom);
}

QWidget* BrickWizardDialog::buildEndPage() {
    auto* page = new QWidget(panel_);
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
    f->addRow(QStringLiteral("Designator"), name_);
    balloon_ = new QCheckBox(QStringLiteral("Balloon"), page);
    balloon_->setObjectName(QStringLiteral("brickBalloon"));
    f->addRow(QString(), balloon_);
    bomState_ = new QLabel(page);
    bomState_->setObjectName(QStringLiteral("brickBomState"));
    bomState_->setWordWrap(true);
    bomAdd_ = new QPushButton(QStringLiteral("Add to BOM"), page);
    bomAdd_->setObjectName(QStringLiteral("brickBomAdd"));
    auto* bomRow = new QHBoxLayout;
    bomRow->addWidget(bomState_, 1);
    bomRow->addWidget(bomAdd_);
    f->addRow(QStringLiteral("BOM"), bomRow);
    params_ = new QTableWidget(0, 2, page);
    params_->setObjectName(QStringLiteral("brickParams"));
    params_->setHorizontalHeaderLabels({QStringLiteral("Parameter"), QStringLiteral("Value")});
    params_->horizontalHeader()->setStretchLastSection(true);
    params_->verticalHeader()->setVisible(false);
    params_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    params_->setMinimumHeight(220);
    f->addRow(params_);
    connect(part_, &QComboBox::currentIndexChanged, this, [this](int) {
        writeEnd();
        loadPanel();
    });
    connect(name_, &QLineEdit::textEdited, this, [this](const QString&) { writeEnd(); });
    connect(balloon_, &QCheckBox::toggled, this, [this](bool) { writeEnd(); });
    connect(bomAdd_, &QPushButton::clicked, this, [this] {
        const Brick* b = design_.brick(canvas_->selected());
        if (!b || b->libPart.isEmpty()) return;
        if (addPartToBom(b->libPart)) status_->setText(QStringLiteral("Added to the BOM"));
        loadPanel();
        refreshPreview();
    });
    return page;
}

QWidget* BrickWizardDialog::buildRunPage() {
    auto* page = new QWidget(panel_);
    auto* v = new QVBoxLayout(page);
    auto* form = new QFormLayout;
    v->addLayout(form);
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
    length_ = spin("brickLength", 10.0, 100000.0, 10.0, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Length"), length_);
    wireWidth_ = spin("brickWireWidth", 0.2, 20.0, 0.1, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Wire width"), wireWidth_);
    fanStart_ = spin("brickFanStart", -1.0, 1000.0, 1.0, QStringLiteral(" mm"), QStringLiteral("Auto"));
    form->addRow(QStringLiteral("Fan length, start"), fanStart_);
    fanFinish_ = spin("brickFanFinish", -1.0, 1000.0, 1.0, QStringLiteral(" mm"), QStringLiteral("Auto"));
    form->addRow(QStringLiteral("Fan length, finish"), fanFinish_);
    knee_ = spin("brickKnee", -1.0, 1000.0, 1.0, QStringLiteral(" mm"), QStringLiteral("Auto"));
    form->addRow(QStringLiteral("Bend radius, body"), knee_);
    fanKnee_ = spin("brickFanKnee", -1.0, 1000.0, 0.5, QStringLiteral(" mm"), QStringLiteral("Auto"));
    form->addRow(QStringLiteral("Bend radius, fans"), fanKnee_);
    fanScale_ = spin("brickFanScale", 5.0, 100.0, 5.0, QStringLiteral(" %"));
    form->addRow(QStringLiteral("Fans across the face"), fanScale_);
    bodyScale_ = spin("brickBodyScale", 5.0, 100.0, 5.0, QStringLiteral(" %"));
    form->addRow(QStringLiteral("Body across the face"), bodyScale_);
    bodyWidth_ = spin("brickBodyWidth", 0.0, 1000.0, 0.5, QStringLiteral(" mm"), QStringLiteral("From the face"));
    form->addRow(QStringLiteral("Body width"), bodyWidth_);
    transitions_ = new QComboBox(page);
    transitions_->setObjectName(QStringLiteral("brickTransitions"));
    transitions_->addItems({QStringLiteral("None"), QStringLiteral("Start"), QStringLiteral("Finish"),
                            QStringLiteral("Both")});
    form->addRow(QStringLiteral("Transition heatshrink"), transitions_);
    for (QDoubleSpinBox* s : {length_, wireWidth_, fanStart_, fanFinish_, knee_, fanKnee_, fanScale_,
                              bodyScale_, bodyWidth_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) { writeRun(); });
    connect(wires_, &QSpinBox::valueChanged, this, [this](int) {
        writeRun();
        loadLabels();   // the wires a label can go on
    });
    connect(transitions_, &QComboBox::currentIndexChanged, this, [this](int) { writeRun(); });

    // THE STACK, from the middle out; the top of the list is the outermost, and only it is seen.
    auto* stackBox = new QGroupBox(QStringLiteral("Stack — from the outside in"), page);
    auto* sv = new QVBoxLayout(stackBox);
    stack_ = new QListWidget(stackBox);
    stack_->setObjectName(QStringLiteral("brickStack"));
    stack_->setMaximumHeight(110);
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
    v->addWidget(stackBox);
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
    // The list shows the outermost first: list row r is stack index size-1-r.
    connect(outBtn, &QPushButton::clicked, this, [this, editStack] {
        const int r = stack_->currentRow();
        editStack([&](QStringList& s) {
            const int i = s.size() - 1 - r;
            if (r > 0 && i >= 1 && i + 1 < s.size()) s.swapItemsAt(i, i + 1);
        });
        stack_->setCurrentRow(std::max(0, r - 1));
    });
    connect(inBtn, &QPushButton::clicked, this, [this, editStack] {
        const int r = stack_->currentRow();
        editStack([&](QStringList& s) {
            const int i = s.size() - 1 - r;
            if (i >= 2) s.swapItemsAt(i, i - 1);
        });
        stack_->setCurrentRow(r + 1);
    });
    connect(rmBtn, &QPushButton::clicked, this, [this, editStack] {
        const int r = stack_->currentRow();
        editStack([&](QStringList& s) {
            const int i = s.size() - 1 - r;
            if (i >= 1 && i < s.size()) s.removeAt(i);
        });
    });

    // DIMENSIONS.
    auto* dims = new QGroupBox(QStringLiteral("Dimensions"), page);
    auto* dv = new QHBoxLayout(dims);
    dimTotal_ = new QCheckBox(QStringLiteral("Whole length"), dims);
    dimTotal_->setObjectName(QStringLiteral("brickDimTotal"));
    dimFans_ = new QCheckBox(QStringLiteral("Fans only"), dims);
    dimFans_->setObjectName(QStringLiteral("brickDimFans"));
    dimBetween_ = new QCheckBox(QStringLiteral("Between fans"), dims);
    dimBetween_->setObjectName(QStringLiteral("brickDimBetween"));
    for (QCheckBox* c : {dimTotal_, dimFans_, dimBetween_}) {
        dv->addWidget(c);
        connect(c, &QCheckBox::toggled, this, [this](bool) { writeRun(); });
    }
    v->addWidget(dims);

    // LABELS.
    auto* labBox = new QGroupBox(QStringLiteral("Labels"), page);
    auto* lv = new QVBoxLayout(labBox);
    labels_ = new QTableWidget(0, 4, labBox);
    labels_->setObjectName(QStringLiteral("brickLabels"));
    labels_->setHorizontalHeaderLabels({QStringLiteral("On"), QStringLiteral("From"),
                                        QStringLiteral("Distance"), QStringLiteral("Text")});
    labels_->horizontalHeader()->setStretchLastSection(true);
    labels_->verticalHeader()->setVisible(false);
    labels_->setMinimumHeight(140);
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
    v->addWidget(labBox);
    v->addStretch(1);
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
    return page;
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
            auto* it = new QTreeWidgetItem(parts_, cols);
            it->setData(0, Qt::UserRole, p->id);
            it->setToolTip(1, p->description);
        }
}

QString BrickWizardDialog::addFromPalette(const QString& what) {
    const QStringList parts = what.split(QLatin1Char('|'));
    if (parts.size() != 2) return QString();
    double col = 0.0;
    for (const Brick& b : design_.bricks) col = std::max(col, b.col + 1.0);
    const QString id = design_.addBrick(brickTypeFromKey(parts[0]), partKindFromKey(parts[1]),
                                        design_.bricks.empty() ? 0.0 : col, 0.0);
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
    arrangeBricks(design_);
    canvas_->fit();
    designChanged();
    status_->setText(QStringLiteral("Arranged symmetrically"));
}

std::vector<Entity> BrickWizardDialog::drawing() const {
    return buildBrickHarness(design_, &catalog_,
                             [this](const QString& id) { return bomItemOf(id); })
        .entities;
}

void BrickWizardDialog::designChanged() {
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
        if (sp && en && sp->type == BrickType::Fanout && design_.portOn(sp->id, en->id) == kPortRight) {
            panel_->setCurrentIndex(3);
            const QSignalBlocker block(linkWires_);
            linkText_->setText(QStringLiteral("The fanout %1 to %2.")
                                   .arg(brickTitle(*sp), brickTitle(*en)));
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
        const QSignalBlocker b1(part_), b2(name_), b3(balloon_);
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
        bomState_->setText(!p      ? QStringLiteral("Choose a library part first")
                           : item  ? QStringLiteral("✔ In the BOM as item %1").arg(item)
                                   : QStringLiteral("Not in the BOM"));
        return;
    }
    panel_->setCurrentIndex(2);
    const QSignalBlocker s1(wires_), s2(length_), s3(wireWidth_), s4(fanStart_), s5(fanFinish_),
        s6(knee_), s7(fanKnee_), s8(fanScale_), s9(transitions_), s10(bodyScale_), s11(bodyWidth_),
        s12(dimTotal_), s13(dimFans_), s14(dimBetween_);
    const BrickRunSpec& r = b->run;
    runKind_->setText(brickTypeName(b->type));
    wires_->setValue(r.wires);
    wires_->setEnabled(b->type != BrickType::TwistedPair);
    length_->setValue(r.lengthMm);
    wireWidth_->setValue(r.wireWidthMm);
    fanStart_->setValue(r.fanStartMm);
    fanFinish_->setValue(r.fanFinishMm);
    knee_->setValue(r.kneeRadiusMm);
    fanKnee_->setValue(r.fanKneeMm);
    fanScale_->setValue(r.fanScalePct);
    bodyScale_->setValue(r.bodyScalePct);
    bodyWidth_->setValue(r.bodyWidthMm);
    transitions_->setCurrentIndex(r.transitions == RunEnds::Start    ? 1
                                  : r.transitions == RunEnds::Finish ? 2
                                  : r.transitions == RunEnds::Both   ? 3
                                                                     : 0);
    dimTotal_->setChecked(r.dimTotal);
    dimFans_->setChecked(r.dimFans);
    dimBetween_->setChecked(r.dimBetween);
    const int keep = stack_->currentRow();
    stack_->clear();
    for (int i = r.stack.size() - 1; i >= 0; --i) {
        auto* it = new QListWidgetItem(layerName(r.stack[i]), stack_);
        if (i == r.stack.size() - 1) {
            QFont f = it->font();
            f.setBold(true);
            it->setFont(f);
            it->setText(it->text() + QStringLiteral("   (seen)"));
        }
    }
    stack_->setCurrentRow(std::clamp(keep, 0, stack_->count() - 1));
    loadLabels();
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
        auto* text = new QLineEdit(l.text, labels_);
        labels_->setCellWidget(i, 0, on);
        labels_->setCellWidget(i, 1, from);
        labels_->setCellWidget(i, 2, dist);
        labels_->setCellWidget(i, 3, text);
        auto write = [this, id, i, on, from, dist, text] {
            Brick* br = design_.brick(id);
            if (!br || i >= static_cast<int>(br->run.labels.size())) return;
            BrickLabel& lab = br->run.labels[i];
            lab.wire = on->currentData().toInt();
            lab.fromFinish = from->currentIndex() == 1;
            lab.distanceMm = dist->value();
            lab.text = text->text();
            refreshPreview();
        };
        connect(on, &QComboBox::currentIndexChanged, this, write);
        connect(from, &QComboBox::currentIndexChanged, this, write);
        connect(dist, &QDoubleSpinBox::valueChanged, this, write);
        connect(text, &QLineEdit::textEdited, this, write);
    }
}

void BrickWizardDialog::writeEnd() {
    Brick* b = design_.brick(canvas_->selected());
    if (!b || b->type != BrickType::End) return;
    b->libPart = part_->currentData().toString();
    b->name = name_->text();
    b->balloon = balloon_->isChecked();
    canvas_->update();
    refreshPreview();
}

void BrickWizardDialog::writeRun() {
    Brick* b = design_.brick(canvas_->selected());
    if (!b || !brickIsSpecial(b->type)) return;
    BrickRunSpec& r = b->run;
    r.wires = b->type == BrickType::TwistedPair ? 2 : wires_->value();
    r.lengthMm = length_->value();
    r.wireWidthMm = wireWidth_->value();
    r.fanStartMm = fanStart_->value();
    r.fanFinishMm = fanFinish_->value();
    r.kneeRadiusMm = knee_->value();
    r.fanKneeMm = fanKnee_->value();
    r.fanScalePct = fanScale_->value();
    r.bodyScalePct = bodyScale_->value();
    r.bodyWidthMm = bodyWidth_->value();
    r.transitions = transitions_->currentIndex() == 1   ? RunEnds::Start
                    : transitions_->currentIndex() == 2 ? RunEnds::Finish
                    : transitions_->currentIndex() == 3 ? RunEnds::Both
                                                        : RunEnds::None;
    r.dimTotal = dimTotal_->isChecked();
    r.dimFans = dimFans_->isChecked();
    r.dimBetween = dimBetween_->isChecked();
    canvas_->update();
    refreshPreview();
}

void BrickWizardDialog::refreshPreview() {
    std::vector<Entity> ents = drawing();
    const QStringList sel = canvas_->selection();
    for (Entity& e : ents)
        if (sel.contains(e.meta.value(QStringLiteral("brickId")).toString()))
            e.meta.insert(QLatin1String(kPreviewSelectedKey), true);
    preview_->setEntities(ents);
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
    return true;
}

void BrickWizardDialog::place() {
    std::vector<Entity> ents = drawing();
    if (ents.empty() || !editor_) {
        status_->setText(QStringLiteral("Nothing to place yet"));
        return;
    }
    for (Entity& e : ents) {
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
