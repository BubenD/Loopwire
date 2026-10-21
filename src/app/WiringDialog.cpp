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
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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
    const double shift = std::max(0.0, (width() - whole) / 2.0);
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
    setMinimumSize(content_);
    updateGeometry();
    update();
}

QSize WiringCanvas::sizeHint() const { return content_; }

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

QStringList WiringCanvas::barsAcross(const QPointF& a, const QPointF& b) const {
    QStringList out;
    for (const Bar& bar : bars_) {
        if ((a.y() - bar.y) * (b.y() - bar.y) > 0.0 || std::abs(a.y() - b.y()) < 1e-9) continue;
        const double t = (bar.y - a.y()) / (b.y() - a.y());
        const double x = a.x() + (b.x() - a.x()) * t;
        if (x >= laneLeft_ - 4.0 && x <= laneRight_ + 4.0) out << bar.wire;
    }
    return out;
}

void WiringCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(32, 36, 42));
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
        p.setPen(QPen(QColor(90, 130, 180), 1.4));
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
    for (const Bar& bar : bars_) {
        const HarnessWire* w = byId.value(bar.wire);
        if (!w) continue;
        const bool sel = selection_.contains(bar.wire);
        const QPointF a(bar.fromSide < 0 ? laneLeft_ : laneRight_, bar.y), b(bar.toSide < 0 ? laneLeft_ : laneRight_, bar.y);
        const QColor base = baseColour(w->color);
        QPainterPath path;
        path.moveTo(bar.from);
        path.lineTo(QPointF(bar.from.x() + bar.fromSide * -14.0, bar.from.y()));
        path.lineTo(a);
        path.lineTo(b);
        path.lineTo(QPointF(bar.to.x() + bar.toSide * -14.0, bar.to.y()));
        path.lineTo(bar.to);
        // An open path: drawn as a line, never filled - whatever brush the words before it left set.
        p.setBrush(Qt::NoBrush);
        if (sel) {
            p.setPen(QPen(QColor(255, 170, 60), 6.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
        const double half = (laneRight_ - laneLeft_) / 2.0 - 70.0;
        const QRectF text((laneLeft_ + laneRight_) / 2.0 - half, bar.y - kRow / 2 + 1, 2 * half, kRow / 2 - 1);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(32, 36, 42, 200));
        // Behind the words only: the bar shows either side of them.
        const double wordsW = std::min(text.width(), QFontMetricsF(small).horizontalAdvance(words) + 12.0);
        p.drawRoundedRect(QRectF(text.center().x() - wordsW / 2.0, text.top(), wordsW, text.height()), 3, 3);
        p.setPen(QColor(220, 228, 238));
        p.drawText(text, Qt::AlignCenter, words);
        p.setFont(font());
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
    if (ghostOn_) {
        const QPointF a = pinCentre(ghostFrom_, ghostFromPin_), b = pinCentre(ghostTo_, ghostToPin_);
        if (!a.isNull() && !b.isNull()) {
            p.setPen(QPen(QColor(255, 209, 102, 150), 1.6, Qt::DashLine));
            p.drawLine(a, b);
            p.drawText(QRectF((a + b) / 2.0 - QPointF(100, 20), QSizeF(200, 16)), Qt::AlignCenter, QStringLiteral("Enter: wire it"));
        }
    }
    if (pressing_ && moved_) {
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
    if (e->button() != Qt::LeftButton) return;
    pressing_ = true;
    moved_ = false;
    ctrl_ = e->modifiers() & Qt::ControlModifier;
    pressAt_ = now_ = e->position();
    pressPin_ = tool_ == Tool::Connect || tool_ == Tool::Select ? pinAt(pressAt_) : QString();
    pressBar_ = pressPin_.isEmpty() && (tool_ == Tool::Connect || tool_ == Tool::Select) ? barAt(pressAt_) : QString();
    update();
}

void WiringCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (!pressing_) {
        const QString pin = pinAt(e->position());
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
    now_ = e->position();
    if ((now_ - pressAt_).manhattanLength() > 4) moved_ = true;
    update();
}

void WiringCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (!pressing_ || e->button() != Qt::LeftButton) return;
    pressing_ = false;
    now_ = e->position();
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
        // A STROKE ACROSS LINES: picked, or made twisted, screened, one cable.
        const QStringList crossed = barsAcross(pressAt_, now_);
        if (tool_ == Tool::Select) setSelection(ctrl_ ? selection_ + crossed : crossed);
        else emit strokeMade(tool_, crossed);
    } else if (!moved_) {
        pending_.clear();
        if (!ctrl_) setSelection({});
    }
    pressPin_.clear();
    pressBar_.clear();
    update();
}

void WiringCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        pending_.clear();
        setSelection({});
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
                  {"wiringToolScreen", "Screen", "Draw a stroke across lines to screen them - then say where the drain goes"},
                  {"wiringToolCable", "Cable", "Draw a stroke across lines to make them one cable"}};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QToolButton(this);
        b->setObjectName(QLatin1String(tools[i].name));
        b->setText(QString::fromLatin1(tools[i].text));
        b->setToolTip(QString::fromLatin1(tools[i].tip));
        b->setCheckable(true);
        b->setAutoRaise(true);
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
                                                                                "pins to the right's, its pairs twisted and screens screened"),
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
    scroll_ = new QScrollArea(split);
    scroll_->setWidgetResizable(true);
    canvas_ = new WiringCanvas(def_, scroll_);
    scroll_->setWidget(canvas_);
    split->addWidget(scroll_);
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
    cut_ = new QDoubleSpinBox(panel_);
    cut_->setObjectName(QStringLiteral("wiringCutLength"));
    cut_->setRange(0, 1e9);
    cut_->setSuffix(QStringLiteral(" mm"));
    cut_->setSpecialValueText(QStringLiteral("Not specified"));
    cut_->setKeyboardTracking(false);
    form->addRow(QStringLiteral("Cut length"), cut_);
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
    groupButton(QStringLiteral("Screened..."), "wiringMakeScreened", WiringCanvas::Tool::Screen);
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
    auto* del = new QPushButton(QStringLiteral("Delete the lines picked"), panel_);
    del->setObjectName(QStringLiteral("wiringDelete"));
    del->setAutoDefault(false);
    connect(del, &QPushButton::clicked, this, [this] { deleteSelected(); });
    pl->addWidget(del);
    pl->addStretch(1);
    split->addWidget(panel_);
    split->setStretchFactor(0, 1);
    split->setSizes({960, 320});
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
    connect(cut_, &QDoubleSpinBox::valueChanged, this, [this](double mm) {
        if (!filling_) setSelectedCutLength(mm);
    });

    // ---- THE CANVAS INTO THE HARNESS ----
    connect(canvas_, &WiringCanvas::connectAsked, this, [this](const QString& fc, int fp, const QString& tc, int tp) { connectPins(fc, fp, tc, tp); });
    connect(canvas_, &WiringCanvas::relandAsked, this, [this](const QString& id, int end, const QString& conn, int pin) { relandEnd(id, end, conn, pin); });
    connect(canvas_, &WiringCanvas::moveAsked, this, [this](const QString& id, int at) { moveWire(id, at); });
    connect(canvas_, &WiringCanvas::strokeMade, this, [this](WiringCanvas::Tool tool, const QStringList& ids) {
        groupWires(tool, ids, tool == WiringCanvas::Tool::Screen && askDrain_ && ids.size() > 0 ? askDrain_() : QString());
    });
    connect(canvas_, &WiringCanvas::selectionChanged, this, [this] { fillPanel(); });
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
    sayProblems();
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
        said_->setText(tool == WiringCanvas::Tool::Screen ? QStringLiteral("Draw the stroke across the lines to screen")
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
    if (!twoMode() || canvas_->shown().size() < 2) {
        said_->setText(QStringLiteral("Auto 1:1 wires two connectors - show two"));
        return 0;
    }
    const QString a = canvas_->shown().at(0), b = canvas_->shown().at(1);
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
    if (first) {
        part_->setCurrentIndex(std::max(0, part_->findData(first->libPart)));
        const QString b = first->color.section(QLatin1Char('-'), 0, 0), s = first->color.section(QLatin1Char('-'), 1, 1);
        if (base_->findData(b) < 0) base_->addItem(b, b);
        base_->setCurrentIndex(base_->findData(b));
        stripe_->setCurrentIndex(std::max(0, stripe_->findData(s)));
        const int g = gauge_->findData(first->gauge);
        if (g >= 0) gauge_->setCurrentIndex(g);
        label_->setText(first->label);
        cut_->setValue(first->cutLengthMm);
        print_[0]->setChecked(first->showColor);
        print_[1]->setChecked(first->showGauge);
        print_[2]->setChecked(first->showLabel);
    }
    filling_ = false;
}

