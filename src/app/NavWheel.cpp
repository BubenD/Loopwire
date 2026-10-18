#include "app/NavWheel.h"

#include <QMouseEvent>
#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QSettings>

#include <algorithm>

#include <cmath>

namespace Loopwire {

namespace {
int& wheelSizeSetting() { static int v = QSettings().value(QStringLiteral("view3d/wheelSizePercent"), 100).toInt(); return v; }
bool& wheelHoverSetting() { static bool v = QSettings().value(QStringLiteral("view3d/wheelEnlargeOnHover"), true).toBool(); return v; }
double& wheelHoverScaleSetting() { static double v = QSettings().value(QStringLiteral("view3d/wheelHoverScale"), 1.2).toDouble(); return v; }
constexpr int wheelBaseSide() { return static_cast<int>(2.0 * 58.8) + 4; }
} // namespace

// ==================================================================================================
// The navigation wheel
// ==================================================================================================

std::vector<NavWheel::Item> NavWheel::axisItems(const std::array<std::pair<double, double>, 3>& screen,
                                                const std::function<void(int, int)>& move,
                                                const std::function<void(int, int)>& turn) {
    const int ring = 12;
    std::vector<Item> items(static_cast<size_t>(ring));
    std::vector<bool> taken(static_cast<size_t>(ring), false);
    struct Way {
        int axis, sign;
        double deg, shown;
    };
    std::vector<Way> ways;
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : {1, -1}) {
            const double sx = screen[static_cast<size_t>(axis)].first * sign, sy = screen[static_cast<size_t>(axis)].second * sign;
            ways.push_back({axis, sign, std::atan2(sy, sx) * 180.0 / M_PI, std::hypot(sx, sy)});
        }
    // Those that show longest first; one pointing straight at the eye takes what is left.
    std::sort(ways.begin(), ways.end(), [](const Way& a, const Way& b) { return a.shown > b.shown; });
    for (const Way& w : ways) {
        int best = -1;
        double nearest = 1e9;
        for (int i = 0; i < ring; ++i) {
            if (taken[static_cast<size_t>(i)]) continue;
            const double slot = 90.0 - i * 360.0 / ring;
            const double off = std::abs(std::remainder(w.deg - slot, 360.0));
            if (off < nearest) {
                nearest = off;
                best = i;
            }
        }
        taken[static_cast<size_t>(best)] = true;
        const QString key = QStringLiteral("%1%2").arg(QLatin1Char("xyz"[w.axis])).arg(w.sign > 0 ? QLatin1Char('+') : QLatin1Char('-'));
        const QString name = QStringLiteral("%1%2").arg(QChar(QLatin1Char("XYZ"[w.axis]))).arg(w.sign > 0 ? QLatin1Char('+') : QLatin1Char('-'));
        Item it{key, QStringLiteral("Move %1").arg(name), QStringLiteral("Move it a step along %1").arg(name),
                [move, axis = w.axis, sign = w.sign] { move(axis, sign); }, {}};
        it.arrowDeg = w.shown > 0.05 ? w.deg : 1e9;
        items[static_cast<size_t>(best)] = it;
    }
    int next = 0;
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : {1, -1}) {
            while (next < ring && taken[static_cast<size_t>(next)]) ++next;
            if (next >= ring) break;
            taken[static_cast<size_t>(next)] = true;
            const QString key = QStringLiteral("r%1%2").arg(QLatin1Char("xyz"[axis])).arg(sign > 0 ? QLatin1Char('+') : QLatin1Char('-'));
            items[static_cast<size_t>(next)] = {key, QStringLiteral("Turn %1").arg(QChar(QLatin1Char("XYZ"[axis]))),
                                                QStringLiteral("Turn it 15° round %1").arg(QChar(QLatin1Char("XYZ"[axis]))),
                                                [turn, axis, sign] { turn(axis, sign); }, {}};
        }
    return items;
}

NavWheel::NavWheel(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::PointingHandCursor);
    shrinkTimer_.setSingleShot(true);
    shrinkTimer_.setInterval(2000);
    connect(&shrinkTimer_, &QTimer::timeout, this, [this] {
        pointerOver_ = false;
        updateDrawingSize();
    });
    updateDrawingSize();
}

