#include "app/SystemCanvas.h"
#include "system/SystemExport.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

constexpr double kPortSize = 8.0;

double segDistance(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF e = b - a;
    const double ll = e.x() * e.x() + e.y() * e.y();
    const double t = ll < 1e-12 ? 0.0 : std::clamp(((p.x() - a.x()) * e.x() + (p.y() - a.y()) * e.y()) / ll, 0.0, 1.0);
    const QPointF q = a + e * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

QColor stateColour(SysLinkState s) {
    switch (s) {
    case SysLinkState::Match: return QColor(60, 180, 90);
    case SysLinkState::Mismatch:
    case SysLinkState::Missing: return QColor(220, 60, 50);
    case SysLinkState::None: break;
    }
    return QColor(230, 160, 40);
}

// WHAT A UNIT IS, as a mark in its corner: a hexagon nut for a mechanical part, a green board with a chip for a PCBA.
void drawKind(QPainter& p, const QRectF& box, const QString& kind) {
    p.save();
    if (kind == QLatin1String("pcba")) {
        p.setPen(QPen(QColor(20, 90, 50), 1.0));
        p.setBrush(QColor(46, 150, 80));
        p.drawRoundedRect(box, 2, 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 30, 34));
        p.drawRect(QRectF(box.center().x() - box.width() * 0.22, box.center().y() - box.height() * 0.22, box.width() * 0.44,
                          box.height() * 0.44));
        p.setBrush(QColor(225, 180, 60));
        for (int i = 0; i < 3; ++i) {
            const double y = box.top() + box.height() * (0.25 + 0.25 * i);
            p.drawRect(QRectF(box.left() + 1.5, y - 1, 2.5, 2));
            p.drawRect(QRectF(box.right() - 4, y - 1, 2.5, 2));
        }
    } else {
        QPolygonF hex;
        const QPointF c = box.center();
        const double r = std::min(box.width(), box.height()) / 2.0;
        for (int i = 0; i < 6; ++i) hex << c + QPointF(r * std::cos(M_PI / 3.0 * i), r * std::sin(M_PI / 3.0 * i));
        p.setPen(QPen(QColor(70, 78, 90), 1.0));
        p.setBrush(QColor(170, 178, 190));
        p.drawPolygon(hex);
        p.setBrush(QColor(240, 242, 246));
        p.drawEllipse(c, r * 0.42, r * 0.42);
    }
    p.restore();
}

} // namespace

SystemCanvas::SystemCanvas(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("systemCanvas"));
    setMinimumSize(300, 200);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setToolTip(QStringLiteral("Unit (U): click to put a unit. Port (P): click a unit's side. Cable (H): drag from a port or a unit "
                              "to another - a port is put where there is none. Note (N): click to put a note. Select: drag a "
                              "unit, a note, a port along the sides, or a unit's corner or edge to size it. Wheel: zoom; right or "
                              "middle drag: move the view; Delete: delete"));
}

void SystemCanvas::setDesign(SystemDesign* design) {
    design_ = design;
    selUnit_.clear();
    selPort_.clear();
    selCable_.clear();
    selNote_.clear();
    update();
}

