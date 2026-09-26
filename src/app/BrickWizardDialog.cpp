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
#include <QToolButton>
#include <QResizeEvent>
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

// A SECTION OF THE RIGHT-HAND PANEL: a header that folds it away and back, and the section itself,
// in a splitter whose edges pull it taller or shorter.
QWidget* foldSection(const QString& title, QWidget* body, QSplitter* into, const char* name) {
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
    lay->addWidget(body, 1);
    QObject::connect(head, &QToolButton::toggled, sec, [sec, head, body](bool open) {
        body->setVisible(open);
        head->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        sec->setMaximumHeight(open ? QWIDGETSIZE_MAX : head->sizeHint().height() + 2);
    });
    into->addWidget(sec);
    return sec;
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

void BrickCanvas::setMinimap(bool on) {
    minimap_ = on;
    if (mapButton_->isChecked() != on) mapButton_->setChecked(on);
    update();
}

void BrickCanvas::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    mapButton_->move(width() - mapButton_->sizeHint().width() - 6, 6);
}

QRectF BrickCanvas::minimapRect() const {
    return QRectF(width() - 190.0, height() - 124.0, 180.0, 114.0);
}

QPointF BrickCanvas::viewCentreGrid() const {
    double c, r;
    toGrid(QPointF(width() / 2.0, height() / 2.0), c, r);
    return QPointF(c, r);
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

    // THE MAP: every brick small, and the part in view outlined.
    if (minimap_ && !d_->bricks.empty()) {
        QRectF all;
        for (const Brick& b : d_->bricks) all = all.isNull() ? brickRect(b) : all.united(brickRect(b));
        all = all.united(QRectF(rect()));
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
        p.drawRect(map(QRectF(rect())));
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
    pressAt_ = cursor_ = e->position();
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
        all = all.united(QRectF(rect()));
        const QRectF in = minimapRect().adjusted(6, 6, -6, -6);
        const double s = std::min(in.width() / all.width(), in.height() / all.height());
        const QPointF at = all.center() + (e->position() - in.center()) / s;   // on the pane
        origin_ += QPointF(width() / 2.0, height() / 2.0) - at;
        update();
        return;
    }
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
    bomAtCombo_->addItems({QStringLiteral("Left, a tab"), QStringLiteral("Left, bottom"),
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
    // THE BILL, LIVE: every line of the drawing's bill of materials as it stands, the parts this
    // harness uses in bold - so what adding a part did is seen as it is done. Where it sits is chosen.
    bom_ = new QTableWidget(0, 5, this);
    bom_->setObjectName(QStringLiteral("brickBom"));
    bom_->setHorizontalHeaderLabels({QStringLiteral("Item"), QStringLiteral("Qty"), QStringLiteral("Unit"),
                                     QStringLiteral("P/N"), QStringLiteral("Description")});
    bom_->horizontalHeader()->setStretchLastSection(true);
    bom_->verticalHeader()->setVisible(false);
    bom_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bom_->setSelectionMode(QAbstractItemView::NoSelection);
    connect(canvas_, &BrickCanvas::selectionChanged, this, [this](const QString&) {
        loadPanel();
        refreshPreview();
    });
    connect(canvas_, &BrickCanvas::linkPicked, this, [this](const QString&, const QString&) { loadPanel(); });
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
        const int at = std::clamp(QSettings().value(QStringLiteral("brickWizard/bomAt"), 0).toInt(), 0, 4);
        const QSignalBlocker block(bomAtCombo_);
        bomAtCombo_->setCurrentIndex(at);
        placeBom(at);
    }
    refreshBom();
}

QWidget* BrickWizardDialog::buildEndPage() {
    auto* sections = new QSplitter(Qt::Vertical, panel_);
    sections->setObjectName(QStringLiteral("brickEndSections"));
    sections->setChildrenCollapsible(false);
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
    params_->setMinimumHeight(80);
    foldSection(QStringLiteral("Part"), scrolled(page), sections, "brickFoldPart");
    foldSection(QStringLiteral("Parameters"), params_, sections, "brickFoldParams");
    sections->setStretchFactor(0, 2);
    sections->setStretchFactor(1, 3);
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
    return sections;
}

QWidget* BrickWizardDialog::buildRunPage() {
    auto* sections = new QSplitter(Qt::Vertical, panel_);
    sections->setObjectName(QStringLiteral("brickRunSections"));
    sections->setChildrenCollapsible(false);
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
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
    fanStart_ = spin("brickFanStart", 0.0, 1000.0, 1.0, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Fan length, start"), withAuto(fanStart_, "brickFanStartAuto"));
    fanFinish_ = spin("brickFanFinish", 0.0, 1000.0, 1.0, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Fan length, finish"), withAuto(fanFinish_, "brickFanFinishAuto"));
    knee_ = spin("brickKnee", 0.0, 1000.0, 1.0, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Bend radius, body"), withAuto(knee_, "brickKneeAuto"));
    fanKnee_ = spin("brickFanKnee", 0.0, 1000.0, 0.5, QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Bend radius, fans"), withAuto(fanKnee_, "brickFanKneeAuto"));
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
    for (QDoubleSpinBox* s : {length_, wireWidth_, fanStart_, fanFinish_, knee_, fanKnee_, fanScale_,
                              bodyScale_, bodyWidth_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this, s](double) {
            // A figure typed in takes it over from Auto.
            if (QToolButton* a = autoOf_.value(s)) {
                const QSignalBlocker block(a);
                a->setChecked(false);
            }
            writeRun();
        });
    foldSection(QStringLiteral("Run"), scrolled(page), sections, "brickFoldRun");
    connect(wires_, &QSpinBox::valueChanged, this, [this](int) {
        writeRun();
        loadLabels();   // the wires a label can go on
        loadNets();
    });
    connect(transitions_, &QComboBox::currentIndexChanged, this, [this](int) { writeRun(); });

    // THE WIRES AND THEIR NETS, numbered from the top down as the labels are. A net is picked from
    // the schematic's - those landing on the parts this run is linked to - or typed in.
    auto* netBox = new QGroupBox(QStringLiteral("Wires and nets"), page);
    auto* nv = new QVBoxLayout(netBox);
    nets_ = new QTableWidget(0, 2, netBox);
    nets_->setObjectName(QStringLiteral("brickNets"));
    nets_->setHorizontalHeaderLabels({QStringLiteral("Wire"), QStringLiteral("Net")});
    nets_->horizontalHeader()->setStretchLastSection(true);
    nets_->verticalHeader()->setVisible(false);
    nets_->setMinimumHeight(60);
    nv->addWidget(nets_);
    netBox->setTitle(QString());
    foldSection(QStringLiteral("Wires and nets"), netBox, sections, "brickFoldNets");

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
    stackBox->setTitle(QString());
    foldSection(QStringLiteral("Stack — from the outside in"), stackBox, sections, "brickFoldStack");
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
    int dr = 0;
    for (auto [c, e] : {std::pair<QCheckBox*, QLineEdit*>{dimTotal_, dimTextTotal_}, {dimFans_, dimTextFans_},
                        {dimBetween_, dimTextBetween_}}) {
        dv->addWidget(c, dr, 0);
        dv->addWidget(e, dr, 1);
        ++dr;
        connect(c, &QCheckBox::toggled, this, [this](bool) { writeRun(); });
        connect(e, &QLineEdit::textEdited, this, [this](const QString&) { writeRun(); });
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
    dv->addLayout(styleRow, dr, 0, 1, 2);
    for (QCheckBox* c : {dimLinear_, dimTrace_, dimSingle_})
        connect(c, &QCheckBox::toggled, this, [this](bool) {
            dimTrace_->setVisible(!dimLinear_->isChecked());
            writeRun();
        });
    dims->setTitle(QString());
    foldSection(QStringLiteral("Dimensions"), dims, sections, "brickFoldDims");

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
    labels_->setMinimumHeight(60);
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
    foldSection(QStringLiteral("Labels"), labBox, sections, "brickFoldLabels");
    // The run's own figures get most of the height to begin with; the rest pull out as needed.
    sections->setStretchFactor(0, 5);
    for (int i = 1; i < sections->count(); ++i) sections->setStretchFactor(i, 2);
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
    return sections;
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
    if (parts.size() != 2) return QString();
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
    // THE RUN'S OWN FIGURES get most of the height, the first time the page has a height to share.
    if (!runSized_) {
        if (auto* sp = findChild<QSplitter*>(QStringLiteral("brickRunSections")); sp && sp->height() > 200) {
            const int h = sp->height();
            sp->setSizes({int(h * 0.44), int(h * 0.12), int(h * 0.13), int(h * 0.14), int(h * 0.17)});
            runSized_ = true;
        }
    }
    const QSignalBlocker s1(wires_), s2(length_), s3(wireWidth_), s4(fanStart_), s5(fanFinish_),
        s6(knee_), s7(fanKnee_), s8(fanScale_), s9(transitions_), s10(bodyScale_), s11(bodyWidth_),
        s12(dimTotal_), s13(dimFans_), s14(dimBetween_);
    const BrickRunSpec& r = b->run;
    runKind_->setText(brickTypeName(b->type));
    wires_->setValue(r.wires);
    wires_->setEnabled(b->type != BrickType::TwistedPair);
    length_->setValue(r.lengthMm);
    wireWidth_->setValue(r.wireWidthMm);
    // Each Auto figure: "A" lit when left alone, the field its actual size then (showActuals).
    for (auto [spinBox, value, isAuto] : {std::tuple<QDoubleSpinBox*, double, bool>{fanStart_, r.fanStartMm, r.fanStartMm < 0.0},
                                          {fanFinish_, r.fanFinishMm, r.fanFinishMm < 0.0},
                                          {knee_, r.kneeRadiusMm, r.kneeRadiusMm < 0.0},
                                          {fanKnee_, r.fanKneeMm, r.fanKneeMm < 0.0},
                                          {bodyWidth_, r.bodyWidthMm, r.bodyWidthMm <= 1e-9}}) {
        QToolButton* a = autoOf_.value(spinBox);
        const QSignalBlocker ba(a);
        a->setChecked(isAuto);
        if (!isAuto) spinBox->setValue(value);
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
    dimTotal_->setChecked(r.dimTotal);
    dimFans_->setChecked(r.dimFans);
    dimBetween_->setChecked(r.dimBetween);
    for (auto [e, t] : {std::pair<QLineEdit*, QString>{dimTextTotal_, r.dimTextTotal}, {dimTextFans_, r.dimTextFans},
                        {dimTextBetween_, r.dimTextBetween}}) {
        const QSignalBlocker block(e);
        if (e->text() != t) e->setText(t);
    }
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
    loadNets();
    showActuals();
}

void BrickWizardDialog::showActuals() {
    const Brick* b = design_.brick(canvas_->selected());
    if (!b || !brickIsSpecial(b->type) || !actual_.contains(b->id)) return;
    const BrickActual a = actual_.value(b->id);
    for (auto [spinBox, value] : {std::pair<QDoubleSpinBox*, double>{fanStart_, a.fanStart},
                                  {fanFinish_, a.fanFinish},
                                  {knee_, a.knee},
                                  {fanKnee_, a.fanKnee},
                                  {bodyWidth_, a.bodyWidth}}) {
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
    auto manual = [this](QDoubleSpinBox* s) { return !autoOf_.value(s)->isChecked(); };
    r.fanStartMm = manual(fanStart_) ? fanStart_->value() : -1.0;
    r.fanFinishMm = manual(fanFinish_) ? fanFinish_->value() : -1.0;
    r.kneeRadiusMm = manual(knee_) ? knee_->value() : -1.0;
    r.fanKneeMm = manual(fanKnee_) ? fanKnee_->value() : -1.0;
    r.fanScalePct = fanScale_->value();
    r.bodyScalePct = bodyScale_->value();
    r.bodyWidthMm = manual(bodyWidth_) ? bodyWidth_->value() : 0.0;
    r.dimLinear = dimLinear_->isChecked();
    r.dimTrace = dimTrace_->isChecked();
    r.dimSingle = dimSingle_->isChecked();
    r.transitions = transitions_->currentIndex() == 1   ? RunEnds::Start
                    : transitions_->currentIndex() == 2 ? RunEnds::Finish
                    : transitions_->currentIndex() == 3 ? RunEnds::Both
                                                        : RunEnds::None;
    r.dimTotal = dimTotal_->isChecked();
    r.dimFans = dimFans_->isChecked();
    r.dimBetween = dimBetween_->isChecked();
    r.dimTextTotal = dimTextTotal_->text().trimmed();
    r.dimTextFans = dimTextFans_->text().trimmed();
    r.dimTextBetween = dimTextBetween_->text().trimmed();
    canvas_->update();
    refreshPreview();
}

void BrickWizardDialog::refreshPreview() {
    if (editor_) design_.faceFanPct = editor_->faceFanScale();
    const BrickBuild built = buildBrickHarness(design_, &catalog_,
                                               [this](const QString& id) { return bomItemOf(id); });
    actual_ = built.actual;
    std::vector<Entity> ents = built.entities;
    const QStringList sel = canvas_->selection();
    for (Entity& e : ents)
        if (sel.contains(e.meta.value(QStringLiteral("brickId")).toString()))
            e.meta.insert(QLatin1String(kPreviewSelectedKey), true);
    preview_->setEntities(ents);
    refreshBom();
    showActuals();
}

void BrickWizardDialog::placeBom(int where) {
    if (!bom_ || where < 0 || where > 4) return;
    // Out of wherever it was.
    if (const int t = leftTabs_->indexOf(bom_); t >= 0) leftTabs_->removeTab(t);
    switch (where) {
    case 0: leftTabs_->addTab(bom_, QStringLiteral("BOM")); break;
    case 1: leftSplit_->addWidget(bom_); break;
    case 2: middle_->addWidget(bom_); break;
    case 3: middle_->insertWidget(0, bom_); break;
    default: rightSplit_->addWidget(bom_); break;
    }
    bom_->show();
    bomAt_ = where;
    QSettings().setValue(QStringLiteral("brickWizard/bomAt"), where);
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
    const bool added = addPartToBom(partId);
    status_->setText(added ? QStringLiteral("%1 is %2, and on the BOM").arg(brickTitle(*b), p->title())
                           : QStringLiteral("%1 is %2").arg(brickTitle(*b), p->title()));
    canvas_->update();
    loadPanel();
    refreshPreview();
    return true;
}

void BrickWizardDialog::keyPressEvent(QKeyEvent* e) {
    // Enter finishes typing, and nothing else: it does not press a button or close the window.
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
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

void BrickWizardDialog::refreshBom() {
    if (!bom_) return;
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
        dimFrom_->setCurrentIndex(design_.dimFromFar ? 1 : 0);
        dimTo_->setCurrentIndex(design_.dimToFar ? 1 : 0);
    }
    currentName_ = name;
    harnessName_->setText(name);
    canvas_->setSelection({});
    canvas_->fit();
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