void NavWheel::setGlobalAppearance(int sizePercent, bool enlargeOnHover, double hoverScale) {
    wheelSizeSetting() = std::clamp(sizePercent, 50, 200);
    wheelHoverSetting() = enlargeOnHover;
    wheelHoverScaleSetting() = std::clamp(hoverScale, 1.05, 2.0);
    const auto widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets)
        if (auto* wheel = qobject_cast<NavWheel*>(widget)) wheel->updateDrawingSize();
}

int NavWheel::globalSizePercent() { return wheelSizeSetting(); }
bool NavWheel::globalEnlargeOnHover() { return wheelHoverSetting(); }
double NavWheel::globalHoverScale() { return wheelHoverScaleSetting(); }

double NavWheel::drawingScale() const {
    const double base = wheelSizeSetting() / 100.0;
    return base * (pointerOver_ && wheelHoverSetting() ? wheelHoverScaleSetting() : 1.0);
}

void NavWheel::updateDrawingSize() {
    const int side = qRound(wheelBaseSide() * drawingScale());
    setFixedSize(side, side);
    update();
    emit geometryChanged();
}

void NavWheel::setMiddle(const QString& label, std::function<void(const QPoint& global)> act) {
    middleLabel_ = label;
    middleAct_ = std::move(act);
    update();
}

void NavWheel::setItems(std::vector<Item> items) {
    items_ = std::move(items);
    update();
}

QSize NavWheel::sizeHint() const {
    const int side = qRound(wheelBaseSide() * drawingScale());
    return {side, side};
}

void NavWheel::resizeEvent(QResizeEvent*) {
    // ROUND TO THE POINTER as well as to the eye: the corners of its square let clicks through to the pane.
    const QPointF c = rect().center() + QPointF(0.5, 0.5);
    const double scale = drawingScale();
    const double radius = kOuter * scale;
    setMask(QRegion(QRect(static_cast<int>(c.x() - radius - 1), static_cast<int>(c.y() - radius - 1),
                          static_cast<int>(2 * radius + 2), static_cast<int>(2 * radius + 2)),
                    QRegion::Ellipse));
}

double NavWheel::angleOf(int i) const {
    const int n = std::max(1, count());
    return 90.0 - i * 360.0 / n;   // the first at the top, the rest clockwise
}

int NavWheel::segmentAt(const QPointF& p) const {
    if (items_.empty()) return -1;
    const QPointF c = QRectF(rect()).center();
    const double scale = drawingScale();
    const double dx = (p.x() - c.x()) / scale, dy = (c.y() - p.y()) / scale;
    const double r = std::hypot(dx, dy);
    if (r < kInner || r > kOuter) return -1;
    const double step = 360.0 / count();
    double a = std::atan2(dy, dx) * 180.0 / M_PI;   // counter-clockwise from three o'clock
    double k = (90.0 - a) / step;
    int i = static_cast<int>(std::lround(k)) % count();
    if (i < 0) i += count();
    return i;
}

QPointF NavWheel::segmentCentre(int i) const {
    const QPointF c = QRectF(rect()).center();
    const double a = angleOf(i) * M_PI / 180.0, r = (kOuter + kInner) / 2.0 * drawingScale();
    return {c.x() + r * std::cos(a), c.y() - r * std::sin(a)};
}

