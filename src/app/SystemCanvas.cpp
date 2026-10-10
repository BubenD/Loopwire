#include "app/SystemCanvas.h"
#include "system/SystemExport.h"
#include "system/SystemRouting.h"
#include <QJsonDocument>
#include <QCryptographicHash>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QTimer>
#include <QPainterPathStroker>
#include <QTextOption>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Edraw_Desktop {

namespace {

constexpr double kPortSize = 8.0;
constexpr int kRuleDuration = 700;
const char* const kCheckRules[] = {"DESIGNATOR", "PORT-CONNECTION", "PART-NUMBER", "DRAWING-LINK", "DRAWING-FIELDS", "ROUTING", "NOTE-LINK", "SCHEMATIC-NAME"};
constexpr int kCheckRuleCount=8;
const char* const kCheckTitles[] = {"Designators", "Port connections", "Part numbers", "Drawing links and revisions", "Drawing details fields", "Cable routing clearance", "Note links", "Schematic line names"};

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
    if(kind==QLatin1String("module")){
        p.setPen(QPen(QColor(80,160,235),1.1));p.setBrush(QColor(80,160,235,40));p.drawRect(box);
        const double size=box.width()*.25;
        for(const QPointF& at:{box.topLeft()+QPointF(2,2),box.topRight()-QPointF(size+2,-2),box.bottomLeft()+QPointF(box.width()*.38,-size-2)})
            p.drawRect(QRectF(at,QSizeF(size,size)));
        p.drawLine(box.center()-QPointF(0,2),box.center()+QPointF(0,4));
    } else if (kind == QLatin1String("pcba")) {
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
    locateTimer_ = new QTimer(this);
    locateTimer_->setObjectName(QStringLiteral("systemIssueFlash"));
    locateTimer_->setInterval(30);
    connect(locateTimer_, &QTimer::timeout, this, [this] {
        if (locateClock_.elapsed() >= 1600) { locateOn_ = false; locateTimer_->stop(); }
        update();
    });
    checkTimer_ = new QTimer(this);
    checkTimer_->setInterval(30);
    checkTimer_->setObjectName(QStringLiteral("systemCheckAnimation"));
    connect(checkTimer_, &QTimer::timeout, this, [this] {
        const int elapsed = static_cast<int>(checkClock_.elapsed());
        checkPhase_ = elapsed / 150.0;
        const int items = design_ ? static_cast<int>(design_->units.size() + design_->cables.size()) : 0;
        checkStep_ = items > 0 ? std::min(items - 1, elapsed * items / (kCheckRuleCount * kRuleDuration)) : -1;
        if (!design_ || elapsed >= kCheckRuleCount * kRuleDuration) { checkTimer_->stop(); checkStep_ = -1; }
        update();
    });
    setToolTip(QStringLiteral("Unit (U): click to put a unit. Port (P): click a unit's side. Cable (H): drag from a port or a unit "
                              "to another - a port is put where there is none. Note (N): click to put a note. Select: drag a "
                              "unit, a note, a port along the sides, or a unit's corner or edge to size it. Wheel: zoom; right or "
                              "middle drag: move the view; Delete: delete"));
}

void SystemCanvas::setDesign(SystemDesign* design) {
    clearCheck();
    zoneUnits_.clear(); zoneCables_.clear(); zoneNotes_.clear();
    design_ = design;
    selUnit_.clear();
    selPort_.clear();
    selCable_.clear();
    selNote_.clear();
    update();
}

bool SystemCanvas::startNoteLink(){if(!design_||!design_->note(selNote_))return false;setTool(SysTool::Link);rubber_=noteOnScreen(selNote_);setFocus();update();return true;}

void SystemCanvas::setTool(SysTool tool) {
    if(tool==SysTool::Link){if(!design_||!design_->note(selNote_))return;linkNote_=selNote_;}else linkNote_.clear();
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

void SystemCanvas::select(const QString& unit, const QString& port, const QString& cable) {
    zoneUnits_.clear(); zoneCables_.clear(); zoneNotes_.clear();
    if (!unit.isEmpty() && port.isEmpty()) zoneUnits_.insert(unit);
    if (!cable.isEmpty()) zoneCables_.insert(cable);
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
    zoneUnits_.clear(); zoneCables_.clear(); zoneNotes_.clear();
    if (!note.isEmpty()) zoneNotes_.insert(note);
    selNote_ = note;
    update();
    emit selectionChanged();
}

bool SystemCanvas::zoneCrossing() const {
    return selectionMode_ == SysSelectionMode::Crossing || (selectionMode_ == SysSelectionMode::Direction && zoneEnd_.x() < pressWorld_.x());
}

int SystemCanvas::selectionCount() const {
    return zoneUnits_.size() + zoneCables_.size() + zoneNotes_.size() + (selPort_.isEmpty() ? 0 : 1);
}

void SystemCanvas::clearCheck() {
    if (locateTimer_) locateTimer_->stop();
    locateOn_ = false;
    if (checkTimer_) checkTimer_->stop();
    checkIssues_.clear(); checkStep_ = -1; checkShown_ = false; update();
}

bool SystemCanvas::locateIssue(const SysIssue& issue) {
    if (!design_) return false;
    QRectF bounds;
    QPointF point;
    if (const SysNote* n = design_->note(issue.note)) {
        bounds = QRectF(n->x, n->y, n->w, n->h); point = bounds.center(); selectNote(n->id);
    } else if (const SysCable* c = design_->cable(issue.cable)) {
        const auto path = cablePath(*c); if (path.empty()) return false;
        for (const QPointF& at : path) bounds = bounds.united(QRectF(at, QSizeF(.01, .01)));
        point = path.size() > 1 ? (path[path.size()/2-1] + path[path.size()/2])/2.0 : path.front();
        select({}, {}, c->id);
    } else if (const SysUnit* u = design_->unit(issue.unit)) {
        if (const SysPort* pt = u->port(issue.port)) {
            point = portPoint(*u, *pt); bounds = QRectF(point - QPointF(60, 45), QSizeF(120, 90));
            select(u->id, pt->id, {});
        } else { bounds = u->rect(); point = bounds.center(); select(u->id, {}, {}); }
    } else return false;
    checkTimer_->stop(); checkStep_ = -1; checkShown_ = false;
    const double fitScale = std::min(std::max(1, width()-100) / std::max(1.0, bounds.width()+100),
                                    std::max(1, height()-100) / std::max(1.0, bounds.height()+100));
    scale_ = std::clamp(std::min(fitScale, std::max(scale_, 1.5)), 0.15, 3.0);
    offset_ = QPointF(width()/2.0, height()/2.0) - point * scale_;
    locatePoint_ = point; locateOn_ = true; locateClock_.start(); locateTimer_->start(); update();
    return true;
}

void SystemCanvas::animateCheck(const std::vector<SysIssue>& issues) {
    checkIssues_ = issues; checkStep_ = 0; checkPhase_ = 0.0;
    checkShown_ = true; checkClock_.start();
    checkTimer_->start(); update();
}

QRectF SystemCanvas::checkPanelRect() const {
    const double panelWidth = std::min(340.0, width() - 24.0);
    return QRectF(width() - panelWidth - 12.0, 12.0, panelWidth, 70.0+kCheckRuleCount*27.0);
}

bool SystemCanvas::issueOn(const QString& unit, const QString& cable) const {
    return std::any_of(checkIssues_.begin(), checkIssues_.end(), [&](const SysIssue& issue) {
        if (checkTimer_->isActive()) {
            const int rule = std::min(kCheckRuleCount-1, static_cast<int>(checkClock_.elapsed()) / kRuleDuration);
            if (issue.rule != QLatin1String(kCheckRules[rule])) return false;
        }
        return issue.level > 0 && ((!unit.isEmpty() && issue.unit == unit) || (!cable.isEmpty() && issue.cable == cable));
    });
}

QPointF SystemCanvas::portLabelOnScreen(const QString& unit, const QString& port) const {
    const SysUnit* u = design_ ? design_->unit(unit) : nullptr;
    const SysPort* p = u ? u->port(port) : nullptr;
    return u && p ? toScreen(sysPortLabelRect(*u, *p).center()) : QPointF();
}

bool SystemCanvas::portLabelAt(const QPointF& world, QString& unit, QString& port) const {
    if (!design_) return false;
    for (const SysUnit& u : design_->units) for (const SysPort& p : u.ports) {
        if (sysPortLabelRect(u, p).contains(world)) { unit = u.id; port = p.id; return true; }
    }
    return false;
}

void SystemCanvas::selectZone(const QRectF& zone, bool crossing) {
    zoneUnits_.clear(); zoneCables_.clear(); zoneNotes_.clear();
    for (const SysUnit& u : design_->units)
        if (crossing ? zone.intersects(u.rect()) : zone.contains(u.rect())) zoneUnits_.insert(u.id);
    for (const SysNote& n : design_->notes) {
        const QRectF box(n.x, n.y, n.w, n.h);
        if (crossing ? zone.intersects(box) : zone.contains(box)) zoneNotes_.insert(n.id);
    }
    for (const SysCable& c : design_->cables) {
        const auto path = cablePath(c);
        if (path.empty()) continue;
        bool all = true;
        QPainterPath line;
        line.moveTo(path.front());
        for (const QPointF& at : path) { all = all && zone.contains(at); line.lineTo(at); }
        QPainterPathStroker stroke; stroke.setWidth(2.0 / scale_);
        if (crossing ? stroke.createStroke(line).intersects(zone) : all) zoneCables_.insert(c.id);
    }
    selUnit_ = zoneUnits_.size() == 1 && zoneCables_.isEmpty() && zoneNotes_.isEmpty() ? *zoneUnits_.begin() : QString();
    selCable_ = zoneCables_.size() == 1 && zoneUnits_.isEmpty() && zoneNotes_.isEmpty() ? *zoneCables_.begin() : QString();
    selNote_ = zoneNotes_.size() == 1 && zoneUnits_.isEmpty() && zoneCables_.isEmpty() ? *zoneNotes_.begin() : QString();
    selPort_.clear(); emit selectionChanged(); update();
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
    if(u->kind==QLatin1String("module"))for(SysUnit& child:design_->units)
        if(child.id!=u->id&&child.parent.isEmpty()&&!design_->isWithin(u->id,child.id)&&u->rect().contains(child.rect()))child.parent=u->id;
    // KEPT INSIDE IT, everything inside the unit moved with it.
    if (const SysUnit* p = design_->unit(around)) {
        const double nx = std::clamp(u->x, p->x, p->x + p->w - u->w), ny = std::clamp(u->y, p->y, p->y + p->h - u->h);
        design_->moveUnit(id, nx - u->x, ny - u->y);
    }
}

// SQUARE: straight out of each port - out of its outside, or in from its inside, beyond its square - then across and
// along to meet (as the exports draw it).
std::vector<QPointF> SystemCanvas::cablePath(const SysCable& c) const {
    const QByteArray key=QCryptographicHash::hash(QJsonDocument(design_->toJson()).toJson(QJsonDocument::Compact)+QByteArray::number(portHalf(),'g',9),QCryptographicHash::Sha256);
    if(key!=routesKey_){routesKey_=key;routes_.clear();}
    if(!routes_.contains(c.id))routes_[c.id]=sysCablePath(*design_,c,2*portHalf());
    return routes_.value(c.id);
}

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

void SystemCanvas::deleteSelected(bool removeContents) {
    if (!design_ || selectionCount() == 0) return;
    emit aboutToChange();
    if (!selPort_.isEmpty()) design_->removePort(selUnit_, selPort_);
    for (const QString& id : zoneNotes_) design_->removeNote(id);
    for (const QString& id : zoneCables_) design_->removeCable(id);
    for (const QString& id : zoneUnits_) design_->removeUnit(id, removeContents);
    select(QString(), QString(), QString());
    emit changed();
}

void SystemCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor background = design_ ? design_->background : QColor(48, 52, 58);
    const bool dark = background.lightness() < 128;
    const QColor canvasInk = dark ? QColor(220, 226, 235) : QColor(50, 56, 66);
    p.fillRect(rect(), background);
    // A GRID, faint, every 20 of the diagram's units.
    {
        QColor grid = dark ? background.lighter(122) : background.darker(105);
        if(dark && grid.lightness()<20) grid=QColor(28,32,38);
        p.setPen(QPen(grid, 1.0));
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
    // THE UNITS: a box - a mother before what is in it, so what is inside is drawn over it - filled as solid as it is
    // set; its mark for what it is in its corner and its description beside it, its name in it, its part number and
    // revision under the name - each where it is to be shown.
    std::vector<const SysUnit*> order;
    for (const SysUnit& u : design_->units) order.push_back(&u);
    std::stable_sort(order.begin(), order.end(), [this](const SysUnit* a, const SysUnit* b) { return design_->depth(a->id) < design_->depth(b->id); });
    for (const SysUnit* up : order) {
        const SysUnit& u = *up;
        const QRectF r(toScreen(QPointF(u.x, u.y)), QSizeF(u.w * scale_, u.h * scale_));
        const bool on = zoneUnits_.contains(u.id);
        p.setPen(QPen(on ? QColor(40, 140, 230) : u.kind==QLatin1String("module") ? QColor(110,165,225) : QColor(90, 98, 110), on ? 2.5 : 1.5,u.kind==QLatin1String("module")?Qt::DashLine:Qt::SolidLine));
        QColor fill = u.color.isValid() ? u.color : u.kind == QLatin1String("pcba") ? QColor(226, 242, 230) : u.kind==QLatin1String("module") ? QColor(110,160,210) : QColor(232, 238, 246);
        fill.setAlphaF(static_cast<float>(std::clamp(u.opacity, 0.0, 1.0)));
        p.setBrush(fill);
        const bool darkUnit = fill.lightness() * u.opacity + background.lightness() * (1-u.opacity) < 128;
        p.drawRoundedRect(r, 5, 5);
        const double mark = 14.0 * std::clamp(scale_, 0.7, 1.5);
        drawKind(p, QRectF(r.left() + 5, r.top() + 5, mark, mark), u.kind);
        if (u.shows(QStringLiteral("desc")) && !u.description.isEmpty()) {
            QFont small = f;
            small.setItalic(true);
            p.setFont(small);
            p.setPen(darkUnit ? QColor(210, 219, 230) : QColor(70, 78, 90));
            const QRectF beside(r.left() + 5 + mark + 4, r.top() + 5, std::max(0.0, r.width() - mark - 18), mark);
            p.drawText(beside, Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(u.description, Qt::ElideRight, static_cast<int>(beside.width())));
            p.setFont(f);
        }
        QFont bold = f;
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(darkUnit ? QColor(235, 239, 245) : QColor(25, 30, 36));
        if (u.shows(QStringLiteral("name"))) p.drawText(r.adjusted(6, 4, -6, -r.height() / 2), Qt::AlignHCenter | Qt::AlignBottom, u.name);
        p.setFont(f);
        QStringList sub;
        if (!u.pn.isEmpty() && u.shows(QStringLiteral("pn"))) sub << u.pn;
        if (!u.rev.isEmpty() && u.shows(QStringLiteral("rev"))) sub << QStringLiteral("rev ") + u.rev;
        p.setPen(darkUnit ? QColor(195, 205, 220) : QColor(80, 88, 100));
        if (!sub.isEmpty()) p.drawText(r.adjusted(6, r.height() / 2 + 2, -6, -4), Qt::AlignHCenter | Qt::AlignTop, sub.join(QStringLiteral("  ")));
    }
    // THE CABLES, each labelled at its middle with a dot for how its drawing stands.
    for (const SysCable& c : design_->cables) {
        const std::vector<QPointF> path = cablePath(c);
        if (path.size() < 2) continue;
        const bool on = zoneCables_.contains(c.id);
        QPolygonF line;
        for (const QPointF& q : path) line << toScreen(q);
        const auto index = static_cast<int>(&c - design_->cables.data());
        const bool scanning = checkStep_ == static_cast<int>(design_->units.size()) + index;
        if (scanning || issueOn({}, c.id)) {
            p.setBrush(Qt::NoBrush);
            QPen highlight(scanning ? QColor(35, 155, 220) : QColor(220, 110, 40), 5, Qt::DashLine);
            if (checkTimer_->isActive()) highlight.setDashOffset(checkPhase_);
            p.setPen(highlight);
            p.drawPolyline(line);
        }
        p.setPen(QPen(on ? QColor(40, 140, 230) : c.color.isValid() ? c.color : dark ? QColor(210, 218, 229) : QColor(60, 66, 76), on ? 3.0 : 2.0));
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
    if(tool_==SysTool::Link)if(const SysNote* note=design_->note(linkNote_)){
        const QRectF box(toScreen({note->x,note->y}),QSizeF(note->w*scale_,note->h*scale_));
        const QPointF edge(std::clamp(rubber_.x(),box.left(),box.right()),std::clamp(rubber_.y(),box.top(),box.bottom()));
        p.setPen(QPen(QColor(60,170,245),2,Qt::DashLine));p.drawLine(edge,rubber_);
        p.setPen(canvasInk);p.drawText(QRectF(12,height()-30,width()-24,22),Qt::AlignLeft|Qt::AlignVCenter,QStringLiteral("Click a unit, cable or port to link this note - Esc cancels"));
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
            p.setPen(QPen(pon ? QColor(40, 140, 230) : dark ? QColor(155, 170, 188) : QColor(60, 66, 76), pon ? 2.0 : 1.2));
            const QPointF ic = c - out * (s / 2.0);
            p.setBrush(inUsed ? QColor(70, 80, 95) : QColor(255, 255, 255));
            p.drawRect(QRectF(ic.x() - s / 2, ic.y() - s / 2, s, s));
            if (outUsed) {
                const QPointF oc = c + out * (s / 2.0);
                p.setBrush(QColor(70, 80, 95));
                p.drawRect(QRectF(oc.x() - s / 2, oc.y() - s / 2, s, s));
            }
            const QRectF worldBox = sysPortLabelRect(u, pt);
            const QRectF nameBox(toScreen(worldBox.topLeft()), worldBox.size() * scale_);
            if (pon) { p.setPen(QPen(QColor(40, 140, 230), 1, Qt::DashLine)); p.setBrush(Qt::NoBrush); p.drawRect(nameBox); }
            p.setPen(canvasInk);
            p.drawText(nameBox, Qt::AlignCenter, pt.name);
        }
    // THE NOTES, over everything: a pale yellow box with its text.
    for (const SysNote& n : design_->notes) {
        const QRectF r(toScreen(QPointF(n.x, n.y)), QSizeF(n.w * scale_, n.h * scale_));
        QPointF target;
        if (sysNoteTarget(*design_, n, target)) {
            const QPointF end = toScreen(target);
            const QPointF edge(std::clamp(end.x(), r.left(), r.right()), std::clamp(end.y(), r.top(), r.bottom()));
            p.setPen(QPen(dark ? QColor(190, 201, 215) : QColor(125, 130, 140), 1.2, Qt::DashLine));
            p.drawLine(edge, end);
            p.setBrush(dark ? QColor(190, 201, 215) : QColor(125, 130, 140)); p.drawEllipse(end, 3, 3);
        }
        const bool on = zoneNotes_.contains(n.id);
        p.setPen(QPen(on ? QColor(40, 140, 230) : QColor(200, 170, 60), on ? 2.0 : 1.0));
        p.setBrush(n.color.isValid() ? n.color : QColor(255, 247, 190));
        p.drawRect(r);
        p.setPen(QColor(60, 52, 20));
        p.save();
        p.setClipRect(r.adjusted(5, 3, -5, -3));
        const QColor bg = n.color.isValid() ? n.color : QColor(255, 247, 190);
        p.setPen(bg.lightness() < 120 ? Qt::white : QColor(45, 45, 45));
        QTextOption textOption(Qt::AlignLeft | Qt::AlignTop);
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        p.drawText(r.adjusted(6, 4, -6, -4), n.text, textOption);
        p.restore();
        if (on) {
            p.setBrush(QColor(40, 140, 230)); p.setPen(Qt::NoPen);
            p.drawRect(QRectF(r.bottomRight() - QPointF(4, 4), QSizeF(8, 8)));
        }
    }
    if (drag_ == Drag::Zone) {
        const bool crossing = zoneCrossing();
        QColor colour = crossing ? QColor(35, 170, 90) : QColor(40, 140, 230);
        p.setPen(QPen(colour, 1.2, crossing ? Qt::DashLine : Qt::SolidLine));
        colour.setAlpha(35); p.setBrush(colour);
        const QRectF zone(toScreen(pressWorld_), toScreen(zoneEnd_)); p.drawRect(zone.normalized());
        p.setPen(canvasInk);
        p.drawText(zone.normalized().topLeft() - QPointF(0, 5), crossing ? QStringLiteral("Crossing selection") : QStringLiteral("Window selection"));
    }
    // THE CHECK, traced over the real objects; findings stay outlined until the next edit.
    for (size_t i = 0; i < design_->units.size(); ++i) {
        const SysUnit& u = design_->units[i];
        const bool scanning = checkStep_ == static_cast<int>(i);
        if (!scanning && !issueOn(u.id, {})) continue;
        p.setBrush(Qt::NoBrush);
        QPen highlight(scanning ? QColor(35, 155, 220) : QColor(220, 110, 40),
                       checkTimer_->isActive() ? 3.5 + std::sin(checkPhase_) : 2.0, Qt::DashLine);
        if (checkTimer_->isActive()) highlight.setDashOffset(checkPhase_);
        p.setPen(highlight);
        p.drawRoundedRect(QRectF(toScreen(u.rect().topLeft()), u.rect().size() * scale_).adjusted(-4, -4, 4, 4), 5, 5);
    }
    if (locateOn_) {
        p.save(); p.setBrush(Qt::NoBrush);
        const double pulse = 0.5 + 0.5 * std::sin(locateClock_.elapsed()/90.0);
        QColor amber(255, 175, 25); amber.setAlphaF(.5 + .5*pulse);
        p.setPen(QPen(amber, 3));
        const QPointF centre = toScreen(locatePoint_);
        p.drawEllipse(centre, 16 + 5*pulse, 16 + 5*pulse);
        p.drawEllipse(centre, 29 + 5*pulse, 29 + 5*pulse);
        p.restore();
    }
    if (checkShown_) {
        // THE RULE REVIEW: long enough to follow, with a moving progress bar and a result for every enabled rule.
        p.save();
        QFont reviewFont = font(); reviewFont.setPointSizeF(9.0); p.setFont(reviewFont);
        const QRectF card = checkPanelRect();
        p.setPen(QPen(QColor(120, 165, 200), 1.3)); p.setBrush(QColor(255, 255, 255, 246));
        p.drawRoundedRect(card, 7, 7);
        const bool running = checkTimer_->isActive();
        const int elapsed = static_cast<int>(checkClock_.elapsed());
        const int activeRule = running ? std::min(kCheckRuleCount-1, elapsed / kRuleDuration) : kCheckRuleCount;
        p.setPen(QColor(30, 75, 110));
        reviewFont.setBold(true); p.setFont(reviewFont);
        p.drawText(card.adjusted(12, 6, -32, -card.height() + 30), Qt::AlignVCenter | Qt::AlignLeft,
                   running ? QStringLiteral("Reviewing design rules") : QStringLiteral("Design rule check complete"));
        p.drawText(QRectF(card.right() - 28, card.top() + 5, 22, 24), Qt::AlignCenter, QStringLiteral("x"));
        reviewFont.setBold(false); p.setFont(reviewFont);
        for (int rule = 0; rule < kCheckRuleCount; ++rule) {
            const QString id = QString::fromLatin1(kCheckRules[rule]);
            const bool disabled = design_->ruleSeverity(id)==0;
            const bool current = running && rule == activeRule && !disabled;
            int findings = 0, errors = 0, bypasses = 0;
            for (const SysIssue& accepted : design_->bypassed) if (accepted.rule==id) ++bypasses;
            for (const SysIssue& issue : checkIssues_) if (issue.rule == id) { ++findings; if (issue.level == 2) ++errors; }
            const QColor colour = disabled || rule > activeRule ? QColor(145, 150, 160)
                                : current ? QColor(35, 145, 225)
                                : errors ? QColor(205, 60, 50) : findings || bypasses ? QColor(185, 120, 25) : QColor(40, 150, 80);
            const double y = card.top() + 40 + rule * 27;
            if (current) { p.setPen(Qt::NoPen); p.setBrush(QColor(226, 240, 253)); p.drawRoundedRect(QRectF(card.left() + 6, y - 2, card.width() - 12, 25), 3, 3); }
            p.setPen(Qt::NoPen); p.setBrush(colour);
            const double radius = current ? 4.5 + std::sin(checkPhase_) : 4.0;
            p.drawEllipse(QPointF(card.left() + 17, y + 10), radius, radius);
            p.setPen(QColor(45, 55, 65));
            p.drawText(QRectF(card.left() + 29, y, card.width() - 145, 22), Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1(kCheckTitles[rule]));
            const QString status = disabled ? QStringLiteral("Not checked") : current ? QStringLiteral("Reviewing...")
                                 : rule > activeRule ? QStringLiteral("Pending")
                                 : findings ? QStringLiteral("%1 finding(s)").arg(findings) : bypasses ? QStringLiteral("%1 bypassed").arg(bypasses) : QStringLiteral("Passed");
            p.setPen(colour); p.drawText(QRectF(card.right() - 110, y, 98, 22), Qt::AlignRight | Qt::AlignVCenter, status);
        }
        p.setPen(Qt::NoPen); p.setBrush(QColor(228, 235, 243));
        const QRectF progress(card.left() + 12, card.bottom() - 16, card.width() - 24, 5);
        p.drawRoundedRect(progress, 2, 2);
        p.setBrush(QColor(35, 145, 225));
        p.drawRoundedRect(QRectF(progress.topLeft(), QSizeF(progress.width() * std::min(1.0, elapsed / (double(kCheckRuleCount) * kRuleDuration)), 5)), 2, 2);
        p.restore();
    }
    if (design_->units.empty() && design_->notes.empty()) {
        p.setPen(dark ? QColor(170, 181, 198) : QColor(120, 128, 140));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("The system diagram is empty - choose Unit on the ribbon (or press U) and click here to put the "
                                  "first unit"));
    }
}

void SystemCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if (checkShown_ && e->button() == Qt::LeftButton &&
        QRectF(checkPanelRect().right() - 30, checkPanelRect().top(), 30, 32).contains(e->position())) {
        clearCheck(); return;
    }
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
        const QString around = newKind_==QLatin1String("module")?QString():design_->unitAround(pressWorld_);
        const SysUnit* mother = design_->unit(around);
        const double w = mother ? std::min(160.0, std::max(60.0, std::floor(mother->w * 0.5 / 10.0) * 10.0)) : newKind_==QLatin1String("module")?360.0:160.0;
        const double h = mother ? std::min(100.0, std::max(40.0, std::floor(mother->h * 0.5 / 10.0) * 10.0)) : newKind_==QLatin1String("module")?240.0:100.0;
        const QString id = design_->addUnit(std::round((pressWorld_.x() - w / 2) / 10.0) * 10.0, std::round((pressWorld_.y() - h / 2) / 10.0) * 10.0);
        if (SysUnit* u = design_->unit(id)) {
            u->kind = newKind_;if(newKind_==QLatin1String("module"))u->opacity=.15;
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
    case SysTool::Link: {
        SysNote* note=design_->note(linkNote_);if(!note)return;
        QString cable;
        if(!portAt(pressWorld_,unit,port)){cable=cableAt(pressWorld_);if(cable.isEmpty())unit=unitAt(pressWorld_);}
        if(unit.isEmpty()&&cable.isEmpty())return;
        emit aboutToChange();note->unit=unit;note->port=port;note->cable=cable;
        const QString id=note->id;selectNote(id);emit changed();setTool(SysTool::Select);return;
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
        const QString hitUnit = unitAt(pressWorld_), hitNote = noteAt(pressWorld_);
        if (selectionCount() > 1 && (zoneUnits_.contains(hitUnit) || zoneNotes_.contains(hitNote))) {
            drag_ = Drag::Group; groupDelta_ = {}; return;
        }
        // A NOTE CORNER: resize its text container.
        if (!hitNote.isEmpty()) {
            const SysNote* n = design_->note(hitNote);
            if (std::hypot(pressWorld_.x() - n->x - n->w, pressWorld_.y() - n->y - n->h) < 9.0 / scale_) {
                selectNote(hitNote); drag_ = Drag::NoteSize; return;
            }
        }
        if (!hitNote.isEmpty()) {
            const SysNote* n = design_->note(hitNote);
            selectNote(hitNote); drag_ = Drag::Note;
            grabOffset_ = pressWorld_ - QPointF(n->x, n->y); return;
        }
        // A DESIGNATOR: moved independently from the physical port.
        if (portLabelAt(pressWorld_, unit, port)) {
            select(unit, port, {}); drag_ = Drag::PortLabel;
            const SysPort* pt = design_->unit(unit)->port(port);
            grabOffset_ = QPointF(pt->labelDx, pt->labelDy); return;
        }
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
            sizeFrom_ = design_->unit(sized)->rect();
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
            select(QString(),QString(),c);const auto path=cablePath(*design_->cable(c));
            double closest=std::numeric_limits<double>::infinity();
            for(size_t i=1;i+2<path.size();++i){const double distance=segDistance(pressWorld_,path[i],path[i+1]);if(distance<closest){closest=distance;cableDragA_=path[i];cableDragB_=path[i+1];}}
            drag_=Drag::CableMove;return;
        }
        select(QString(), QString(), QString());
        drag_ = Drag::Zone; zoneEnd_ = pressWorld_; update();
        return;
    }
    }
}

void SystemCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (!design_) return;
    if(tool_==SysTool::Link&&drag_!=Drag::Pan){rubber_=e->position();update();return;}
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
            design_->moveUnit(u->id, nx - u->x, ny - u->y);
            arrangeSystemPorts(*design_,{u->id});
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
    case Drag::Zone:
        zoneEnd_ = w; update(); break;
    case Drag::Group: {
        const QPointF total(std::round((w.x() - pressWorld_.x()) / 10.0) * 10.0,
                            std::round((w.y() - pressWorld_.y()) / 10.0) * 10.0);
        const QPointF step = total - groupDelta_;
        if (step.isNull()) break;
        announce(); groupDelta_ = total;
        for (const QString& id : zoneUnits_) {
            bool ancestorSelected = false;
            for (const QString& parent : zoneUnits_) if (design_->isWithin(id, parent)) ancestorSelected = true;
            if (!ancestorSelected) design_->moveUnit(id, step.x(), step.y());
        }
        for (const QString& id : zoneNotes_) if (SysNote* n = design_->note(id)) { n->x += step.x(); n->y += step.y(); }
        update(); break;
    }
    case Drag::NoteSize:
        if (SysNote* n = design_->note(selNote_); n && moved_) {
            announce(); n->w = std::max(60.0, w.x() - n->x); n->h = std::max(30.0, w.y() - n->y); update();
        }
        break;
    case Drag::PortLabel:
        if (SysUnit* u = design_->unit(selUnit_)) if (SysPort* pt = u->port(selPort_); pt && moved_) {
            announce(); const QPointF offset = grabOffset_ + w - pressWorld_;
            pt->labelDx = offset.x(); pt->labelDy = offset.y(); update();
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
    case Drag::CableMove:
        if(SysCable* cable=design_->cable(selCable_);cable&&moved_){
            announce();const QPointF translation=w-pressWorld_;
            cable->waypoints={cableDragA_+translation,cableDragB_+translation};
            arrangeSystemPorts(*design_,{cable->aUnit,cable->bUnit});update();
        }
        break;
    case Drag::Cable:
        rubber_ = e->position();
        update();
        break;
    case Drag::None: {
        QString unit, port, sized;
        const int how = tool_ == SysTool::Select ? sizeHandleAt(w, sized) : 0;
        const QString note = noteAt(w);
        const SysNote* n = design_->note(note);
        const bool noteCorner = n && std::hypot(w.x() - n->x - n->w, w.y() - n->y - n->h) < 9.0 / scale_;
        const bool label = portLabelAt(w, unit, port);
        setCursor(tool_ == SysTool::Select && noteCorner ? Qt::SizeFDiagCursor
                  : tool_ == SysTool::Select && label ? Qt::SizeAllCursor
                  : portAt(w, unit, port) ? (tool_ == SysTool::Select ? Qt::SizeAllCursor : Qt::CrossCursor)
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
        zoneEnd_ = toWorld(e->position());
        selectZone(QRectF(pressWorld_, zoneEnd_).normalized(), zoneCrossing()); return;
    }
    if ((was == Drag::Unit || was == Drag::Note || was == Drag::Size || was == Drag::Port || was == Drag::NoteSize ||
         was == Drag::PortLabel || was == Drag::Group || was == Drag::CableMove) && moveAnnounced_) {
        // A UNIT DROPPED: into the unit it is now in, or out of the one it was in.
        if (was == Drag::Unit){settleUnit(selUnit_);arrangeSystemPorts(*design_,{selUnit_});}
        if(was==Drag::Group)arrangeSystemPorts(*design_,QStringList(zoneUnits_.values()));
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
        case Qt::Key_L: startNoteLink(); return;
        default: break;
        }
    }
    QWidget::keyPressEvent(e);
}

} // namespace Edraw_Desktop
