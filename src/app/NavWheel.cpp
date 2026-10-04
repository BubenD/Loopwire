#include "app/NavWheel.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace Edraw_Desktop {

// ==================================================================================================
// The navigation wheel
// ==================================================================================================

NavWheel::NavWheel(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::PointingHandCursor);
    const int side = static_cast<int>(2.0 * kOuter) + 4;
    setFixedSize(side, side);
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
    const int side = static_cast<int>(2.0 * kOuter) + 4;
    return {side, side};
}

void NavWheel::resizeEvent(QResizeEvent*) {
    // ROUND TO THE POINTER as well as to the eye: the corners of its square let clicks through to the pane.
    const QPointF c = rect().center() + QPointF(0.5, 0.5);
    setMask(QRegion(QRect(static_cast<int>(c.x() - kOuter - 1), static_cast<int>(c.y() - kOuter - 1),
                          static_cast<int>(2 * kOuter + 2), static_cast<int>(2 * kOuter + 2)),
                    QRegion::Ellipse));
}

double NavWheel::angleOf(int i) const {
    const int n = std::max(1, count());
    return 90.0 - i * 360.0 / n;   // the first at the top, the rest clockwise
}

int NavWheel::segmentAt(const QPointF& p) const {
    if (items_.empty()) return -1;
    const QPointF c = QRectF(rect()).center();
    const double dx = p.x() - c.x(), dy = c.y() - p.y();
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
    const double a = angleOf(i) * M_PI / 180.0, r = (kOuter + kInner) / 2.0;
    return {c.x() + r * std::cos(a), c.y() - r * std::sin(a)};
}

void NavWheel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF c = QRectF(rect()).center();
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
        const QPointF m = segmentCentre(i);
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
        if (std::hypot(e->position().x() - c.x(), e->position().y() - c.y()) < kInner) {
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
}
} // namespace Edraw_Desktop