// WHAT IS WRONG, said under the picture: a pin with two wires, a pair on pins apart, a screen with nothing to drain it.
void WiringDialog::sayProblems() {
    QStringList found;
    for (const HarnessIssue& issue : validateHarness(def_))
        if (issue.key.startsWith(QLatin1String("twistsplit:")) || issue.key.startsWith(QLatin1String("shieldsplit:")) ||
            issue.key.startsWith(QLatin1String("twistalone:")) || issue.key.startsWith(QLatin1String("dup")))
            found << issue.message;
    QSet<QString> pins;
    for (const HarnessWire& w : def_.wires)
        for (const auto& [conn, pin] : {std::pair<QString, int>{w.fromConn, w.fromPin}, {w.toConn, w.toPin}}) {
            if (!harnessConnById(def_, conn) || pin <= 0) continue;
            const QString key = pinKey(conn, pin);
            if (pins.contains(key)) {
                const HarnessConnector* c = harnessConnById(def_, conn);
                found << QStringLiteral("%1 pin %2 has two wires").arg(c->designator, harnessPinLabel(*c, pin));
            }
            pins.insert(key);
        }
    QSet<QString> screens;
    for (const HarnessWire& w : def_.wires)
        if (!w.shieldGroup.isEmpty()) screens.insert(w.shieldGroup);
    for (const QString& s : screens)
        if (std::none_of(def_.wires.begin(), def_.wires.end(), [&](const HarnessWire& w) {
                return w.fromConn == harnessInternalShieldKey(s) || w.toConn == harnessInternalShieldKey(s);
            }))
            found << QStringLiteral("Screen %1 has no drain wire").arg(s);
    found.removeDuplicates();
    problems_->setText(found.isEmpty() ? QString() : QStringLiteral("⚠ ") + found.join(QStringLiteral("   ·   ")));
}

} // namespace Loopwire
