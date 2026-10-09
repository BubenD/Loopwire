#include "app/SystemCanvas.h"
#include "system/SystemExport.h"

#include <QKeyEvent>
#include <QTimer>
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
    if (pending_) {
        pending_ = false;
        drag_ = Drag::None;
        update();
    }
    tool_ = tool;
    if (tool != SysTool::Select) {
        lastTool_ = tool;
        lastDelete_ = false;
    }
    setCursor(tool == SysTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    if (changedTool) emit toolChanged(tool);
}

void SystemCanvas::selectZone(const QStringList& units, const QStringList& cables, const QStringList& notes) {
    // The first of it the one shown on the right.
    selUnit_ = units.value(0);
    selPort_.clear();
    selCable_ = units.isEmpty() ? cables.value(0) : QString();
    selNote_ = units.isEmpty() && cables.isEmpty() ? notes.value(0) : QString();
    zoneUnits_ = units;
    zoneCables_ = cables;
    zoneNotes_ = notes;
    update();
    emit selectionChanged();
}

QFont SystemCanvas::diagramFont() const {
    QFont f = font();
    f.setPointSizeF(std::clamp(9.0 * scale_, 6.0, 16.0));
    return f;
}

QRectF SystemCanvas::portLabelRect(const SysUnit& u, const SysPort& p) const {
    const QFontMetricsF fm(diagramFont());
    const QSizeF size(fm.horizontalAdvance(p.name.isEmpty() ? QStringLiteral(" ") : p.name) + 6, fm.height());
    const QPointF c = toScreen(portPoint(u, p));
    const double s = kPortSize * std::clamp(scale_, 0.6, 1.6);
    // OUTSIDE ITS UNIT, ABOVE THE CABLE'S LINE: beside the line out of a left or right side, up from it; to the right of
    // the line out of the top or the bottom.
    QPointF at;
    if (p.side == QLatin1String("left")) at = QPointF(c.x() - s - 4 - size.width(), c.y() - size.height() - 1);
    else if (p.side == QLatin1String("top")) at = QPointF(c.x() + 4, c.y() - s - 4 - size.height());
    else if (p.side == QLatin1String("bottom")) at = QPointF(c.x() + 4, c.y() + s + 4);
    else at = QPointF(c.x() + s + 4, c.y() - size.height() - 1);
    return QRectF(at + QPointF(p.labelDx, p.labelDy) * scale_, size);
}

QRectF SystemCanvas::portLabelOnScreen(const QString& unit, const QString& port) const {
    const SysUnit* u = design_ ? design_->unit(unit) : nullptr;
    const SysPort* p = u ? u->port(port) : nullptr;
    return u && p ? portLabelRect(*u, *p) : QRectF();
}

bool SystemCanvas::labelAt(const QPointF& screen, QString& unit, QString& port) const {
    if (!design_) return false;
    for (const SysUnit& u : design_->units)
        for (const SysPort& p : u.ports)
            if (portLabelRect(u, p).contains(screen)) {
                unit = u.id;
                port = p.id;
                return true;
            }
    return false;
}

bool SystemCanvas::noteTarget(const SysNote& n, QPointF& w) const {
    const SysUnit* u = design_->unit(n.linkUnit);
    if (u && !n.linkPort.isEmpty()) {
        const SysPort* p = u->port(n.linkPort);
        if (!p) return false;
        w = portPoint(*u, *p);
        return true;
    }
    if (const SysCable* c = design_->cable(n.linkCable)) {
        const std::vector<QPointF> path = cablePath(*c);
        if (path.size() < 2) return false;
        w = (path[path.size() / 2 - 1] + path[path.size() / 2]) / 2.0;
        return true;
    }
    if (u) {
        const QPointF mid(n.x + n.w / 2, n.y + n.h / 2);
        w = QPointF(std::clamp(mid.x(), u->x, u->x + u->w), std::clamp(mid.y(), u->y, u->y + u->h));
        // On its wall, where the note's middle is outside it.
        if (u->rect().contains(mid)) w = u->rect().center();
        return true;
    }
    return false;
}

