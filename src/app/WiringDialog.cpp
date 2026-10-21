#include "app/WiringDialog.h"

#include "app/HarnessBuilderDialog.h"
#include "harness/CableSpec.h"
#include "harness/Harness.h"
#include "harness/Harness3D.h"
#include "harness/PartsLibrary.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGroupBox>
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
constexpr double kRow = 24.0;       // a wire's bar, one under another
constexpr double kMargin = 18.0;

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
        if (shown_.contains(w.fromConn) && shown_.contains(w.toConn) && w.fromPin > 0 && w.toPin > 0) out << w.id;
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
    return {};
}

void WiringCanvas::relayout() {
    bricks_.clear();
    bars_.clear();
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
        if (placed == n) {
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
        for (const HarnessWire& w : def_.wires) {
            auto shownHere = [&](const QString& conn) {
                return std::any_of(leftCol.begin(), leftCol.end(), [&](const Brick& b) { return b.conn == conn; }) ||
                       std::any_of(rightCol.begin(), rightCol.end(), [&](const Brick& b) { return b.conn == conn; });
            };
            if (shownHere(w.fromConn) && shownHere(w.toConn) && w.fromPin > 0 && w.toPin > 0) ids << w.id;
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
    for (const HarnessWire& w : def_.wires)
        if (wires.contains(w.id)) {
            QString text = QStringLiteral("%1  %2").arg(w.color, wireGaugeLabel(w.gauge, true));
            if (!w.label.isEmpty()) text += QStringLiteral("  ") + w.label;
            words = std::max(words, metrics.horizontalAdvance(text) + 24.0);
        }
    const double laneW = std::max(260.0, words + 140.0);
    const double whole = 2 * kMargin + leftW + rightW + 120.0 + laneW;
    const double shift = std::max(0.0, (width() / zoom_ - whole) / 2.0);
    laneLeft_ = shift + kMargin + leftW + 60.0;
    laneRight_ = laneLeft_ + laneW;
    barTop_ = kMargin + 40.0;
    auto stack = [&](std::vector<Brick>& col, double x) {
        double y = kMargin + 24.0;
        for (Brick& b : col) {
            b.rect.moveTo(x + (b.left ? leftW - b.rect.width() : 0.0), y);
            for (auto it = b.pins.begin(); it != b.pins.end(); ++it) it.value().translate(b.rect.topLeft());
            y += b.rect.height() + 28.0;
            bricks_.push_back(b);
        }
        return y;
    };
    const double leftBottom = stack(leftCol, shift + kMargin);
    const double rightBottom = stack(rightCol, laneRight_ + 60.0);
    for (int i = 0; i < wires.size(); ++i) {
        Bar bar;
        bar.wire = wires[i];
        bar.y = barTop_ + i * kRow;
        for (const HarnessWire& w : def_.wires)
            if (w.id == bar.wire) {
                bar.from = pinExit(w.fromConn, w.fromPin, bar.fromSide);
                bar.to = pinExit(w.toConn, w.toPin, bar.toSide);
            }
        bars_.push_back(bar);
    }
    content_ = QSize(static_cast<int>(whole),
                     static_cast<int>(std::max({leftBottom, rightBottom, barTop_ + wires.size() * kRow + 2 * kRow}) + kMargin));
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

QString WiringCanvas::barAt(const QPointF& p) const {
    for (const Bar& bar : bars_)
        if (std::abs(p.y() - bar.y) <= kRow / 2.0 - 1.0 && p.x() >= laneLeft_ && p.x() <= laneRight_) return bar.wire;
    return {};
}

// A BAR'S REACH ACROSS THE LANE: the whole lane for a wire from one side to the other; for one between two connectors on
// the same side, a short stub in from that side, its label on it.
constexpr double kStub = 96.0;

QStringList WiringCanvas::barsAcross(const QPointF& a, const QPointF& b) const {
    QStringList out;
    for (const Bar& bar : bars_) {
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
    return out;
}

QHash<QString, QRectF> WiringCanvas::markRects(bool screens) const {
    QHash<QString, std::pair<double, double>> span;
    for (const Bar& bar : bars_)
        for (const HarnessWire& w : def_.wires) {
            if (w.id != bar.wire) continue;
            const QString key = screens ? w.shieldGroup : w.twistGroup;
            if (key.isEmpty()) continue;
            auto it = span.find(key);
            if (it == span.end()) span.insert(key, {bar.y, bar.y});
            else it.value() = {std::min(it.value().first, bar.y), std::max(it.value().second, bar.y)};
        }
    QHash<QString, QRectF> out;
    for (auto it = span.cbegin(); it != span.cend(); ++it) {
        const double top = it.value().first, bottom = it.value().second;
        out.insert(it.key(), screens ? QRectF(laneRight_ - 46.0 - 12, top - kRow / 2 + 2, 24, bottom - top + kRow - 4)
                                     : QRectF(laneLeft_ + 46.0 - 10, top - 6, 20, bottom - top + 12));
    }
    return out;
}

std::vector<QPointF> WiringCanvas::jointPath(const HarnessWire& w) const {
    const bool fromJoint = harnessIsJointEndpoint(w.fromConn), toJoint = harnessIsJointEndpoint(w.toConn);
    if (fromJoint == toJoint) return {};
    const QString target = harnessJointEndpointWire(fromJoint ? w.fromConn : w.toConn);
    const QString conn = fromJoint ? w.toConn : w.fromConn;
    const int pin = fromJoint ? w.toPin : w.fromPin;
    if (!shown_.contains(conn)) return {};
    const Bar* onto = nullptr;
    for (const Bar& bar : bars_)
        if (bar.wire == target) onto = &bar;
    if (!onto) return {};
    int side = 0;
    const QPointF exit = pinExit(conn, pin, side);
    if (exit.isNull()) return {};
    const double x = side < 0 ? laneLeft_ + (laneRight_ - laneLeft_) * 0.22 : laneRight_ - (laneRight_ - laneLeft_) * 0.22;
    return {exit, QPointF(exit.x() + side * -14.0, exit.y()), QPointF(x, exit.y()), QPointF(x, onto->y)};
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
        const QHash<QString, QRectF> marks = markRects(screens);
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
        const QHash<QString, QRectF> marks = markRects(screens);
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
            const bool here = first && shown_.contains(first->fromConn) && shown_.contains(first->toConn);
            const bool lit = pending_ == pinKey(b.conn, it.key()) || (pressing_ && pressPin_ == pinKey(b.conn, it.key()));
            QColor fill = QColor(26, 30, 36);
            if (first && here) fill = baseColour(first->color);
            p.setPen(QPen(on.size() > 1 ? QColor(240, 80, 70) : lit ? QColor(255, 209, 102) : QColor(150, 165, 185), on.size() > 1 || lit ? 2.4 : 1.0));
            p.setBrush(fill);
            p.drawRoundedRect(r, 3, 3);
            // TERMINATED, NOTHING ON IT: its X, as the drawing marks it.
            if (on.isEmpty() && def_.layout.terminateUnconnected) {
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
                if (const QColor s = stripeColour(first->color); s.isValid()) {
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
    // THE LINES PICKED, blue; the rest of a group they are in - its cable, its screen, its pair - orange.
    QSet<QString> mates;
    for (const HarnessWire& w : def_.wires)
        if (selection_.contains(w.id))
            for (const HarnessWire& o : def_.wires)
                if ((!w.bundle.isEmpty() && o.bundle == w.bundle) || (!w.shieldGroup.isEmpty() && o.shieldGroup == w.shieldGroup) ||
                    (!w.twistGroup.isEmpty() && o.twistGroup == w.twistGroup))
                    mates.insert(o.id);
    for (const Bar& bar : bars_) {
        const HarnessWire* w = byId.value(bar.wire);
        if (!w) continue;
        const bool sel = selection_.contains(bar.wire);
        const bool sameSide = bar.fromSide == bar.toSide;
        const double edge = bar.fromSide < 0 ? laneLeft_ : laneRight_;
        // BETWEEN TWO CONNECTORS ON ONE SIDE: both pins to the lane's edge, and a short bar in from it for its label.
        const QPointF a(bar.fromSide < 0 ? laneLeft_ : laneRight_, bar.y),
            b = sameSide ? QPointF(edge - bar.fromSide * kStub, bar.y) : QPointF(bar.toSide < 0 ? laneLeft_ : laneRight_, bar.y);
        const QColor base = baseColour(w->color);
        QPainterPath path;
        path.moveTo(bar.from);
        path.lineTo(QPointF(bar.from.x() + bar.fromSide * -14.0, bar.from.y()));
        path.lineTo(a);
        path.lineTo(b);
        if (sameSide) path.moveTo(a);
        path.lineTo(QPointF(bar.to.x() + bar.toSide * -14.0, bar.to.y()));
        path.lineTo(bar.to);
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
        if (const QColor s = stripeColour(w->color); s.isValid()) {
            p.setPen(QPen(s, 1.2, Qt::DashLine));
            p.drawLine(a, b);
        }
        // WHAT IT IS, on its bar: its colour, its gauge, its label.
        QString words = QStringLiteral("%1  %2").arg(w->color, wireGaugeLabel(w->gauge, true));
        if (!w->label.isEmpty()) words += QStringLiteral("  ") + w->label;
        p.setFont(small);
        const double half = sameSide ? kStub / 2.0 - 4.0 : (laneRight_ - laneLeft_) / 2.0 - 70.0;
        const double mid = sameSide ? (a.x() + b.x()) / 2.0 : (laneLeft_ + laneRight_) / 2.0;
        const QRectF text(mid - half, bar.y - kRow / 2 + 1, 2 * half, kRow / 2 - 1);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(32, 36, 42, 200));
        // Behind the words only: the bar shows either side of them.
        const double wordsW = std::min(text.width(), QFontMetricsF(small).horizontalAdvance(words) + 12.0);
        p.drawRoundedRect(QRectF(text.center().x() - wordsW / 2.0, text.top(), wordsW, text.height()), 3, 3);
        p.setPen(QColor(220, 228, 238));
        p.drawText(text, Qt::AlignCenter, words);
        p.setFont(font());
    }
    // ---- WIRES JOINED TO ANOTHER'S RUN: from the pin to a dot on that wire's bar ----
    for (const HarnessWire& w : def_.wires) {
        const std::vector<QPointF> pts = jointPath(w);
        if (pts.size() < 2) continue;
        const double x = pts.back().x();
        const Bar onto{QString(), pts.back().y()};
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
        p.setPen(QPen(baseColour(w.color), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(235, 240, 248));
        p.drawEllipse(QPointF(x, onto.y), 4.0, 4.0);
    }
    // ---- TWISTS AND SCREENS, by their bars ----
    const double twistX = laneLeft_ + 46.0, screenX = laneRight_ - 46.0;
    for (auto it = twists.begin(); it != twists.end(); ++it) {
        const double top = it.value().first, bottom = it.value().second;
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
    for (auto it = screens.begin(); it != screens.end(); ++it) {
        const double top = it.value().first, bottom = it.value().second;
        const QRectF ring(screenX - 12, top - kRow / 2 + 2, 24, bottom - top + kRow - 4);
        p.setPen(QPen(QColor(140, 190, 255), 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring);
        p.drawText(QRectF(screenX - 40, top - kRow / 2 - 14, 80, 14), Qt::AlignCenter, it.key());
        // ITS DRAIN: dashed to its pin.
        for (const HarnessWire& w : def_.wires)
            if (w.fromConn == harnessInternalShieldKey(it.key()) || w.toConn == harnessInternalShieldKey(it.key())) {
                const bool fromScreen = w.fromConn == harnessInternalShieldKey(it.key());
                const QString conn = fromScreen ? w.toConn : w.fromConn;
                const int pin = fromScreen ? w.toPin : w.fromPin;
                const QPointF at = pinCentre(conn, pin);
                if (at.isNull()) continue;
                p.setPen(QPen(QColor(140, 190, 255), 1.4, Qt::DashLine));
                p.drawLine(QPointF(ring.center().x(), ring.bottom()), at);
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
        const QString bar = barAt(e->position() / zoom_);
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
    // A CAUGHT WIRE LET GO: moved in the list where it is let go, its pins with it.
    if (carrying_ && carryingBar_) {
        carrying_ = carryingBar_ = false;
        unsetCursor();
        const int at = std::clamp(static_cast<int>(std::lround((now_.y() - barTop_) / kRow)), 0, static_cast<int>(bars_.size()));
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
        emit jointAsked(pc, pp, barAt(now_));
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
        if (moved_) {
            const int at = std::clamp(static_cast<int>(std::lround((now_.y() - barTop_) / kRow)), 0, static_cast<int>(bars_.size()));
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
    auto* bar = new QHBoxLayout;
    const struct {
        const char* name;
        const char* text;
        const char* tip;
    } tools[5] = {{"wiringToolConnect", "Connect", "Click a pin, then the pin it goes to - or drag from one to the other. Drag a wired pin to "
                                                   "another to move its end there (Ctrl: another wire instead)"},
                  {"wiringToolSelect", "Select", "Click lines to pick them (Ctrl: more), or draw a stroke across them"},
                  {"wiringToolTwist", "Twist", "Draw a stroke across lines to twist them together"},
                  {"wiringToolScreen", "Shield", "Draw a stroke across lines to shield them - then say where the drain goes"},
                  {"wiringToolCable", "Cable", "Draw a stroke across lines to make them one cable"}};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QToolButton(this);
        b->setObjectName(QLatin1String(tools[i].name));
        b->setText(QString::fromLatin1(tools[i].text));
        b->setToolTip(QString::fromLatin1(tools[i].tip));
        b->setCheckable(true);
        // FRAMED, each a button of its own - the one in use filled.
        b->setAutoRaise(false);
        b->setStyleSheet(QStringLiteral("QToolButton{border:1px solid #6b7785;border-radius:4px;padding:3px 10px;}"
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
    bar->addStretch(1);
    lay->addLayout(bar);

    // ---- THE CANVAS, AND THE PANEL BESIDE IT ----
    auto* split = new QSplitter(Qt::Horizontal, this);
    // THE GROUPS, on the left: every pair, screen and cable - one clicked picks its wires; the lines picked light the
    // groups they make up.
    groups_ = new QListWidget(split);
    groups_->setObjectName(QStringLiteral("wiringGroups"));
    groups_->setToolTip(QStringLiteral("The twisted pairs, shields and cables - click one to pick its wires"));
    groups_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    split->addWidget(groups_);
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
    mv->addWidget(scroll_, 1);
    // UNDER THE PICTURE: what to optimise the pin stacks for, and the stacks of the connectors picked put in order.
    {
        auto* under = new QHBoxLayout;
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
        });
        under->addWidget(terminate_);
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
    auto* form = new QFormLayout;
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
    form->addRow(QStringLiteral("Gauge"), gauge_);
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
    auto* gl = new QHBoxLayout(groups);
    auto groupButton = [&](const QString& text, const char* name, WiringCanvas::Tool tool) {
        auto* b = new QPushButton(text, groups);
        b->setObjectName(QLatin1String(name));
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, [this, tool] {
            groupWires(tool, canvas_->selection(), tool == WiringCanvas::Tool::Screen && askDrain_ ? askDrain_() : QString());
        });
        gl->addWidget(b);
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
    gl->addWidget(apart);
    pl->addWidget(groups);
    // A SCREEN PICKED (every line picked in one): its own part, and how it is drained at each end.
    {
        screenBox_ = new QGroupBox(QStringLiteral("Shield"), panel_);
        screenBox_->setObjectName(QStringLiteral("wiringScreenBox"));
        auto* sf = new QFormLayout(screenBox_);
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
            hl->addWidget(screenEnd_[end], 1);
            hl->addWidget(screenPin_[end], 1);
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
    pl->addWidget(del);
    pl->addStretch(1);
    split->addWidget(panel_);
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
    connect(canvas_, &WiringCanvas::connectAsked, this, [this](const QString& fc, int fp, const QString& tc, int tp) { connectPins(fc, fp, tc, tp); });
    connect(canvas_, &WiringCanvas::relandAsked, this, [this](const QString& id, int end, const QString& conn, int pin) { relandEnd(id, end, conn, pin); });
    connect(canvas_, &WiringCanvas::pinStackMoveAsked, this,
            [this](const QString& conn, int pin, int toPin) { movePinInStack(conn, pin, toPin); });
    connect(canvas_, &WiringCanvas::wireWithPinsMoveAsked, this, [this](const QString& id, int at) { moveWireWithPins(id, at); });
    connect(canvas_, &WiringCanvas::jointAsked, this,
            [this](const QString& conn, int pin, const QString& wire) { connectToWire(conn, pin, wire); });
    connect(canvas_, &WiringCanvas::jointMenuAsked, this, [this](const QString& wire, const QPoint& at) {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Add a joint on wire %1...").arg(wire), this, [this, wire] { addJointOnWire(wire); });
        menu.exec(at);
    });
    connect(canvas_, &WiringCanvas::toolPicked, this, [this](WiringCanvas::Tool tool) {
        for (int k = 0; k < 5; ++k) {
            const QSignalBlocker hold(tools_[k]);
            tools_[k]->setChecked(k == static_cast<int>(tool));
        }
        said_->setText(QStringLiteral("Select"));
    });
    connect(canvas_, &WiringCanvas::moveAsked, this, [this](const QString& id, int at) { moveWire(id, at); });
    connect(canvas_, &WiringCanvas::strokeMade, this, [this](WiringCanvas::Tool tool, const QStringList& ids) {
        groupWires(tool, ids, tool == WiringCanvas::Tool::Screen && askDrain_ && ids.size() > 0 ? askDrain_() : QString());
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
    for (const HarnessConnector& c : def_.connectors) {
        const QString name = QStringLiteral("%1 (%2 pins)").arg(c.designator.isEmpty() ? c.id : c.designator).arg(c.pinCount);
        left_->addItem(name, c.id);
        right_->addItem(name, c.id);
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
    auto add = [this](const QString& kind, const QString& name, const std::function<bool(const HarnessWire&)>& in) {
        QStringList ids;
        for (const HarnessWire& w : def_.wires)
            if (in(w)) ids << w.id;
        if (ids.isEmpty()) return;
        auto* it = new QListWidgetItem(QStringLiteral("%1  %2 · %3 wire(s)").arg(name, kind).arg(ids.size()), groups_);
        it->setData(Qt::UserRole, ids);
        it->setToolTip(ids.join(QStringLiteral(", ")));
    };
    QStringList twists, screens, cables;
    for (const HarnessWire& w : def_.wires) {
        if (!w.twistGroup.isEmpty() && !twists.contains(w.twistGroup)) twists << w.twistGroup;
        if (!w.shieldGroup.isEmpty() && !screens.contains(w.shieldGroup)) screens << w.shieldGroup;
        if (!w.bundle.isEmpty() && !cables.contains(w.bundle)) cables << w.bundle;
    }
    for (const QString& g : cables)
        add(QStringLiteral("cable"), g, [&g](const HarnessWire& w) { return w.bundle == g; });
    for (const QString& g : screens)
        add(QStringLiteral("shielded"), g, [&g](const HarnessWire& w) { return w.shieldGroup == g; });
    for (const QString& g : twists)
        add(QStringLiteral("twisted"), g, [&g](const HarnessWire& w) { return w.twistGroup == g; });
    markGroups();
}

void WiringDialog::markGroups() {
    if (!groups_) return;
    const QSignalBlocker hush(groups_);
    const QStringList sel = canvas_->selection();
    for (int i = 0; i < groups_->count(); ++i) {
        QListWidgetItem* it = groups_->item(i);
        const QStringList ids = it->data(Qt::UserRole).toStringList();
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
    bool made = true;
    if (!how.isEmpty() && !target.isEmpty()) {
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
            made = c != nullptr;
            if (c) {
                (c->side == HarnessSide::Left ? c->shieldLeft : c->shieldRight) = true;
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
        c->addItem(QStringLiteral("Not drained"), QString());
        c->addItem(QStringLiteral("Drain wire"), QStringLiteral("drain"));
        c->addItem(QStringLiteral("Hook-up wire (joint)"), QStringLiteral("hook"));
        c->setCurrentIndex(std::max(0, c->findData(how)));
        // WHERE TO: any connector's pin - its own end's connector first - any connector's screen, any other screen.
        QComboBox* to = screenPin_[end];
        to->clear();
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
            if (const HarnessConnector* x = harnessConnById(def_, id))
                to->addItem(QStringLiteral("%1 shield").arg(x->designator), QStringLiteral("shield:") + x->id);
        QStringList others;
        for (const HarnessWire& w : def_.wires)
            if (!w.shieldGroup.isEmpty() && w.shieldGroup != g && !others.contains(w.shieldGroup)) others << w.shieldGroup;
        for (const QString& o : others) to->addItem(QStringLiteral("Shield %1").arg(o), QStringLiteral("screen:") + o);
        to->setCurrentIndex(std::max(0, to->findData(target)));
        to->setEnabled(!how.isEmpty());
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
    return connectToWire(conn, pin, wireId);
}

QString WiringDialog::connectToWire(const QString& conn, int pin, const QString& wireId) {
    if (!harnessConnById(def_, conn) || pin <= 0) return {};
    const HarnessWire* target = nullptr;
    for (const HarnessWire& w : def_.wires)
        if (w.id == wireId) target = &w;
    if (!target) return {};
    HarnessWire w;
    w.id = harnessFreshWireId(def_);
    w.fromConn = conn;
    w.fromPin = pin;
    w.toConn = harnessJointEndpointKey(wireId);
    w.toPin = 0;
    // The same conductor as the wire it joins, to begin with.
    w.gauge = target->gauge;
    w.color = target->color;
    def_.wires.push_back(w);
    {
        const QSignalBlocker hold(canvas_);
        canvas_->setSelection({w.id});
    }
    changed();
    said_->setText(QStringLiteral("Wire %1 made - from the pin to a joint on %2").arg(w.id, wireId));
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
    auto placeIn = [&](const QStringList& col) {
        QHash<QString, double> place;   // "conn:pin" -> down the column
        for (int k = 0; k < col.size(); ++k)
            if (const HarnessConnector* c = harnessConnById(def_, col[k])) {
                const std::vector<int> seq = harnessPinSequence(*c);
                for (size_t r = 0; r < seq.size(); ++r) place.insert(pinKey(c->id, seq[r]), k * 10000.0 + static_cast<double>(r));
            }
        return place;
    };
    // The wires drawn here, by the pins they are on.
    QHash<QString, QList<const HarnessWire*>> onPin;
    for (const HarnessWire& w : def_.wires) {
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
    int reordered = 0;
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
    // THE RIGHT COLUMN: each pin where the left-hand pins its wires come from are - so no line crosses another - its wires
    // that stay on this side after them, each connector's own order kept between those.
    for (const QString& id : rightCol)
        if (HarnessConnector* c = connOf(id)) {
            const QHash<QString, double> own = placeIn({id});
            reordered += reorder(*c, [&](int pin) {
                             const double mine = own.value(pinKey(id, pin));
                             if (!cross) return mine;
                             double best = 1e12;
                             for (const HarnessWire* w : onPin.value(pinKey(id, pin))) {
                                 const QString other = w->fromConn == id && w->fromPin == pin ? pinKey(w->toConn, w->toPin) : pinKey(w->fromConn, w->fromPin);
                                 if (leftAt.contains(other)) best = std::min(best, leftAt.value(other));
                             }
                             return best < 1e12 ? best + mine * 1e-6 : 1e9 + mine;
                         })
                             ? 1
                             : 0;
        }
    // THE LINES' BARS in the same order, among the places they hold in the list: by where they leave the left column, then
    // where they reach the right; those between two connectors on one side after them.
    bool listMoved = false;
    if (cross) {
        const QHash<QString, double> rightAt = placeIn(rightCol);
        std::vector<size_t> places;
        std::vector<std::pair<std::pair<double, double>, size_t>> keyed;
        for (size_t k = 0; k < def_.wires.size(); ++k) {
            const HarnessWire& w = def_.wires[k];
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
        said_->setText(reordered > 0 || listMoved ? QStringLiteral("Pin stacks optimised - %1 connector(s) reordered").arg(reordered)
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

void WiringDialog::fillPanel() {
    filling_ = true;
    const QStringList ids = canvas_->selection();
    const HarnessWire* first = nullptr;
    for (const HarnessWire& w : def_.wires)
        if (ids.contains(w.id)) {
            first = &w;
            break;
        }
    panel_->setEnabled(first != nullptr);
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
    for (const HarnessIssue& issue : validateHarness(def_))
        if (issue.key.startsWith(QLatin1String("twistsplit:")) || issue.key.startsWith(QLatin1String("shieldsplit:")) ||
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