void NavWheel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double scale = drawingScale();
    p.translate(QRectF(rect()).center());
    p.scale(scale, scale);
    p.translate(-QPointF(wheelBaseSide() / 2.0, wheelBaseSide() / 2.0));
    const QPointF c(wheelBaseSide() / 2.0, wheelBaseSide() / 2.0);
    const QRectF outer(c.x() - kOuter, c.y() - kOuter, 2 * kOuter, 2 * kOuter);
    const QRectF inner(c.x() - kInner, c.y() - kInner, 2 * kInner, 2 * kInner);
    // The ring's dark rim.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 26, 34, 150));
    p.drawEllipse(outer.adjusted(-1.5, -1.5, 1.5, 1.5));
    const double step = 360.0 / std::max(1, count());
    for (int i = 0; i < count(); ++i) {
        const Item& it = items_[static_cast<size_t>(i)];
        const bool on = it.lit && it.lit();
        const double a0 = angleOf(i) - step / 2.0;
        QPainterPath seg;
        seg.arcMoveTo(outer, a0);
        seg.arcTo(outer, a0, step);
        seg.arcTo(inner, a0 + step, -step);
        seg.closeSubpath();
        const QColor fill = i == pressed_ ? QColor(43, 208, 255, 190)
                            : on          ? QColor(47, 133, 90, 200)
                            : i == hover_ ? QColor(96, 126, 160, 215)
                                          : QColor(62, 82, 106, 150);
        p.setBrush(fill);
        p.setPen(QPen(QColor(150, 172, 196, 120), 1.0));
        p.drawPath(seg);
        // ITS PICTURE, in the middle of the segment, upright.
        // The painter is already scaled; use the original wheel coordinates here.
        const double pictureAngle = angleOf(i) * M_PI / 180.0;
        const double pictureRadius = (kOuter + kInner) / 2.0;
        const QPointF m(c.x() + pictureRadius * std::cos(pictureAngle), c.y() - pictureRadius * std::sin(pictureAngle));
        p.save();
        p.translate(m);
        const QColor ink(236, 242, 248, i == hover_ || on ? 255 : 215);
        p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(ink);
        const double s = 6.0;
        auto arrow = [&](double deg) {   // a solid arrowhead with a short stem, pointing that way
            p.save();
            p.rotate(-deg);
            p.drawLine(QPointF(-s, 0.0), QPointF(s * 0.2, 0.0));
            p.drawPolygon(QPolygonF({QPointF(s, 0.0), QPointF(0.0, -s * 0.75), QPointF(0.0, s * 0.75)}));
            p.restore();
        };
        if (it.key == QLatin1String("up")) arrow(90.0);
        else if (it.key == QLatin1String("right")) arrow(0.0);
        else if (it.key == QLatin1String("down")) arrow(-90.0);
        else if (it.key == QLatin1String("left")) arrow(180.0);
        else if (it.key == QLatin1String("cw") || it.key == QLatin1String("ccw")) {
            // Most of a circle, an arrowhead at its end - clockwise or counter-clockwise.
            const bool cw = it.key == QLatin1String("cw");
            const QRectF ring(-s, -s, 2 * s, 2 * s);
            p.setBrush(Qt::NoBrush);
            p.drawArc(ring, (cw ? 120 : 60) * 16, (cw ? 270 : -270) * 16);
            const double end = (cw ? 120.0 - 270.0 : 60.0 + 270.0) * M_PI / 180.0;
            const QPointF tip(s * std::cos(end), -s * std::sin(end));
            const QPointF along = cw ? QPointF(std::sin(end), std::cos(end)) : QPointF(-std::sin(end), -std::cos(end));
            const QPointF across(along.y(), -along.x());
            p.setBrush(ink);
            p.drawPolygon(QPolygonF({tip + along * 3.5, tip - across * 3.0, tip + across * 3.0}));
        } else if (it.key == QLatin1String("sym")) {
            // Two halves mirrored about a dashed line.
            p.save();
            p.setPen(QPen(ink, 1.2, Qt::DashLine));
            p.drawLine(QPointF(0.0, -s - 1.0), QPointF(0.0, s + 1.0));
            p.restore();
            p.setPen(Qt::NoPen);
            p.drawPolygon(QPolygonF({QPointF(-1.8, -s * 0.8), QPointF(-1.8, s * 0.8), QPointF(-s - 1.0, 0.0)}));
            p.drawPolygon(QPolygonF({QPointF(1.8, -s * 0.8), QPointF(1.8, s * 0.8), QPointF(s + 1.0, 0.0)}));
        } else if (it.key == QLatin1String("plus") || it.key == QLatin1String("minus")) {
            // A lens: a circle and its handle, with a plus or a minus in it.
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(-1.5, -1.5), s * 0.75, s * 0.75);
            p.drawLine(QPointF(2.2, 2.2), QPointF(s, s));
            p.drawLine(QPointF(-4.0, -1.5), QPointF(1.0, -1.5));
            if (it.key == QLatin1String("plus")) p.drawLine(QPointF(-1.5, -4.0), QPointF(-1.5, 1.0));
        } else if (it.key == QLatin1String("fit")) {
            // Four corners of a frame.
            p.setBrush(Qt::NoBrush);
            const double k = s * 0.95, l = s * 0.5;
            for (const QPointF& sgn : {QPointF(-1, -1), QPointF(1, -1), QPointF(1, 1), QPointF(-1, 1)}) {
                const QPointF corner(k * sgn.x(), k * sgn.y());
                p.drawLine(corner, corner - QPointF(l * sgn.x(), 0.0));
                p.drawLine(corner, corner - QPointF(0.0, l * sgn.y()));
            }
        } else if (it.key == QLatin1String("lock")) {
            // A padlock: its shackle and its body.
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(-s * 0.55, -s, s * 1.1, s * 1.2), 0, 180 * 16);
            p.drawLine(QPointF(-s * 0.55, -s * 0.4), QPointF(-s * 0.55, -s * 0.05));
            p.drawLine(QPointF(s * 0.55, -s * 0.4), QPointF(s * 0.55, -s * 0.05));
            p.setBrush(ink);
            p.drawRoundedRect(QRectF(-s * 0.85, -s * 0.05, s * 1.7, s * 1.05), 1.2, 1.2);
        } else if (it.key == QLatin1String("iso")) {
            // A cube seen at an angle.
            p.setBrush(Qt::NoBrush);
            const QPointF t(0, -s), l(-s * 0.87, -s * 0.5), r(s * 0.87, -s * 0.5), hub(0, 0), bl(-s * 0.87, s * 0.5),
                br(s * 0.87, s * 0.5), b(0, s);
            p.drawPolygon(QPolygonF({t, r, br, b, bl, l}));
            p.drawLine(hub, l);
            p.drawLine(hub, r);
            p.drawLine(hub, b);
        } else if (it.key == QLatin1String("gear")) {
            // A cog: a ring with teeth.
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(0, 0), s * 0.55, s * 0.55);
            for (int k = 0; k < 8; ++k) {
                const double a = k * M_PI / 4.0;
                p.drawLine(QPointF(std::cos(a) * s * 0.6, std::sin(a) * s * 0.6), QPointF(std::cos(a) * s, std::sin(a) * s));
            }
        } else if (it.key == QLatin1String("tie")) {
            // Two links of a chain.
            p.setBrush(Qt::NoBrush);
            p.save();
            p.rotate(-35.0);
            p.drawRoundedRect(QRectF(-s - 1.0, -2.6, s + 2.5, 5.2), 2.6, 2.6);
            p.drawRoundedRect(QRectF(-1.5, -2.6, s + 2.5, 5.2), 2.6, 2.6);
            p.restore();
        } else if (it.key.size() >= 2 && QStringLiteral("xyz").contains(it.key.at(it.key.size() - 2))) {
            // MOVED ALONG AN AXIS, or TURNED ROUND ONE: in the axis's own colour (X red, Y green, Z blue), its letter
            // beside an arrow pointing the way (+ up, - down) or round it.
            const QChar axis = it.key.at(it.key.size() - 2);
            const bool plus = it.key.endsWith(QLatin1Char('+'));
            const QColor tint = axis == QLatin1Char('x') ? QColor(240, 110, 110) : axis == QLatin1Char('y') ? QColor(110, 220, 130) : QColor(120, 160, 255);
            p.setPen(QPen(tint, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(tint);
            if (it.key.startsWith(QLatin1Char('r'))) {
                const QRectF ring(-s * 0.9, -s * 0.9, 1.8 * s, 1.8 * s);
                p.setBrush(Qt::NoBrush);
                p.drawArc(ring, (plus ? 120 : 60) * 16, (plus ? 240 : -240) * 16);
                const double end = (plus ? 120.0 + 240.0 : 60.0 - 240.0) * M_PI / 180.0;
                const QPointF tip(s * 0.9 * std::cos(end), -s * 0.9 * std::sin(end));
                p.setBrush(tint);
                p.drawEllipse(tip, 2.0, 2.0);
            } else {
                // The way the axis runs on the screen, as the view is turned - left where it goes left.
                arrow(it.arrowDeg < 1e8 ? it.arrowDeg : (plus ? 90.0 : -90.0));
                const double back = (it.arrowDeg < 1e8 ? it.arrowDeg : (plus ? 90.0 : -90.0)) * M_PI / 180.0;
                p.translate(-s * 1.0 * std::cos(back), s * 1.0 * std::sin(back));
            }
            QFont f = p.font();
            f.setBold(true);
            f.setPixelSize(9);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255));
            p.drawText(QRectF(-6, -6, 12, 12), Qt::AlignCenter, QString(axis.toUpper()));
        }
        p.restore();
    }
    // THE MIDDLE: what the segment under the pointer does.
    p.setPen(QPen(QColor(150, 172, 196, 140), 1.0));
    p.setBrush(QColor(43, 150, 200, 205));
    p.drawEllipse(inner.adjusted(1.5, 1.5, -1.5, -1.5));
    const QString say = hover_ >= 0 && hover_ < count() ? items_[static_cast<size_t>(hover_)].label : middleLabel_;
    // ON ONE LINE, INSIDE THE CIRCLE: as big as it can be up to 9 px, smaller for a long word ("Symmetric").
    QFont f = font();
    f.setBold(true);
    const double room = 2.0 * kInner - 7.0;
    double px = 9.0;
    for (; px > 6.0; px -= 0.5) {
        f.setPixelSize(static_cast<int>(px));
        if (QFontMetricsF(f).horizontalAdvance(say) <= room) break;
    }
    f.setPixelSize(static_cast<int>(px));
    p.setFont(f);
    p.setPen(QColor(255, 255, 255));
    p.drawText(inner, Qt::AlignCenter, say);
}

