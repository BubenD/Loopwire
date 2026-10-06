#include "app/SystemCanvas.h"

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
constexpr double kLead = 22.0;

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

} // namespace

SystemCanvas::SystemCanvas(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("systemCanvas"));
    setMinimumSize(300, 200);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setToolTip(QStringLiteral("Unit: click to put a unit. Port: click a unit's side. Cable: drag from one port to another (or "
                              "from a port with Select). Wheel: zoom; right or middle drag: move the view; Delete: delete"));
}

void SystemCanvas::setDesign(SystemDesign* design) {
    design_ = design;
    selUnit_.clear();
    selPort_.clear();
    selCable_.clear();
    update();
}

void SystemCanvas::setTool(SysTool tool) {
    tool_ = tool;
    setCursor(tool == SysTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}

void SystemCanvas::select(const QString& unit, const QString& port, const QString& cable) {
    selUnit_ = unit;
    selPort_ = port;
    selCable_ = cable;
    update();
    emit selectionChanged();
}

QPointF SystemCanvas::toScreen(const QPointF& w) const { return w * scale_ + offset_; }

QPointF SystemCanvas::toWorld(const QPointF& s) const { return (s - offset_) / scale_; }

QPointF SystemCanvas::portPoint(const SysUnit& u, const SysPort& p) const {
    if (p.side == QLatin1String("left")) return {u.x, u.y + u.h * p.at};
    if (p.side == QLatin1String("top")) return {u.x + u.w * p.at, u.y};
    if (p.side == QLatin1String("bottom")) return {u.x + u.w * p.at, u.y + u.h};
    return {u.x + u.w, u.y + u.h * p.at};
}

QPointF SystemCanvas::portOut(const SysPort& p) const {
    if (p.side == QLatin1String("left")) return {-1, 0};
    if (p.side == QLatin1String("top")) return {0, -1};
    if (p.side == QLatin1String("bottom")) return {0, 1};
    return {1, 0};
}

std::vector<QPointF> SystemCanvas::cablePath(const SysCable& c) const {
    const SysUnit* ua = design_->unit(c.aUnit);
    const SysUnit* ub = design_->unit(c.bUnit);
    const SysPort* pa = ua ? ua->port(c.aPort) : nullptr;
    const SysPort* pb = ub ? ub->port(c.bPort) : nullptr;
    if (!pa || !pb) return {};
    // SQUARE: straight out of each port, then across and along to meet.
    const QPointF a = portPoint(*ua, *pa), b = portPoint(*ub, *pb);
    const QPointF da = portOut(*pa), db = portOut(*pb);
    const QPointF a1 = a + da * kLead, b1 = b + db * kLead;
    std::vector<QPointF> path{a, a1};
    if (std::abs(da.x()) > 0.5 && std::abs(db.x()) > 0.5) {
        const double mx = (a1.x() + b1.x()) / 2.0;
        path.push_back({mx, a1.y()});
        path.push_back({mx, b1.y()});
    } else if (std::abs(da.y()) > 0.5 && std::abs(db.y()) > 0.5) {
        const double my = (a1.y() + b1.y()) / 2.0;
        path.push_back({a1.x(), my});
        path.push_back({b1.x(), my});
    } else if (std::abs(da.x()) > 0.5) {
        path.push_back({b1.x(), a1.y()});
    } else {
        path.push_back({a1.x(), b1.y()});
    }
    path.push_back(b1);
    path.push_back(b);
    return path;
}

QString SystemCanvas::unitAt(const QPointF& w) const {
    if (!design_) return QString();
    for (auto it = design_->units.rbegin(); it != design_->units.rend(); ++it)
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

QPointF SystemCanvas::portOnScreen(const QString& unit, const QString& port) const {
    const SysUnit* u = design_ ? design_->unit(unit) : nullptr;
    const SysPort* p = u ? u->port(port) : nullptr;
    return u && p ? toScreen(portPoint(*u, *p)) : QPointF();
}

QPointF SystemCanvas::unitOnScreen(const QString& unit) const {
    const SysUnit* u = design_ ? design_->unit(unit) : nullptr;
    return u ? toScreen(QPointF(u->x + u->w / 2.0, u->y + u->h / 2.0)) : QPointF();
}

QPointF SystemCanvas::cableOnScreen(const QString& cable) const {
    const SysCable* c = design_ ? design_->cable(cable) : nullptr;
    if (!c) return {};
    const std::vector<QPointF> p = cablePath(*c);
    return p.size() >= 4 ? toScreen((p[p.size() / 2 - 1] + p[p.size() / 2]) / 2.0) : QPointF();
}

void SystemCanvas::fit() {
    if (!design_ || design_->units.empty()) {
        scale_ = 1.0;
        offset_ = {40.0, 40.0};
        update();
        return;
    }
    QRectF box;
    for (const SysUnit& u : design_->units) box = box.united(QRectF(u.x, u.y, u.w, u.h));
    box.adjust(-60, -60, 60, 60);
    scale_ = std::clamp(std::min(width() / box.width(), height() / box.height()), 0.2, 3.0);
    offset_ = QPointF(width() / 2.0, height() / 2.0) - box.center() * scale_;
    update();
}

void SystemCanvas::deleteSelected() {
    if (!design_ || (selUnit_.isEmpty() && selCable_.isEmpty())) return;
    emit aboutToChange();
    if (!selCable_.isEmpty()) design_->removeCable(selCable_);
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
    // THE CABLES under the units, each labelled at its middle with a dot for how its drawing stands.
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
        const SysUnit* u = design_->unit(fromUnit_);
        const SysPort* pt = u ? u->port(fromPort_) : nullptr;
        if (u && pt) {
            p.setPen(QPen(QColor(40, 140, 230), 2.0, Qt::DashLine));
            p.drawLine(toScreen(portPoint(*u, *pt)), rubber_);
        }
    }
    // THE UNITS: a box, its name in it, its part number and revision under the name; its ports on its sides.
    for (const SysUnit& u : design_->units) {
        const QRectF r(toScreen(QPointF(u.x, u.y)), QSizeF(u.w * scale_, u.h * scale_));
        const bool on = u.id == selUnit_ && selPort_.isEmpty();
        p.setPen(QPen(on ? QColor(40, 140, 230) : QColor(90, 98, 110), on ? 2.5 : 1.5));
        p.setBrush(u.color.isValid() ? u.color : QColor(232, 238, 246));
        p.drawRoundedRect(r, 5, 5);
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
        for (const SysPort& pt : u.ports) {
            const QPointF c = toScreen(portPoint(u, pt));
            const bool pon = u.id == selUnit_ && pt.id == selPort_;
            const double s = kPortSize * std::clamp(scale_, 0.6, 1.6);
            p.setPen(QPen(pon ? QColor(40, 140, 230) : QColor(60, 66, 76), 1.2));
            p.setBrush(design_->cablesAt(u.id, pt.id).empty() ? QColor(255, 255, 255) : QColor(70, 80, 95));
            p.drawRect(QRectF(c.x() - s / 2, c.y() - s / 2, s, s));
            // Its name just inside the unit.
            const QPointF in = -portOut(pt) * (s + 3);
            const QPointF at = c + in;
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
    }
    if (design_->units.empty()) {
        p.setPen(QColor(120, 128, 140));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("The system diagram is empty - choose Unit on the ribbon and click here to put the first unit"));
    }
}

void SystemCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    lastScreen_ = e->position();
    pressWorld_ = toWorld(e->position());
    moved_ = false;
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
        select(id, QString(), QString());
        emit changed();
        return;
    }
    case SysTool::Port: {
        const QString id = unitAt(pressWorld_);
        const SysUnit* u = design_->unit(id);
        if (!u) return;
        // THE NEAREST SIDE, so far along it.
        const double dl = pressWorld_.x() - u->x, dr = u->x + u->w - pressWorld_.x();
        const double dt = pressWorld_.y() - u->y, db = u->y + u->h - pressWorld_.y();
        const double least = std::min({dl, dr, dt, db});
        const QString side = least == dl ? QStringLiteral("left") : least == dr ? QStringLiteral("right") : least == dt ? QStringLiteral("top")
                                                                                                                      : QStringLiteral("bottom");
        const double at = side == QLatin1String("left") || side == QLatin1String("right") ? dt / u->h : dl / u->w;
        emit aboutToChange();
        const QString p = design_->addPort(id, side, at);
        select(id, p, QString());
        emit changed();
        return;
    }
    case SysTool::Cable:
    case SysTool::Select:
        if (portAt(pressWorld_, unit, port)) {
            // FROM A PORT: a cable drawn from it (Select too) - or, released where it was pressed, the port selected.
            drag_ = Drag::Cable;
            fromUnit_ = unit;
            fromPort_ = port;
            rubber_ = e->position();
            return;
        }
        if (tool_ == SysTool::Cable) return;
        if (const QString id = unitAt(pressWorld_); !id.isEmpty()) {
            const SysUnit* u = design_->unit(id);
            drag_ = Drag::Unit;
            moveAnnounced_ = false;
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

void SystemCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (!design_) return;
    const QPointF delta = e->position() - lastScreen_;
    if ((e->position() - lastScreen_).manhattanLength() > 0.0 && drag_ != Drag::None) moved_ = true;
    lastScreen_ = e->position();
    switch (drag_) {
    case Drag::Pan:
        offset_ += delta;
        update();
        break;
    case Drag::Unit:
        if (SysUnit* u = design_->unit(selUnit_)) {
            if (!moved_) break;
            // THE FIRST MOVE: where it was, kept for Undo.
            if (!moveAnnounced_) {
                emit aboutToChange();
                moveAnnounced_ = true;
            }
            const QPointF w = toWorld(e->position()) - grabOffset_;
            // On the grid, ten units at a time.
            u->x = std::round(w.x() / 10.0) * 10.0;
            u->y = std::round(w.y() / 10.0) * 10.0;
            update();
        }
        break;
    case Drag::Cable:
        rubber_ = e->position();
        update();
        break;
    case Drag::None: {
        QString unit, port;
        const QPointF w = toWorld(e->position());
        setCursor(portAt(w, unit, port) ? Qt::CrossCursor : !unitAt(w).isEmpty() && tool_ == SysTool::Select ? Qt::SizeAllCursor : tool_ == SysTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        break;
    }
    }
}

void SystemCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (!design_) return;
    const Drag was = drag_;
    drag_ = Drag::None;
    if (was == Drag::Unit && moved_) {
        // Moved: one change - its place before the move kept for Undo by the view, which saved it at the press.
        emit changed();
        return;
    }
    if (was == Drag::Cable) {
        QString unit, port;
        const QPointF w = toWorld(e->position());
        if (portAt(w, unit, port) && !(unit == fromUnit_ && port == fromPort_)) {
            emit aboutToChange();
            const QString id = design_->addCable(fromUnit_, fromPort_, unit, port);
            select(QString(), QString(), id);
            emit changed();
        } else if (!moved_ || (unit == fromUnit_ && port == fromPort_)) {
            select(fromUnit_, fromPort_, QString());
        }
        update();
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
    QWidget::keyPressEvent(e);
}

} // namespace Edraw_Desktop
