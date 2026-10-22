#include "app/WiringDialog.h"

#include "app/HarnessBuilderDialog.h"
#include "app/DrawingPreview.h"
#include "app/WizardFlowLayout.h"
#include "harness/CableSpec.h"
#include "analysis/Cma.h"
#include <QRegularExpression>
#include "harness/Harness.h"
#include "harness/Harness3D.h"
#include "harness/PartsLibrary.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QJsonDocument>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace Loopwire {

namespace {

constexpr double kCell = 26.0;      // a pin's square, and the gap round it
constexpr int kPanelField = 210;   // a field of the panel, across
constexpr double kRow = 24.0;       // a wire's bar, one under another
constexpr double kMargin = 18.0;
constexpr double kTrunkFan = 56.0;  // a cable shown as one harness: how far in from the lane's edge its wires fan into the trunk

class CanvasCorner : public QWidget {
public:
    explicit CanvasCorner(QWidget* viewport) : QWidget(viewport) {
        viewport->installEventFilter(this);
        setObjectName(QStringLiteral("wiringCmaOverlay"));
        setStyleSheet(QStringLiteral("#wiringCmaOverlay { background:#263443; border:1px solid #536579; border-radius:4px; } #wiringCmaOverlay QLabel { color:#dce4ee; background:transparent; border:none; }"));
    }
    void place() {
        adjustSize();
        move(std::max(0, parentWidget()->width()-width()-10), std::max(0, parentWidget()->height()-height()-10));
        raise();
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) place();
        return QWidget::eventFilter(watched, event);
    }
};

class WiringGroups : public QListWidget {
public:
    explicit WiringGroups(QWidget* parent) : QListWidget(parent) {
        setDragEnabled(true); setAcceptDrops(true); setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
    }
    std::function<void(const QString&, const QString&)> promote;
protected:
    void startDrag(Qt::DropActions) override {
        const auto* item = currentItem();
        if (!item || !item->data(Qt::UserRole+3).toBool()) return;
        auto* mime = new QMimeData;
        mime->setData("application/x-loopwire-group", QJsonDocument(QJsonObject{
            {"kind", item->data(Qt::UserRole+1).toString()}, {"name", item->data(Qt::UserRole+2).toString()}}).toJson());
        QDrag drag(this); drag.setMimeData(mime); drag.exec(Qt::MoveAction);
    }
    bool canDrop(const QMimeData* mime, const QPoint& at) const {
        const auto* item = itemAt(at);
        return mime->hasFormat("application/x-loopwire-group") && (!item || !item->data(Qt::UserRole+3).toBool() || at.x()<24);
    }
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat("application/x-loopwire-group")) event->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        if (canDrop(event->mimeData(), event->position().toPoint())) event->acceptProposedAction();
        else event->ignore();
    }
    void dropEvent(QDropEvent* event) override {
        if (!canDrop(event->mimeData(), event->position().toPoint())) return;
        const auto payload = QJsonDocument::fromJson(event->mimeData()->data("application/x-loopwire-group")).object();
        event->setDropAction(Qt::MoveAction); event->accept();
        if (promote) promote(payload.value("kind").toString(), payload.value("name").toString());
    }
};

QString pinKey(const QString& conn, int pin) { return QStringLiteral("%1:%2").arg(conn).arg(pin); }

bool splitPinKey(const QString& key, QString& conn, int& pin) {
    const int at = key.lastIndexOf(QLatin1Char(':'));
    if (at <= 0) return false;
    conn = key.left(at);
    pin = key.mid(at + 1).toInt();
    return pin > 0;
}

// A wire's colour as a pen: its base, and its stripe where it has one.
QColor baseColour(const QString& code) { return QColor(wireColorHex(code.section(QLatin1Char('-'), 0, 0))); }
QColor stripeColour(const QString& code) {
    const QString stripe = code.section(QLatin1Char('-'), 1, 1);
    return stripe.isEmpty() ? QColor() : QColor(wireColorHex(stripe));
}

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF d = b - a;
    const double len = d.x() * d.x() + d.y() * d.y();
    const double t = len < 1e-9 ? 0.0 : std::clamp(((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / len, 0.0, 1.0);
    const QPointF q = a + d * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

} // namespace

// ---- The canvas ----------------------------------------------------------------------------------------------

WiringCanvas::WiringCanvas(HarnessDef& def, QWidget* parent) : QWidget(parent), def_(def) {
    setObjectName(QStringLiteral("wiringCanvas"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    // HELD STILL ON A PIN 0.3 S - a hand's slight wobble allowed: the pin caught, to be put elsewhere in its connector's pin
    // stack. ON A WIRE'S BAR: the wire caught, to be moved in the list with its pins.
    holdTimer_ = new QTimer(this);
    holdTimer_->setSingleShot(true);
    holdTimer_->setInterval(300);
    connect(holdTimer_, &QTimer::timeout, this, [this] {
        if (!pressing_ || QLineF(pressAt_, now_).length() > 8.0 || (pressPin_.isEmpty() && pressBar_.isEmpty())) return;
        carrying_ = true;
        carryingBar_ = pressPin_.isEmpty();
        setCursor(Qt::ClosedHandCursor);
        emit said(carryingBar_ ? QStringLiteral("Wire caught - let it go where it goes in the list; its pins move with it")
                               : QStringLiteral("Pin caught - let it go on the pin whose place it takes in the pin stack"));
        update();
    });
}

void WiringCanvas::setSelectedBricks(const QStringList& ids) {
    selectedBricks_ = ids;
    update();
    emit brickSelectionChanged();
}

QString WiringCanvas::brickAt(const QPointF& p) const {
    for (const Brick& b : bricks_)
        if (b.rect.contains(p)) return b.conn;
    return {};
}

void WiringCanvas::setShown(const QStringList& connIds, bool twoMode) {
    shown_ = connIds;
    twoMode_ = twoMode;
    pending_.clear();
    relayout();
}

void WiringCanvas::setTool(Tool tool) {
    tool_ = tool;
    pending_.clear();
    setCursor(tool == Tool::Connect ? Qt::CrossCursor : tool == Tool::Select ? Qt::ArrowCursor : Qt::PointingHandCursor);
    update();
}

QStringList WiringCanvas::shownWires() const {
    QStringList out;
    for (const HarnessWire& w : def_.wires)
        if ((shown_.contains(w.fromConn) && shown_.contains(w.toConn) && w.fromPin > 0 && w.toPin > 0) ||
            ((harnessIsJointEndpoint(w.fromConn) || harnessIsJointEndpoint(w.toConn)) && !jointPaths_.value(w.id).empty())) out << w.id;
    return out;
}

void WiringCanvas::setSelection(const QStringList& ids) {
    selection_ = ids;
    update();
    emit selectionChanged();
}

void WiringCanvas::setGhost(const QString& fromConn, int fromPin, const QString& toConn, int toPin) {
    ghostOn_ = fromPin > 0 && toPin > 0;
    ghostFrom_ = fromConn;
    ghostTo_ = toConn;
    ghostFromPin_ = fromPin;
    ghostToPin_ = toPin;
    update();
}

void WiringCanvas::clearGhost() {
    ghostOn_ = false;
    update();
}

bool WiringCanvas::ghost(QString& fromConn, int& fromPin, QString& toConn, int& toPin) const {
    if (!ghostOn_) return false;
    fromConn = ghostFrom_;
    fromPin = ghostFromPin_;
    toConn = ghostTo_;
    toPin = ghostToPin_;
    return true;
}

QStringList WiringCanvas::wiresOn(const QString& conn, int pin) const {
    QStringList out;
    for (const HarnessWire& w : def_.wires)
        if ((w.fromConn == conn && w.fromPin == pin) || (w.toConn == conn && w.toPin == pin)) out << w.id;
    return out;
}

const WiringCanvas::Brick* WiringCanvas::brickOf(const QString& conn) const {
    for (const Brick& b : bricks_)
        if (b.conn == conn) return &b;
    return nullptr;
}

QPointF WiringCanvas::pinCentre(const QString& conn, int pin) const {
    const Brick* b = brickOf(conn);
    return b && b->pins.contains(pin) ? b->pins.value(pin).center() : QPointF();
}

// Where a wire leaves a pin: the side of its square facing the lines.
QPointF WiringCanvas::pinExit(const QString& conn, int pin, int& side) const {
    const Brick* b = brickOf(conn);
    side = b && b->left ? -1 : 1;
    if (!b || !b->pins.contains(pin)) return {};
    const QRectF r = b->pins.value(pin);
    return b->left ? QPointF(r.right(), r.center().y()) : QPointF(r.left(), r.center().y());
}

QPointF WiringCanvas::barCentre(const QString& wireId) const {
    for (const Bar& bar : bars_)
        if (bar.wire == wireId) return QPointF((laneLeft_ + laneRight_) / 2.0, bar.y);
    const auto path = jointPaths_.value(wireId);
    size_t best = 0; double longest = 0;
    for (size_t i=1;i<path.size();++i)
        if (std::abs(path[i].y-path[i-1].y)<1e-6 && distance(path[i],path[i-1])>longest) {
            best=i;longest=distance(path[i],path[i-1]);
        }
    if (best) return QPointF((path[best].x+path[best-1].x)*.5,path[best].y);
    return {};
}

void WiringCanvas::relayout() {
    bricks_.clear();
    bars_.clear();
    std::vector<HarnessWire> layoutWires = def_.wires;
    for (const auto& j : def_.joints) {
        HarnessWire w;
        w.id = j.id; w.fromConn = j.fromConn; w.fromPin = j.fromPin;
        w.toConn = j.toConn; w.toPin = j.toPin;
        layoutWires.push_back(w);
    }
    auto layoutRank = [this](const QString& id) {
        for (size_t i=0;i<def_.wires.size();++i) {
            const auto& w=def_.wires[i];
            if (w.id==id || w.fromConn==harnessJointEndpointKey(id) || w.toConn==harnessJointEndpointKey(id)) return i;
        }
        return def_.wires.size();
    };
    std::stable_sort(layoutWires.begin(),layoutWires.end(),[&](const auto& a,const auto& b) { return layoutRank(a.id)<layoutRank(b.id); });
    // EACH CONNECTOR A BRICK: its pins where its pinout drawing has them, or a numbered grid - in columns of a dozen.
    auto makeBrick = [this](const HarnessConnector& c, bool left) {
        Brick b;
        b.conn = c.id;
        b.left = left;
        const int n = std::max(1, c.pinCount);
        const std::vector<int> order = harnessPinSequence(c);
        const LibraryPart* part = c.libPart.isEmpty() ? nullptr : harnessBuilderLibrary().byId(c.libPart);
        const PartPins face = part ? partPinsOf(part) : PartPins();
        int placed = 0;
        for (const PartPins::Pin& p : face.each) placed += p.placed && p.number >= 1 && p.number <= n ? 1 : 0;
        if (placed == n && c.pinOrder.empty()) {
            // AS ITS FACE HAS THEM: a square for the face, each pin where the drawing puts it.
            const double side = std::max(3.0, std::ceil(std::sqrt(static_cast<double>(n))) + 1.0) * kCell;
            b.rect = QRectF(0, 0, side + kCell, side + kCell * 1.6);
            for (const PartPins::Pin& p : face.each)
                if (p.placed && p.number >= 1 && p.number <= n)
                    b.pins.insert(p.number, QRectF(kCell / 2 + side / 2 + p.u * side / 2 - kCell * 0.4,
                                                   kCell * 1.3 + side / 2 - p.v * side / 2 - kCell * 0.4, kCell * 0.8, kCell * 0.8));
        } else {
            const int cols = std::max(1, static_cast<int>(std::ceil(n / 12.0)));
            const int rows = static_cast<int>(std::ceil(n / static_cast<double>(cols)));
            b.rect = QRectF(0, 0, cols * kCell + kCell * 0.8, rows * kCell + kCell * 1.8);
            for (int i = 0; i < static_cast<int>(order.size()); ++i) {
                // The column nearest the lines last, so its pins are the ones the lines reach first.
                const int col = left ? i / rows : cols - 1 - i / rows;
                const int row = i % rows;
                b.pins.insert(order[static_cast<size_t>(i)], QRectF(kCell * 0.4 + col * kCell + 2, kCell * 1.4 + row * kCell + 2, kCell - 4, kCell - 4));
            }
        }
        return b;
    };
    std::vector<Brick> leftCol, rightCol;
    for (int i = 0; i < shown_.size(); ++i) {
        const HarnessConnector* c = harnessConnById(def_, shown_[i]);
        if (!c) continue;
        const bool left = twoMode_ ? i == 0 : c->side == HarnessSide::Left;
        (left ? leftCol : rightCol).push_back(makeBrick(*c, left));
    }
    double leftW = 0.0, rightW = 0.0;
    for (const Brick& b : leftCol) leftW = std::max(leftW, b.rect.width());
    for (const Brick& b : rightCol) rightW = std::max(rightW, b.rect.width());
    const QStringList wires = [&] {
        QStringList ids;
        for (const HarnessWire& w : layoutWires) {
            auto shownHere = [&](const QString& conn) {
                return std::any_of(leftCol.begin(), leftCol.end(), [&](const Brick& b) { return b.conn == conn; }) ||
                       std::any_of(rightCol.begin(), rightCol.end(), [&](const Brick& b) { return b.conn == conn; });
            };
            if (shownHere(w.fromConn) && shownHere(w.toConn) && w.fromPin > 0 && w.toPin > 0) ids << w.id;
        }
        // A CABLE SHOWN AS ONE HARNESS: its rows together, where its first one is - no other wire's bar inside its trunk.
        for (const QString& cable : trunks_) {
            QStringList members;
            for (const QString& id : ids)
                for (const HarnessWire& w : layoutWires)
                    if (w.id == id && w.bundle == cable) members << id;
            if (members.size() < 2) continue;
            const int at = static_cast<int>(ids.indexOf(members.front()));
            for (const QString& id : members) ids.removeAll(id);
            for (int k = 0; k < members.size(); ++k) ids.insert(at + k, members[k]);
        }
        return ids;
    }();
    // THE LANE BETWEEN THEM: as wide as the longest bar's words want, and the twist and screen marks either side of them -
    // the connectors side by side across it, not pushed to the window's edges - its bars one under another below the
    // titles. The two, with the lane, in the middle of the width there is.
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.5));
    const QFontMetricsF metrics(small);
    double words = 120.0;
    for (const HarnessWire& w : layoutWires)
        if (wires.contains(w.id)) {
            QString text = QStringLiteral("%1  %2").arg(w.color, wireGaugeLabel(w.gauge, true));
            if (!w.label.isEmpty()) text += QStringLiteral("  ") + w.label;
            words = std::max(words, metrics.horizontalAdvance(text) + 24.0);
        }
    const double laneW = std::max(connectorSpacing_, words + 140.0);
    const double whole = 2 * kMargin + leftW + rightW + 120.0 + laneW;
    const double shift = std::max(0.0, (width() / zoom_ - whole) / 2.0);
    laneLeft_ = shift + kMargin + leftW + 60.0;
    laneRight_ = laneLeft_ + laneW;
    barTop_ = kMargin + 40.0;
    auto stack = [&](std::vector<Brick>& col, double x) {
        double y = kMargin + 24.0;
        for (Brick& b : col) {
            b.rect.moveTo(x + (b.left ? leftW - b.rect.width() : 0.0), y + def_.layout.markAt.value(QStringLiteral("connectorOffset:")+b.conn)*kCell);
            for (auto it = b.pins.begin(); it != b.pins.end(); ++it) it.value().translate(b.rect.topLeft());
            y += b.rect.height() + 28.0;
            bricks_.push_back(b);
        }
        return y;
    };
    const double leftBottom = stack(leftCol, shift + kMargin);
    const double rightBottom = stack(rightCol, laneRight_ + 60.0);
    int sameLanes[2] = {0, 0};
    for (int i = 0; i < wires.size(); ++i) {
        Bar bar;
        bar.wire = wires[i];
        bar.y = barTop_ + layoutRank(bar.wire) * kRow;
        for (const HarnessWire& w : layoutWires)
            if (w.id == bar.wire) {
                bar.from = pinExit(w.fromConn, w.fromPin, bar.fromSide);
                bar.to = pinExit(w.toConn, w.toPin, bar.toSide);
            }
        // ONE SIDE TO THE SAME SIDE: a lane of its own beside that side; its row the middle of its two pins.
        if (bar.fromSide == bar.toSide) {
            bar.lane = sameLanes[bar.fromSide < 0 ? 0 : 1]++;
            // Just off its pins: out of each, along a lane of its own beside them, up or down to the other's row, and in.
            const double pinsEdge = bar.fromSide < 0 ? std::max(bar.from.x(), bar.to.x()) : std::min(bar.from.x(), bar.to.x());
            bar.laneX = pinsEdge - bar.fromSide * (24.0 + bar.lane * 12.0);
            bar.y = (bar.from.y() + bar.to.y()) / 2.0;
        }
        bars_.push_back(bar);
    }
    for(int side : {-1,1}) {
        std::vector<Bar*> loops;
        for(auto& bar:bars_)if(bar.fromSide==side && bar.toSide==side)loops.push_back(&bar);
        std::stable_sort(loops.begin(),loops.end(),[](const auto* a,const auto* b){return std::abs(a->to.y()-a->from.y())<std::abs(b->to.y()-b->from.y());});
        for(size_t i=0;i<loops.size();++i) {
            auto& bar=*loops[i];bar.lane=static_cast<int>(i);
            const double edge=side<0?std::max(bar.from.x(),bar.to.x()):std::min(bar.from.x(),bar.to.x());
            bar.laneX=edge-side*(24+i*18);
        }
    }
    // Order the routing rows by the actual left pin positions. This only changes
    // geometry: endpoint identities, connector pin order and the wire table stay intact.
    std::stable_sort(bars_.begin(),bars_.end(),[](const auto& a,const auto& b) {
        const double ay=a.fromSide<0?a.from.y():a.to.y();
        const double by=b.fromSide<0?b.from.y():b.to.y();
        if(std::abs(ay-by)>1e-6)return ay<by;
        return (a.fromSide<0?a.to.y():a.from.y())<(b.fromSide<0?b.to.y():b.from.y());
    });
    double previousY=-1e20;
    for(auto& bar:bars_)if(bar.fromSide!=bar.toSide) {
        const double desired=std::min(bar.from.y(),bar.to.y());
        bar.y=std::max(desired,previousY+kRow);
        previousY=bar.y;
    }
    QHash<QString,double> branchY;
    struct ReservedRow {double y, root;};
    std::vector<ReservedRow> reservedRows;
    QHash<QString,int> branchCounts;
    for(const auto& wire:def_.wires) {
        const bool from=harnessIsJointEndpoint(wire.fromConn),to=harnessIsJointEndpoint(wire.toConn);
        if(from==to)continue;
        const QString key=from?wire.fromConn:wire.toConn,conn=from?wire.toConn:wire.fromConn;
        const int pin=from?wire.toPin:wire.fromPin;
        for(const auto& j:def_.joints)if(j.id==harnessJointEndpointWire(key)) {
            if((j.fromConn==conn && j.fromPin==pin)||(j.toConn==conn && j.toPin==pin))break;
            for(const auto& bar:bars_)if(bar.wire==j.id) {
                int side=0;const auto pin=pinExit(conn,from?wire.toPin:wire.fromPin,side);
                const double natural=pin.y()<(bar.fromSide==side?bar.from.y():bar.to.y())?0:1;
                const double direction=def_.layout.markAt.value(QStringLiteral("jointSide:")+j.id+QStringLiteral(":")+conn,natural)>.5?1:-1;
                const double y=bar.y+direction*kRow*++branchCounts[key+conn];
                branchY[wire.id]=y;reservedRows.push_back({y,bar.y});break;
            }
        }
    }
    std::vector<double> occupied;
    for(const auto& row:reservedRows)occupied.push_back(row.y);
    for(const auto& bar:bars_)if(std::any_of(def_.joints.begin(),def_.joints.end(),[&](const auto& j){return j.id==bar.wire;}))occupied.push_back(bar.y);
    double lastRow=-1e20;
    for(auto& bar:bars_) {
        if(bar.fromSide==bar.toSide || std::any_of(def_.joints.begin(),def_.joints.end(),[&](const auto& j){return j.id==bar.wire;}))continue;
        bar.y=std::max(bar.y,lastRow+kRow);
        for(size_t pass=0;pass<=occupied.size();++pass) {
            bool moved=false;
            for(double y:occupied)if(std::abs(bar.y-y)<kRow-.01){bar.y=y+kRow;moved=true;}
            if(!moved)break;
        }
        occupied.push_back(bar.y);lastRow=bar.y;
    }
    HarnessPaths seeds;
    for (const Bar& bar : bars_) {
        std::vector<QPointF> pts;
        if (bar.fromSide == bar.toSide)
            pts = {bar.from, {bar.laneX, bar.from.y()}, {bar.laneX, bar.to.y()}, bar.to};
        else
            pts = {bar.from, {bar.from.x() - bar.fromSide * 14, bar.from.y()},
                   {bar.fromSide < 0 ? laneLeft_ : laneRight_, bar.y},
                   {bar.toSide < 0 ? laneLeft_ : laneRight_, bar.y},
                   {bar.to.x() - bar.toSide * 14, bar.to.y()}, bar.to};
        if(bar.fromSide!=bar.toSide) {
            auto protect=[&](bool from) {
                const auto tip=from?bar.from:bar.to;
                const int side=from?bar.fromSide:bar.toSide;
                bool below=false;
                for(const auto& loop:bars_)if(loop.fromSide==side && loop.toSide==side && tip.y()>std::max(loop.from.y(),loop.to.y())+1)below=true;
                if(!below)return;
                double x=side<0?laneLeft_:laneRight_;
                for(const auto& loop:bars_)if(loop.fromSide==side && loop.toSide==side)
                    x=side<0?std::max(x,loop.laneX+18):std::min(x,loop.laneX-18);
                if(from){pts[1]={x,tip.y()};pts[2]={x,bar.y};}
                else{pts[pts.size()-2]={x,tip.y()};pts[pts.size()-3]={x,bar.y};}
            };
            protect(true);protect(false);
        }
        for (const auto& p : pts) seeds[bar.wire].push_back({p.x(), p.y()});
    }
    jointPaths_ = harnessJointPaths(def_, seeds, [this](const QString& conn, int pin) -> std::optional<Vec2> {
        const auto* b = brickOf(conn);
        if (!b || !b->pins.contains(pin)) return {};
        int side; const auto p = pinExit(conn, pin, side);
        return Vec2{p.x(), p.y()};
    }, kCell);
    QHash<QString,int> branchRows;
    for (const auto& w : def_.wires) {
        const bool fromJoint=harnessIsJointEndpoint(w.fromConn),toJoint=harnessIsJointEndpoint(w.toConn);
        if (fromJoint==toJoint) continue;
        const QString joint=fromJoint?w.fromConn:w.toConn,conn=fromJoint?w.toConn:w.fromConn;
        const int pin=fromJoint?w.toPin:w.fromPin;
        const auto j=std::find_if(def_.joints.begin(),def_.joints.end(),[&](const auto& node) {return node.id==harnessJointEndpointWire(joint);});
        if(j==def_.joints.end() || (j->fromConn==conn && j->fromPin==pin) || (j->toConn==conn && j->toPin==pin))continue;
        auto path=jointPaths_.value(w.id);if(path.empty())continue;
        int side=0;const QPointF exit=pinExit(conn,pin,side);const Vec2 node=fromJoint?path.front():path.back();
        const int row=++branchRows[joint+QString::number(side)];
        const double direction=def_.layout.markAt.value(QStringLiteral("jointSide:")+j->id+QStringLiteral(":")+conn,1)>.5?1:-1;
        const double y=branchY.value(w.id,node.y+direction*kRow*row);
        path={{exit.x(),exit.y()},{exit.x()-side*14,exit.y()},
              {side<0?laneLeft_:laneRight_,y},{node.x,y},node};
        if(fromJoint)std::reverse(path.begin(),path.end());
        jointPaths_[w.id]=path;
    }
    content_ = QSize(static_cast<int>(whole),
                     static_cast<int>(std::max({leftBottom, rightBottom, barTop_ + (def_.wires.size()+def_.joints.size()) * kRow + 2 * kRow}) + kMargin));
    setMinimumSize(QSize(static_cast<int>(content_.width() * zoom_), static_cast<int>(content_.height() * zoom_)));
    updateGeometry();
    update();
}

QSize WiringCanvas::sizeHint() const { return QSize(static_cast<int>(content_.width() * zoom_), static_cast<int>(content_.height() * zoom_)); }

// A WIDER WINDOW, a wider lane: laid out again for the width there is.
void WiringCanvas::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (e->oldSize().width() != e->size().width()) relayout();
}