void SystemCanvas::setTool(SysTool tool) {
    const bool changedTool = tool != tool_;
    tool_ = tool;
    setCursor(tool == SysTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    if (changedTool) emit toolChanged(tool);
}

void SystemCanvas::select(const QString& unit, const QString& port, const QString& cable) {
    selUnit_ = unit;
    selPort_ = port;
    selCable_ = cable;
    selNote_.clear();
    update();
    emit selectionChanged();
}

void SystemCanvas::selectNote(const QString& note) {
    selUnit_.clear();
    selPort_.clear();
    selCable_.clear();
    selNote_ = note;
    update();
    emit selectionChanged();
}

QPointF SystemCanvas::toScreen(const QPointF& w) const { return w * scale_ + offset_; }

QPointF SystemCanvas::toWorld(const QPointF& s) const { return (s - offset_) / scale_; }

QPointF SystemCanvas::portPoint(const SysUnit& u, const SysPort& p) const { return sysPortPoint(u, p); }

QPointF SystemCanvas::portOut(const SysPort& p) const { return sysPortOut(p); }

double SystemCanvas::portHalf() const { return kPortSize * std::clamp(scale_, 0.6, 1.6) / 2.0 / scale_; }

bool SystemCanvas::inside(const QString& inner, const QString& outer) const {
    const SysUnit* a = design_ ? design_->unit(inner) : nullptr;
    const SysUnit* b = design_ ? design_->unit(outer) : nullptr;
    return a && b && inner != outer && QRectF(b->x, b->y, b->w, b->h).contains(QRectF(a->x, a->y, a->w, a->h));
}

// SQUARE: straight out of each port - out of its outside, or in from its inside, beyond its square - then across and
// along to meet (as the exports draw it).
std::vector<QPointF> SystemCanvas::cablePath(const SysCable& c) const { return sysCablePath(*design_, c, 2.0 * portHalf()); }

QString SystemCanvas::unitAt(const QPointF& w) const {
    if (!design_) return QString();
    // The innermost first: a unit inside another is drawn over it.
    QString best;
    double area = 1e300;
    for (const SysUnit& u : design_->units)
        if (QRectF(u.x, u.y, u.w, u.h).contains(w) && u.w * u.h < area) {
            area = u.w * u.h;
            best = u.id;
        }
    return best;
}

QString SystemCanvas::noteAt(const QPointF& w) const {
    if (!design_) return QString();
    for (auto it = design_->notes.rbegin(); it != design_->notes.rend(); ++it)
        if (QRectF(it->x, it->y, it->w, it->h).contains(w)) return it->id;
    return QString();
}

bool SystemCanvas::portAt(const QPointF& w, QString& unit, QString& port) const {
    if (!design_) return false;
    const double reach = std::max(6.0, 8.0 / scale_);
    double best = reach;
    for (const SysUnit& u : design_->units)
        for (const SysPort& p : u.ports) {
            const QPointF q = portPoint(u, p);
            const double d = std::hypot(q.x() - w.x(), q.y() - w.y());
            if (d < best) {
                best = d;
                unit = u.id;
                port = p.id;
            }
        }
    return best < reach;
}

QString SystemCanvas::cableAt(const QPointF& w) const {
    if (!design_) return QString();
    const double reach = 5.0 / scale_;
    QString best;
    double nearest = reach;
    for (const SysCable& c : design_->cables) {
        const std::vector<QPointF> p = cablePath(c);
        for (size_t i = 1; i < p.size(); ++i) {
            const double d = segDistance(w, p[i - 1], p[i]);
            if (d < nearest) {
                nearest = d;
                best = c.id;
            }
        }
    }
    return best;
}

void SystemCanvas::nearestSide(const SysUnit& u, const QPointF& w, QString& side, double& at) {
    const double dl = std::abs(w.x() - u.x), dr = std::abs(u.x + u.w - w.x());
    const double dt = std::abs(w.y() - u.y), db = std::abs(u.y + u.h - w.y());
    const double least = std::min({dl, dr, dt, db});
    side = least == dl ? QStringLiteral("left") : least == dr ? QStringLiteral("right") : least == dt ? QStringLiteral("top")
                                                                                                      : QStringLiteral("bottom");
    at = side == QLatin1String("left") || side == QLatin1String("right") ? (w.y() - u.y) / std::max(1e-9, u.h)
                                                                         : (w.x() - u.x) / std::max(1e-9, u.w);
    at = std::clamp(at, 0.05, 0.95);
}

int SystemCanvas::sizeHandleAt(const QPointF& w, QString& unit) const {
    if (!design_) return 0;
    const double reach = 6.0 / scale_;
    for (auto it = design_->units.rbegin(); it != design_->units.rend(); ++it) {
        const SysUnit& u = *it;
        const double r = u.x + u.w, b = u.y + u.h;
        const bool nearRight = std::abs(w.x() - r) <= reach && w.y() >= u.y - reach && w.y() <= b + reach;
        const bool nearBottom = std::abs(w.y() - b) <= reach && w.x() >= u.x - reach && w.x() <= r + reach;
        if (nearRight || nearBottom) {
            unit = u.id;
            return nearRight && nearBottom ? 3 : nearRight ? 1 : 2;
        }
    }
    return 0;
}

QPointF SystemCanvas::portOnScreen(const QString& unit, const QString& port) const {
    const SysUnit* u = design_ ? design_->unit(unit) : nullptr;
    const SysPort* p = u ? u->port(port) : nullptr;
    return u && p ? toScreen(portPoint(*u, *p)) : QPointF();
}

QPointF SystemCanvas::unitOnScreen(const QString& unit) const {
    const SysUnit* u = design_ ? design_->unit(unit) : nullptr;
    return u ? toScreen(QPointF(u->x + u->w / 2.0, u->y + u->h / 2.0)) : QPointF();
}

QPointF SystemCanvas::unitCornerOnScreen(const QString& unit) const {
    const SysUnit* u = design_ ? design_->unit(unit) : nullptr;
    return u ? toScreen(QPointF(u->x + u->w, u->y + u->h)) : QPointF();
}

QPointF SystemCanvas::noteOnScreen(const QString& note) const {
    const SysNote* n = design_ ? design_->note(note) : nullptr;
    return n ? toScreen(QPointF(n->x + n->w / 2.0, n->y + n->h / 2.0)) : QPointF();
}

QPointF SystemCanvas::cableOnScreen(const QString& cable) const {
    const SysCable* c = design_ ? design_->cable(cable) : nullptr;
    if (!c) return {};
    const std::vector<QPointF> p = cablePath(*c);
    return p.size() >= 4 ? toScreen((p[p.size() / 2 - 1] + p[p.size() / 2]) / 2.0) : QPointF();
}

void SystemCanvas::fit() {
    if (!design_ || (design_->units.empty() && design_->notes.empty())) {
        scale_ = 1.0;
        offset_ = {40.0, 40.0};
        update();
        return;
    }
    QRectF box;
    for (const SysUnit& u : design_->units) box = box.united(QRectF(u.x, u.y, u.w, u.h));
    for (const SysNote& n : design_->notes) box = box.united(QRectF(n.x, n.y, n.w, n.h));
    box.adjust(-60, -60, 60, 60);
    scale_ = std::clamp(std::min(width() / box.width(), height() / box.height()), 0.2, 3.0);
    offset_ = QPointF(width() / 2.0, height() / 2.0) - box.center() * scale_;
    update();
}

void SystemCanvas::deleteSelected() {
    if (!design_ || (selUnit_.isEmpty() && selCable_.isEmpty() && selNote_.isEmpty())) return;
    emit aboutToChange();
    if (!selNote_.isEmpty()) design_->removeNote(selNote_);
    else if (!selCable_.isEmpty()) design_->removeCable(selCable_);
    else if (!selPort_.isEmpty()) design_->removePort(selUnit_, selPort_);
    else design_->removeUnit(selUnit_);
    select(QString(), QString(), QString());
    emit changed();
}

void SystemCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(250, 251, 252));
    // A GRID, faint, every 20 of the diagram's units.
    {
        p.setPen(QPen(QColor(232, 235, 240), 1.0));
        const double step = 20.0 * scale_;
        if (step > 6.0) {
            for (double x = std::fmod(offset_.x(), step); x < width(); x += step) p.drawLine(QPointF(x, 0), QPointF(x, height()));
            for (double y = std::fmod(offset_.y(), step); y < height(); y += step) p.drawLine(QPointF(0, y), QPointF(width(), y));
        }
    }
    if (!design_) return;
    QFont f = font();
    f.setPointSizeF(std::clamp(9.0 * scale_, 6.0, 16.0));
    p.setFont(f);
    // THE UNITS: a box - the biggest first, so a unit inside another is drawn over it - its mark for what it is in its
    // corner, its name in it, its part number and revision under the name.
    std::vector<const SysUnit*> order;
    for (const SysUnit& u : design_->units) order.push_back(&u);
    std::stable_sort(order.begin(), order.end(), [](const SysUnit* a, const SysUnit* b) { return a->w * a->h > b->w * b->h; });
    for (const SysUnit* up : order) {
        const SysUnit& u = *up;
        const QRectF r(toScreen(QPointF(u.x, u.y)), QSizeF(u.w * scale_, u.h * scale_));
        const bool on = u.id == selUnit_ && selPort_.isEmpty();
        p.setPen(QPen(on ? QColor(40, 140, 230) : QColor(90, 98, 110), on ? 2.5 : 1.5));
        p.setBrush(u.color.isValid() ? u.color : u.kind == QLatin1String("pcba") ? QColor(226, 242, 230) : QColor(232, 238, 246));
        p.drawRoundedRect(r, 5, 5);
        const double mark = 14.0 * std::clamp(scale_, 0.7, 1.5);
        drawKind(p, QRectF(r.left() + 5, r.top() + 5, mark, mark), u.kind);
        QFont bold = f;
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(QColor(25, 30, 36));
        p.drawText(r.adjusted(6, 4, -6, -r.height() / 2), Qt::AlignHCenter | Qt::AlignBottom, u.name);
        p.setFont(f);
        QStringList sub;
        if (!u.pn.isEmpty()) sub << u.pn;
        if (!u.rev.isEmpty()) sub << QStringLiteral("rev ") + u.rev;
        p.setPen(QColor(80, 88, 100));
        if (!sub.isEmpty()) p.drawText(r.adjusted(6, r.height() / 2 + 2, -6, -4), Qt::AlignHCenter | Qt::AlignTop, sub.join(QStringLiteral("  ")));
        // Selected: its corner to size it by.
        if (on) {
            p.setPen(QPen(QColor(40, 140, 230), 1.0));
            p.setBrush(QColor(255, 255, 255));
            p.drawRect(QRectF(r.right() - 4, r.bottom() - 4, 8, 8));
        }
    }
    // THE CABLES, each labelled at its middle with a dot for how its drawing stands.
    for (const SysCable& c : design_->cables) {
        const std::vector<QPointF> path = cablePath(c);
        if (path.size() < 2) continue;
        const bool on = c.id == selCable_;
        QPolygonF line;
        for (const QPointF& q : path) line << toScreen(q);
        p.setPen(QPen(on ? QColor(40, 140, 230) : c.color.isValid() ? c.color : QColor(60, 66, 76), on ? 3.0 : 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(line);
        const QPointF mid = (line[line.size() / 2 - 1] + line[line.size() / 2]) / 2.0;
        QString label = c.name;
        if (!c.pn.isEmpty()) label += QStringLiteral("  ") + c.pn;
        if (!c.rev.isEmpty()) label += QStringLiteral("  rev ") + c.rev;
        const QRectF text = p.fontMetrics().boundingRect(label).adjusted(-4, -2, 14, 2);
        QRectF box(mid - QPointF(text.width() / 2.0, text.height() / 2.0), text.size());
        p.setPen(QPen(QColor(180, 186, 196), 1.0));
        p.setBrush(QColor(255, 255, 255, 235));
        p.drawRoundedRect(box, 3, 3);
        p.setPen(QColor(30, 34, 40));
        p.drawText(box.adjusted(4, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
        p.setPen(Qt::NoPen);
        p.setBrush(stateColour(SystemDesign::linkState(c)));
        p.drawEllipse(QPointF(box.right() - 7, box.center().y()), 3.5, 3.5);
    }
    // THE CABLE BEING DRAWN.
    if (drag_ == Drag::Cable) {
        QPointF from;
        if (const SysUnit* u = design_->unit(fromUnit_)) {
            if (fromNew_) {
                SysPort np;
                np.side = fromSide_;
                np.at = fromAt_;
                from = toScreen(portPoint(*u, np));
            } else if (const SysPort* pt = u->port(fromPort_)) {
                from = toScreen(portPoint(*u, *pt));
            }
            p.setPen(QPen(QColor(40, 140, 230), 2.0, Qt::DashLine));
            p.drawLine(from, rubber_);
        }
    }
    // THE PORTS: each a connector through its unit's wall - its inside a square just inside the side, filled where a
    // cable is plugged into it from within; its outside a square just outside, there once a cable is plugged in from
    // outside - and its name inside the unit.
    for (const SysUnit& u : design_->units)
        for (const SysPort& pt : u.ports) {
            const QPointF c = toScreen(portPoint(u, pt));
            const QPointF out = portOut(pt);
            const bool pon = u.id == selUnit_ && pt.id == selPort_;
            const double s = kPortSize * std::clamp(scale_, 0.6, 1.6);
            const bool inUsed = !design_->cablesAt(u.id, pt.id, true).empty();
            const bool outUsed = !design_->cablesAt(u.id, pt.id, false).empty();
            p.setPen(QPen(pon ? QColor(40, 140, 230) : QColor(60, 66, 76), pon ? 2.0 : 1.2));
            const QPointF ic = c - out * (s / 2.0);
            p.setBrush(inUsed ? QColor(70, 80, 95) : QColor(255, 255, 255));
            p.drawRect(QRectF(ic.x() - s / 2, ic.y() - s / 2, s, s));
            if (outUsed) {
                const QPointF oc = c + out * (s / 2.0);
                p.setBrush(QColor(70, 80, 95));
                p.drawRect(QRectF(oc.x() - s / 2, oc.y() - s / 2, s, s));
            }
            const QPointF at = c - out * (s + 4);
            const QRectF nameBox = pt.side == QLatin1String("left")    ? QRectF(at.x(), at.y() - 8, 60, 16)
                                   : pt.side == QLatin1String("right") ? QRectF(at.x() - 60, at.y() - 8, 60, 16)
                                   : pt.side == QLatin1String("top")   ? QRectF(at.x() - 30, at.y(), 60, 16)
                                                                       : QRectF(at.x() - 30, at.y() - 16, 60, 16);
            const Qt::Alignment align = pt.side == QLatin1String("left")    ? (Qt::AlignLeft | Qt::AlignVCenter)
                                        : pt.side == QLatin1String("right") ? (Qt::AlignRight | Qt::AlignVCenter)
                                                                            : Qt::AlignCenter;
            p.setPen(QColor(50, 56, 66));
            p.drawText(nameBox, align, pt.name);
        }
    // THE NOTES, over everything: a pale yellow box with its text.
    for (const SysNote& n : design_->notes) {
        const QRectF r(toScreen(QPointF(n.x, n.y)), QSizeF(n.w * scale_, n.h * scale_));
        const bool on = n.id == selNote_;
        p.setPen(QPen(on ? QColor(40, 140, 230) : QColor(200, 170, 60), on ? 2.0 : 1.0));
        p.setBrush(QColor(255, 247, 190));
        p.drawRect(r);
        p.setPen(QColor(60, 52, 20));
        p.drawText(r.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, n.text);
    }
    if (design_->units.empty() && design_->notes.empty()) {
        p.setPen(QColor(120, 128, 140));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("The system diagram is empty - choose Unit on the ribbon (or press U) and click here to put the "
                                  "first unit"));
    }
}

void SystemCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    lastScreen_ = e->position();
    pressWorld_ = toWorld(e->position());
    moved_ = false;
    moveAnnounced_ = false;
    fromNew_ = false;
    if (!design_) return;
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    QString unit, port;
    switch (tool_) {
    case SysTool::Unit: {
        emit aboutToChange();
        const QString id = design_->addUnit(std::round((pressWorld_.x() - 80.0) / 10.0) * 10.0, std::round((pressWorld_.y() - 50.0) / 10.0) * 10.0);
        if (SysUnit* u = design_->unit(id)) u->kind = newKind_;
        select(id, QString(), QString());
        emit changed();
        return;
    }
    case SysTool::Note: {
        emit aboutToChange();
        const QString id = design_->addNote(std::round(pressWorld_.x() / 10.0) * 10.0, std::round(pressWorld_.y() / 10.0) * 10.0);
        selectNote(id);
        emit changed();
        return;
    }
    case SysTool::Port: {
        const QString id = unitAt(pressWorld_);
        const SysUnit* u = design_->unit(id);
        if (!u) return;
        // THE NEAREST SIDE, so far along it.
        QString side;
        double at = 0.5;
        nearestSide(*u, pressWorld_, side, at);
        emit aboutToChange();
        const QString p = design_->addPort(id, side, at);
        select(id, p, QString());
        emit changed();
        return;
    }
    case SysTool::Cable:
        // FROM A PORT - or from a unit where it has none: a port put there with the cable.
        if (portAt(pressWorld_, unit, port)) {
            drag_ = Drag::Cable;
            fromUnit_ = unit;
            fromPort_ = port;
            rubber_ = e->position();
            return;
        }
        if (const QString id = unitAt(pressWorld_); !id.isEmpty()) {
            drag_ = Drag::Cable;
            fromUnit_ = id;
            fromPort_.clear();
            fromNew_ = true;
            nearestSide(*design_->unit(id), pressWorld_, fromSide_, fromAt_);
            rubber_ = e->position();
        }
        return;
    case SysTool::Select: {
        // A PORT: slid along its unit's sides.
        if (portAt(pressWorld_, unit, port)) {
            drag_ = Drag::Port;
            select(unit, port, QString());
            return;
        }
        // A UNIT'S CORNER OR EDGE: sized.
        QString sized;
        if (const int how = sizeHandleAt(pressWorld_, sized); how != 0) {
            drag_ = Drag::Size;
            sizeHow_ = how;
            select(sized, QString(), QString());
            return;
        }
        if (const QString n = noteAt(pressWorld_); !n.isEmpty()) {
            const SysNote* note = design_->note(n);
            drag_ = Drag::Note;
            grabOffset_ = pressWorld_ - QPointF(note->x, note->y);
            selectNote(n);
            return;
        }
        if (const QString id = unitAt(pressWorld_); !id.isEmpty()) {
            const SysUnit* u = design_->unit(id);
            drag_ = Drag::Unit;
            grabOffset_ = pressWorld_ - QPointF(u->x, u->y);
            select(id, QString(), QString());
            return;
        }
        if (const QString c = cableAt(pressWorld_); !c.isEmpty()) {
            select(QString(), QString(), c);
            return;
        }
        select(QString(), QString(), QString());
        return;
    }
    }
}

void SystemCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (!design_) return;
    const QPointF delta = e->position() - lastScreen_;
    if ((e->position() - lastScreen_).manhattanLength() > 0.0 && drag_ != Drag::None) moved_ = true;
    lastScreen_ = e->position();
    // THE FIRST MOVE OF A DRAG: how things were, kept for Undo.
    auto announce = [this] {
        if (!moveAnnounced_) {
            emit aboutToChange();
            moveAnnounced_ = true;
        }
    };
    const QPointF w = toWorld(e->position());
    switch (drag_) {
    case Drag::Pan:
        offset_ += delta;
        update();
        break;
    case Drag::Unit:
        if (SysUnit* u = design_->unit(selUnit_)) {
            if (!moved_) break;
            announce();
            const QPointF to = w - grabOffset_;
            // On the grid, ten units at a time.
            u->x = std::round(to.x() / 10.0) * 10.0;
            u->y = std::round(to.y() / 10.0) * 10.0;
            update();
        }
        break;
    case Drag::Note:
        if (SysNote* n = design_->note(selNote_)) {
            if (!moved_) break;
            announce();
            const QPointF to = w - grabOffset_;
            n->x = std::round(to.x() / 10.0) * 10.0;
            n->y = std::round(to.y() / 10.0) * 10.0;
            update();
        }
        break;
    case Drag::Size:
        if (SysUnit* u = design_->unit(selUnit_)) {
            if (!moved_) break;
            announce();
            // ITS PORTS where they were along its sides - so far along each, the side longer or shorter.
            if (sizeHow_ & 1) u->w = std::max(60.0, std::round((w.x() - u->x) / 10.0) * 10.0);
            if (sizeHow_ & 2) u->h = std::max(40.0, std::round((w.y() - u->y) / 10.0) * 10.0);
            update();
        }
        break;
    case Drag::Port:
        if (SysUnit* u = design_->unit(selUnit_)) {
            SysPort* pt = u->port(selPort_);
            if (!pt || !moved_) break;
            announce();
            // ALONG ITS UNIT'S SIDES: to the side nearest the pointer, so far along it.
            nearestSide(*u, w, pt->side, pt->at);
            update();
        }
        break;
    case Drag::Cable:
        rubber_ = e->position();
        update();
        break;
    case Drag::None: {
        QString unit, port, sized;
        const int how = tool_ == SysTool::Select ? sizeHandleAt(w, sized) : 0;
        setCursor(portAt(w, unit, port) ? (tool_ == SysTool::Select ? Qt::SizeAllCursor : Qt::CrossCursor)
                  : how == 3                                        ? Qt::SizeFDiagCursor
                  : how == 1                                        ? Qt::SizeHorCursor
                  : how == 2                                        ? Qt::SizeVerCursor
                  : (!unitAt(w).isEmpty() || !noteAt(w).isEmpty()) && tool_ == SysTool::Select ? Qt::SizeAllCursor
                  : tool_ == SysTool::Select                                                   ? Qt::ArrowCursor
                                                                                               : Qt::CrossCursor);
        break;
    }
    }
}

void SystemCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (!design_) return;
    const Drag was = drag_;
    drag_ = Drag::None;
    if ((was == Drag::Unit || was == Drag::Note || was == Drag::Size || was == Drag::Port) && moveAnnounced_) {
        // Moved, sized or slid: one change - how it was before kept for Undo by the view, at the first move.
        emit changed();
        return;
    }
    if (was == Drag::Cable) {
        QString unit, port;
        const QPointF w = toWorld(e->position());
        const bool onPort = portAt(w, unit, port);
        const QString onUnit = onPort ? unit : unitAt(w);
        const bool same = onPort && unit == fromUnit_ && port == fromPort_ && !fromNew_;
        // Let go on a port, or on another unit (a port put there); on its own unit's body, or on nothing: given up.
        if (moved_ && !same && (onPort || (!onUnit.isEmpty() && onUnit != fromUnit_))) {
            emit aboutToChange();
            // PORTS PUT WHERE THERE WERE NONE: where the cable started, and where it ends on a unit.
            QString aPort = fromPort_, bPort = port, bUnit = onPort ? unit : onUnit;
            if (fromNew_) aPort = design_->addPort(fromUnit_, fromSide_, fromAt_);
            if (!onPort) {
                QString side;
                double at = 0.5;
                nearestSide(*design_->unit(bUnit), w, side, at);
                bPort = design_->addPort(bUnit, side, at);
            }
            const QString id = design_->addCable(fromUnit_, aPort, bUnit, bPort);
            // EACH END ON THE PORT'S INSIDE OR ITS OUTSIDE: inside where the other end is within the same unit (another of
            // its ports, or a unit inside it); outside - and the port's outside put there - otherwise.
            if (SysCable* c = design_->cable(id)) {
                c->aInside = fromUnit_ == bUnit || inside(bUnit, fromUnit_);
                c->bInside = fromUnit_ == bUnit || inside(fromUnit_, bUnit);
            }
            select(QString(), QString(), id);
            emit changed();
        } else if (!fromNew_ && (!moved_ || same)) {
            select(fromUnit_, fromPort_, QString());
        }
        fromNew_ = false;
        update();
    }
}

void SystemCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!design_ || e->button() != Qt::LeftButton) return;
    // A NOTE DOUBLE-CLICKED: its text to be written.
    if (const QString n = noteAt(toWorld(e->position())); !n.isEmpty()) {
        selectNote(n);
        emit noteEditRequested(n);
    }
}

void SystemCanvas::wheelEvent(QWheelEvent* e) {
    const double factor = std::pow(1.0015, e->angleDelta().y());
    const QPointF at = e->position();
    const QPointF w = toWorld(at);
    scale_ = std::clamp(scale_ * factor, 0.15, 5.0);
    offset_ = at - w * scale_;
    update();
}

void SystemCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        drag_ = Drag::None;
        setTool(SysTool::Select);
        select(QString(), QString(), QString());
        return;
    }
    // THE TOOLS BY THEIR KEYS: U a unit, P a port, H a harness (a cable), N a note.
    if (!(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        switch (e->key()) {
        case Qt::Key_U: setTool(SysTool::Unit); return;
        case Qt::Key_P: setTool(SysTool::Port); return;
        case Qt::Key_H: setTool(SysTool::Cable); return;
        case Qt::Key_N: setTool(SysTool::Note); return;
        default: break;
        }
    }
    QWidget::keyPressEvent(e);
}

} // namespace Edraw_Desktop