void SystemCanvas::playCheck(const std::vector<SysIssue>& issues) {
    marks_.clear();
    for (const SysIssue& i : issues) {
        Mark m;
        m.level = i.level;
        m.what = i.what;
        const SysUnit* u = design_ ? design_->unit(i.unit) : nullptr;
        if (u && !i.port.isEmpty()) {
            if (const SysPort* p = u->port(i.port)) m.at = portPoint(*u, *p);
            else continue;
        } else if (u) {
            m.at = QPointF(u->x + u->w - 10, u->y + 10);
        } else if (const SysCable* c = design_ ? design_->cable(i.cable) : nullptr) {
            const std::vector<QPointF> path = cablePath(*c);
            if (path.size() < 2) continue;
            m.at = (path[path.size() / 2 - 1] + path[path.size() / 2]) / 2.0;
        } else if (const SysNote* n = design_ ? design_->note(i.note) : nullptr) {
            m.at = QPointF(n->x + n->w - 8, n->y + 8);
        } else {
            continue;
        }
        marks_.push_back(m);
    }
    if (!anim_) {
        anim_ = new QTimer(this);
        anim_->setInterval(30);
        connect(anim_, &QTimer::timeout, this, [this] {
            if (!checkAnimating()) {
                anim_->stop();
                animating_ = false;
            }
            update();
        });
    }
    animating_ = true;
    animClock_.start();
    anim_->start();
    update();
}

// The sweep 0.9 s, then the rings pulsing to 4 s.
bool SystemCanvas::checkAnimating() const { return animating_ && animClock_.isValid() && animClock_.elapsed() < 4000; }

void SystemCanvas::finishCheck() {
    animating_ = false;
    if (anim_) anim_->stop();
    update();
}

void SystemCanvas::clearCheck() {
    marks_.clear();
    finishCheck();
}

void SystemCanvas::select(const QString& unit, const QString& port, const QString& cable) {
    zoneUnits_.clear();
    zoneCables_.clear();
    zoneNotes_.clear();
    selUnit_ = unit;
    selPort_ = port;
    selCable_ = cable;
    selNote_.clear();
    update();
    emit selectionChanged();
}