QString WiringCanvas::pinAt(const QPointF& p) const {
    for (const Brick& b : bricks_)
        for (auto it = b.pins.begin(); it != b.pins.end(); ++it)
            if (it.value().adjusted(-2, -2, 2, 2).contains(p)) return pinKey(b.conn, it.key());
    return {};
}

void WiringCanvas::setTrunk(const QString& cable, bool on) {
    if (on) trunks_.insert(cable);
    else trunks_.remove(cable);
    relayout();
}

QString WiringCanvas::barAt(const QPointF& p) const {
    // ON A TRUNK: the cable's first wire - the rest of the cable lit with it.
    for (const QString& cable : trunks_) {
        double top = 1e18, bottom = -1e18;
        QString first;
        for (const Bar& bar : bars_)
            for (const HarnessWire& w : def_.wires)
                if (w.id == bar.wire && w.bundle == cable && bar.fromSide != bar.toSide) {
                    top = std::min(top, bar.y);
                    bottom = std::max(bottom, bar.y);
                    if (first.isEmpty()) first = w.id;
                }
        const double y = (top + bottom) / 2.0;
        if (!first.isEmpty() && std::abs(p.y() - y) <= 8.0 && p.x() >= laneLeft_ + kTrunkFan && p.x() <= laneRight_ - kTrunkFan) return first;
    }
    const QString joined = jointWireAt(p);
    if (!joined.isEmpty()) return joined;
    for (const Bar& bar : bars_) {
        if (std::none_of(def_.wires.begin(), def_.wires.end(), [&](const auto& w) { return w.id == bar.wire; })) continue;
        // A wire in a trunk is caught on the trunk only.
        bool inTrunk = false;
        for (const HarnessWire& w : def_.wires)
            inTrunk = inTrunk || (bar.fromSide != bar.toSide && w.id == bar.wire && !w.bundle.isEmpty() && trunks_.contains(w.bundle));
        if (inTrunk) continue;
        if (bar.fromSide == bar.toSide) {
            const double x = bar.laneX;
            if (std::abs(p.x() - x) <= 5.0 && p.y() >= std::min(bar.from.y(), bar.to.y()) - 3 &&
                p.y() <= std::max(bar.from.y(), bar.to.y()) + 3)
                return bar.wire;
            continue;
        }
        if (std::abs(p.y() - bar.y) <= kRow / 2.0 - 1.0 && p.x() >= laneLeft_ && p.x() <= laneRight_) return bar.wire;
    }
    return {};
}

// THE WORDS OF A WIRE BETWEEN TWO CONNECTORS ON ONE SIDE: so wide, beside its upright run.
constexpr double kStub = 96.0;

QStringList WiringCanvas::barsAcross(const QPointF& a, const QPointF& b) const {
    QStringList out;
    const QLineF stroke(a, b);
    for (const Bar& bar : bars_) {
        if (std::none_of(def_.wires.begin(), def_.wires.end(), [&](const auto& w) { return w.id == bar.wire; })) continue;
        if (bar.fromSide == bar.toSide) {
            // Its upright run, crossed.
            const double x = bar.laneX;
            const QLineF run(QPointF(x, bar.from.y()), QPointF(x, bar.to.y()));
            if (stroke.intersects(run, nullptr) == QLineF::BoundedIntersection) out << bar.wire;
            continue;
        }
        if ((a.y() - bar.y) * (b.y() - bar.y) > 0.0 || std::abs(a.y() - b.y()) < 1e-9) continue;
        const double t = (bar.y - a.y()) / (b.y() - a.y());
        const double x = a.x() + (b.x() - a.x()) * t;
        double lo = laneLeft_, hi = laneRight_;
        if (bar.fromSide == bar.toSide) {
            if (bar.fromSide < 0) hi = laneLeft_ + kStub;
            else lo = laneRight_ - kStub;
        }
        if (x >= lo - 4.0 && x <= hi + 4.0) out << bar.wire;
    }
    for (const auto& w : def_.wires) {
        const auto path = jointPath(w);
        for (size_t i=1;i<path.size();++i)
            if (stroke.intersects(QLineF(path[i-1],path[i]), nullptr) == QLineF::BoundedIntersection) {
                if (!out.contains(w.id)) out << w.id;
                break;
            }
    }
    return out;
}

QList<int> WiringCanvas::screenEnds(const QString& group) const {
    bool allLeads=true;
    for(const auto& w:def_.wires)if(w.shieldGroup==group && !harnessIsOpenEndpoint(w.fromConn) && !harnessIsOpenEndpoint(w.toConn))allLeads=false;
    QList<int> ends;
    for(int i=0;i<(allLeads?1:std::max(1,def_.layout.wireShieldAmount));++i)ends<<i;
    return ends;
}

void WiringCanvas::setConnectorSpacing(double spacing) {
    connectorSpacing_=std::clamp(spacing,260.0,2400.0);
    relayout();updateGeometry();update();
}

void WiringCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if(event->button()!=Qt::LeftButton){QWidget::mouseDoubleClickEvent(event);return;}
    QString conn;int pin=0;
    if(!splitPinKey(pinAt(event->position()/zoom_),conn,pin) || !wiresOn(conn,pin).isEmpty())return;
    pressing_=false;pending_.clear();pressPin_.clear();pressBar_.clear();holdTimer_->stop();
    emit terminatePinAsked(conn,pin);update();event->accept();
}

QMultiHash<QString, QRectF> WiringCanvas::markRects(bool screens) const {
    {
        QMultiHash<QString,QRectF> result;
        QSet<QString> groups;
        for(const auto& w:def_.wires)if(!(screens?w.shieldGroup:w.twistGroup).isEmpty())groups.insert(screens?w.shieldGroup:w.twistGroup);
        for(const auto& group:groups) {
            double low=1e20,high=-1e20;
            double sharedLow=-1e20,sharedHigh=1e20;
            for(const auto& w:def_.wires)if((screens?w.shieldGroup:w.twistGroup)==group) {
                double memberLow=1e20,memberHigh=-1e20;
                for(const auto& p:wirePath(w.id)) {
                    low=std::min(low,p.x());high=std::max(high,p.x());
                    memberLow=std::min(memberLow,p.x());memberHigh=std::max(memberHigh,p.x());
                }
                if(memberLow<=memberHigh){sharedLow=std::max(sharedLow,memberLow);sharedHigh=std::min(sharedHigh,memberHigh);}
            }
            if(sharedLow<sharedHigh){low=sharedLow;high=sharedHigh;}
            if(low>=high)continue;
            QList<int> ends;
            if(screens)ends=screenEnds(group);
            else for(int i=0;i<std::max(1,def_.layout.twistAmount);++i)ends<<i;
            for(int end:ends) {
                const double inset=screens?.1:.2;
                const double natural=ends.size()==1?(screens?.1:.2):inset+(1-2*inset)*end/(ends.size()-1.);
                const double fraction=def_.layout.markAt.value(QStringLiteral("%1:%2#%3").arg(screens?QStringLiteral("shield"):QStringLiteral("twist"),group).arg(end),natural);
                const double x=low+(high-low)*std::clamp(fraction,.06,.94);
                double top=1e20,bottom=-1e20;
                for(const auto& w:def_.wires)if((screens?w.shieldGroup:w.twistGroup)==group) {
                    const auto path=wirePath(w.id);
                    for(size_t i=1;i<path.size();++i) {
                        const auto a=path[i-1],b=path[i];
                        if(std::abs(a.x()-b.x())<1e-6 || x<std::min(a.x(),b.x()) || x>std::max(a.x(),b.x()))continue;
                        const double y=a.y()+(b.y()-a.y())*(x-a.x())/(b.x()-a.x());
                        top=std::min(top,y);bottom=std::max(bottom,y);
                    }
                }
                if(top<=bottom)result.insert(group,screens?QRectF(x-12,top-kRow/2+2,24,bottom-top+kRow-4):QRectF(x-10,top-6,20,bottom-top+12));
            }
        }
        return result;
    }
}

std::vector<QPointF> WiringCanvas::wirePath(const QString& id) const {
    std::vector<QPointF> points;
    for(const auto& p:jointPaths_.value(id))points.emplace_back(p.x,p.y);
    return points;
}
std::vector<QPointF> WiringCanvas::jointPath(const HarnessWire& w) const {
    if (!harnessIsJointEndpoint(w.fromConn) && !harnessIsJointEndpoint(w.toConn)) return {};
    return wirePath(w.id);
}

QString WiringCanvas::jointEndpointAt(const QPointF& p) const {
    for (const auto& w : def_.wires) {
        const auto path = jointPath(w);
        if (path.empty()) continue;
        if (harnessIsJointEndpoint(w.fromConn) && QLineF(p,path.front()).length() <= 7) return w.fromConn;
        if (harnessIsJointEndpoint(w.toConn) && QLineF(p,path.back()).length() <= 7) return w.toConn;
    }
    return {};
}

QString WiringCanvas::jointWireAt(const QPointF& p) const {
    for (const HarnessWire& w : def_.wires) {
        const std::vector<QPointF> pts = jointPath(w);
        for (size_t k = 1; k < pts.size(); ++k) {
            const QLineF seg(pts[k - 1], pts[k]);
            const double len = seg.length();
            if (len < 1e-9) continue;
            const double t = std::clamp(QPointF::dotProduct(p - pts[k - 1], pts[k] - pts[k - 1]) / (len * len), 0.0, 1.0);
            if (QLineF(p, pts[k - 1] + (pts[k] - pts[k - 1]) * t).length() <= 4.0) return w.id;
        }
    }
    return {};
}

QStringList WiringCanvas::groupMarkAt(const QPointF& p) const {
    for (bool screens : {true, false}) {
        const auto marks = markRects(screens);
        for (auto it = marks.cbegin(); it != marks.cend(); ++it)
            if (it.value().adjusted(-3, -3, 3, 3).contains(p)) {
                QStringList ids;
                for (const HarnessWire& w : def_.wires)
                    if ((screens ? w.shieldGroup : w.twistGroup) == it.key()) ids << w.id;
                return ids;
            }
    }
    return {};
}

QStringList WiringCanvas::groupMarksAcross(const QPointF& a, const QPointF& b) const {
    QStringList ids;
    const QLineF stroke(a, b);
    for (bool screens : {true, false}) {
        const auto marks = markRects(screens);
        for (auto it = marks.cbegin(); it != marks.cend(); ++it) {
            const QRectF r = it.value();
            bool hit = r.contains(a) || r.contains(b);
            for (const QLineF& edge : {QLineF(r.topLeft(), r.topRight()), QLineF(r.topRight(), r.bottomRight()),
                                       QLineF(r.bottomRight(), r.bottomLeft()), QLineF(r.bottomLeft(), r.topLeft())})
                hit = hit || stroke.intersects(edge, nullptr) == QLineF::BoundedIntersection;
            if (!hit) continue;
            for (const HarnessWire& w : def_.wires)
                if ((screens ? w.shieldGroup : w.twistGroup) == it.key() && !ids.contains(w.id)) ids << w.id;
        }
    }
    return ids;
}

double WiringCanvas::fitZoom(const QSize& room) const {
    if (content_.width() <= 0 || content_.height() <= 0) return 1.0;
    return std::clamp(std::min(room.width() / double(content_.width()), room.height() / double(content_.height())), 0.3, 4.0);
}

void WiringCanvas::setWireColours(bool on) {
    colours_ = on;
    update();
}

QColor WiringCanvas::shownBase(const QString& code) const { return colours_ ? baseColour(code) : QColor(232, 236, 242); }

QColor WiringCanvas::shownStripe(const QString& code) const { return colours_ ? stripeColour(code) : QColor(); }

void WiringCanvas::setZoom(double z) {
    zoom_ = std::clamp(z, 0.3, 4.0);
    relayout();
}

// THE WHEEL ZOOMS, about nothing in particular: the scroll area keeps where it was.
void WiringCanvas::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.0;
    if (std::abs(steps) < 1e-9) return;
    setZoom(zoom_ * std::pow(1.15, steps));
    emit said(QStringLiteral("Zoom %1 %").arg(qRound(zoom_ * 100.0)));
    e->accept();
}

void WiringCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(32, 36, 42));
    p.scale(zoom_, zoom_);   // everything below laid out unzoomed
    const QHash<QString, const HarnessWire*> byId = [&] {
        QHash<QString, const HarnessWire*> m;
        for (const HarnessWire& w : def_.wires) m.insert(w.id, &w);
        return m;
    }();
    // ---- CABLES: a band behind their bars, with the cable's name ----
    QHash<QString, std::pair<double, double>> bundles, twists, screens;
    for (const Bar& bar : bars_) {
        const HarnessWire* w = byId.value(bar.wire);
        if (!w) continue;
        auto widen = [&](QHash<QString, std::pair<double, double>>& m, const QString& key) {
            if (key.isEmpty()) return;
            auto it = m.find(key);
            if (it == m.end()) m.insert(key, {bar.y, bar.y});
            else it.value() = {std::min(it.value().first, bar.y), std::max(it.value().second, bar.y)};
        };
        widen(bundles, w->bundle);
        widen(twists, w->twistGroup);
        widen(screens, w->shieldGroup);
    }
    p.setPen(Qt::NoPen);
    for (auto it = bundles.begin(); it != bundles.end(); ++it) {
        if (trunks_.contains(it.key())) continue;   // drawn as its trunk, below
        p.setBrush(QColor(90, 70, 40, 90));
        p.drawRoundedRect(QRectF(laneLeft_ - 8, it.value().first - kRow / 2, laneRight_ - laneLeft_ + 16, it.value().second - it.value().first + kRow), 6, 6);
        p.setPen(QColor(230, 190, 120));
        p.drawText(QRectF(laneLeft_ - 4, it.value().first - kRow / 2 - 14, 200, 14), Qt::AlignLeft | Qt::AlignVCenter, it.key());
        p.setPen(Qt::NoPen);
    }
    // ---- THE BRICKS: each connector, its pins in squares ----
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.5));
    for (const Brick& b : bricks_) {
        const HarnessConnector* c = harnessConnById(def_, b.conn);
        const bool picked = selectedBricks_.contains(b.conn);
        p.setPen(picked ? QPen(QColor(255, 170, 60), 2.6) : QPen(QColor(90, 130, 180), 1.4));
        p.setBrush(QColor(47, 79, 116));
        p.drawRoundedRect(b.rect, 8, 8);
        p.setPen(QColor(235, 240, 248));
        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);
        p.drawText(QRectF(b.rect.left() + 6, b.rect.top() + 3, b.rect.width() - 12, kCell), Qt::AlignLeft | Qt::AlignVCenter,
                   c ? (c->designator.isEmpty() ? c->id : c->designator) : b.conn);
        p.setFont(small);
        for (auto it = b.pins.begin(); it != b.pins.end(); ++it) {
            const QRectF r = it.value();
            const QStringList on = wiresOn(b.conn, it.key());
            const HarnessWire* first = on.isEmpty() ? nullptr : byId.value(on.front());
            const bool here = first && shownWires().contains(first->id);
            const bool lit = pending_ == pinKey(b.conn, it.key()) || (pressing_ && pressPin_ == pinKey(b.conn, it.key()));
            QColor fill = QColor(26, 30, 36);
            if (first && here) fill = shownBase(first->color);
            p.setPen(QPen(on.size() > 1 ? QColor(240, 80, 70) : lit ? QColor(255, 209, 102) : QColor(150, 165, 185), on.size() > 1 || lit ? 2.4 : 1.0));
            p.setBrush(fill);
            p.drawRoundedRect(r, 3, 3);
            // TERMINATED, NOTHING ON IT: its X, as the drawing marks it.
            if (on.isEmpty() && def_.layout.pinTerminations.value(pinKey(b.conn,it.key()),def_.layout.terminateUnconnected)) {
                p.setPen(QPen(QColor(240, 120, 90), 1.4));
                p.drawLine(r.topLeft() + QPointF(3, 3), r.bottomRight() - QPointF(3, 3));
                p.drawLine(r.topRight() + QPointF(-3, 3), r.bottomLeft() + QPointF(3, -3));
            }
            if (first && !here) {
                // WIRED TO A CONNECTOR NOT SHOWN: hatched.
                p.setBrush(QBrush(QColor(150, 160, 175), Qt::BDiagPattern));
                p.drawRoundedRect(r, 3, 3);
            }
            if (first && here)
                if (const QColor s = shownStripe(first->color); s.isValid()) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(s);
                    p.drawRect(QRectF(r.left() + 2, r.center().y() - 2, r.width() - 4, 4));
                }
            p.setPen(fill.lightness() > 140 ? QColor(20, 24, 30) : QColor(230, 236, 244));
            p.drawText(r, Qt::AlignCenter, c ? harnessPinLabel(*c, it.key()) : QString::number(it.key()));
        }
        p.setFont(font());
    }
    // ---- THE WIRES: pin to its bar, across the bar, to the other pin ----
    // THE LINES PICKED, blue; the rest of the group each is in - its twisted pair, or else its cable - orange. A shield is no
    // group: the wires under one are not lit with it.
    QSet<QString> mates;
    for (const HarnessWire& w : def_.wires)
        if (selection_.contains(w.id))
            for (const HarnessWire& o : def_.wires)
                if (!w.twistGroup.isEmpty() ? o.twistGroup == w.twistGroup : !w.bundle.isEmpty() && o.bundle == w.bundle)
                    mates.insert(o.id);
    for (const Bar& bar : bars_) {
        const HarnessWire* w = byId.value(bar.wire);
        if (!w) continue;
        const bool sel = selection_.contains(bar.wire);
        const bool sameSide = bar.fromSide == bar.toSide;
        const double edge = bar.fromSide < 0 ? laneLeft_ : laneRight_;
        const QPointF a(bar.fromSide < 0 ? laneLeft_ : laneRight_, bar.y), b(bar.toSide < 0 ? laneLeft_ : laneRight_, bar.y);
        const QColor base = shownBase(w->color);
        // ONE HARNESS: into its cable's trunk from the lane's edge, along it, and out again to its pin.
        const bool trunk = !sameSide && !w->bundle.isEmpty() && trunks_.contains(w->bundle);
        QPainterPath path;
        path.moveTo(bar.from);
        if (trunk) {
            const auto span = bundles.value(w->bundle);
            const double trunkY = (span.first + span.second) / 2.0;
            auto fan = [this](const QPointF& edge) { return QPointF(edge.x() < (laneLeft_ + laneRight_) / 2.0 ? laneLeft_ + kTrunkFan : laneRight_ - kTrunkFan, 0.0); };
            path.lineTo(QPointF(bar.from.x() + bar.fromSide * -14.0, bar.from.y()));
            path.lineTo(a);
            path.lineTo(QPointF(fan(a).x(), trunkY));
            path.lineTo(QPointF(fan(b).x(), trunkY));
            path.lineTo(b);
            path.lineTo(QPointF(bar.to.x() + bar.toSide * -14.0, bar.to.y()));
            path.lineTo(bar.to);
        } else {
            for(const auto& point:wirePath(w->id))path.lineTo(point);
        }
        // An open path: drawn as a line, never filled - whatever brush the words before it left set.
        p.setBrush(Qt::NoBrush);
        if (sel || mates.contains(bar.wire)) {
            p.setPen(QPen(sel ? QColor(80, 160, 255) : QColor(255, 170, 60), 6.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
        p.setPen(QPen(QColor(10, 12, 16), 4.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(QPen(base, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        if (trunk) continue;   // its words are the trunk's
        if (const QColor s = shownStripe(w->color); s.isValid()) {
            p.setPen(QPen(s, 1.2, Qt::DashLine));
            p.drawLine(a, b);
        }
        // WHAT IT IS, on its bar: its colour, its gauge, its label.
        QStringList printed;
        if (w->showColor) printed << w->color;
        if (w->showGauge) printed << wireGaugeLabel(w->gauge, true);
        if (w->showLabel && !w->label.isEmpty()) printed << w->label;
        const QString words = printed.join(QStringLiteral("  "));
        if (words.isEmpty()) continue;
        p.setFont(small);
        // ITS WORDS: across the middle of the lane - or, between two on one side, beside its upright run.
        const bool middleTwist=!sameSide && !w->twistGroup.isEmpty();
        const double half = sameSide ? kStub / 2.0 - 4.0 : middleTwist ? (laneRight_-laneLeft_)/4.0-55.0 : (laneRight_ - laneLeft_) / 2.0 - 70.0;
        const double mid = sameSide ? bar.laneX - bar.fromSide * (half + 6.0) : middleTwist ? laneLeft_+70.0+half : (laneLeft_ + laneRight_) / 2.0;
        const double labelY=bar.y+(sameSide?bar.lane*kRow*.75:0);
        const QRectF text(mid - half, labelY - kRow / 2 + 1, 2 * half, kRow / 2 - 1);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(32, 36, 42, 200));
        // Behind the words only: the bar shows either side of them.
        const double wordsW = std::min(text.width(), QFontMetricsF(small).horizontalAdvance(words) + 12.0);
        p.drawRoundedRect(QRectF(text.center().x() - wordsW / 2.0, text.top(), wordsW, text.height()), 3, 3);
        p.setPen(QColor(220, 228, 238));
        p.drawText(text, Qt::AlignCenter, QFontMetricsF(small).elidedText(words,Qt::ElideRight,text.width()));
        p.setFont(font());
    }
    // ---- THE TRUNKS: a cable shown as one harness, over its wires where they run together - its name, its count, its P/N ----
    for (auto it = bundles.begin(); it != bundles.end(); ++it) {
        if (!trunks_.contains(it.key())) continue;
        const double y = (it.value().first + it.value().second) / 2.0;
        int count = 0;
        QString part;
        for (const HarnessWire& w : def_.wires)
            if (w.bundle == it.key()) {
                ++count;
                if (part.isEmpty() && !w.libPart.isEmpty())
                    if (const LibraryPart* lp = harnessBuilderLibrary().byId(w.libPart)) part = lp->mfrPn;
                if (part.isEmpty()) part = w.partNumber;
            }
        const QRectF band(laneLeft_ + kTrunkFan, y - 7.0, laneRight_ - laneLeft_ - 2 * kTrunkFan, 14.0);
        const bool lit = std::any_of(selection_.begin(), selection_.end(), [&](const QString& id) {
            const HarnessWire* w = byId.value(id);
            return w && w->bundle == it.key();
        });
        p.setPen(QPen(lit ? QColor(255, 170, 60) : QColor(10, 12, 16), lit ? 2.6 : 1.4));
        p.setBrush(QColor(120, 92, 52));
        p.drawRoundedRect(band, 7, 7);
        p.setFont(small);
        p.setPen(QColor(245, 232, 210));
        QString words = QStringLiteral("%1 · %2 wires").arg(it.key()).arg(count);
        if (!part.isEmpty()) words += QStringLiteral(" · ") + part;
        p.drawText(band, Qt::AlignCenter, words);
        p.setFont(font());
    }
    // ---- WIRES JOINED TO ANOTHER'S RUN: from the pin to a dot on that wire's bar ----
    for (const HarnessWire& w : def_.wires) {
        const std::vector<QPointF> pts = jointPath(w);
        if (pts.size() < 2) continue;
        QPainterPath path;
        path.moveTo(pts.front());
        for (size_t k = 1; k < pts.size(); ++k) path.lineTo(pts[k]);
        p.setBrush(Qt::NoBrush);
        if (selection_.contains(w.id)) {
            p.setPen(QPen(QColor(255, 170, 60), 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
        p.setPen(QPen(QColor(10, 12, 16), 4.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(QPen(shownBase(w.color), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(235, 240, 248));
        if (harnessIsJointEndpoint(w.toConn)) p.drawEllipse(pts.back(), 4.0, 4.0);
        if (harnessIsJointEndpoint(w.fromConn)) p.drawEllipse(pts.front(), 4.0, 4.0);
        if (const QColor stripe = shownStripe(w.color); stripe.isValid()) {
            p.setPen(QPen(stripe, 1.2, Qt::DashLine)); p.drawPath(path);
        }
        size_t longest = 0; double length = 0;
        for (size_t k = 1; k < pts.size(); ++k)
            if (std::abs(pts[k].y()-pts[k-1].y()) < 1e-6 && QLineF(pts[k-1],pts[k]).length() > length) {
                longest = k; length = QLineF(pts[k-1],pts[k]).length();
            }
        if (longest) {
            QStringList words;
            if (w.showColor) words << w.color;
            if (w.showGauge) words << wireGaugeLabel(w.gauge, true);
            if (w.showLabel && !w.label.isEmpty()) words << w.label;
            p.setFont(small); p.setPen(QColor(220,228,238));
            const QPointF middle = (pts[longest-1]+pts[longest])*.5;
            p.drawText(QRectF(middle.x()-length*.5, middle.y()-22, length, 18), Qt::AlignCenter,
                       QFontMetricsF(small).elidedText(words.join(QStringLiteral("  ")), Qt::ElideRight, length));
        }
    }
    // ---- TWISTS AND SCREENS, by their bars ----
    const auto twistMarks=markRects(false);
    for (auto it = twistMarks.begin(); it != twistMarks.end(); ++it) {
        const double twistX=it.value().center().x();
        const double top = it.value().top()+6, bottom = it.value().bottom()-6;
        p.setPen(QPen(QColor(120, 220, 160), 1.8));
        p.setBrush(Qt::NoBrush);
        for (int k = 0; k < 2; ++k) {
            QPainterPath s;
            for (int i = 0; i <= 24; ++i) {
                const double t = i / 24.0, y = top - 6 + (bottom - top + 12) * t;
                const QPointF q(twistX + (k ? -1 : 1) * 8.0 * std::sin(t * M_PI * 3), y);
                if (i == 0) s.moveTo(q);
                else s.lineTo(q);
            }
            p.drawPath(s);
        }
        p.drawText(QRectF(twistX - 40, top - kRow / 2 - 14, 80, 14), Qt::AlignCenter, it.key());
    }
    auto activeDrain=[&](const HarnessWire& wire) {
        for(const auto& key:{wire.fromConn,wire.toConn}) {
            QString id;HarnessSide side=HarnessSide::Left;
            if(harnessShieldEndpointConn(key,id,side)) {
                const auto* c=harnessConnById(def_,id);
                if(!c || !(side==HarnessSide::Left?c->shieldLeft:c->shieldRight))return false;
            }
        }
        return true;
    };
    const auto screenMarks=markRects(true);
    for (const auto& group : screenMarks.uniqueKeys()) {
        auto rings=screenMarks.values(group);
        std::sort(rings.begin(),rings.end(),[](const auto& a,const auto& b){return a.center().x()<b.center().x();});
        for(int symbol=0;symbol<rings.size();++symbol) {
        const int end=symbol*2<rings.size()?0:1;
        const QRectF ring=rings[symbol];
        const double screenX=ring.center().x(),top=ring.top()+kRow/2-2;
        p.setPen(QPen(QColor(140, 190, 255), 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring);
        int logicalEnd=end;
        QString firstConn;
        for(const auto& member:def_.wires)if(member.shieldGroup==group) {
            firstConn=member.fromConn;
            if(const auto* source=brickOf(firstConn);source && !source->left)logicalEnd=1-end;
            break;
        }
        bool connected=false;
        for(const auto& drain:def_.wires)
            if(activeDrain(drain) && (drain.fromConn==harnessInternalShieldKey(group) || drain.toConn==harnessInternalShieldKey(group))) {
                const int which=drain.label==QLatin1String("drain right")?1:drain.label==QLatin1String("drain left")?0:
                    ((drain.fromConn==harnessInternalShieldKey(group)?drain.toConn:drain.fromConn)==firstConn?0:1);
                if(which==logicalEnd)connected=true;
            }
        const QPointF badge(ring.center().x(),ring.bottom()+8);
        if(connected) {
            p.setPen(QPen(QColor(100,220,150),2));
            p.drawLine(badge+QPointF(-4,0),badge+QPointF(-1,3));p.drawLine(badge+QPointF(-1,3),badge+QPointF(5,-4));
        } else {
            p.setPen(QPen(QColor(230,190,120),2));
            p.drawLine(badge+QPointF(-4,-4),badge+QPointF(4,4));p.drawLine(badge+QPointF(-4,4),badge+QPointF(4,-4));
        }
        p.setPen(QColor(140,190,255));
        p.drawText(QRectF(screenX - 40, top - kRow / 2 - 14, 80, 14), Qt::AlignCenter, group);
        // ITS DRAIN: dashed to its pin.
        for (const HarnessWire& w : def_.wires)
            if (activeDrain(w) && (w.fromConn == harnessInternalShieldKey(group) || w.toConn == harnessInternalShieldKey(group))) {
                const bool fromScreen = w.fromConn == harnessInternalShieldKey(group);
                int visualEnd=-1;
                if(w.label.startsWith(QLatin1String("drain "))) {
                    for(const auto& member:def_.wires)if(member.shieldGroup==group) {
                        const auto* brick=brickOf(w.label==QLatin1String("drain right")?member.toConn:member.fromConn);
                        if(brick)visualEnd=brick->left?0:1;
                        break;
                    }
                } else if(const auto* brick=brickOf(fromScreen?w.toConn:w.fromConn))visualEnd=brick->left?0:1;
                if(visualEnd>=0 && visualEnd!=end)continue;
                const QString conn = fromScreen ? w.toConn : w.fromConn;
                const int pin = fromScreen ? w.toPin : w.fromPin;
                const QPointF at = pinCentre(conn, pin);
                if (at.isNull()) continue;
                p.setPen(QPen(QColor(140, 190, 255), 1.4, Qt::DashLine));
                p.drawLine(QPointF(ring.center().x(), ring.bottom()), at);
            }
    }
    }
    // ---- WHAT IS BEING DONE: the next wire offered, a line being drawn, a stroke, a bar being moved ----
    // THE NEXT PAIR OFFERED is not drawn - Enter still takes it, as the line under the picture says.
    if (false && ghostOn_) {
        const QPointF a = pinCentre(ghostFrom_, ghostFromPin_), b = pinCentre(ghostTo_, ghostToPin_);
        if (!a.isNull() && !b.isNull()) {
            p.setPen(QPen(QColor(255, 209, 102, 150), 1.6, Qt::DashLine));
            p.drawLine(a, b);
            p.drawText(QRectF((a + b) / 2.0 - QPointF(100, 20), QSizeF(200, 16)), Qt::AlignCenter, QStringLiteral("Enter: wire it"));
        }
    }
    if (pressing_ && carrying_ && carryingBar_) {
        // THE WIRE CAUGHT: where it would go in the list.
        const int at = std::clamp(static_cast<int>(std::lround((now_.y() - barTop_) / kRow)), 0, static_cast<int>(bars_.size()));
        const double y = barTop_ + at * kRow - kRow / 2.0;
        p.setPen(QPen(QColor(255, 209, 102), 3.0));
        p.drawLine(QPointF(laneLeft_ - 40, y), QPointF(laneRight_ + 40, y));
    } else if (pressing_ && carrying_) {
        // THE PIN CAUGHT, carried under the pointer; the pin whose place it would take ringed.
        QString conn, overConn;
        int pin = 0, overPin = 0;
        splitPinKey(pressPin_, conn, pin);
        // WHERE IT WOULD GO: an orange line between the pins, on the side of the pin whose place it takes it comes from.
        if (splitPinKey(pinAt(now_), overConn, overPin) && overConn == conn && overPin != pin) {
            const Brick* b = brickOf(conn);
            const HarnessConnector* c = harnessConnById(def_, conn);
            if (b && c && b->pins.contains(overPin)) {
                const std::vector<int> seq = harnessPinSequence(*c);
                const bool down = std::find(seq.begin(), seq.end(), overPin) > std::find(seq.begin(), seq.end(), pin);
                const QRectF r = b->pins.value(overPin);
                const double y = down ? r.bottom() + 2.0 : r.top() - 2.0;
                p.setPen(QPen(QColor(255, 150, 40), 3.0, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(QPointF(b->rect.left() + 5, y), QPointF(b->rect.right() - 5, y));
            }
        }
        p.setPen(QPen(QColor(255, 209, 102), 1.5));
        p.setBrush(QColor(255, 209, 102, 120));
        p.drawRect(QRectF(now_ - QPointF(kCell * 0.4, kCell * 0.4), QSizeF(kCell * 0.8, kCell * 0.8)));
        p.setPen(QColor(20, 24, 30));
        p.drawText(QRectF(now_ - QPointF(kCell * 0.4, kCell * 0.4), QSizeF(kCell * 0.8, kCell * 0.8)), Qt::AlignCenter,
                   QString::number(pin));
    } else if (pressing_ && moved_) {
        if (!pressPin_.isEmpty()) {
            QString conn;
            int pin = 0;
            splitPinKey(pressPin_, conn, pin);
            p.setPen(QPen(QColor(255, 209, 102), 2.0));
            p.drawLine(pinCentre(conn, pin), now_);
        } else if (!pressBar_.isEmpty()) {
            const int at = std::clamp(static_cast<int>(std::lround((now_.y() - barTop_) / kRow)), 0, static_cast<int>(bars_.size()));
            const double y = barTop_ + at * kRow - kRow / 2.0;
            p.setPen(QPen(QColor(255, 255, 255), 2.0));
            p.drawLine(QPointF(laneLeft_, y), QPointF(laneRight_, y));
        } else if (tool_ != Tool::Connect) {
            const QColor c = tool_ == Tool::Twist ? QColor(120, 220, 160) : tool_ == Tool::Screen ? QColor(140, 190, 255)
                             : tool_ == Tool::Cable ? QColor(230, 190, 120) : QColor(255, 170, 60);
            p.setPen(QPen(c, 2.0, Qt::DashLine));
            p.drawLine(pressAt_, now_);
        }
    }
    if (bars_.empty()) {
        p.setPen(QColor(160, 172, 188));
        p.drawText(QRectF(laneLeft_, barTop_ - 10, laneRight_ - laneLeft_, 40), Qt::AlignCenter | Qt::TextWordWrap,
                   QStringLiteral("Click a pin, then the pin it goes to - or drag from one to the other"));
    }
}

void WiringCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    // RIGHT-CLICKED ON A WIRE: a joint on it.
    if (e->button() == Qt::RightButton) {
        const auto point = e->position() / zoom_;
        const QString node = jointEndpointAt(point);
        const QString bar = node.isEmpty() ? barAt(point) : node;
        if (!bar.isEmpty()) emit jointMenuAsked(bar, e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    pressing_ = true;
    moved_ = false;
    ctrl_ = e->modifiers() & Qt::ControlModifier;
    pressAt_ = now_ = (e->position() / zoom_);
    pressPin_ = tool_ == Tool::Connect || tool_ == Tool::Select ? pinAt(pressAt_) : QString();
    pressBar_ = pressPin_.isEmpty() && (tool_ == Tool::Connect || tool_ == Tool::Select) ? barAt(pressAt_) : QString();
    pressBrick_ = pressPin_.isEmpty() && pressBar_.isEmpty() ? brickAt(pressAt_) : QString();
    carrying_ = carryingBar_ = false;
    if (!pressPin_.isEmpty() || !pressBar_.isEmpty()) holdTimer_->start();
    update();
}

void WiringCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (!pressing_) {
        const QString pin = pinAt((e->position() / zoom_));
        QString conn;
        int number = 0;
        if (splitPinKey(pin, conn, number)) {
            const QStringList on = wiresOn(conn, number);
            const HarnessConnector* c = harnessConnById(def_, conn);
            setToolTip(QStringLiteral("%1 pin %2 - %3").arg(c ? c->designator : conn, c ? harnessPinLabel(*c, number) : QString::number(number),
                                                            on.isEmpty() ? QStringLiteral("free") : QStringLiteral("wired (%1)").arg(on.join(QStringLiteral(", ")))));
        } else {
            setToolTip(QString());
        }
        return;
    }
    now_ = (e->position() / zoom_);
    if ((now_ - pressAt_).manhattanLength() > 4) moved_ = true;
    if (!carrying_ && QLineF(pressAt_, now_).length() > 8.0) holdTimer_->stop();   // pulled away before it was caught
    update();
}

void WiringCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (!pressing_ || e->button() != Qt::LeftButton) return;
    pressing_ = false;
    now_ = (e->position() / zoom_);
    holdTimer_->stop();
    auto dropIndex = [this] {
        const auto wires=shownWires();
        int index=0;double closest=1e30;
        for(int i=0;i<wires.size();++i) {
            const double y=barCentre(wires[i]).y(), delta=std::abs(now_.y()-y);
            if(delta<closest){closest=delta;index=i+(now_.y()>y?1:0);}
        }
        return index;
    };
    // A CAUGHT WIRE LET GO: moved in the list where it is let go, its pins with it.
    if (carrying_ && carryingBar_) {
        carrying_ = carryingBar_ = false;
        unsetCursor();
        const int at = dropIndex();
        emit wireWithPinsMoveAsked(pressBar_, at);
        pressPin_.clear();
        pressBar_.clear();
        update();
        return;
    }
    // A CAUGHT PIN LET GO: on another pin of its connector, it takes that one's place in the pin stack.
    if (carrying_) {
        carrying_ = false;
        unsetCursor();
        QString from, to;
        int fromPin = 0, toPin = 0;
        if (splitPinKey(pressPin_, from, fromPin) && splitPinKey(pinAt(now_), to, toPin) && from == to && fromPin != toPin)
            emit pinStackMoveAsked(from, fromPin, toPin);
        else
            emit said(QStringLiteral("Let go on another pin of the same connector to move it there in the pin stack"));
        pressPin_.clear();
        pressBar_.clear();
        update();
        return;
    }
    const QString releasePin = pinAt(now_);
    QString pc, rc;
    int pp = 0, rp = 0;
    const bool fromPin = splitPinKey(pressPin_, pc, pp), toPin = splitPinKey(releasePin, rc, rp);
    if (moved_ && fromPin && toPin && pressPin_ != releasePin) {
        // DRAGGED PIN TO PIN: a wire's end moved where the pin had one wire (Ctrl: another wire instead); else a new wire.
        const QStringList on = wiresOn(pc, pp);
        if (on.size() == 1 && !ctrl_ && wiresOn(rc, rp).isEmpty()) {
            for (const HarnessWire& w : def_.wires)
                if (w.id == on.front()) emit relandAsked(w.id, w.fromConn == pc && w.fromPin == pp ? 0 : 1, rc, rp);
        } else {
            emit connectAsked(pc, pp, rc, rp);
        }
        pending_.clear();
    } else if (moved_ && fromPin && !toPin && !barAt(now_).isEmpty()) {
        // DRAGGED FROM A PIN ONTO A WIRE: a wire from the pin to a joint on that wire's run.
        const QString node = jointEndpointAt(now_);
        emit jointAsked(pc, pp, node.isEmpty() ? barAt(now_) : node);
        pending_.clear();
    } else if (!moved_ && fromPin) {
        // A PIN CLICKED: the first end, or - one waiting - the other.
        if (!pending_.isEmpty() && pending_ != pressPin_) {
            QString wc;
            int wp = 0;
            splitPinKey(pending_, wc, wp);
            pending_.clear();
            emit connectAsked(wc, wp, pc, pp);
        } else if (pending_ == pressPin_) {
            pending_.clear();
        } else {
            pending_ = pressPin_;
            const QStringList on = wiresOn(pc, pp);
            if (!on.isEmpty()) setSelection(on);
            emit said(QStringLiteral("Now click the pin it goes to (Esc to let go)"));
        }
    } else if (!pressBar_.isEmpty()) {
        if (!moved_ && tool_ == Tool::Connect && !pending_.isEmpty()) {
            QString conn; int pin = 0;
            if (splitPinKey(pending_, conn, pin)) {
                const QString node = jointEndpointAt(now_);
                emit jointAsked(conn, pin, node.isEmpty() ? pressBar_ : node);
            }
            pending_.clear();
        } else if (moved_) {
            const int at = dropIndex();
            emit moveAsked(pressBar_, at);
        } else {
            QStringList ids = ctrl_ ? selection_ : QStringList();
            if (ids.contains(pressBar_)) ids.removeAll(pressBar_);
            else ids << pressBar_;
            setSelection(ids);
        }
    } else if (moved_ && tool_ != Tool::Connect) {
        // A STROKE ACROSS LINES: picked - a screen's ring or a pair's twist it crosses, all of that group - or made twisted,
        // screened, one cable.
        QStringList crossed = barsAcross(pressAt_, now_);
        if (tool_ == Tool::Select)
            for (const QString& id : groupMarksAcross(pressAt_, now_))
                if (!crossed.contains(id)) crossed << id;
        if (tool_ == Tool::Select) setSelection(ctrl_ ? selection_ + crossed : crossed);
        else if (crossed.isEmpty()) emit said(QStringLiteral("The stroke crossed no line - nothing made"));
        else emit strokeMade(tool_, crossed);
    } else if (!moved_ && !pressBrick_.isEmpty()) {
        // A CONNECTOR'S BRICK CLICKED: picked (Ctrl: more, or let go of), for what is done to pin stacks.
        QStringList picked = ctrl_ ? selectedBricks_ : QStringList();
        if (picked.contains(pressBrick_)) picked.removeAll(pressBrick_);
        else picked << pressBrick_;
        setSelectedBricks(picked);
    } else if (!moved_ && !jointWireAt(now_).isEmpty()) {
        // A WIRE JOINED TO ANOTHER'S RUN CLICKED: picked, for its part, colour, gauge in the panel.
        const QString id = jointWireAt(now_);
        setSelection(ctrl_ ? selection_ + QStringList{id} : QStringList{id});
    } else if (!moved_ && !groupMarkAt(now_).isEmpty()) {
        // A SCREEN'S RING OR A PAIR'S TWIST CLICKED: its wires picked.
        const QStringList ids = groupMarkAt(now_);
        setSelection(ctrl_ ? selection_ + ids : ids);
    } else if (!moved_) {
        pending_.clear();
        if (!ctrl_) {
            setSelection({});
            if (!selectedBricks_.isEmpty()) setSelectedBricks({});
        }
    }
    pressPin_.clear();
    pressBar_.clear();
    pressBrick_.clear();
    update();
}

void WiringCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        pending_.clear();
        setSelection({});
        // OUT OF TWISTING, SHIELDING, MAKING A CABLE: back to Select.
        if (tool_ == Tool::Twist || tool_ == Tool::Screen || tool_ == Tool::Cable) {
            setTool(Tool::Select);
            emit toolPicked(Tool::Select);
        }
    } else if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        emit deleteAsked();
    } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        emit ghostAccepted();
    } else {
        QWidget::keyPressEvent(e);
    }
}

// ---- The window ----------------------------------------------------------------------------------------------

WiringDialog::WiringDialog(HarnessDef& def, std::function<void()> changed, QWidget* parent)
    : QDialog(parent), def_(def), changed_(std::move(changed)) {
    setObjectName(QStringLiteral("wiringDialog"));
    setWindowTitle(QStringLiteral("Wire connectors"));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    resize(1280, 780);
    auto* lay = new QVBoxLayout(this);

    // ---- WHAT IS SHOWN: two connectors, or several ----
    auto* top = new QHBoxLayout;
    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("wiringMode"));
    mode_->addItems({QStringLiteral("Two connectors"), QStringLiteral("Several connectors")});
    top->addWidget(mode_);
    pickers_ = new QStackedWidget(this);
    auto* two = new QWidget(pickers_);
    auto* twoRow = new QHBoxLayout(two);
    twoRow->setContentsMargins(0, 0, 0, 0);
    left_ = new QComboBox(two);
    left_->setObjectName(QStringLiteral("wiringLeft"));
    right_ = new QComboBox(two);
    right_->setObjectName(QStringLiteral("wiringRight"));
    // The two side by side, as they are drawn.
    left_->setMinimumWidth(180);
    right_->setMinimumWidth(180);
    twoRow->addWidget(left_);
    twoRow->addWidget(new QLabel(QStringLiteral("↔"), two));
    twoRow->addWidget(right_);
    twoRow->addStretch(1);
    pickers_->addWidget(two);
    several_ = new QListWidget(pickers_);
    several_->setObjectName(QStringLiteral("wiringConnectors"));
    several_->setFlow(QListView::LeftToRight);
    several_->setMaximumHeight(34);
    pickers_->addWidget(several_);
    // One line high, whichever is shown: the list of connectors would otherwise ask for a list's height.
    pickers_->setFixedHeight(34);
    top->addWidget(pickers_, 1);
    lay->addLayout(top);

    // ---- THE TOOLS ----
    auto* bar = new WizardFlowLayout;
    const struct {
        const char* name;
        const char* text;
        const char* tip;
    } tools[5] = {{"wiringToolConnect", "Connect", "Click a pin, then the pin it goes to - or drag from one to the other. Drag a wired pin to "
                                                   "another to move its end there (Ctrl: another wire instead)"},
                  {"wiringToolSelect", "Select", "Click lines to pick them (Ctrl: more), or draw a stroke across them"},
                  {"wiringToolTwist", "Twist", "Draw a stroke across lines to twist them together"},
                  {"wiringToolScreen", "Shield", "Draw a stroke across lines to shield them. Set each end connection or termination in the shield properties."},
                  {"wiringToolCable", "Cable", "Draw a stroke across lines to make them one cable"}};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QToolButton(this);
        b->setObjectName(QLatin1String(tools[i].name));
        b->setText(QString::fromLatin1(tools[i].text));
        b->setToolTip(QString::fromLatin1(tools[i].tip));
        b->setCheckable(true);
        // FRAMED, each a button of its own - the one in use filled.
        b->setAutoRaise(false);
        b->setStyleSheet(QStringLiteral("QToolButton{font-weight:bold;border:2px solid #8fa4bb;border-radius:4px;padding:5px 10px;}"
                                        "QToolButton:checked{background:#2b6cb0;color:white;border-color:#2b6cb0;}"));
        b->setMinimumWidth(70);
        connect(b, &QToolButton::clicked, this, [this, i] {
            canvas_->setTool(static_cast<WiringCanvas::Tool>(i));
            for (int k = 0; k < 5; ++k) {
                const QSignalBlocker hold(tools_[k]);
                tools_[k]->setChecked(k == i);
            }
        });
        bar->addWidget(b);
        tools_[i] = b;
    }
    tools_[0]->setChecked(true);
    bar->addSpacing(16);
    auto button = [&](const QString& text, const char* name, const QString& tip, auto fn) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QLatin1String(name));
        b->setToolTip(tip);
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, fn);
        bar->addWidget(b);
        return b;
    };
    button(QStringLiteral("Auto 1:1"), "wiringAuto", QStringLiteral("Every free pin of the left connector wired to the next free pin of the right, in turn"),
           [this] { autoOneToOne(); });
    button(QStringLiteral("Place cable..."), "wiringPlaceCable", QStringLiteral("A cable from the library: its conductors from the left connector's free "
                                                                                "pins to the right's, its pairs twisted and shields shielded"),
           [this] {
               QStringList titles, ids;
               for (const LibraryPart& p : harnessBuilderLibrary().parts)
                   if (p.kind == PartKind::Cable) {
                       titles << p.title();
                       ids << p.id;
                   }
               if (ids.isEmpty()) {
                   said_->setText(QStringLiteral("No cable in the connected libraries"));
                   return;
               }
               bool ok = false;
               const QString chosen = QInputDialog::getItem(this, QStringLiteral("Place cable"), QStringLiteral("Cable:"), titles, 0, false, &ok);
               if (ok) placeCable(ids.value(titles.indexOf(chosen)));
           });
    // ---- OPTIMISED PIN STACKS: round switches for what to optimise for, and Apply - put under the picture (below). While
    // any is on, every new connection is optimised as it is made.
    auto roundSwitch = [&](const QString& text, const char* name, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setObjectName(QLatin1String(name));
        b->setText(text);
        b->setToolTip(tip + QStringLiteral(" - while it is on, every new connection is optimised as it is made"));
        b->setCheckable(true);
        b->setStyleSheet(QStringLiteral("QToolButton{border:1px solid #6b7785;border-radius:11px;padding:2px 10px;}"
                                        "QToolButton:checked{background:#2f855a;color:white;border-color:#2f855a;}"));
        return b;
    };
    optCross_ = roundSwitch(QStringLiteral("Avoid crossing"), "wiringOptCross",
                            QStringLiteral("The second connector's pins put in the order of the first's - and the lines with them - "
                                           "so no line crosses another"));
    optShield_ = roundSwitch(QStringLiteral("By shield"), "wiringOptShield",
                             QStringLiteral("The wires shielded together on pins side by side"));
    optTwist_ = roundSwitch(QStringLiteral("By twist"), "wiringOptTwist",
                            QStringLiteral("The wires twisted together on pins side by side"));
    auto* optApply = new QPushButton(QStringLiteral("Apply"), this);
    optApply->setObjectName(QStringLiteral("wiringOptApply"));
    optApply->setToolTip(QStringLiteral("Optimise the pin stacks now, as the switches say - only the order the pins are drawn "
                                        "in changes; every wire stays on its pins"));
    optApply->setAutoDefault(false);
    connect(optApply, &QPushButton::clicked, this, [this] { optimizePinStack(); });
    bar->addSpacing(16);
    button(QStringLiteral("Undo"), "wiringUndo", QStringLiteral("Take back the last change (the wizard's own undo)"), [this] {
        if (undo_) undo_();
    });
    button(QStringLiteral("Redo"), "wiringRedo", QStringLiteral("Do it again"), [this] {
        if (redo_) redo_();
    });
    // THE BRICKS' SIZE: smaller or bigger (the wheel does the same), or as big as fits the view.
    bar->addSpacing(16);
    bar->addWidget(new QLabel(QStringLiteral("Bricks"), this));
    zoomSaid_ = new QLabel(QStringLiteral("100 %"), this);
    zoomSaid_->setObjectName(QStringLiteral("wiringZoomSaid"));
    zoomSaid_->setMinimumWidth(42);
    zoomSaid_->setAlignment(Qt::AlignCenter);
    auto sized = [this](double z) {
        canvas_->setZoom(z);
        zoomSaid_->setText(QStringLiteral("%1 %").arg(qRound(canvas_->zoom() * 100.0)));
        QSettings().setValue(QStringLiteral("harnessBuilder/ui/v2/wiringZoom"), canvas_->zoom());
    };
    button(QStringLiteral("−"), "wiringZoomOut", QStringLiteral("Smaller bricks"), [this, sized] { sized(canvas_->zoom() / 1.15); })->setMaximumWidth(30);
    bar->addWidget(zoomSaid_);
    button(QStringLiteral("+"), "wiringZoomIn", QStringLiteral("Bigger bricks"), [this, sized] { sized(canvas_->zoom() * 1.15); })->setMaximumWidth(30);
    button(QStringLiteral("Fit"), "wiringZoomFit", QStringLiteral("The bricks as big as fits the view"), [this, sized] {
        sized(canvas_->fitZoom(scroll_->viewport()->size() - QSize(8, 8)));
    })->setMaximumWidth(44);
    // THE WIRES IN THEIR COLOURS, or all white.
    colours_ = new QToolButton(this);
    colours_->setObjectName(QStringLiteral("wiringColours"));
    colours_->setText(QStringLiteral("Wire colours"));
    colours_->setToolTip(QStringLiteral("The wires drawn in their colours - off: all white, to see what goes where"));
    colours_->setCheckable(true);
    colours_->setChecked(QSettings().value(QStringLiteral("harnessBuilder/ui/v2/wiringColours"), true).toBool());
    connect(colours_, &QToolButton::toggled, this, [this](bool on) {
        canvas_->setWireColours(on);
        QSettings().setValue(QStringLiteral("harnessBuilder/ui/v2/wiringColours"), on);
    });
    bar->addSpacing(8);
    bar->addWidget(colours_);
    bar->addWidget(new QLabel(QStringLiteral("Print on all wires:"), this));
    const char* allNames[] = {"Colour", "Gauge", "Label"};
    for (int i=0;i<3;++i) {
        auto* box=new QCheckBox(QLatin1String(allNames[i]),this);
        box->setObjectName(QStringLiteral("wiringPrintAll%1").arg(i));
        box->setTristate(false);
        box->setStyleSheet(QStringLiteral("QCheckBox::indicator{width:12px;height:12px;border:1px solid #aebdca;border-radius:2px;image:none;background:transparent;}QCheckBox::indicator:checked{image:none;background:#62b5ef;border-color:#c4e8ff;}"));
        bar->addWidget(box);
        connect(box,&QCheckBox::clicked,this,[this,box,i](bool on) {
            if(filling_)return;
            for(auto& wire:def_.wires)(i==0?wire.showColor:i==1?wire.showGauge:wire.showLabel)=on;
            box->setCheckState(on?Qt::Checked:Qt::Unchecked);
            this->changed();
        });
    }
    bar->addStretch(1);
    lay->addLayout(bar);

    // ---- THE CANVAS, AND THE PANEL BESIDE IT ----
    auto* split = new QSplitter(Qt::Horizontal, this);
    // THE GROUPS, on the left: every pair, screen and cable - one clicked picks its wires; the lines picked light the
    // groups they make up.
    auto* groupsSide = new QWidget(split);
    auto* gsl = new QVBoxLayout(groupsSide);
    gsl->setContentsMargins(0, 0, 0, 0);
    auto* groupsHead = new QLabel(QStringLiteral("<b>Groups</b>"), groupsSide);
    groupsHead->setObjectName(QStringLiteral("wiringGroupsTitle"));
    gsl->addWidget(groupsHead);
    auto* draggableGroups = new WiringGroups(groupsSide);
    groups_ = draggableGroups;
    draggableGroups->promote = [this](const QString& kind, const QString& name) { promoteGroup(kind, name); };
    groups_->setObjectName(QStringLiteral("wiringGroups"));
    groups_->setToolTip(QStringLiteral("Click a group to select its wires. Drag a child shield into the root area to make it an independent part."));
    groups_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    gsl->addWidget(groups_, 1);
    // A SHIELD OVER A CABLE: a layer of the cable picked, a part of its own - not a group of its wires.
    addShield_ = new QPushButton(QStringLiteral("Add shield to cable"), groupsSide);
    addShield_->setObjectName(QStringLiteral("wiringAddCableShield"));
    addShield_->setToolTip(QStringLiteral("A shield over every wire of the cable picked - a layer of that cable, with a P/N of its own"));
    addShield_->setAutoDefault(false);
    addShield_->setEnabled(false);
    gsl->addWidget(addShield_);
    // ONE HARNESS: the cable picked drawn as one trunk, its wires fanned out only to their pins - this picture only.
    trunk_ = new QPushButton(QStringLiteral("Show as one harness"), groupsSide);
    trunk_->setObjectName(QStringLiteral("wiringCableTrunk"));
    trunk_->setToolTip(QStringLiteral("The cable picked drawn as one trunk here, its wires fanned out only to their pins - the drawing is not changed"));
    trunk_->setAutoDefault(false);
    trunk_->setCheckable(true);
    trunk_->setEnabled(false);
    gsl->addWidget(trunk_);
    auto cableOfItem = [this](QListWidgetItem* it) {
        return it && it->data(Qt::UserRole + 1).toString() == QLatin1String("cable") ? it->data(Qt::UserRole + 2).toString() : QString();
    };
    connect(groups_, &QListWidget::currentItemChanged, this, [this, cableOfItem](QListWidgetItem* it) {
        const QString cable = cableOfItem(it);
        bool shielded = false;
        for (const HarnessWire& w : def_.wires)
            shielded = shielded || (!cable.isEmpty() && w.bundle == cable && !w.shieldGroup.isEmpty() && cableOfShield(w.shieldGroup) == cable);
        addShield_->setEnabled(!cable.isEmpty() && !shielded);
        const QSignalBlocker hush(trunk_);
        trunk_->setEnabled(!cable.isEmpty());
        trunk_->setChecked(!cable.isEmpty() && canvas_->isTrunk(cable));
    });
    connect(trunk_, &QPushButton::toggled, this, [this, cableOfItem](bool on) {
        const QString cable = cableOfItem(groups_->currentItem());
        if (!cable.isEmpty()) canvas_->setTrunk(cable, on);
    });
    connect(addShield_, &QPushButton::clicked, this, [this, cableOfItem] {
        const QString cable = cableOfItem(groups_->currentItem());
        if (!cable.isEmpty()) addCableShield(cable);
    });
    groups_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(groups_, &QListWidget::customContextMenuRequested, this, [this, cableOfItem](const QPoint& at) {
        QListWidgetItem* it = groups_->itemAt(at);
        if (!it) return;
        QMenu menu(this);
        const QString cable = cableOfItem(it);
        const QString kind = it->data(Qt::UserRole + 1).toString(), name = it->data(Qt::UserRole + 2).toString();
        if (!cable.isEmpty()) {
            menu.addAction(QStringLiteral("Add shield (a layer of %1)").arg(cable), this, [this, cable] { addCableShield(cable); });
            const bool trunk = canvas_->isTrunk(cable);
            menu.addAction(trunk ? QStringLiteral("Show its wires apart") : QStringLiteral("Show as one harness (fan-out to the pins)"), this,
                           [this, cable, trunk, cableOfItem] {
                               canvas_->setTrunk(cable, !trunk);
                               if (cableOfItem(groups_->currentItem()) == cable) {
                                   const QSignalBlocker hush(trunk_);
                                   trunk_->setChecked(!trunk);
                               }
                           });
        }
        if (it->data(Qt::UserRole + 3).toBool())
            menu.addAction(QStringLiteral("Make root part"), this, [this, kind, name] { promoteGroup(kind, name); });
        if (kind == QLatin1String("shield"))
            menu.addAction(QStringLiteral("Remove shield %1").arg(name), this, [this, it] {
                harnessUngroupWires(def_, it->data(Qt::UserRole).toStringList(), HarnessGrouping::Screen);
                this->changed();
            });
        if (!menu.isEmpty()) menu.exec(groups_->viewport()->mapToGlobal(at));
    });
    split->addWidget(groupsSide);
    connect(groups_, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        QStringList ids;
        for (QListWidgetItem* it : groups_->selectedItems())
            for (const QString& id : it->data(Qt::UserRole).toStringList())
                if (!ids.contains(id)) ids << id;
        canvas_->setSelection(ids);
    });
    auto* middle = new QWidget(split);
    auto* mv = new QVBoxLayout(middle);
    mv->setContentsMargins(0, 0, 0, 0);
    scroll_ = new QScrollArea(middle);
    scroll_->setWidgetResizable(true);
    canvas_ = new WiringCanvas(def_, scroll_);
    scroll_->setWidget(canvas_);
    // As they were left: the colours on or off, the bricks' size.
    canvas_->setWireColours(colours_->isChecked());
    canvas_->setZoom(QSettings().value(QStringLiteral("harnessBuilder/ui/v2/wiringZoom"), 1.0).toDouble());
    zoomSaid_->setText(QStringLiteral("%1 %").arg(qRound(canvas_->zoom() * 100.0)));
    // Zoomed with the wheel too: said.
    connect(canvas_, &WiringCanvas::said, this, [this] { zoomSaid_->setText(QStringLiteral("%1 %").arg(qRound(canvas_->zoom() * 100.0))); });
    mv->addWidget(scroll_, 1);
    // UNDER THE PICTURE: what to optimise the pin stacks for, and the stacks of the connectors picked put in order.
    {
        auto* under = new WizardFlowLayout;
        under->addWidget(new QLabel(QStringLiteral("Optimise:"), middle));
        for (QWidget* w : {static_cast<QWidget*>(optCross_), static_cast<QWidget*>(optShield_), static_cast<QWidget*>(optTwist_),
                           static_cast<QWidget*>(optApply)})
            under->addWidget(w);
        under->addSpacing(20);
        under->addWidget(new QLabel(QStringLiteral("Pin stack:"), middle));
        auto* top = new QPushButton(QStringLiteral("Wired to top"), middle);
        top->setObjectName(QStringLiteral("wiringStackTop"));
        top->setToolTip(QStringLiteral("The connectors picked (click a brick; Ctrl for more - none picked: all shown): their wired "
                                       "pins at the top of the stack, the free ones below"));
        top->setAutoDefault(false);
        connect(top, &QPushButton::clicked, this, [this] { orderStacks(true); });
        auto* bottom = new QPushButton(QStringLiteral("Wired to bottom"), middle);
        bottom->setObjectName(QStringLiteral("wiringStackBottom"));
        bottom->setToolTip(QStringLiteral("The connectors picked: their free pins at the top, the wired ones below"));
        bottom->setAutoDefault(false);
        connect(bottom, &QPushButton::clicked, this, [this] { orderStacks(false); });
        under->addWidget(top);
        under->addWidget(bottom);
        under->addSpacing(20);
        // THE PINS NOTHING IS WIRED TO, terminated - their X on the drawing, and here.
        terminate_ = roundSwitch(QStringLiteral("Terminate unconnected"), "wiringTerminate",
                                 QStringLiteral("Mark every pin nothing is wired to with an X - on the pin, on the drawing"));
        terminate_->setToolTip(QStringLiteral("Mark every pin nothing is wired to with an X - on the pin, on the drawing"));
        terminate_->setParent(middle);
        terminate_->setChecked(def_.layout.terminateUnconnected);
        connect(terminate_, &QToolButton::toggled, this, [this](bool on) {
            if (filling_) return;
            def_.layout.terminateUnconnected = on;
            // ONLY THE DRAWING CHANGES: drawn again here and there, not every table of the wizard rebuilt - which is what
            // made it slow.
            if (layoutChanged_) layoutChanged_();
            else if (changed_) changed_();
            canvas_->update();
            if (previewTimer_) previewTimer_->start();
        });
        under->addWidget(terminate_);
        under->addWidget(new QLabel(QStringLiteral("Connector spacing"),middle));
        auto* spacing=new QDoubleSpinBox(middle);spacing->setObjectName(QStringLiteral("wiringConnectorSpacing"));
        spacing->setRange(260,2400);spacing->setDecimals(0);spacing->setSuffix(QStringLiteral(" px"));
        spacing->setKeyboardTracking(false);
        spacing->setValue(QSettings().value(QStringLiteral("wiring/connectorSpacing"),460).toDouble());
        canvas_->setConnectorSpacing(spacing->value());
        spacing->setToolTip(QStringLiteral("Width between the connector columns before zooming"));
        connect(spacing,&QDoubleSpinBox::valueChanged,this,[this](double value){
            canvas_->setConnectorSpacing(value);QSettings().setValue(QStringLiteral("wiring/connectorSpacing"),value);
        });
        under->addWidget(spacing);
        under->addStretch(1);
        mv->addLayout(under);
    }
    split->addWidget(middle);
    panel_ = new QWidget(split);
    panel_->setObjectName(QStringLiteral("wiringPanel"));
    auto* pl = new QVBoxLayout(panel_);
    panelTitle_ = new QLabel(panel_);
    panelTitle_->setObjectName(QStringLiteral("wiringPanelTitle"));
    panelTitle_->setWordWrap(true);
    pl->addWidget(panelTitle_);
    // SHORT FIELDS, the same whatever the width: each as wide as what goes in it, not as the panel.
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    part_ = new QComboBox(panel_);
    part_->setObjectName(QStringLiteral("wiringPart"));
    form->addRow(QStringLiteral("Part (P/N)"), part_);
    auto* colours = new QWidget(panel_);
    auto* cl = new QHBoxLayout(colours);
    cl->setContentsMargins(0, 0, 0, 0);
    base_ = new QComboBox(colours);
    base_->setObjectName(QStringLiteral("wiringBaseColor"));
    stripe_ = new QComboBox(colours);
    stripe_->setObjectName(QStringLiteral("wiringStripeColor"));
    stripe_->addItem(QStringLiteral("No stripe"), QString());
    for (const QString& code : wireColorCodes()) {
        if (code.contains(QLatin1Char('-'))) continue;
        QPixmap swatch(14, 14);
        swatch.fill(QColor(wireColorHex(code)));
        base_->addItem(QIcon(swatch), QStringLiteral("%1 · %2").arg(code, wireColorName(code)), code);
        if (code != QLatin1String("SIGNAL") && code != QLatin1String("SHIELD") && code != QLatin1String("DRAIN"))
            stripe_->addItem(QIcon(swatch), QStringLiteral("%1 · %2").arg(code, wireColorName(code)), code);
    }
    cl->addWidget(base_);
    cl->addWidget(stripe_);
    form->addRow(QStringLiteral("Colour"), colours);
    gauge_ = new QComboBox(panel_);
    gauge_->setObjectName(QStringLiteral("wiringGauge"));
    for (const QString& mm2 : wireGaugeChoices()) gauge_->addItem(wireGaugeLabel(mm2, true), mm2);
    form->addRow(QStringLiteral("Cross section"), gauge_);
    auto* cmaCorner = new CanvasCorner(scroll_->viewport());
    auto* cmaLayout = new QHBoxLayout(cmaCorner);
    cmaLayout->setContentsMargins(8, 4, 8, 4);
    cmaLayout->addWidget(new QLabel(QStringLiteral("CMA"), cmaCorner));
    cma_ = new QLabel(cmaCorner); cma_->setObjectName(QStringLiteral("wiringCma"));
    cma_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cmaLayout->addWidget(cma_);
    cmaCorner->show();
    label_ = new QLineEdit(panel_);
    label_->setObjectName(QStringLiteral("wiringLabel"));
    form->addRow(QStringLiteral("Label"), label_);

    auto* marks = new QWidget(panel_);
    auto* ml = new QHBoxLayout(marks);
    ml->setContentsMargins(0, 0, 0, 0);
    const char* markNames[3] = {"Colour", "Gauge", "Label"};
    for (int i = 0; i < 3; ++i) {
        print_[i] = new QCheckBox(QString::fromLatin1(markNames[i]), marks);
        print_[i]->setObjectName(QStringLiteral("wiringPrint%1").arg(QString::fromLatin1(markNames[i])));
        ml->addWidget(print_[i]);
        connect(print_[i], &QCheckBox::toggled, this, [this, i](bool on) {
            if (filling_) return;
            applyToSelection([i, on](HarnessWire& w) { (i == 0 ? w.showColor : i == 1 ? w.showGauge : w.showLabel) = on; });
        });
    }
    form->addRow(QStringLiteral("Print on wire"), marks);
    pl->addLayout(form);
    auto* groups = new QGroupBox(QStringLiteral("The lines picked, made"), panel_);
    auto* gl = new QGridLayout(groups);
    int groupButtonIndex=0;
    auto groupButton = [&](const QString& text, const char* name, WiringCanvas::Tool tool) {
        auto* b = new QPushButton(text, groups);
        b->setObjectName(QLatin1String(name));
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, [this, tool] {
            groupWires(tool, canvas_->selection(), tool == WiringCanvas::Tool::Screen && askDrain_ ? askDrain_() : QString());
        });
        gl->addWidget(b,groupButtonIndex/2,groupButtonIndex%2);++groupButtonIndex;
    };
    groupButton(QStringLiteral("Twisted"), "wiringMakeTwisted", WiringCanvas::Tool::Twist);
    groupButton(QStringLiteral("Shielded..."), "wiringMakeScreened", WiringCanvas::Tool::Screen);
    groupButton(QStringLiteral("One cable"), "wiringMakeCable", WiringCanvas::Tool::Cable);
    auto* apart = new QPushButton(QStringLiteral("Apart"), groups);
    apart->setObjectName(QStringLiteral("wiringMakeApart"));
    apart->setAutoDefault(false);
    connect(apart, &QPushButton::clicked, this, [this] {
        harnessUngroupWires(def_, canvas_->selection(), HarnessGrouping::All);
        this->changed();
    });
    gl->addWidget(apart,1,1);
    pl->addWidget(groups);
    // A SCREEN PICKED (every line picked in one): its own part, and how it is drained at each end.
    {
        screenBox_ = new QGroupBox(QStringLiteral("Shield"), panel_);
        screenBox_->setObjectName(QStringLiteral("wiringScreenBox"));
        auto* sf = new QFormLayout(screenBox_);
        sf->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        sf->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        screenPart_ = new QComboBox(screenBox_);
        screenPart_->setObjectName(QStringLiteral("wiringScreenPart"));
        screenPart_->setEditable(true);
        screenPart_->setInsertPolicy(QComboBox::NoInsert);
        screenPart_->setToolTip(QStringLiteral("The shield's own part: one from the parts list, or a P/N typed"));
        sf->addRow(QStringLiteral("P/N"), screenPart_);
        connect(screenPart_, &QComboBox::activated, this, [this](int i) {
            if (filling_) return;
            const QString g = screenOfSelection();
            if (!g.isEmpty()) setScreenPart(g, screenPart_->itemData(i).toString(), screenPart_->itemData(i).toString().isEmpty() ? screenPart_->currentText() : QString());
        });
        connect(screenPart_->lineEdit(), &QLineEdit::editingFinished, this, [this] {
            if (filling_) return;
            const QString g = screenOfSelection();
            const int at = screenPart_->findText(screenPart_->currentText());
            if (g.isEmpty() || (at >= 0 && !screenPart_->itemData(at).toString().isEmpty())) return;
            setScreenPart(g, QString(), screenPart_->currentText().trimmed());
        });
        for (int end = 0; end < 2; ++end) {
            auto* host = new QWidget(screenBox_);
            auto* hl = new QHBoxLayout(host);
            hl->setContentsMargins(0, 0, 0, 0);
            // EACH END: how it is drained, onto what, and - a hook-up wire - which wire.
            auto* v = new QVBoxLayout;
            v->setContentsMargins(0, 0, 0, 0);
            v->setSpacing(2);
            screenEnd_[end] = new QComboBox(host);
            screenEnd_[end]->setObjectName(end ? QStringLiteral("wiringScreenRight") : QStringLiteral("wiringScreenLeft"));
            screenPin_[end] = new QComboBox(host);
            screenPin_[end]->setObjectName(end ? QStringLiteral("wiringScreenRightTo") : QStringLiteral("wiringScreenLeftTo"));
            screenPin_[end]->setToolTip(QStringLiteral("Where it goes: any connector's pin, any connector's shield, or another shield"));
            screenWire_[end] = new QComboBox(host);
            screenWire_[end]->setObjectName(end ? QStringLiteral("wiringScreenRightWire") : QStringLiteral("wiringScreenLeftWire"));
            screenWire_[end]->setToolTip(QStringLiteral("The hook-up wire, from the parts list"));
            hl->addWidget(screenEnd_[end]);
            v->addWidget(screenPin_[end]);
            v->addWidget(screenWire_[end]);
            auto* box = new QWidget(screenBox_);
            box->setLayout(v);
            host->setParent(box);
            v->insertWidget(0, host);
            sf->addRow(end ? QStringLiteral("Right") : QStringLiteral("Left"), box);
            auto apply = [this, end] {
                if (filling_) return;
                const QString g = screenOfSelection();
                if (g.isEmpty()) return;
                setScreenDrain(g, end, screenEnd_[end]->currentData().toString(), screenPin_[end]->currentData().toString(),
                               screenWire_[end]->currentData().toString());
            };
            connect(screenEnd_[end], &QComboBox::activated, this, apply);
            connect(screenPin_[end], &QComboBox::activated, this, apply);
            connect(screenWire_[end], &QComboBox::activated, this, apply);
        }
        pl->addWidget(screenBox_);
    }
    auto* del = new QPushButton(QStringLiteral("Delete the lines picked"), panel_);
    del->setObjectName(QStringLiteral("wiringDelete"));
    del->setAutoDefault(false);
    connect(del, &QPushButton::clicked, this, [this] { deleteSelected(); });
    pl->addWidget(del, 0, Qt::AlignLeft);
    // THE HARNESS AS IT WILL BE DRAWN, small, under the details - the wiring seen as the drawing has it, as it is made.
    preview_ = new DrawingPreview(QString(), this);
    preview_->setObjectName(QStringLiteral("wiringPreview"));
    preview_->setKeepView(false);
    preview_->setEmptyText(QStringLiteral("Wire the connectors - the harness is drawn here"));
    preview_->setToolTip(QStringLiteral("The harness as Generate will draw it - wheel to zoom, drag to move"));
    preview_->setMinimumHeight(200);
    preview_->setMaximumHeight(260);
    preview_->setMinimumWidth(kPanelField + 90);
    preview_->setMaximumWidth(kPanelField + 110);
    // ITS HEADING, and beside it Enlarge: the same drawing in a window of its own, as big as it is made.
    auto* previewHead = new QHBoxLayout;
    previewHead->addWidget(new QLabel(QStringLiteral("<b>As it will be drawn</b>"), panel_));
    previewHead->addStretch(1);
    auto* enlarge = new QToolButton(panel_);
    enlarge->setObjectName(QStringLiteral("wiringPreviewEnlarge"));
    enlarge->setText(QStringLiteral("Enlarge"));
    enlarge->setToolTip(QStringLiteral("The drawing in a window of its own - made as big as wanted, drawn again as the wiring changes"));
    enlarge->setAutoRaise(true);
    connect(enlarge, &QToolButton::clicked, this, [this] { enlargePreview(); });
    previewHead->addWidget(enlarge);
    pl->addLayout(previewHead);
    pl->addWidget(preview_, 0, Qt::AlignLeft);
    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(120);
    connect(previewTimer_, &QTimer::timeout, this, [this] {
        const std::vector<Entity> shown = harnessPreviewEntities(def_, true);
        preview_->setEntities(shown);
        preview_->fitToView();
        if (bigPreview_) {
            bigPreview_->setEntities(shown);
            bigPreview_->update();
        }
    });
    // THE TWIST AND SHIELD SIGNS moved along their wires here - drawn again as they go, the drawing told when let go.
    auto marked = [this](DrawingPreview* view) {
        armHarnessMarkDrag(view, [this]() -> HarnessDef& { return def_; }, [this, view](bool done) {
            const std::vector<Entity> shown = harnessPreviewEntities(def_, true);
            view->setEntities(shown);
            view->update();
            canvas_->relayout();
            if (!done) return;
            if (layoutChanged_) layoutChanged_();
            else if (changed_) changed_();
            previewTimer_->start();
        });
    };
    marked(preview_);
    markDrag_ = marked;
    pl->addStretch(1);
    // EVERY FIELD SHORT: a set width, whatever the panel's - the long part names elided rather than widening it.
    for (QComboBox* box : panel_->findChildren<QComboBox*>()) {
        const bool pair = box == base_ || box == stripe_;
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(8);
        box->setFixedWidth(pair ? (kPanelField - 4) / 2 : kPanelField);
    }
    for (QLineEdit* edit : panel_->findChildren<QLineEdit*>())
        if (!qobject_cast<QComboBox*>(edit->parentWidget())) edit->setFixedWidth(kPanelField);
    panel_->setMaximumWidth(kPanelField + 140);
    auto* detailsScroll=new QScrollArea(split);
    detailsScroll->setObjectName(QStringLiteral("wiringDetailsScroll"));
    detailsScroll->setWidgetResizable(true);detailsScroll->setFrameShape(QFrame::NoFrame);
    detailsScroll->setMinimumWidth(kPanelField+140);
    detailsScroll->setWidget(panel_);
    split->addWidget(detailsScroll);
    split->setStretchFactor(1, 1);
    split->setSizes({170, 860, 320});
    lay->addWidget(split, 1);
    problems_ = new QLabel(this);
    problems_->setObjectName(QStringLiteral("wiringProblems"));
    problems_->setWordWrap(true);
    problems_->setStyleSheet(QStringLiteral("color:#f08c64;"));
    lay->addWidget(problems_);
    said_ = new QLabel(this);
    said_->setObjectName(QStringLiteral("wiringSaid"));
    lay->addWidget(said_);
    auto* help=new QLabel(QStringLiteral("Joint: right-click a wire and choose Add a joint, or drag a pin onto a wire. "
        "The new branch is a separate wire. Double-click a free pin to toggle its termination."),this);
    help->setObjectName(QStringLiteral("wiringJointHelp"));help->setWordWrap(true);lay->addWidget(help);

    // ---- THE PANEL INTO THE WIRES PICKED ----
    connect(part_, &QComboBox::activated, this, [this](int i) { setSelectedPart(part_->itemData(i).toString()); });
    auto colour = [this] {
        if (filling_) return;
        const QString stripe = stripe_->currentData().toString();
        setSelectedColor(base_->currentData().toString() + (stripe.isEmpty() ? QString() : QLatin1Char('-') + stripe));
    };
    connect(base_, &QComboBox::activated, this, colour);
    connect(stripe_, &QComboBox::activated, this, colour);
    connect(gauge_, &QComboBox::activated, this, [this](int i) { setSelectedGauge(gauge_->itemData(i).toString()); });
    connect(label_, &QLineEdit::editingFinished, this, [this] {
        if (!filling_) setSelectedLabel(label_->text().trimmed());
    });


    // ---- THE CANVAS INTO THE HARNESS ----
    connect(canvas_,&WiringCanvas::terminatePinAsked,this,[this](const QString& conn,int pin){
        const QString key=pinKey(conn,pin);
        const bool was=def_.layout.pinTerminations.value(key,def_.layout.terminateUnconnected);
        def_.layout.pinTerminations.insert(key,!was);
        if(layoutChanged_)layoutChanged_();else if(changed_)changed_();
        canvas_->update();redrawPreview();
        said_->setText(was?QStringLiteral("Pin termination removed"):QStringLiteral("Unconnected pin terminated; double-click again to remove"));
    });
    connect(canvas_, &WiringCanvas::connectAsked, this, [this](const QString& fc, int fp, const QString& tc, int tp) { connectPins(fc, fp, tc, tp); });
    connect(canvas_, &WiringCanvas::relandAsked, this, [this](const QString& id, int end, const QString& conn, int pin) { relandEnd(id, end, conn, pin); });
    connect(canvas_, &WiringCanvas::pinStackMoveAsked, this,
            [this](const QString& conn, int pin, int toPin) { movePinInStack(conn, pin, toPin); });
    connect(canvas_, &WiringCanvas::wireWithPinsMoveAsked, this, [this](const QString& id, int at) { moveWireWithPins(id, at); });
    connect(canvas_, &WiringCanvas::jointAsked, this,
            [this](const QString& conn, int pin, const QString& wire) { connectToWire(conn, pin, wire); });
    connect(canvas_, &WiringCanvas::jointMenuAsked, this, [this](const QString& wire, const QPoint& at) {
        QMenu menu(this);
        if (!harnessIsJointEndpoint(wire))
            menu.addAction(QStringLiteral("Split wire at a joint"), this, [this, wire] { splitAtJoint(wire); });
        menu.addAction(harnessIsJointEndpoint(wire) ? QStringLiteral("Connect a pin to joint %1...").arg(harnessJointEndpointWire(wire))
                                                   : QStringLiteral("Add a joint on wire %1...").arg(wire), this, [this, wire] { addJointOnWire(wire); });
        menu.exec(at);
    });
    connect(canvas_, &WiringCanvas::toolPicked, this, [this](WiringCanvas::Tool tool) {
        for (int k = 0; k < 5; ++k) {
            const QSignalBlocker hold(tools_[k]);
            tools_[k]->setChecked(k == static_cast<int>(tool));
        }
        said_->setText(QStringLiteral("Select"));
    });
    connect(canvas_, &WiringCanvas::moveAsked, this, [this](const QString& id, int at) { moveWireWithPins(id, at); });
    connect(canvas_, &WiringCanvas::strokeMade, this, [this](WiringCanvas::Tool tool, const QStringList& ids) {
        groupWires(tool, ids);
    });
    connect(canvas_, &WiringCanvas::selectionChanged, this, [this] {
        fillPanel();
        markGroups();
    });
    connect(canvas_, &WiringCanvas::deleteAsked, this, [this] { deleteSelected(); });
    connect(canvas_, &WiringCanvas::ghostAccepted, this, [this] { acceptGhost(); });
    connect(canvas_, &WiringCanvas::said, said_, &QLabel::setText);
    connect(mode_, &QComboBox::activated, this, [this](int i) {
        if (i == 0) showTwo(left_->currentData().toString(), right_->currentData().toString());
        else showSeveral();
    });
    auto pair = [this] {
        if (!filling_) showTwo(left_->currentData().toString(), right_->currentData().toString());
    };
    connect(left_, &QComboBox::activated, this, pair);
    connect(right_, &QComboBox::activated, this, pair);
    connect(several_, &QListWidget::itemChanged, this, [this] {
        if (filling_) return;
        QStringList ids;
        for (int i = 0; i < several_->count(); ++i)
            if (several_->item(i)->checkState() == Qt::Checked) ids << several_->item(i)->data(Qt::UserRole).toString();
        showSeveral(ids);
    });
    fillConnectorChoices();
    // TWO, to begin with: the first left-hand connector and the first right-hand one.
    QString a, b;
    for (const HarnessConnector& c : def_.connectors) {
        if (a.isEmpty() && c.side == HarnessSide::Left) a = c.id;
        if (b.isEmpty() && c.side == HarnessSide::Right) b = c.id;
    }
    if (a.isEmpty() && !def_.connectors.empty()) a = def_.connectors.front().id;
    if ((b.isEmpty() || b == a) && def_.connectors.size() > 1) b = def_.connectors.back().id;
    showTwo(a, b);
}

void WiringDialog::setUndo(std::function<void()> undo, std::function<void()> redo) {
    undo_ = std::move(undo);
    redo_ = std::move(redo);
}

void WiringDialog::fillConnectorChoices() {
    filling_ = true;
    const QString a = left_->currentData().toString(), b = right_->currentData().toString();
    left_->clear();
    right_->clear();
    several_->clear();
    // EACH PICKER ITS OWN SIDE: the left one the left-hand connectors, the right one the right-hand ones - every connector
    // in both only where a side has none.
    bool anyLeft = false, anyRight = false;
    for (const HarnessConnector& c : def_.connectors) (c.side == HarnessSide::Left ? anyLeft : anyRight) = true;
    for (const HarnessConnector& c : def_.connectors) {
        const QString name = QStringLiteral("%1 (%2 pins)").arg(c.designator.isEmpty() ? c.id : c.designator).arg(c.pinCount);
        if (c.side == HarnessSide::Left || !anyLeft) left_->addItem(name, c.id);
        if (c.side != HarnessSide::Left || !anyRight) right_->addItem(name, c.id);
        auto* item = new QListWidgetItem(c.designator.isEmpty() ? c.id : c.designator, several_);
        item->setData(Qt::UserRole, c.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(canvas_ && canvas_->shown().contains(c.id) ? Qt::Checked : Qt::Unchecked);
    }
    left_->setCurrentIndex(std::max(0, left_->findData(a)));
    right_->setCurrentIndex(std::max(0, right_->findData(b)));
    filling_ = false;
}

bool WiringDialog::twoMode() const { return mode_->currentIndex() == 0; }

void WiringDialog::showTwo(const QString& left, const QString& right) {
    {
        const QSignalBlocker hold(mode_);
        mode_->setCurrentIndex(0);
    }
    pickers_->setCurrentIndex(0);
    filling_ = true;
    left_->setCurrentIndex(std::max(0, left_->findData(left)));
    right_->setCurrentIndex(std::max(0, right_->findData(right)));
    filling_ = false;
    QStringList ids;
    if (!left.isEmpty()) ids << left;
    if (!right.isEmpty() && right != left) ids << right;
    canvas_->setShown(ids, true);
    canvas_->clearGhost();
    fillConnectorChoices();
    fillPanel();
    sayProblems();
}

void WiringDialog::showSeveral(const QStringList& ids) {
    {
        const QSignalBlocker hold(mode_);
        mode_->setCurrentIndex(1);
    }
    pickers_->setCurrentIndex(1);
    QStringList shown = ids;
    if (shown.isEmpty())
        for (const HarnessConnector& c : def_.connectors) shown << c.id;
    canvas_->setShown(shown, false);
    canvas_->clearGhost();
    fillConnectorChoices();
    fillPanel();
    sayProblems();
}

void WiringDialog::refresh() {
    // The small drawing again, a moment after the last change.
    if (previewTimer_) previewTimer_->start();
    if (zoomSaid_) zoomSaid_->setText(QStringLiteral("%1 %").arg(qRound(canvas_->zoom() * 100.0)));
    if (terminate_) {
        const QSignalBlocker hush(terminate_);
        terminate_->setChecked(def_.layout.terminateUnconnected);
    }
    QStringList shown;
    for (const QString& id : canvas_->shown())
        if (harnessConnById(def_, id)) shown << id;
    QStringList kept;
    for (const QString& id : canvas_->selection())
        if (std::any_of(def_.wires.begin(), def_.wires.end(), [&id](const HarnessWire& w) { return w.id == id; })) kept << id;
    canvas_->setShown(shown, twoMode());
    {
        const QSignalBlocker hold(canvas_);
        canvas_->setSelection(kept);
    }
    fillConnectorChoices();
    fillPanel();
    fillGroups();
    sayProblems();
}

// THE GROUPS LIST: every twisted pair, screen and cable, with its wires - those the lines picked make up lit.
void WiringDialog::fillGroups() {
    if (!groups_) return;
    const QSignalBlocker hush(groups_);
    groups_->clear();
    auto add = [this](const QString& kind, const QString& name, const std::function<bool(const HarnessWire&)>& in,
                      bool under = false) {
        QStringList ids;
        for (const HarnessWire& w : def_.wires)
            if (in(w)) ids << w.id;
        if (ids.isEmpty()) return;
        // A shield: its P/N beside it - a part of its own.
        QString pn;
        if (kind != QLatin1String("cable") && kind != QLatin1String("twisted")) {
            const LibraryPart* p = harnessBuilderLibrary().byId(def_.shieldParts.value(name));
            pn = p ? p->mfrPn : def_.shieldPns.value(name);
        }
        auto* it = new QListWidgetItem(QStringLiteral("%1%2  %3 · %4 wire(s)%5").arg(under ? QStringLiteral("    ↳ ") : QString(), name, kind)
                                           .arg(ids.size())
                                           .arg(pn.isEmpty() ? QString() : QStringLiteral(" · ") + pn), groups_);
        it->setData(Qt::UserRole, ids);
        it->setData(Qt::UserRole + 1, kind.startsWith(QLatin1String("shield")) ? QStringLiteral("shield") : kind);
        it->setData(Qt::UserRole + 2, name);
        it->setData(Qt::UserRole + 3, under);
        if (under) it->setFlags(it->flags() | Qt::ItemIsDragEnabled);
        else it->setFlags(it->flags() & ~Qt::ItemIsDragEnabled);
        it->setToolTip(ids.join(QStringLiteral(", ")));
    };
    QStringList twists, screens, cables;
    for (const HarnessWire& w : def_.wires) {
        if (!w.twistGroup.isEmpty() && !twists.contains(w.twistGroup)) twists << w.twistGroup;
        if (!w.shieldGroup.isEmpty() && !screens.contains(w.shieldGroup)) screens << w.shieldGroup;
        if (!w.bundle.isEmpty() && !cables.contains(w.bundle)) cables << w.bundle;
    }
    // EACH CABLE, and under it its shield where it has one - a layer of the cable, not a shield among the others.
    QStringList layers;
    for (const QString& g : cables) {
        add(QStringLiteral("cable"), g, [&g](const HarnessWire& w) { return w.bundle == g; });
        for (const QString& s : screens)
            if (cableOfShield(s) == g) {
                layers << s;
                add(QStringLiteral("shield, layer of ") + g, s, [&s](const HarnessWire& w) { return w.shieldGroup == s; }, true);
            }
    }
    for (const QString& s : layers) screens.removeAll(s);
    for (const QString& g : twists)
        add(QStringLiteral("twisted"), g, [&g](const HarnessWire& w) { return w.twistGroup == g; });
    // THE SHIELDS, apart: no group - the wires under one stay where they are - but each there to be picked.
    if (!screens.isEmpty()) {
        auto* head = new QListWidgetItem(QStringLiteral("Shields"), groups_);
        head->setFlags(Qt::NoItemFlags);
        QFont f = head->font();
        f.setBold(true);
        head->setFont(f);
        head->setForeground(QColor(140, 190, 255));
    }
    for (const QString& g : screens)
        add(QStringLiteral("shield"), g, [&g](const HarnessWire& w) { return w.shieldGroup == g; });
    markGroups();
}

void WiringDialog::redrawPreview() {
    if (previewTimer_) previewTimer_->start();
}

DrawingPreview* WiringDialog::enlargePreview() {
    if (bigPreview_) {
        if(bigPreview_->window()->isMinimized())bigPreview_->window()->showNormal();
        else bigPreview_->window()->show();
        bigPreview_->window()->raise();
        bigPreview_->window()->activateWindow();
        return bigPreview_;
    }
    auto* win = new QDialog(this);
    win->setObjectName(QStringLiteral("wiringPreviewWindow"));
    win->setWindowTitle(QStringLiteral("As it will be drawn"));
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->setSizeGripEnabled(true);
    auto* v = new QVBoxLayout(win);
    v->setContentsMargins(4, 4, 4, 4);
    auto* big = new DrawingPreview(QString(), win);
    big->setObjectName(QStringLiteral("wiringPreviewBig"));
    big->setKeepView(true);
    big->setEmptyText(QStringLiteral("Wire the connectors - the harness is drawn here"));
    big->setToolTip(QStringLiteral("The harness as Generate will draw it - wheel to zoom, drag to move; drag a twist or a shield sign "
                                   "along its wires"));
    v->addWidget(big, 1);
    bigPreview_ = big;
    if (markDrag_) markDrag_(big);
    const QByteArray was = QSettings().value(QStringLiteral("harnessBuilder/ui/v2/wiringPreviewWindow")).toByteArray();
    if (!was.isEmpty()) win->restoreGeometry(was);
    else win->resize(960, 620);
    connect(win, &QDialog::finished, this, [win] { QSettings().setValue(QStringLiteral("harnessBuilder/ui/v2/wiringPreviewWindow"), win->saveGeometry()); });
    big->setEntities(harnessPreviewEntities(def_, true));
    win->show();
    big->fitToView();
    return big;
}

QString WiringDialog::cableOfShield(const QString& shield) const {
    if (shield.isEmpty() || def_.rootShields.contains(shield)) return {};
    QString cable;
    bool any = false;
    for (const HarnessWire& w : def_.wires) {
        if (!harnessConnById(def_, w.fromConn) || !harnessConnById(def_, w.toConn)) continue;   // a drain, a lead
        if (w.shieldGroup == shield) {
            if (w.bundle.isEmpty() || (any && w.bundle != cable)) return {};
            cable = w.bundle;
            any = true;
        }
    }
    if (!any) return {};
    for (const HarnessWire& w : def_.wires)
        if (w.bundle == cable && harnessConnById(def_, w.fromConn) && harnessConnById(def_, w.toConn) && w.shieldGroup != shield) return {};
    return cable;
}

bool WiringDialog::promoteGroup(const QString& kind, const QString& name) {
    if (kind != QLatin1String("shield") || cableOfShield(name).isEmpty()) return false;
    def_.rootShields << name;
    changed();
    return true;
}

QString WiringDialog::addCableShield(const QString& cable) {
    QStringList ids;
    for (const HarnessWire& w : def_.wires)
        if (w.bundle == cable && harnessConnById(def_, w.fromConn) && harnessConnById(def_, w.toConn)) ids << w.id;
    if (ids.isEmpty()) return {};
    const QString name = harnessScreenWires(def_, ids);
    def_.rootShields.removeAll(name);
    changed();
    // Picked, so its P/N and its drains are asked for in the panel.
    canvas_->setSelection(ids);
    return name;
}

void WiringDialog::markGroups() {
    if (!groups_) return;
    const QSignalBlocker hush(groups_);
    const QStringList sel = canvas_->selection();
    for (int i = 0; i < groups_->count(); ++i) {
        QListWidgetItem* it = groups_->item(i);
        const QStringList ids = it->data(Qt::UserRole).toStringList();
        if (ids.isEmpty()) continue;   // a heading
        // LIT when every wire of it is picked.
        const bool all = !sel.isEmpty() && std::all_of(ids.begin(), ids.end(), [&sel](const QString& id) { return sel.contains(id); });
        it->setSelected(all);
        QFont f = it->font();
        f.setBold(all);
        it->setFont(f);
    }
}

QString WiringDialog::screenOfSelection() const {
    QString g;
    for (const HarnessWire& w : def_.wires) {
        if (!canvas_->selection().contains(w.id)) continue;
        if (w.shieldGroup.isEmpty() || (!g.isEmpty() && g != w.shieldGroup)) return {};
        g = w.shieldGroup;
    }
    return g;
}

QString WiringDialog::screenEndConn(const QString& group, int end) const {
    for (const HarnessWire& w : def_.wires)
        if (w.shieldGroup == group && harnessConnById(def_, w.fromConn) && harnessConnById(def_, w.toConn))
            return end == 0 ? w.fromConn : w.toConn;
    return {};
}

// WHICH END A DRAIN IS AT: as its label says ("drain left" / "drain right") - or, a drain made before that (a pin given when
// the screen was made), by the connector its pin is on.
static int drainEndOf(const HarnessWire& w, const QString& leftConn) {
    if (w.label == QLatin1String("drain right")) return 1;
    if (w.label == QLatin1String("drain left")) return 0;
    const QString pinConn = harnessIsShieldEndpoint(w.fromConn) ? w.toConn : w.fromConn;
    return pinConn == leftConn ? 0 : 1;
}

QString WiringDialog::screenDrain(const QString& group, int end, QString* target, QString* wirePart) const {
    const QString key = harnessInternalShieldKey(group);
    const QString leftConn = screenEndConn(group, 0);
    for (const HarnessWire& w : def_.wires) {
        const bool fromScreen = w.fromConn == key, toScreen = w.toConn == key;
        if (!fromScreen && !toScreen) continue;
        if (drainEndOf(w, leftConn) != end) continue;
        const QString other = fromScreen ? w.toConn : w.fromConn;
        const int otherPin = fromScreen ? w.toPin : w.fromPin;
        QString shieldConn;HarnessSide side=HarnessSide::Left;
        if(harnessShieldEndpointConn(other,shieldConn,side)) {
            const auto* c=harnessConnById(def_,shieldConn);
            if(!c || !(side==HarnessSide::Left?c->shieldLeft:c->shieldRight))continue;
        }
        if (target) {
            QString conn;
            HarnessSide side = HarnessSide::Left;
            if (harnessConnById(def_, other)) *target = QStringLiteral("pin:%1:%2").arg(other).arg(otherPin);
            else if (harnessShieldEndpointConn(other, conn, side)) *target = QStringLiteral("shield:") + conn;
            else if (!harnessShieldEndpointGroup(other).isEmpty()) *target = QStringLiteral("screen:") + harnessShieldEndpointGroup(other);
        }
        if (wirePart) *wirePart = w.libPart;
        const HarnessTermination& atScreen = fromScreen ? w.fromTermination : w.toTermination;
        return atScreen.via == QLatin1String("direct") ? QStringLiteral("drain") : QStringLiteral("hook");
    }
    return {};
}

bool WiringDialog::setScreenDrain(const QString& group, int end, const QString& how, const QString& target, const QString& wirePart) {
    const QString key = harnessInternalShieldKey(group);
    const QString leftConn = screenEndConn(group, 0);
    // WHAT DRAINED IT AT THAT END GOES FIRST.
    for (size_t k = def_.wires.size(); k-- > 0;) {
        const HarnessWire& w = def_.wires[k];
        if ((w.fromConn == key || w.toConn == key) && drainEndOf(w, leftConn) == end)
            def_.wires.erase(def_.wires.begin() + static_cast<long long>(k));
    }
    const QString endKey=key+QStringLiteral(":end:%1").arg(end);
    def_.layout.pinTerminations.remove(endKey);
    bool made = true;
    if (!how.isEmpty() && how!=QLatin1String("terminated") && !target.isEmpty()) {
        HarnessWire d;
        d.id = harnessFreshWireId(def_);
        d.fromConn = key;
        d.fromPin = 0;
        d.color = QStringLiteral("DRAIN");
        d.label = end ? QStringLiteral("drain right") : QStringLiteral("drain left");
        // THE SCREEN'S OWN DRAIN, or a hook-up wire spliced onto it - a library wire, its gauge and colour with it.
        d.fromTermination.via = how == QLatin1String("drain") ? QStringLiteral("direct") : QStringLiteral("wire");
        if (how == QLatin1String("hook") && !wirePart.isEmpty())
            if (const LibraryPart* part = harnessBuilderLibrary().byId(wirePart)) {
                d.libPart = wirePart;
                applyHarnessWirePart(d, *part, 0);
            }
        // ONTO ANY END PART: a pin of any connector (a ferrule, a terminal, ...), a connector's own screen - which it is
        // given, where it had none - or another screen.
        if (target.startsWith(QLatin1String("pin:"))) {
            d.toConn = target.section(QLatin1Char(':'), 1, 1);
            d.toPin = target.section(QLatin1Char(':'), 2, 2).toInt();
            made = harnessConnById(def_, d.toConn) && d.toPin > 0;
        } else if (target.startsWith(QLatin1String("shield:"))) {
            HarnessConnector* c = nullptr;
            for (HarnessConnector& x : def_.connectors)
                if (x.id == target.mid(7)) c = &x;
            made = c && (c->side==HarnessSide::Left?c->shieldLeft:c->shieldRight);
            if (made) {
                d.toConn = harnessOverallShieldKey(c->id, c->side);
                d.toPin = 0;
            }
        } else if (target.startsWith(QLatin1String("screen:"))) {
            d.toConn = harnessInternalShieldKey(target.mid(7));
            d.toPin = 0;
        } else {
            made = false;
        }
        if (made) def_.wires.push_back(d);
    }
    changed();
    return made;
}

void WiringDialog::setScreenPart(const QString& group, const QString& libPartId, const QString& typedPn) {
    def_.shieldParts.remove(group);
    def_.shieldPns.remove(group);
    if (!libPartId.isEmpty()) def_.shieldParts.insert(group, libPartId);
    else if (!typedPn.trimmed().isEmpty()) def_.shieldPns.insert(group, typedPn.trimmed());
    changed();
}

void WiringDialog::fillScreenBox() {
    if (!screenBox_) return;
    const QString g = screenOfSelection();
    screenBox_->setVisible(!g.isEmpty());
    if (g.isEmpty()) return;
    screenBox_->setTitle(QStringLiteral("Shield %1").arg(g));
    screenPart_->clear();
    screenPart_->addItem(QString(), QString());
    for (const LibraryPart& p : harnessBuilderLibrary().parts)
        if (p.kind == PartKind::ShieldBraid) screenPart_->addItem(p.title(), p.id);
    const int at = screenPart_->findData(def_.shieldParts.value(g));
    if (!def_.shieldParts.value(g).isEmpty() && at >= 0) screenPart_->setCurrentIndex(at);
    else screenPart_->setEditText(def_.shieldPns.value(g));
    for (int end = 0; end < 2; ++end) {
        QString target, wirePart;
        const QString how = screenDrain(g, end, &target, &wirePart);
        QComboBox* c = screenEnd_[end];
        c->clear();
        c->addItem(QStringLiteral("Not connected"), QString());
        c->addItem(QStringLiteral("Drain wire"), QStringLiteral("drain"));
        c->addItem(QStringLiteral("Hook-up wire (joint)"), QStringLiteral("hook"));
        c->setCurrentIndex(std::max(0, c->findData(how)));
        // WHERE TO: any connector's pin - its own end's connector first - any connector's screen, any other screen.
        QComboBox* to = screenPin_[end];
        to->clear();
        to->addItem(QStringLiteral("Not connected"),QString());
        QStringList order;
        const QString own = screenEndConn(g, end);
        if (!own.isEmpty()) order << own;
        for (const HarnessConnector& x : def_.connectors)
            if (!order.contains(x.id)) order << x.id;
        for (const QString& id : order)
            if (const HarnessConnector* x = harnessConnById(def_, id))
                for (int n : harnessPinSequence(*x))
                    to->addItem(QStringLiteral("%1 pin %2").arg(x->designator, harnessPinLabel(*x, n)), QStringLiteral("pin:%1:%2").arg(x->id).arg(n));
        for (const QString& id : order)
            if (const HarnessConnector* x = harnessConnById(def_, id); x && (x->side==HarnessSide::Left?x->shieldLeft:x->shieldRight))
                to->addItem(QStringLiteral("%1 shield").arg(x->designator), QStringLiteral("shield:") + x->id);
        QStringList others;
        for (const HarnessWire& w : def_.wires)
            if (!w.shieldGroup.isEmpty() && w.shieldGroup != g && !others.contains(w.shieldGroup)) others << w.shieldGroup;
        for (const QString& o : others) to->addItem(QStringLiteral("Shield %1").arg(o), QStringLiteral("screen:") + o);
        to->setCurrentIndex(std::max(0, to->findData(target)));
        to->setEnabled(!how.isEmpty() && how!=QLatin1String("terminated"));
        // THE HOOK-UP WIRE'S PART, from the library: only for a hook-up wire.
        QComboBox* wire = screenWire_[end];
        wire->clear();
        wire->addItem(QStringLiteral("(wire, no P/N)"), QString());
        for (const LibraryPart& p : harnessBuilderLibrary().parts)
            if (p.kind == PartKind::Wire) wire->addItem(p.title(), p.id);
        wire->setCurrentIndex(std::max(0, wire->findData(wirePart)));
        wire->setVisible(how == QLatin1String("hook"));
    }
}

QString WiringDialog::addJointOnWire(const QString& wireId, const QString& pinKeyGiven) {
    QString chosen = pinKeyGiven;
    if (chosen.isEmpty()) {
        // THE PIN ITS NEW WIRE GOES TO: any connector's, those free first.
        QStringList labels, keys;
        for (bool freeFirst : {true, false})
            for (const HarnessConnector& c : def_.connectors)
                for (int n : harnessPinSequence(c)) {
                    const bool isFree = canvas_->wiresOn(c.id, n).isEmpty();
                    if (isFree != freeFirst) continue;
                    labels << QStringLiteral("%1 pin %2%3").arg(c.designator, harnessPinLabel(c, n), isFree ? QString() : QStringLiteral(" (wired)"));
                    keys << pinKey(c.id, n);
                }
        if (keys.isEmpty()) return {};
        bool ok = false;
        const QString picked = QInputDialog::getItem(this, QStringLiteral("Joint on wire %1").arg(wireId),
                                                     QStringLiteral("The joint's new wire goes to:"), labels, 0, false, &ok);
        if (!ok) return {};
        chosen = keys.value(labels.indexOf(picked));
    }
    QString conn;
    int pin = 0;
    if (!splitPinKey(chosen, conn, pin)) return {};
    const auto* connector = harnessConnById(def_, conn);
    if (!connector || pin > connector->pinCount || !canvas_->wiresOn(conn, pin).isEmpty()) return {};
    const QString joint = harnessIsJointEndpoint(wireId) ? wireId : harnessSplitWire(def_, wireId);
    return joint.isEmpty() ? QString() : connectToWire(conn, pin, joint);
}

QString WiringDialog::splitAtJoint(const QString& wireId) {
    const QString joint = harnessSplitWire(def_, wireId);
    if (!joint.isEmpty()) changed();
    return joint;
}

QString WiringDialog::connectToWire(const QString& conn, int pin, const QString& wireId) {
    const auto* connector = harnessConnById(def_, conn);
    if (!connector || pin < 1 || pin > connector->pinCount || !canvas_->wiresOn(conn, pin).isEmpty()) return {};
    HarnessWire target;
    bool found = false;
    for (const auto& w : def_.wires) if (w.id == wireId) { target = w; found = true; break; }
    QString joint = harnessIsJointEndpoint(wireId) ? wireId : QString();
    if (joint.isEmpty()) {
        if (!found) return {};
        if (harnessIsJointEndpoint(target.fromConn)) joint = target.fromConn;
        else if (harnessIsJointEndpoint(target.toConn)) joint = target.toConn;
        else joint = harnessSplitWire(def_, wireId);
    }
    const auto node = std::find_if(def_.joints.begin(), def_.joints.end(), [&](const auto& j) {
        return j.id == harnessJointEndpointWire(joint);
    });
    if (node == def_.joints.end()) return {};
    if (!found) for (const auto& w : def_.wires)
        if (w.fromConn == joint || w.toConn == joint) { target = w; break; }
    // Keep the new pin directly below the existing wired pin. The branch runs
    // beside that conductor, then turns up into the shared joint.
    QSet<QString> visited;
    std::function<int(const QString&,int)> sourcePin = [&](const QString& key, int number) {
        if (key == conn) return number;
        if (!harnessIsJointEndpoint(key) || visited.contains(key)) return 0;
        visited.insert(key);
        for (const auto& j : def_.joints) if (j.id == harnessJointEndpointWire(key)) {
            const int first = sourcePin(j.fromConn,j.fromPin);
            return first > 0 ? first : sourcePin(j.toConn,j.toPin);
        }
        return 0;
    };
    int neighbour = sourcePin(node->fromConn,node->fromPin);
    if (!neighbour) neighbour = sourcePin(node->toConn,node->toPin);
    if (neighbour > 0 && neighbour != pin) for (auto& c : def_.connectors) if (c.id == conn) {
        auto order = harnessPinSequence(c);
        order.erase(std::remove(order.begin(), order.end(), pin), order.end());
        auto at = std::find(order.begin(), order.end(), neighbour);
        if (at != order.end()) order.insert(at + 1, pin);
        c.pinOrder = order;
    }
    HarnessWire w;
    w.id = harnessFreshWireId(def_);
    w.fromConn = conn; w.fromPin = pin; w.toConn = joint; w.toPin = 0;
    w.gauge = target.gauge; w.color = target.color;
    w.showColor = w.showGauge = w.showLabel = true;
    def_.wires.push_back(w);
    { const QSignalBlocker hold(canvas_); canvas_->setSelection({w.id}); }
    changed();
    said_->setText(QStringLiteral("Wire %1 connected to joint %2").arg(w.id, harnessJointEndpointWire(joint)));
    return w.id;
}

bool WiringDialog::moveWireWithPins(const QString& wireId, int toShownIndex) {
    const QStringList shown = canvas_->shownWires();
    if (!shown.contains(wireId)) return false;
    const QString ref = toShownIndex < shown.size() ? shown.value(toShownIndex) : QString();
    const HarnessWire* moved = nullptr;
    const HarnessWire* before = nullptr;
    for (const HarnessWire& w : def_.wires) {
        if (w.id == wireId) moved = &w;
        if (w.id == ref) before = &w;
    }
    if (!moved) return false;
    // ITS PINS FIRST: on each connector, put where the wire it now comes before has its pin.
    if (before && before != moved) {
        for (const auto& [conn, pin] : {std::pair<QString, int>{moved->fromConn, moved->fromPin}, {moved->toConn, moved->toPin}}) {
            const int refPin = before->fromConn == conn ? before->fromPin : before->toConn == conn ? before->toPin : 0;
            if (refPin <= 0 || refPin == pin) continue;
            for (HarnessConnector& c : def_.connectors) {
                if (c.id != conn) continue;
                std::vector<int> order = harnessPinSequence(c);
                const auto from = std::find(order.begin(), order.end(), pin);
                if (from == order.end()) continue;
                order.erase(from);
                const auto to = std::find(order.begin(), order.end(), refPin);
                order.insert(to, pin);
                c.pinOrder = order;
            }
        }
    }
    return moveWire(wireId, toShownIndex);
}

int WiringDialog::orderStacks(bool wiredFirst) {
    QStringList which = canvas_->selectedBricks();
    if (which.isEmpty()) which = canvas_->shown();
    int reordered = 0;
    for (HarnessConnector& c : def_.connectors) {
        if (!which.contains(c.id)) continue;
        const std::vector<int> order = harnessPinSequence(c);
        std::vector<int> wired, free;
        for (int pin : order) (canvas_->wiresOn(c.id, pin).isEmpty() ? free : wired).push_back(pin);
        std::vector<int> now = wiredFirst ? wired : free;
        const std::vector<int>& rest = wiredFirst ? free : wired;
        now.insert(now.end(), rest.begin(), rest.end());
        if (now != order) {
            c.pinOrder = now;
            ++reordered;
        }
    }
    said_->setText(QStringLiteral("%1 pin stack(s) put in order - the %2 pins at the top").arg(reordered).arg(wiredFirst ? QStringLiteral("wired")
                                                                                                                       : QStringLiteral("free")));
    if (reordered > 0) changed();
    return reordered;
}

void WiringDialog::changed() {
    if (changed_) changed_();
    refresh();
}

int WiringDialog::nextFreePinAfter(const QString& conn, int after) const {
    const HarnessConnector* c = harnessConnById(def_, conn);
    if (!c) return 0;
    const std::vector<int> order = harnessPinSequence(*c);
    auto free = [&](int pin) {
        return std::none_of(def_.wires.begin(), def_.wires.end(), [&](const HarnessWire& w) {
            return (w.fromConn == conn && w.fromPin == pin) || (w.toConn == conn && w.toPin == pin);
        });
    };
    const auto at = std::find(order.begin(), order.end(), after);
    for (auto it = at == order.end() ? order.begin() : at + 1; it != order.end(); ++it)
        if (free(*it)) return *it;
    for (int pin : order)
        if (free(pin)) return pin;
    return 0;
}

QString WiringDialog::connectPins(const QString& fromConn, int fromPin, const QString& toConn, int toPin) {
    if (!harnessConnById(def_, fromConn) || !harnessConnById(def_, toConn) || fromPin <= 0 || toPin <= 0) return {};
    if (fromConn == toConn && fromPin == toPin) return {};
    HarnessWire w;
    w.id = harnessFreshWireId(def_);
    w.fromConn = fromConn;
    w.fromPin = fromPin;
    w.toConn = toConn;
    w.toPin = toPin;
    // LIKE THE LAST ONE MADE HERE: its gauge and part, so a run of wires is wired at one go.
    if (!def_.wires.empty()) {
        const HarnessWire& last = def_.wires.back();
        if (!harnessIsShieldEndpoint(last.fromConn) && !harnessIsCoaxKind(last.kind)) {
            w.gauge = last.gauge;
            w.libPart = last.bundle.isEmpty() ? last.libPart : QString();
        }
    }
    def_.wires.push_back(w);
    // OPTIMISED AS IT IS MADE, while any of the round switches is on.
    if (optimizeOn()) optimizePinStack(false);
    canvas_->setGhost(fromConn, nextFreePinAfter(fromConn, fromPin), toConn, nextFreePinAfter(toConn, toPin));
    {
        const QSignalBlocker hold(canvas_);
        canvas_->setSelection({w.id});
    }
    changed();
    said_->setText(QStringLiteral("Wire %1 made - Enter for the next pair, or click the next pins").arg(w.id));
    return w.id;
}

bool WiringDialog::relandEnd(const QString& wireId, int end, const QString& conn, int pin) {
    for (HarnessWire& w : def_.wires)
        if (w.id == wireId) {
            (end == 0 ? w.fromConn : w.toConn) = conn;
            (end == 0 ? w.fromPin : w.toPin) = pin;
            changed();
            said_->setText(QStringLiteral("Wire %1 moved to its new pin").arg(wireId));
            return true;
        }
    return false;
}

bool WiringDialog::moveWire(const QString& wireId, int toShownIndex) {
    const QStringList shown = canvas_->shownWires();
    const int from = static_cast<int>(shown.indexOf(wireId));
    if (from < 0) return false;
    // TO BEFORE THE WIRE SHOWN THERE - where it is in the whole list - or after the last shown.
    int target = static_cast<int>(def_.wires.size());
    if (toShownIndex < shown.size()) {
        const QString before = shown.value(toShownIndex);
        for (size_t i = 0; i < def_.wires.size(); ++i)
            if (def_.wires[i].id == before) target = static_cast<int>(i);
    } else if (!shown.isEmpty()) {
        for (size_t i = 0; i < def_.wires.size(); ++i)
            if (def_.wires[i].id == shown.back()) target = static_cast<int>(i) + 1;
    }
    if (!harnessMoveWire(def_, wireId, target)) return false;
    changed();
    return true;
}

QString WiringDialog::groupWires(WiringCanvas::Tool tool, const QStringList& ids, const QString& drainConn) {
    QString name;
    if (tool == WiringCanvas::Tool::Twist) name = harnessTwistWires(def_, ids);
    else if (tool == WiringCanvas::Tool::Screen) {
        int pin = 0;
        if (harnessConnById(def_, drainConn)) pin = nextFreePinAfter(drainConn, 0);
        name = harnessScreenWires(def_, ids, drainConn, pin);
    } else if (tool == WiringCanvas::Tool::Cable) name = harnessCableWires(def_, ids);
    if (name.isEmpty()) {
        said_->setText(tool == WiringCanvas::Tool::Screen ? QStringLiteral("Draw the stroke across the lines to shield")
                                                          : QStringLiteral("Draw the stroke across two lines or more"));
        return {};
    }
    {
        const QSignalBlocker hold(canvas_);
        canvas_->setSelection(ids);
    }
    changed();
    said_->setText(QStringLiteral("%1 line(s) made %2").arg(ids.size()).arg(name));
    return name;
}

int WiringDialog::autoOneToOne() {
    // THE TWO CONNECTORS PICKED (their bricks clicked, Ctrl for the second) - or, two shown and none picked, those two: every
    // pin of one that is free wired to the next free pin of the other, as far as either has pins.
    QStringList pair = canvas_->selectedBricks();
    if (pair.size() != 2 && twoMode() && canvas_->shown().size() >= 2) pair = canvas_->shown().mid(0, 2);
    if (pair.size() != 2) {
        said_->setText(QStringLiteral("Auto 1:1: pick two connectors - click one's brick, Ctrl and click the other's"));
        return 0;
    }
    const QString a = pair.at(0), b = pair.at(1);
    int made = 0;
    for (int pa = nextFreePinAfter(a, 0), pb = nextFreePinAfter(b, 0); pa > 0 && pb > 0;
         pa = nextFreePinAfter(a, pa), pb = nextFreePinAfter(b, pb)) {
        if (!connectPins(a, pa, b, pb).isEmpty()) ++made;
        else break;
        if (made > 1000) break;
    }
    canvas_->clearGhost();
    said_->setText(QStringLiteral("%1 wire(s) made pin to pin").arg(made));
    return made;
}

void WiringDialog::setOptimize(bool avoidCrossing, bool byShield, bool byTwist) {
    optCross_->setChecked(avoidCrossing);
    optShield_->setChecked(byShield);
    optTwist_->setChecked(byTwist);
}

bool WiringDialog::optimizeOn() const {
    return (optCross_ && optCross_->isChecked()) || (optShield_ && optShield_->isChecked()) ||
           (optTwist_ && optTwist_->isChecked());
}

int WiringDialog::optimizePinStack(bool tell) {
    const bool cross = optCross_ && optCross_->isChecked();
    const bool byShield = optShield_ && optShield_->isChecked();
    const bool byTwist = optTwist_ && optTwist_->isChecked();
    if (!cross && !byShield && !byTwist) {
        if (tell) said_->setText(QStringLiteral("Switch on Avoid crossing, By shield or By twist first"));
        return 0;
    }
    // THE TWO COLUMNS, as the picture has them: in two, the first left and the second right; in several, each on its side,
    // one under another in the order shown. A pin's place down its column is its brick's place and its row in the stack.
    const QStringList shown = canvas_->shown();
    QStringList leftCol, rightCol;
    for (int i = 0; i < shown.size(); ++i) {
        const HarnessConnector* c = harnessConnById(def_, shown[i]);
        if (!c) continue;
        const bool left = twoMode() ? i == 0 : c->side == HarnessSide::Left;
        (left ? leftCol : rightCol) << c->id;
    }
    auto connOf = [this](const QString& id) -> HarnessConnector* {
        for (HarnessConnector& c : def_.connectors)
            if (c.id == id) return &c;
        return nullptr;
    };
    int edgeMoved=0;
    if(cross)for(const auto& joint:def_.joints)for(auto& c:def_.connectors) {
        const int mainPin=joint.fromConn==c.id?joint.fromPin:joint.toConn==c.id?joint.toPin:0;
        if(!mainPin)continue;
        const QString key=harnessJointEndpointKey(joint.id);
        std::vector<int> branches,wired;
        for(const auto& w:def_.wires) {
            if(w.fromConn==key && w.toConn==c.id && w.toPin!=mainPin)branches.push_back(w.toPin);
            if(w.toConn==key && w.fromConn==c.id && w.fromPin!=mainPin)branches.push_back(w.fromPin);
        }
        if(branches.size()!=1)continue;
        auto order=harnessPinSequence(c);
        for(int pin:order)if(!canvas_->wiresOn(c.id,pin).isEmpty())wired.push_back(pin);
        if(wired.size()<2)continue;
        const int shortPin=branches.front();
        const bool top=(wired[0]==mainPin && wired[1]==shortPin)||(wired[0]==shortPin && wired[1]==mainPin);
        const bool bottom=(wired[wired.size()-2]==mainPin && wired.back()==shortPin)||(wired[wired.size()-2]==shortPin && wired.back()==mainPin);
        if(!top && !bottom)continue;
        const QString mark=QStringLiteral("jointSide:")+joint.id+QStringLiteral(":")+c.id;
        if(!def_.layout.markAt.contains(mark) || def_.layout.markAt.value(mark)!=(top?0.:1.))++edgeMoved;
        def_.layout.markAt[mark]=top?0.:1.;
        auto main=std::find(order.begin(),order.end(),mainPin),branch=std::find(order.begin(),order.end(),shortPin);
        if((top && branch>main)||(!top && branch<main)){std::iter_swap(main,branch);c.pinOrder=order;++edgeMoved;}
    }
    auto placeIn = [&](const QStringList& col) {
        QHash<QString, double> place;   // "conn:pin" -> down the column
        for (int k = 0; k < col.size(); ++k)
            if (const HarnessConnector* c = harnessConnById(def_, col[k])) {
                const std::vector<int> seq = harnessPinSequence(*c);
                for (size_t r = 0; r < seq.size(); ++r) place.insert(pinKey(c->id, seq[r]), k * 10000.0 + static_cast<double>(r));
            }
        return place;
    };
    // Optimise electrical connections through shared nodes as well as direct pin-to-pin wires.
    std::vector<HarnessWire> effective=def_.wires;
    std::function<std::pair<QString,int>(const QString&,int,const QString&,QSet<QString>&)> resolve;
    resolve=[&](const QString& key,int pin,const QString& avoid,QSet<QString>& visited)->std::pair<QString,int> {
        if(!harnessIsJointEndpoint(key))return key==avoid?std::make_pair(QString(),0):std::make_pair(key,pin);
        if(visited.contains(key))return {};visited.insert(key);
        for(const auto& j:def_.joints)if(j.id==harnessJointEndpointWire(key)) {
            auto found=resolve(j.fromConn,j.fromPin,avoid,visited);
            if(!found.first.isEmpty())return found;
            return resolve(j.toConn,j.toPin,avoid,visited);
        }
        return {};
    };
    for(auto& w:effective) {
        QSet<QString> visited;
        if(harnessIsJointEndpoint(w.fromConn)) {const auto end=resolve(w.fromConn,0,w.toConn,visited);w.fromConn=end.first;w.fromPin=end.second;}
        visited.clear();
        if(harnessIsJointEndpoint(w.toConn)) {const auto end=resolve(w.toConn,0,w.fromConn,visited);w.toConn=end.first;w.toPin=end.second;}
    }
    // The wires drawn here, by the pins they are on.
    QHash<QString, QList<const HarnessWire*>> onPin;
    for (const HarnessWire& w : effective) {
        if (!shown.contains(w.fromConn) || !shown.contains(w.toConn) || w.fromPin <= 0 || w.toPin <= 0) continue;
        onPin[pinKey(w.fromConn, w.fromPin)] << &w;
        onPin[pinKey(w.toConn, w.toPin)] << &w;
    }
    using Key = std::tuple<double, double, double>;
    // A CONNECTOR'S WIRED PINS PUT IN ORDER: by `base` - and, as asked, a screen's or a pair's pins side by side, where the
    // first of them is. Every other pin stays where it is.
    auto reorder = [&](HarnessConnector& c, const std::function<double(int pin)>& base) {
        std::vector<int> order = harnessPinSequence(c);
        QHash<QString, double> shieldAt, twistAt;
        QList<int> wired;
        for (int pin : order)
            if (onPin.contains(pinKey(c.id, pin))) wired << pin;
        for (int pin : wired)
            for (const HarnessWire* w : onPin.value(pinKey(c.id, pin))) {
                const double b = base(pin);
                if (!w->shieldGroup.isEmpty()) shieldAt[w->shieldGroup] = shieldAt.contains(w->shieldGroup) ? std::min(shieldAt[w->shieldGroup], b) : b;
                if (!w->twistGroup.isEmpty()) twistAt[w->twistGroup] = twistAt.contains(w->twistGroup) ? std::min(twistAt[w->twistGroup], b) : b;
            }
        QHash<int, Key> key;
        for (int pin : wired) {
            const double b = base(pin);
            double t = b, s = b;
            for (const HarnessWire* w : onPin.value(pinKey(c.id, pin))) {
                if (byTwist && !w->twistGroup.isEmpty()) t = std::min(t, twistAt.value(w->twistGroup));
            }
            s = t;
            for (const HarnessWire* w : onPin.value(pinKey(c.id, pin)))
                if (byShield && !w->shieldGroup.isEmpty()) s = std::min(s, shieldAt.value(w->shieldGroup));
            key.insert(pin, Key{s, t, b});
        }
        std::vector<size_t> places;
        std::vector<int> pins;
        for (size_t i = 0; i < order.size(); ++i)
            if (key.contains(order[i])) {
                places.push_back(i);
                pins.push_back(order[i]);
            }
        std::stable_sort(pins.begin(), pins.end(), [&key](int x, int y) { return key.value(x) < key.value(y); });
        bool moved = false;
        for (size_t k = 0; k < places.size(); ++k) {
            moved = moved || order[places[k]] != pins[k];
            order[places[k]] = pins[k];
        }
        if (moved) c.pinOrder = order;
        return moved;
    };
    int reordered = edgeMoved;
    // THE LEFT COLUMN: its screened and twisted wires brought side by side - and, no line to cross another, each connector's
    // pins in the order of the right-hand connectors their wires go to, the one higher up first (its own order between).
    QHash<QString, int> rightBrick;
    for (int k = 0; k < rightCol.size(); ++k) rightBrick.insert(rightCol[k], k);
    if (byShield || byTwist || cross)
        for (const QString& id : leftCol)
            if (HarnessConnector* c = connOf(id)) {
                const QHash<QString, double> own = placeIn({id});
                reordered += reorder(*c, [&](int pin) {
                                 const double mine = own.value(pinKey(id, pin));
                                 if (!cross) return mine;
                                 int to = 1000;
                                 for (const HarnessWire* w : onPin.value(pinKey(id, pin))) {
                                     const QString other = w->fromConn == id && w->fromPin == pin ? w->toConn : w->fromConn;
                                     if (rightBrick.contains(other)) to = std::min(to, rightBrick.value(other));
                                 }
                                 return to * 10000.0 + mine;
                             })
                                 ? 1
                                 : 0;
            }
    const QHash<QString, double> leftAt = placeIn(leftCol);
    // Lower connectors receive the innermost loop first; across-column arrivals
    // come below those loops. On the upper connector the loop order is reversed.
    for(auto item=rightCol.rbegin();item!=rightCol.rend();++item)
        if(auto* c=connOf(*item)) {
            const QString id=*item;
            const auto own=placeIn({id});
            const auto rightAt=placeIn(rightCol);
            reordered+=reorder(*c,[&](int pin) {
                const double mine=own.value(pinKey(id,pin));
                if(!cross)return mine;
                double best=1e15;
                for(const auto* w:onPin.value(pinKey(id,pin))) {
                    const QString otherConn=w->fromConn==id && w->fromPin==pin?w->toConn:w->fromConn;
                    const int otherPin=w->fromConn==id && w->fromPin==pin?w->toPin:w->fromPin;
                    const QString other=pinKey(otherConn,otherPin);
                    if(leftAt.contains(other))best=std::min(best,1e9+leftAt.value(other));
                    else if(rightAt.contains(other)) {
                        const bool above=rightCol.indexOf(otherConn)<rightCol.indexOf(id);
                        best=std::min(best,(above?0.:2e9)-rightAt.value(other));
                    }
                }
                return best<1e15?best+mine*1e-6:3e9+mine;
            })?1:0;
        }
    if(cross && !def_.joints.empty()) {
        const auto left=placeIn(leftCol),right=placeIn(rightCol);
        auto position=[&](const QString& id,int pin,double candidate,const QString& changing) {
            const auto& column=left.contains(pinKey(id,pin))?left:right;
            return column.value(pinKey(id,pin),1e12)+(id==changing?candidate:def_.layout.markAt.value(QStringLiteral("connectorOffset:")+id));
        };
        std::vector<HarnessJoint> links=def_.joints;
        for(const auto& w:def_.wires)if(harnessConnById(def_,w.fromConn) && harnessConnById(def_,w.toConn))
            links.push_back({w.id,w.fromConn,w.toConn,w.fromPin,w.toPin});
        for(const auto& id:rightCol) {
            double best=-1e20,chosen=0;
            for(double shift:{-1.,0.,1.}) {
                const int index=static_cast<int>(rightCol.indexOf(id));
                if(index>0 && shift-def_.layout.markAt.value(QStringLiteral("connectorOffset:")+rightCol[index-1]) < -1)continue;
                if(index+1<rightCol.size() && def_.layout.markAt.value(QStringLiteral("connectorOffset:")+rightCol[index+1])-shift < -1)continue;
                double score=-std::abs(shift)*.001;
                for(const auto& link:links) {
                    if(link.fromConn!=id && link.toConn!=id)continue;
                    if(std::abs(position(link.fromConn,link.fromPin,shift,id)-position(link.toConn,link.toPin,shift,id))<.01)
                        score+=std::any_of(def_.joints.begin(),def_.joints.end(),[&](const auto& j){return j.id==link.id;})?3:1;
                }
                if(score>best){best=score;chosen=shift;}
            }
            const QString key=QStringLiteral("connectorOffset:")+id;
            if(def_.layout.markAt.value(key)!=chosen){def_.layout.markAt[key]=chosen;++reordered;}
        }
    }
    // THE LINES' BARS in the same order, among the places they hold in the list: by where they leave the left column, then
    // where they reach the right; those between two connectors on one side after them.
    bool listMoved = false;
    if (cross) {
        const QHash<QString, double> rightAt = placeIn(rightCol);
        std::vector<size_t> places;
        std::vector<std::pair<std::pair<double, double>, size_t>> keyed;
        for (size_t k = 0; k < def_.wires.size(); ++k) {
            const HarnessWire& w = effective[k];
            if (!shown.contains(w.fromConn) || !shown.contains(w.toConn) || w.fromPin <= 0 || w.toPin <= 0) continue;
            const QString a = pinKey(w.fromConn, w.fromPin), b = pinKey(w.toConn, w.toPin);
            const double l = leftAt.contains(a) ? leftAt.value(a) : leftAt.contains(b) ? leftAt.value(b) : 1e9;
            const double r = rightAt.contains(a) ? rightAt.value(a) : rightAt.contains(b) ? rightAt.value(b) : 1e9;
            // On one side only: its top pin decides, after every wire across.
            const double first = l < 1e9 && r < 1e9 ? l : 1e10 + std::min(l < 1e9 ? l : r, r < 1e9 ? r : l);
            places.push_back(k);
            keyed.push_back({{first, r}, k});
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        std::vector<HarnessWire> moved;
        for (const auto& kv : keyed) moved.push_back(def_.wires[kv.second]);
        for (size_t k = 0; k < places.size(); ++k) {
            listMoved = listMoved || def_.wires[places[k]].id != moved[k].id;
            def_.wires[places[k]] = moved[k];
        }
    }
    if (tell) {
        said_->setText(reordered > 0 || listMoved ? QStringLiteral("Routing optimised - pin order, joint branches and connector alignment updated")
                                                  : QStringLiteral("The pin stacks are already as the switches ask"));
        if (reordered > 0 || listMoved) changed();
    }
    return reordered;
}

bool WiringDialog::movePinInStack(const QString& conn, int pin, int toPin) {
    HarnessConnector* c = nullptr;
    for (HarnessConnector& x : def_.connectors)
        if (x.id == conn) c = &x;
    if (!c || pin == toPin) return false;
    std::vector<int> order = harnessPinSequence(*c);
    const auto from = std::find(order.begin(), order.end(), pin);
    const auto to = std::find(order.begin(), order.end(), toPin);
    if (from == order.end() || to == order.end()) return false;
    const long long at = to - order.begin();
    order.erase(from);
    order.insert(order.begin() + at, pin);
    c->pinOrder = order;
    changed();
    said_->setText(QStringLiteral("%1 pin %2 put where pin %3 was in the pin stack").arg(c->designator).arg(pin).arg(toPin));
    return true;
}

bool WiringDialog::acceptGhost() {
    QString fc, tc;
    int fp = 0, tp = 0;
    if (!canvas_->ghost(fc, fp, tc, tp)) return false;
    return !connectPins(fc, fp, tc, tp).isEmpty();
}

int WiringDialog::placeCable(const QString& libPartId) {
    const LibraryPart* part = harnessBuilderLibrary().byId(libPartId);
    if (!part || canvas_->shown().size() < 2) {
        said_->setText(QStringLiteral("Show the two connectors the cable runs between"));
        return 0;
    }
    const QString a = canvas_->shown().at(0), b = canvas_->shown().at(1);
    QString bundle;
    for (int n = 1;; ++n) {
        bundle = QStringLiteral("CABLE%1").arg(n);
        if (std::none_of(def_.wires.begin(), def_.wires.end(), [&](const HarnessWire& w) { return w.bundle == bundle; })) break;
    }
    const std::vector<CableConductor> cores = cableConductors(cableSpecOf(*part), bundle);
    int made = 0, pa = 0, pb = 0;
    for (size_t i = 0; i < cores.size(); ++i) {
        pa = nextFreePinAfter(a, pa);
        pb = nextFreePinAfter(b, pb);
        HarnessWire w;
        w.id = harnessFreshWireId(def_);
        // OUT OF PINS: its end left open, to be landed by hand.
        w.fromConn = pa > 0 ? a : harnessOpenEndpointKey();
        w.fromPin = std::max(1, pa);
        w.toConn = pb > 0 ? b : harnessOpenEndpointKey();
        w.toPin = std::max(1, pb);
        w.libPart = part->id;
        applyHarnessWirePart(w, *part, static_cast<int>(i));
        w.bundle = bundle;
        def_.wires.push_back(w);
        ++made;
    }
    if (optimizeOn()) optimizePinStack(false);
    changed();
    said_->setText(QStringLiteral("%1: %2 conductor(s) placed").arg(part->title()).arg(made));
    return made;
}

void WiringDialog::deleteSelected() {
    const QStringList ids = canvas_->selection();
    if (ids.isEmpty()) return;
    def_.wires.erase(std::remove_if(def_.wires.begin(), def_.wires.end(), [&ids](const HarnessWire& w) { return ids.contains(w.id); }), def_.wires.end());
    {
        const QSignalBlocker hold(canvas_);
        canvas_->setSelection({});
    }
    changed();
    said_->setText(QStringLiteral("%1 line(s) deleted").arg(ids.size()));
}

void WiringDialog::applyToSelection(const std::function<void(HarnessWire&)>& edit) {
    const QStringList ids = canvas_->selection();
    if (ids.isEmpty()) return;
    for (HarnessWire& w : def_.wires)
        if (ids.contains(w.id)) edit(w);
    changed();
}

void WiringDialog::setSelectedPart(const QString& libPartId) {
    const LibraryPart* part = libPartId.isEmpty() ? nullptr : harnessBuilderLibrary().byId(libPartId);
    // ONE CABLE GIVEN TO THE LINES PICKED: they are made its conductors - one cable - and checked against what it is. A wire
    // of a cable already made: the cable's P/N, given to every wire of it.
    if (part && part->kind == PartKind::Cable) {
        QStringList ids = canvas_->selection();
        QSet<QString> bundles;
        for (const HarnessWire& w : def_.wires)
            if (ids.contains(w.id)) bundles.insert(w.bundle);
        if (ids.size() > 1 && (bundles.size() != 1 || bundles.contains(QString()))) {
            harnessCableWires(def_, ids);
        } else if (bundles.size() == 1 && !bundles.contains(QString())) {
            for (const HarnessWire& w : def_.wires)
                if (w.bundle == *bundles.begin() && !ids.contains(w.id)) ids << w.id;
            const QSignalBlocker hold(canvas_);
            canvas_->setSelection(ids);
        }
    }
    int conductor = 0;
    applyToSelection([&](HarnessWire& w) {
        w.libPart = libPartId;
        w.partNumber.clear();
        if (part) applyHarnessWirePart(w, *part, part->kind == PartKind::Cable ? conductor++ : 0);
    });
}

void WiringDialog::setSelectedColor(const QString& code) {
    applyToSelection([&](HarnessWire& w) { w.color = code; });
}

void WiringDialog::setSelectedGauge(const QString& mm2) {
    applyToSelection([&](HarnessWire& w) { w.gauge = mm2; });
}

void WiringDialog::setSelectedLabel(const QString& label) {
    applyToSelection([&](HarnessWire& w) { w.label = label; });
}

void WiringDialog::setSelectedCutLength(double mm) {
    applyToSelection([&](HarnessWire& w) { w.cutLengthMm = mm; });
}

namespace {
double wireCma(const HarnessWire& wire, QString& source) {
    if(const auto* part=harnessBuilderLibrary().byId(wire.libPart);part && part->kind==PartKind::Wire) {
        const QString strands=part->value(QStringLiteral("strands")).trimmed();
        const QRegularExpression expression(QStringLiteral("^\\s*(\\d+)\\s*[xX×/]\\s*([0-9]+(?:[.,][0-9]+)?)\\s*(AWG|mm|mil|mils)?\\s*$"),QRegularExpression::CaseInsensitiveOption);
        const auto match=expression.match(strands);
        if(match.hasMatch()) {
            const int count=match.captured(1).toInt();
            const double size=QString(match.captured(2)).replace(QLatin1Char(','),QLatin1Char('.')).toDouble();
            const QString unit=match.captured(3).toLower();
            const double each=unit==QLatin1String("mm") || (unit.isEmpty() && size<1)?cmaOf(size,CmaUnit::DiameterMm):
                unit.startsWith(QLatin1String("mil"))?size*size:cmaOf(size,CmaUnit::Awg);
            if(count>0 && each>0){source=QStringLiteral("Library strand definition: %1").arg(strands);return count*each;}
        }
        bool ok=false;const double gauge=part->value(QStringLiteral("gauge")).toDouble(&ok);
        if(ok) {
            const bool mm2=part->value(QStringLiteral("gaugeMode")).compare(QLatin1String("mm2"),Qt::CaseInsensitive)==0;
            const double area=cmaOf(gauge,mm2?CmaUnit::Mm2:CmaUnit::Awg);
            if(area>0){source=QStringLiteral("Library nominal cross section (strand definition unavailable)");return area;}
        }
    }
    source=QStringLiteral("General nominal cross section: %1 mm2").arg(wire.gauge);
    bool ok=false;const double area=wire.gauge.toDouble(&ok);
    return ok && area>0?cmaFromMm2(area):0;
}
}

void WiringDialog::fillPanel() {
    filling_ = true;
    for(int i=0;i<3;++i)if(auto* box=findChild<QCheckBox*>(QStringLiteral("wiringPrintAll%1").arg(i))) {
        int count=0;
        for(const auto& wire:def_.wires)count+=(i==0?wire.showColor:i==1?wire.showGauge:wire.showLabel)?1:0;
        box->setChecked(!def_.wires.empty() && count==static_cast<int>(def_.wires.size()));
        box->setToolTip(count>0 && count<static_cast<int>(def_.wires.size())?QStringLiteral("Mixed settings. Click to print this on all wires."):QStringLiteral("Apply to every wire"));
        box->setEnabled(!def_.wires.empty());
    }
    const QStringList ids = canvas_->selection();
    const HarnessWire* first = nullptr;
    for (const HarnessWire& w : def_.wires)
        if (ids.contains(w.id)) {
            first = &w;
            break;
        }
    panel_->setEnabled(true);
    for (auto* field : QList<QWidget*>{part_,base_,stripe_,gauge_,label_,cut_,print_[0],print_[1],print_[2]})
        if(field)field->setEnabled(first != nullptr);
    if (auto* remove = findChild<QPushButton*>(QStringLiteral("wiringDelete"))) remove->setEnabled(first != nullptr);
    panelTitle_->setText(!first ? QStringLiteral("<i>Pick lines to give them a part, a colour, a gauge, a label - click them, or draw a stroke "
                                                 "across them with Select.</i>")
                                : ids.size() == 1 ? QStringLiteral("<b>Wire %1</b>").arg(first->id)
                                                  : QStringLiteral("<b>%1 lines picked</b> - a change here goes to all of them").arg(ids.size()));
    part_->clear();
    part_->addItem(QStringLiteral("—"), QString());
    for (const LibraryPart& p : harnessBuilderLibrary().parts)
        if (p.kind == PartKind::Wire || p.kind == PartKind::Cable)
            part_->addItem(QStringLiteral("%1 · %2").arg(partKindName(p.kind), p.title()), p.id);
    fillScreenBox();
    double totalCma=0;int sized=0;QStringList sources;
    for(const auto& wire:def_.wires)if(ids.contains(wire.id)) {
        QString source;const double area=wireCma(wire,source);
        if(area>0){totalCma+=area;++sized;}
        sources<<QStringLiteral("%1: %2 CMA - %3").arg(wire.id).arg(area,0,'f',1).arg(source);
    }
    cma_->setText(sized?QStringLiteral("%1%2").arg(totalCma,0,'f',1).arg(ids.size()>1?QStringLiteral(" total (%1 wires)").arg(sized):QString()):ids.isEmpty()?QStringLiteral("Select wires"):QStringLiteral("Cross section not set"));
    cma_->setToolTip(sources.join(QLatin1Char('\n')));
    static_cast<CanvasCorner*>(cma_->parentWidget())->place();
    if (first) {
        part_->setCurrentIndex(std::max(0, part_->findData(first->libPart)));
        const QString b = first->color.section(QLatin1Char('-'), 0, 0), s = first->color.section(QLatin1Char('-'), 1, 1);
        if (base_->findData(b) < 0) base_->addItem(b, b);
        base_->setCurrentIndex(base_->findData(b));
        stripe_->setCurrentIndex(std::max(0, stripe_->findData(s)));
        const int g = gauge_->findData(first->gauge);
        if (g >= 0) gauge_->setCurrentIndex(g);
        label_->setText(first->label);

        print_[0]->setChecked(first->showColor);
        print_[1]->setChecked(first->showGauge);
        print_[2]->setChecked(first->showLabel);
    }
    filling_ = false;
}

// WHAT IS WRONG, said under the picture one a line, in the order of the wires it is about: a pin with two wires, a pair
// on pins apart, a screen with nothing to drain it, a cable given to wires it is not.
QStringList WiringDialog::problems() const {
    std::vector<std::pair<int, QString>> found;
    auto rowOf = [this](const QString& wireId) {
        for (size_t i = 0; i < def_.wires.size(); ++i)
            if (def_.wires[i].id == wireId) return static_cast<int>(i);
        return static_cast<int>(def_.wires.size());
    };
    auto rowOfGroup = [this](const std::function<bool(const HarnessWire&)>& in) {
        for (size_t i = 0; i < def_.wires.size(); ++i)
            if (in(def_.wires[i])) return static_cast<int>(i);
        return static_cast<int>(def_.wires.size());
    };
    // A SHIELD IS NO GROUP: its wires on pins apart is not said here.
    for (const HarnessIssue& issue : validateHarness(def_))
        if (issue.key.startsWith(QLatin1String("twistsplit:")) ||
            issue.key.startsWith(QLatin1String("twistalone:")) || issue.key.startsWith(QLatin1String("dup")) ||
            issue.key.startsWith(QLatin1String("badjoint:")))
            found.push_back({rowOf(issue.wireId), issue.message});
    QSet<QString> pins;
    for (size_t i = 0; i < def_.wires.size(); ++i) {
        const HarnessWire& w = def_.wires[i];
        for (const auto& [conn, pin] : {std::pair<QString, int>{w.fromConn, w.fromPin}, {w.toConn, w.toPin}}) {
            if (!harnessConnById(def_, conn) || pin <= 0) continue;
            const QString key = pinKey(conn, pin);
            if (pins.contains(key)) {
                const HarnessConnector* c = harnessConnById(def_, conn);
                found.push_back({static_cast<int>(i), QStringLiteral("%1 pin %2 has two wires").arg(c->designator, harnessPinLabel(*c, pin))});
            }
            pins.insert(key);
        }
    }
    QStringList screens;
    for (const HarnessWire& w : def_.wires)
        if (!w.shieldGroup.isEmpty() && !screens.contains(w.shieldGroup)) screens << w.shieldGroup;
    for (const QString& s : screens)
        if (std::none_of(def_.wires.begin(), def_.wires.end(), [&](const HarnessWire& w) {
                return w.fromConn == harnessInternalShieldKey(s) || w.toConn == harnessInternalShieldKey(s);
            }))
            found.push_back({rowOfGroup([&s](const HarnessWire& w) { return w.shieldGroup == s; }),
                             QStringLiteral("Shield %1 has no drain wire").arg(s)});
    // A CABLE GIVEN TO A GROUP: checked against what it is - as many conductors, each of the gauge and colour of its wire,
    // a screen where the wires are screened, twisted where they are twisted.
    QStringList cablesSeen;
    for (const HarnessWire& w : def_.wires) {
        const LibraryPart* part = w.libPart.isEmpty() ? nullptr : harnessBuilderLibrary().byId(w.libPart);
        if (!part || part->kind != PartKind::Cable) continue;
        const QString key = w.libPart + QLatin1Char('|') + w.bundle;
        if (cablesSeen.contains(key)) continue;
        cablesSeen << key;
        const CableSpec spec = cableSpecOf(*part);
        if (spec.isEmpty()) continue;
        std::vector<CableConductor> cores;
        for (const CableConductor& c : cableConductors(spec, QString()))
            if (!c.isScreen) cores.push_back(c);
        std::vector<const HarnessWire*> mine;
        for (const HarnessWire& o : def_.wires)
            if (o.libPart == w.libPart && o.bundle == w.bundle && !harnessIsDrainRow(o) && !harnessIsShieldEndpoint(o.fromConn) &&
                !harnessIsShieldEndpoint(o.toConn))
                mine.push_back(&o);
        const QString name = part->mfrPn.isEmpty() ? part->title() : part->mfrPn;
        const int row = rowOf(w.id);
        if (mine.size() > cores.size())
            found.push_back({row, QStringLiteral("%1 has %2 conductor(s) - %3 wires are given it").arg(name).arg(cores.size()).arg(mine.size())});
        bool screened = false, cableScreened = spec.overallFoil || spec.overallBraid;
        for (const CableConductor& c : cores) cableScreened = cableScreened || !c.shieldGroup.isEmpty();
        for (size_t k = 0; k < mine.size() && k < cores.size(); ++k) {
            const HarnessWire& o = *mine[k];
            const CableConductor& c = cores[k];
            screened = screened || !o.shieldGroup.isEmpty();
            if (!c.gauge.isEmpty() && wireGaugeLabel(c.gauge, true) != wireGaugeLabel(o.gauge, true))
                found.push_back({rowOf(o.id), QStringLiteral("Wire %1 is %2 - %3's conductor %4 is %5")
                                                  .arg(o.id, wireGaugeLabel(o.gauge, true), name).arg(k + 1).arg(wireGaugeLabel(c.gauge, true))});
            if (!c.colour.isEmpty() && o.color != c.colour)
                found.push_back({rowOf(o.id), QStringLiteral("Wire %1 is %2 - %3's conductor %4 is %5").arg(o.id, o.color, name).arg(k + 1).arg(c.colour)});
            if (!o.twistGroup.isEmpty() && c.twistGroup.isEmpty())
                found.push_back({rowOf(o.id), QStringLiteral("Wire %1 is twisted - %2's conductor %3 is not").arg(o.id, name).arg(k + 1)});
        }
        if (screened && !cableScreened)
            found.push_back({row, QStringLiteral("%1 has no shield - its wires are shielded").arg(name)});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QStringList out;
    for (const auto& [row, text] : found)
        if (!out.contains(text)) out << text;
    return out;
}

void WiringDialog::sayProblems() {
    const QStringList found = problems();
    QStringList lines;
    for (const QString& f : found) lines << QStringLiteral("⚠ ") + f;
    problems_->setText(lines.join(QLatin1Char('\n')));
}

} // namespace Loopwire