void NavWheel::mouseMoveEvent(QMouseEvent* e) {
    const QPointF centre = QRectF(rect()).center();
    const bool over = std::hypot(e->position().x() - centre.x(), e->position().y() - centre.y()) <= kOuter * drawingScale();
    if (over) {
        shrinkTimer_.stop();
        if (!pointerOver_) { pointerOver_ = true; updateDrawingSize(); }
    } else if (pointerOver_ && !shrinkTimer_.isActive()) shrinkTimer_.start();
    const int at = segmentAt(e->position());
    if (at != hover_) {
        hover_ = at;
        setToolTip(at >= 0 ? items_[static_cast<size_t>(at)].tip : QString());
        update();
    }
}

void NavWheel::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    pressed_ = segmentAt(e->position());
    update();
}

void NavWheel::mouseReleaseEvent(QMouseEvent* e) {
    const int at = segmentAt(e->position());
    // THE MIDDLE, clicked: its own act.
    if (e->button() == Qt::LeftButton && pressed_ < 0 && middleAct_) {
        const QPointF c = QRectF(rect()).center();
        if (std::hypot(e->position().x() - c.x(), e->position().y() - c.y()) < kInner * drawingScale()) {
            middleAct_(e->globalPosition().toPoint());
            return;
        }
    }
    const int was = pressed_;
    pressed_ = -1;
    update();
    if (e->button() == Qt::LeftButton && at >= 0 && at == was && items_[static_cast<size_t>(at)].act)
        items_[static_cast<size_t>(at)].act();
}

void NavWheel::leaveEvent(QEvent*) {
    hover_ = -1;
    pressed_ = -1;
    update();
    if (pointerOver_) shrinkTimer_.start();
}

void NavWheel::enterEvent(QEnterEvent*) {
    shrinkTimer_.stop();
    if (!pointerOver_) { pointerOver_ = true; updateDrawingSize(); }
}
} // namespace Loopwire