void SystemCanvas::selectNote(const QString& note) {
    zoneUnits_.clear();
    zoneCables_.clear();
    zoneNotes_.clear();
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

bool SystemCanvas::inside(const QString& inner, const QString& outer) const { return design_ && design_->isWithin(inner, outer); }

void SystemCanvas::placed(const QMouseEvent* e) {
    if (!(e->modifiers() & Qt::ShiftModifier)) setTool(SysTool::Select);
}

void SystemCanvas::settleUnit(const QString& id) {
    SysUnit* u = design_ ? design_->unit(id) : nullptr;
    if (!u) return;
    // IN THE UNIT ROUND ITS MIDDLE - one big enough to hold it - or in none.
    QString around = design_->unitAround(u->rect().center(), id);
    if (const SysUnit* p = design_->unit(around); p && (p->w < u->w || p->h < u->h)) around.clear();
    u->parent = around;
    // KEPT INSIDE IT, everything inside the unit moved with it.
    if (const SysUnit* p = design_->unit(around)) {
        const double nx = std::clamp(u->x, p->x, p->x + p->w - u->w), ny = std::clamp(u->y, p->y, p->y + p->h - u->h);
        design_->moveUnit(id, nx - u->x, ny - u->y);
    }
}

// SQUARE: straight out of each port - out of its outside, or in from its inside, beyond its square - then across and
// along to meet (as the exports draw it).
std::vector<QPointF> SystemCanvas::cablePath(const SysCable& c) const { return sysCablePath(*design_, c, 2.0 * portHalf()); }

QString SystemCanvas::unitAt(const QPointF& w) const {
    // The innermost first: a unit inside another is drawn over it.
    return design_ ? design_->unitAround(w) : QString();
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

int SystemCanvas::noteHandleAt(const QPointF& w, QString& note) const {
    if (!design_) return 0;
    const double reach = 5.0 / scale_;
    for (auto it = design_->notes.rbegin(); it != design_->notes.rend(); ++it) {
        const SysNote& n = *it;
        const double r = n.x + n.w, b = n.y + n.h;
        const bool alongY = w.y() >= n.y - reach && w.y() <= b + reach, alongX = w.x() >= n.x - reach && w.x() <= r + reach;
        int how = 0;
        if (alongY && std::abs(w.x() - r) <= reach) how |= 1;
        if (alongX && std::abs(w.y() - b) <= reach) how |= 2;
        if (alongY && std::abs(w.x() - n.x) <= reach) how |= 4;
        if (alongX && std::abs(w.y() - n.y) <= reach) how |= 8;
        if (how != 0) {
            note = n.id;
            return how;
        }
    }
    return 0;
}

int SystemCanvas::sizeHandleAt(const QPointF& w, QString& unit) const {
    if (!design_) return 0;
    const double reach = 5.0 / scale_;
    // The innermost first: its walls lie inside its mother's.
    std::vector<const SysUnit*> order;
    for (const SysUnit& u : design_->units) order.push_back(&u);
    std::stable_sort(order.begin(), order.end(), [this](const SysUnit* a, const SysUnit* b) { return design_->depth(a->id) > design_->depth(b->id); });
    for (const SysUnit* up : order) {
        const SysUnit& u = *up;
        const double r = u.x + u.w, b = u.y + u.h;
        const bool alongY = w.y() >= u.y - reach && w.y() <= b + reach, alongX = w.x() >= u.x - reach && w.x() <= r + reach;
        int how = 0;
        if (alongY && std::abs(w.x() - r) <= reach) how |= 1;
        if (alongX && std::abs(w.y() - b) <= reach) how |= 2;
        if (alongY && std::abs(w.x() - u.x) <= reach) how |= 4;
        if (alongX && std::abs(w.y() - u.y) <= reach) how |= 8;
        if (how != 0) {
            unit = u.id;
            return how;
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
    // A ZONE: everything in it.
    if (zoneCount() > 0) {
        for (const QString& n : zoneNotes_) design_->removeNote(n);
        for (const QString& c : zoneCables_) design_->removeCable(c);
        for (const QString& u : zoneUnits_) design_->removeUnit(u);
        select(QString(), QString(), QString());
        emit changed();
        return;
    }
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
    QFont f = diagramFont();
    p.setFont(f);
    // THE UNITS: a box - a mother before what is in it, so what is inside is drawn over it - filled as solid as it is
    // set; its mark for what it is in its corner and its description beside it, its name in it, its part number and
    // revision under the name - each where it is to be shown.
    std::vector<const SysUnit*> order;
    for (const SysUnit& u : design_->units) order.push_back(&u);
    std::stable_sort(order.begin(), order.end(), [this](const SysUnit* a, const SysUnit* b) { return design_->depth(a->id) < design_->depth(b->id); });
    for (const SysUnit* up : order) {
        const SysUnit& u = *up;
        const QRectF r(toScreen(QPointF(u.x, u.y)), QSizeF(u.w * scale_, u.h * scale_));
        const bool on = (u.id == selUnit_ && selPort_.isEmpty()) || zoneUnits_.contains(u.id);
        p.setPen(QPen(on ? QColor(40, 140, 230) : QColor(90, 98, 110), on ? 2.5 : 1.5));
        QColor fill = u.color.isValid() ? u.color : u.kind == QLatin1String("pcba") ? QColor(226, 242, 230) : QColor(232, 238, 246);
        fill.setAlphaF(static_cast<float>(std::clamp(u.opacity, 0.0, 1.0)));
        p.setBrush(fill);
        p.drawRoundedRect(r, 5, 5);
        const double mark = 14.0 * std::clamp(scale_, 0.7, 1.5);
        drawKind(p, QRectF(r.left() + 5, r.top() + 5, mark, mark), u.kind);
        if (u.shows(QStringLiteral("desc")) && !u.description.isEmpty()) {
            QFont small = f;
            small.setItalic(true);
            p.setFont(small);
            p.setPen(QColor(70, 78, 90));
            const QRectF beside(r.left() + 5 + mark + 4, r.top() + 5, std::max(0.0, r.width() - mark - 18), mark);
            p.drawText(beside, Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(u.description, Qt::ElideRight, static_cast<int>(beside.width())));
            p.setFont(f);
        }
        QFont bold = f;
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(QColor(25, 30, 36));
        if (u.shows(QStringLiteral("name"))) p.drawText(r.adjusted(6, 4, -6, -r.height() / 2), Qt::AlignHCenter | Qt::AlignBottom, u.name);
        p.setFont(f);
        QStringList sub;
        if (!u.pn.isEmpty() && u.shows(QStringLiteral("pn"))) sub << u.pn;
        if (!u.rev.isEmpty() && u.shows(QStringLiteral("rev"))) sub << QStringLiteral("rev ") + u.rev;
        p.setPen(QColor(80, 88, 100));
        if (!sub.isEmpty()) p.drawText(r.adjusted(6, r.height() / 2 + 2, -6, -4), Qt::AlignHCenter | Qt::AlignTop, sub.join(QStringLiteral("  ")));
    }
    // THE CABLES, each labelled at its middle with a dot for how its drawing stands.
    for (const SysCable& c : design_->cables) {
        const std::vector<QPointF> path = cablePath(c);
        if (path.size() < 2) continue;
        const bool on = c.id == selCable_ || zoneCables_.contains(c.id);
        QPolygonF line;
        for (const QPointF& q : path) line << toScreen(q);
        p.setPen(QPen(on ? QColor(40, 140, 230) : c.color.isValid() ? c.color : QColor(60, 66, 76), on ? 3.0 : 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(line);
        const QPointF mid = (line[line.size() / 2 - 1] + line[line.size() / 2]) / 2.0;
        // ITS LABEL: what of it is to be shown.
        QStringList parts;
        if (c.shows(QStringLiteral("name")) && !c.name.isEmpty()) parts << c.name;
        if (c.shows(QStringLiteral("pn")) && !c.pn.isEmpty()) parts << c.pn;
        if (c.shows(QStringLiteral("rev")) && !c.rev.isEmpty()) parts << QStringLiteral("rev ") + c.rev;
        if (c.shows(QStringLiteral("desc")) && !c.description.isEmpty()) parts << c.description;
        const QString label = parts.join(QStringLiteral("  "));
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
    // outside - and its designator outside the unit, above the cable's line (or where it was dragged to).
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
            const QRectF nameBox = portLabelRect(u, pt);
            p.setPen(pon ? QColor(40, 140, 230) : QColor(50, 56, 66));
            p.drawText(nameBox, Qt::AlignCenter, pt.name);
        }
    // THE NOTES, over everything: a box in its colour (pale yellow as it starts) with its text wrapped inside it - and a
    // dashed line to what it is about, where it is linked to something.
    for (const SysNote& n : design_->notes) {
        const QRectF r(toScreen(QPointF(n.x, n.y)), QSizeF(n.w * scale_, n.h * scale_));
        QPointF target;
        if (noteTarget(n, target)) {
            const QPointF to = toScreen(target);
            const QPointF from(std::clamp(to.x(), r.left(), r.right()), std::clamp(to.y(), r.top(), r.bottom()));
            p.setPen(QPen(QColor(120, 110, 70), 1.2, Qt::DashLine));
            p.drawLine(from, to);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(120, 110, 70));
            p.drawEllipse(to, 3, 3);
        }
        const bool on = n.id == selNote_ || zoneNotes_.contains(n.id);
        p.setPen(QPen(on ? QColor(40, 140, 230) : QColor(200, 170, 60), on ? 2.0 : 1.0));
        p.setBrush(n.color.isValid() ? n.color : QColor(255, 247, 190));
        p.drawRect(r);
        p.save();
        p.setClipRect(r.adjusted(1, 1, -1, -1));
        p.setPen(QColor(60, 52, 20));
        p.drawText(r.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, n.text);
        p.restore();
    }
    // THE ZONE BEING DRAGGED: a blue window left to right, a green crossing right to left.
    if (drag_ == Drag::Zone) {
        const QRectF z = QRectF(zoneFrom_, zoneTo_).normalized();
        const bool window = zoneTo_.x() >= zoneFrom_.x();
        p.setPen(QPen(window ? QColor(40, 110, 230) : QColor(40, 170, 80), 1.2, window ? Qt::SolidLine : Qt::DashLine));
        p.setBrush(window ? QColor(40, 110, 230, 40) : QColor(40, 170, 80, 40));
        p.drawRect(z);
    }
    // THE CHECK: a line swept across the diagram, then each thing found ringed - pulsing a while, then kept.
    if (!marks_.empty() || checkAnimating()) {
        const double t = animating_ && animClock_.isValid() ? animClock_.elapsed() / 1000.0 : 99.0;
        const double sweep = std::clamp(t / 0.9, 0.0, 1.0);
        if (sweep < 1.0) {
            const double x = width() * sweep;
            QLinearGradient band(x - 60, 0, x, 0);
            band.setColorAt(0.0, QColor(40, 140, 230, 0));
            band.setColorAt(1.0, QColor(40, 140, 230, 90));
            p.fillRect(QRectF(x - 60, 0, 60, height()), band);
            p.setPen(QPen(QColor(40, 140, 230), 2.0));
            p.drawLine(QPointF(x, 0), QPointF(x, height()));
        }
        for (const Mark& m : marks_) {
            const QPointF at = toScreen(m.at);
            if (at.x() > width() * sweep) continue;   // not reached yet
            const QColor c = m.level >= 2 ? QColor(220, 50, 40) : m.level == 1 ? QColor(235, 150, 20) : QColor(40, 120, 220);
            const double pulse = t < 4.0 ? 0.5 + 0.5 * std::sin((t - 0.9) * 2.0 * M_PI * 1.5) : 0.0;
            QColor halo = c;
            halo.setAlphaF(static_cast<float>(0.25 + 0.35 * pulse));
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(at, 11 + 7 * pulse, 11 + 7 * pulse);
            p.setPen(QPen(c, 2.2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(at, 9, 9);
        }
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
    // THE SECOND CLICK of a cable started by a click: where it ends.
    if (pending_ && e->button() == Qt::LeftButton) {
        finishCable(e);
        return;
    }
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
        // IN THE UNIT CLICKED, where one is - no bigger than half of it.
        const QString around = design_->unitAround(pressWorld_);
        const SysUnit* mother = design_->unit(around);
        const double w = mother ? std::min(160.0, std::max(60.0, std::floor(mother->w * 0.5 / 10.0) * 10.0)) : 160.0;
        const double h = mother ? std::min(100.0, std::max(40.0, std::floor(mother->h * 0.5 / 10.0) * 10.0)) : 100.0;
        const QString id = design_->addUnit(std::round((pressWorld_.x() - w / 2) / 10.0) * 10.0, std::round((pressWorld_.y() - h / 2) / 10.0) * 10.0);
        if (SysUnit* u = design_->unit(id)) {
            u->kind = newKind_;
            u->w = w;
            u->h = h;
        }
        settleUnit(id);
        select(id, QString(), QString());
        emit changed();
        placed(e);
        return;
    }
    case SysTool::Note: {
        emit aboutToChange();
        const QString id = design_->addNote(std::round(pressWorld_.x() / 10.0) * 10.0, std::round(pressWorld_.y() / 10.0) * 10.0);
        selectNote(id);
        emit changed();
        placed(e);
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
        placed(e);
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
        // A PORT'S DESIGNATOR: moved where it is dragged.
        if (labelAt(e->position(), unit, port)) {
            drag_ = Drag::Label;
            labelUnit_ = unit;
            labelPort_ = port;
            select(unit, port, QString());
            return;
        }
        // A NOTE'S WALL OR CORNER: sized.
        QString noted;
        if (const int how = noteHandleAt(pressWorld_, noted); how != 0) {
            drag_ = Drag::NoteSize;
            sizeHow_ = how;
            const SysNote* n = design_->note(noted);
            sizeFrom_ = QRectF(n->x, n->y, n->w, n->h);
            selectNote(noted);
            return;
        }
        // A UNIT'S CORNER OR EDGE: sized.
        QString sized;
        if (const int how = sizeHandleAt(pressWorld_, sized); how != 0) {
            drag_ = Drag::Size;
            sizeHow_ = how;
            sizeFrom_ = design_->unit(sized)->rect();
            select(sized, QString(), QString());
            return;
        }
        // Taken hold of in a zone of more than it: the zone kept, to be moved together.
        const bool inZone = zoneCount() > 1;
        if (const QString n = noteAt(pressWorld_); !n.isEmpty()) {
            const SysNote* note = design_->note(n);
            drag_ = Drag::Note;
            grabOffset_ = pressWorld_ - QPointF(note->x, note->y);
            if (inZone && zoneNotes_.contains(n)) selNote_ = n;
            else selectNote(n);
            return;
        }
        if (const QString id = unitAt(pressWorld_); !id.isEmpty()) {
            const SysUnit* u = design_->unit(id);
            drag_ = Drag::Unit;
            grabOffset_ = pressWorld_ - QPointF(u->x, u->y);
            if (inZone && zoneUnits_.contains(id)) selUnit_ = id;
            else select(id, QString(), QString());
            return;
        }
        if (const QString c = cableAt(pressWorld_); !c.isEmpty()) {
            select(QString(), QString(), c);
            return;
        }
        // ON EMPTY GROUND: a zone dragged out.
        select(QString(), QString(), QString());
        drag_ = Drag::Zone;
        zoneFrom_ = zoneTo_ = e->position();
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
            // On the grid, ten units at a time - everything inside it with it.
            double nx = std::round(to.x() / 10.0) * 10.0, ny = std::round(to.y() / 10.0) * 10.0;
            // KEPT INSIDE ITS MOTHER while its middle is - taken out of it once its middle is dragged past her wall.
            if (const SysUnit* m = design_->unit(u->parent);
                m && QRectF(m->x, m->y, m->w, m->h).contains(QPointF(nx + u->w / 2, ny + u->h / 2))) {
                nx = std::clamp(nx, m->x, m->x + m->w - u->w);
                ny = std::clamp(ny, m->y, m->y + m->h - u->h);
            }
            // A ZONE: everything in it moved alike - the units not inside another of it, and its notes.
            if (zoneCount() > 1 && zoneUnits_.contains(u->id)) {
                const double dx = std::round(to.x() / 10.0) * 10.0 - u->x, dy = std::round(to.y() / 10.0) * 10.0 - u->y;
                for (const QString& z : zoneUnits_) {
                    bool rooted = true;
                    for (const QString& other : zoneUnits_) rooted = rooted && !design_->isWithin(z, other);
                    if (rooted) design_->moveUnit(z, dx, dy);
                }
                for (const QString& z : zoneNotes_)
                    if (SysNote* n = design_->note(z)) {
                        n->x += dx;
                        n->y += dy;
                    }
                update();
                break;
            }
            design_->moveUnit(u->id, nx - u->x, ny - u->y);
            update();
        }
        break;
    case Drag::Note:
        if (SysNote* n = design_->note(selNote_)) {
            if (!moved_) break;
            announce();
            const QPointF to = w - grabOffset_;
            const double dx = std::round(to.x() / 10.0) * 10.0 - n->x, dy = std::round(to.y() / 10.0) * 10.0 - n->y;
            if (zoneCount() > 1 && zoneNotes_.contains(n->id)) {
                for (const QString& z : zoneNotes_)
                    if (SysNote* o = design_->note(z)) {
                        o->x += dx;
                        o->y += dy;
                    }
                for (const QString& z : zoneUnits_) {
                    bool rooted = true;
                    for (const QString& other : zoneUnits_) rooted = rooted && !design_->isWithin(z, other);
                    if (rooted) design_->moveUnit(z, dx, dy);
                }
            } else {
                n->x += dx;
                n->y += dy;
            }
            update();
        }
        break;
    case Drag::NoteSize:
        if (SysNote* n = design_->note(selNote_)) {
            if (!moved_) break;
            announce();
            double left = sizeFrom_.left(), top = sizeFrom_.top(), right = sizeFrom_.right(), bottom = sizeFrom_.bottom();
            const double gx = std::round(w.x() / 10.0) * 10.0, gy = std::round(w.y() / 10.0) * 10.0;
            if (sizeHow_ & 1) right = std::max(gx, left + 60.0);
            if (sizeHow_ & 4) left = std::min(gx, right - 60.0);
            if (sizeHow_ & 2) bottom = std::max(gy, top + 30.0);
            if (sizeHow_ & 8) top = std::min(gy, bottom - 30.0);
            n->x = left;
            n->y = top;
            n->w = right - left;
            n->h = bottom - top;
            update();
        }
        break;
    case Drag::Label:
        if (SysUnit* u = design_->unit(labelUnit_)) {
            SysPort* pt = u->port(labelPort_);
            if (!pt || !moved_) break;
            announce();
            pt->labelDx += delta.x() / scale_;
            pt->labelDy += delta.y() / scale_;
            update();
        }
        break;
    case Drag::Zone:
        zoneTo_ = e->position();
        update();
        break;
    case Drag::Size:
        if (SysUnit* u = design_->unit(selUnit_)) {
            if (!moved_) break;
            announce();
            // BY THE WALL OR CORNER TAKEN HOLD OF - its ports where they were along its sides, so far along each; no
            // smaller than what is inside it, nor out past its mother.
            double left = sizeFrom_.left(), top = sizeFrom_.top(), right = sizeFrom_.right(), bottom = sizeFrom_.bottom();
            const double gx = std::round(w.x() / 10.0) * 10.0, gy = std::round(w.y() / 10.0) * 10.0;
            if (sizeHow_ & 1) right = std::max(gx, left + 60.0);
            if (sizeHow_ & 4) left = std::min(gx, right - 60.0);
            if (sizeHow_ & 2) bottom = std::max(gy, top + 40.0);
            if (sizeHow_ & 8) top = std::min(gy, bottom - 40.0);
            const QRectF inside = design_->contentsOf(u->id);
            if (!inside.isNull()) {
                left = std::min(left, inside.left());
                top = std::min(top, inside.top());
                right = std::max(right, inside.right());
                bottom = std::max(bottom, inside.bottom());
            }
            if (const SysUnit* m = design_->unit(u->parent)) {
                left = std::max(left, m->x);
                top = std::max(top, m->y);
                right = std::min(right, m->x + m->w);
                bottom = std::min(bottom, m->y + m->h);
            }
            u->x = left;
            u->y = top;
            u->w = right - left;
            u->h = bottom - top;
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
        QString noted;
        int how = tool_ == SysTool::Select ? noteHandleAt(w, noted) : 0;
        if (how == 0 && tool_ == SysTool::Select) how = sizeHandleAt(w, sized);
        setCursor(portAt(w, unit, port) ? (tool_ == SysTool::Select ? Qt::SizeAllCursor : Qt::CrossCursor)
                  : how == 3 || how == 12                           ? Qt::SizeFDiagCursor
                  : how == 6 || how == 9                            ? Qt::SizeBDiagCursor
                  : how == 1 || how == 4                            ? Qt::SizeHorCursor
                  : how == 2 || how == 8                            ? Qt::SizeVerCursor
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
    if (was == Drag::Zone) {
        // THE ZONE: a blue window (dragged left to right) takes what is wholly in it, a green crossing what it touches.
        const QRectF z = QRectF(toWorld(zoneFrom_), toWorld(e->position())).normalized();
        const bool window = e->position().x() >= zoneFrom_.x();
        if (z.width() * scale_ < 3 && z.height() * scale_ < 3) return;
        auto takes = [&](const QRectF& r) { return window ? z.contains(r) : z.intersects(r); };
        QStringList units, cables, notes;
        for (const SysUnit& u : design_->units)
            if (takes(u.rect())) units << u.id;
        for (const SysNote& n : design_->notes)
            if (takes(QRectF(n.x, n.y, n.w, n.h))) notes << n.id;
        for (const SysCable& c : design_->cables) {
            const std::vector<QPointF> path = cablePath(c);
            bool all = !path.empty(), any = false;
            for (size_t i = 0; i < path.size(); ++i) {
                all = all && z.contains(path[i]);
                // Along each stretch, a few points: whether any is in it.
                if (i > 0)
                    for (int k = 0; k <= 8 && !any; ++k) any = z.contains(path[i - 1] + (path[i] - path[i - 1]) * (k / 8.0));
            }
            if (window ? all : any) cables << c.id;
        }
        selectZone(units, cables, notes);
        return;
    }
    if ((was == Drag::Unit || was == Drag::Note || was == Drag::Size || was == Drag::Port || was == Drag::NoteSize ||
         was == Drag::Label) && moveAnnounced_) {
        // A UNIT DROPPED: into the unit it is now in, or out of the one it was in - each of a zone's.
        if (was == Drag::Unit) {
            if (zoneCount() > 1 && zoneUnits_.contains(selUnit_))
                for (const QString& z : zoneUnits_) settleUnit(z);
            else
                settleUnit(selUnit_);
        }
        // Moved, sized or slid: one change - how it was before kept for Undo by the view, at the first move.
        emit changed();
        return;
    }
    if (was == Drag::Cable) {
        // A CLICK, NOT A DRAG: the cable started there - it follows the pointer, and a click on another port or unit ends it.
        if (!moved_ && !pending_) {
            pending_ = true;
            drag_ = Drag::Cable;
            if (!fromNew_) select(fromUnit_, fromPort_, QString());
            rubber_ = e->position();
            update();
            return;
        }
        finishCable(e);
    }
}

void SystemCanvas::finishCable(const QMouseEvent* e) {
    {
        QString unit, port;
        const QPointF w = toWorld(e->position());
        const bool onPort = portAt(w, unit, port);
        const QString onUnit = onPort ? unit : unitAt(w);
        const bool same = onPort && unit == fromUnit_ && port == fromPort_ && !fromNew_;
        drag_ = Drag::None;
        pending_ = false;
        // Ended on a port, or on another unit (a port put there); on its own unit's body, or on nothing: given up.
        if (!same && (onPort || (!onUnit.isEmpty() && onUnit != fromUnit_))) {
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
            placed(e);
        } else if (!fromNew_ && same) {
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
        emit deleteRequested();
        return;
    }
    // SPACE: the last thing done again - Delete, or the tool last put down.
    if (e->key() == Qt::Key_Space) {
        if (lastDelete_) emit deleteRequested();
        else if (lastTool_ != SysTool::Select) setTool(lastTool_);
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        pending_ = false;
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
