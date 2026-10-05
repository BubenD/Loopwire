#include "app/Icons3D.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include <cmath>

namespace Edraw_Desktop {

namespace {

// An iso cube round (cx, cy), `s` from its middle to a corner; `face` 0 none, 1 top, 2 left (front), 3 right (side)
// filled in the accent.
void cube(QPainter& p, double cx, double cy, double s, const QColor& ink, const QColor& accent, int face = 0, bool solid = true) {
    const double h = s * 0.5;
    const QPointF t(cx, cy - s), l(cx - s * 0.87, cy - h), r(cx + s * 0.87, cy - h), c(cx, cy), bl(cx - s * 0.87, cy + h),
        br(cx + s * 0.87, cy + h), b(cx, cy + s);
    const QPolygonF top{t, r, c, l}, left{l, c, b, bl}, right{c, r, br, b};
    QColor shade = ink;
    shade.setAlphaF(0.18f);
    p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(face == 1 ? QBrush(accent) : solid ? QBrush(shade) : Qt::NoBrush);
    p.drawPolygon(top);
    shade.setAlphaF(0.08f);
    p.setBrush(face == 2 ? QBrush(accent) : solid ? QBrush(shade) : Qt::NoBrush);
    p.drawPolygon(left);
    shade.setAlphaF(0.30f);
    p.setBrush(face == 3 ? QBrush(accent) : solid ? QBrush(shade) : Qt::NoBrush);
    p.drawPolygon(right);
}

void arrowHead(QPainter& p, const QPointF& tip, const QPointF& from, double size) {
    const QPointF d = tip - from;
    const double l = std::hypot(d.x(), d.y());
    if (l < 1e-6) return;
    const QPointF u = d / l, n(-u.y(), u.x());
    p.drawPolygon(QPolygonF{tip, tip - u * size + n * size * 0.55, tip - u * size - n * size * 0.55});
}

void arrow(QPainter& p, const QPointF& a, const QPointF& b, double head = 5.0) {
    p.drawLine(a, b);
    p.save();
    p.setBrush(p.pen().color());
    arrowHead(p, b, a, head);
    p.restore();
}

// A cylinder standing on its axis from `y0` to `y1`, `rx` wide.
void cylinder(QPainter& p, double cx, double y0, double y1, double rx, const QColor& ink, const QColor& fill, bool hollow = false) {
    const double ry = rx * 0.38;
    QPainterPath body;
    body.moveTo(cx - rx, y0);
    body.lineTo(cx - rx, y1);
    body.arcTo(QRectF(cx - rx, y1 - ry, 2 * rx, 2 * ry), 180, 180);
    body.lineTo(cx + rx, y0);
    p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(fill);
    p.drawPath(body);
    p.setBrush(fill.lighter(125));
    p.drawEllipse(QRectF(cx - rx, y0 - ry, 2 * rx, 2 * ry));
    if (hollow) {
        p.setBrush(QColor(0, 0, 0, 90));
        p.drawEllipse(QRectF(cx - rx * 0.5, y0 - ry * 0.5, rx, ry));
    }
}

void letter(QPainter& p, const QString& s, const QRectF& box, const QColor& ink) {
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(static_cast<int>(box.height()));
    p.setFont(f);
    p.setPen(ink);
    p.drawText(box, Qt::AlignCenter, s);
}

void draw(QPainter& p, const QString& k, const QColor& ink, const QColor& acc) {
    QColor soft = ink;
    soft.setAlphaF(0.22f);
    const QPen line(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const QPen accLine(acc, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(line);
    // ---- The ruler, the other side, the solids, the harness ----
    if (k == QLatin1String("measure")) {
        // A rule at a slant, its marks along it.
        p.save();
        p.translate(16, 16);
        p.rotate(-35);
        p.setPen(line);
        p.setBrush(soft);
        p.drawRect(QRectF(-13, -4, 26, 8));
        for (int m = -10; m <= 10; m += 4) p.drawLine(QPointF(m, -4), QPointF(m, m % 8 == 2 ? 1.5 : -0.5));
        p.restore();
        return;
    }
    if (k == QLatin1String("flip")) {
        // Two arrows round, each the other way: from one side to the other.
        p.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(6, 6, 20, 20), 30 * 16, 120 * 16);
        p.drawArc(QRectF(6, 6, 20, 20), 210 * 16, 120 * 16);
        p.setBrush(ink);
        arrowHead(p, QPointF(7.5, 12), QPointF(10, 8), 5);
        arrowHead(p, QPointF(24.5, 20), QPointF(22, 24), 5);
        p.setPen(QPen(acc, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(16, 10), QPointF(16, 22));
        return;
    }
    if (k == QLatin1String("parts")) {
        // Three boxes, one of them only outlined - hidden.
        cube(p, 10, 20, 6, ink, acc, 0);
        cube(p, 22, 20, 6, ink, acc, 1);
        QPen dashed(ink, 1.4, Qt::DashLine);
        p.setPen(dashed);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(11, 3, 10, 9));
        return;
    }
    if (k == QLatin1String("harness")) {
        // A bundle of three wires, fanning at one end.
        const QColor wires[3] = {QColor(220, 70, 60), ink, QColor(70, 190, 110)};
        for (int w = 0; w < 3; ++w) {
            QPainterPath path;
            path.moveTo(4, 16);
            path.lineTo(16, 16);
            path.cubicTo(20, 16, 22, 8 + w * 8, 28, 8 + w * 8);
            p.setPen(QPen(wires[w], 2.2, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        p.setPen(QPen(acc, 4.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(5, 16), QPointF(14, 16));
        return;
    }
    // ---- Views ----
    if (k == QLatin1String("iso")) return cube(p, 16, 16, 12, ink, acc, 0);
    // The far sides: the cube with the side looked at from lettered on it.
    if (k == QLatin1String("back") || k == QLatin1String("bottom") || k == QLatin1String("left") || k == QLatin1String("right")) {
        cube(p, 14, 17, 11, ink, acc, k == QLatin1String("bottom") ? 1 : k == QLatin1String("back") ? 2 : 3, true);
        letter(p, k == QLatin1String("bottom") ? QStringLiteral("B") : k == QLatin1String("back") ? QStringLiteral("K") : k.left(1).toUpper(),
               QRectF(20, 1, 12, 12), acc);
        return;
    }
    if (k == QLatin1String("display")) {
        // A screen with a gradient on it.
        QLinearGradient g(0, 5, 0, 23);
        g.setColorAt(0, acc.lighter(140));
        g.setColorAt(1, QColor(30, 36, 44));
        p.setBrush(g);
        p.drawRoundedRect(QRectF(3, 5, 26, 18), 2, 2);
        p.drawLine(12, 27, 20, 27);
        p.drawLine(16, 23, 16, 27);
        return;
    }
    if (k == QLatin1String("expand")) {
        // A panel opened out below: a frame, and an arrow down into its lower half.
        p.setBrush(soft);
        p.drawRect(QRectF(4, 4, 24, 24));
        p.setBrush(acc);
        p.drawRect(QRectF(4, 18, 24, 10));
        p.setPen(QPen(ink, 2.2, Qt::SolidLine, Qt::RoundCap));
        arrow(p, QPointF(16, 6), QPointF(16, 17), 5);
        return;
    }
    if (k == QLatin1String("top")) return cube(p, 16, 16, 12, ink, acc, 1);
    if (k == QLatin1String("front")) return cube(p, 16, 16, 12, ink, acc, 2);
    if (k == QLatin1String("side")) return cube(p, 16, 16, 12, ink, acc, 3);
    if (k == QLatin1String("fit")) {
        cube(p, 16, 16, 7, ink, acc, 0);
        p.setPen(QPen(acc, 2.2, Qt::SolidLine, Qt::RoundCap));
        for (const QPointF& c : {QPointF(4, 4), QPointF(28, 4), QPointF(4, 28), QPointF(28, 28)}) {
            const double sx = c.x() < 16 ? 1 : -1, sy = c.y() < 16 ? 1 : -1;
            p.drawLine(c, c + QPointF(6 * sx, 0));
            p.drawLine(c, c + QPointF(0, 6 * sy));
        }
        return;
    }
    if (k == QLatin1String("fullscreen") || k == QLatin1String("back")) {
        p.setPen(QPen(ink, 1.6));
        p.setBrush(soft);
        p.drawRect(QRectF(7, 8, 18, 16));
        p.setPen(QPen(acc, 2.0, Qt::SolidLine, Qt::RoundCap));
        const bool out = k == QLatin1String("fullscreen");
        for (const QPointF& c : {QPointF(3, 3), QPointF(29, 3), QPointF(3, 29), QPointF(29, 29)}) {
            const QPointF in(c.x() < 16 ? 9 : 23, c.y() < 16 ? 10 : 22);
            if (out) arrow(p, in, c, 4.5);
            else arrow(p, c, in, 4.5);
        }
        return;
    }
    if (k == QLatin1String("refresh") || k == QLatin1String("undo") || k == QLatin1String("redo")) {
        p.setPen(QPen(k == QLatin1String("refresh") ? acc : ink, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        const bool back = k == QLatin1String("undo");
        const QRectF r(6, 7, 20, 20);
        p.drawArc(r, back ? 40 * 16 : 140 * 16, back ? 260 * 16 : -260 * 16);
        const double a = (back ? 40.0 : 140.0) * M_PI / 180.0;
        const QPointF tip(16 + 10 * std::cos(a), 17 - 10 * std::sin(a));
        p.setBrush(p.pen().color());
        arrowHead(p, tip + QPointF(back ? 1.5 : -1.5, 3), tip + QPointF(back ? -4 : 4, 6), 6);
        return;
    }
    if (k == QLatin1String("wireframe")) return cube(p, 16, 16, 12, ink, acc, 0, false);
    if (k == QLatin1String("lock")) {
        p.setPen(QPen(ink, 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(10, 4, 12, 14), 0, 180 * 16);
        p.drawLine(10, 11, 10, 15);
        p.drawLine(22, 11, 22, 15);
        p.setPen(QPen(ink, 1.6));
        p.setBrush(acc);
        p.drawRoundedRect(QRectF(7, 14, 18, 14), 2, 2);
        return;
    }
    if (k == QLatin1String("help")) {
        p.setBrush(soft);
        p.drawEllipse(QRectF(4, 4, 24, 24));
        letter(p, QStringLiteral("?"), QRectF(4, 6, 24, 20), acc);
        return;
    }
    if (k == QLatin1String("workplane")) {
        p.setBrush(soft);
        p.drawPolygon(QPolygonF{{4, 21}, {14, 13}, {28, 13}, {18, 21}});
        p.setPen(QPen(ink, 0.9));
        for (int i = 1; i < 4; ++i) p.drawLine(QPointF(4 + i * 3.5, 21 - i * 2), QPointF(18 + i * 3.5, 21 - i * 2));
        p.setPen(QPen(acc, 2.2, Qt::SolidLine, Qt::RoundCap));
        arrow(p, QPointF(16, 17), QPointF(16, 4), 4.5);
        return;
    }
    // ---- The environment ----
    if (k == QLatin1String("load")) {
        cube(p, 13, 18, 9, ink, acc, 0);
        p.setPen(QPen(acc, 2.4, Qt::SolidLine, Qt::RoundCap));
        arrow(p, QPointF(25, 28), QPointF(25, 7), 6);
        return;
    }
    if (k == QLatin1String("clear") || k == QLatin1String("delete")) {
        if (k == QLatin1String("clear")) cube(p, 14, 17, 10, ink, acc, 0);
        else {
            p.setBrush(soft);
            p.drawRect(QRectF(9, 10, 14, 18));
            p.drawLine(6, 9, 26, 9);
            p.drawLine(13, 6, 19, 6);
            for (int i = 0; i < 3; ++i) p.drawLine(QPointF(12.5 + i * 3.5, 13), QPointF(12.5 + i * 3.5, 25));
        }
        p.setPen(QPen(QColor(220, 70, 60), 2.8, Qt::SolidLine, Qt::RoundCap));
        if (k == QLatin1String("clear")) {
            p.drawLine(20, 4, 29, 13);
            p.drawLine(29, 4, 20, 13);
        }
        return;
    }
    if (k == QLatin1String("section")) {
        cube(p, 16, 16, 12, ink, acc, 0);
        p.setPen(Qt::NoPen);
        QColor cut = acc;
        cut.setAlphaF(0.7f);
        p.setBrush(cut);
        p.drawPolygon(QPolygonF{{16, 4}, {16, 28}, {26.4, 22}, {26.4, 10}});
        return;
    }
    // ---- The route ----
    if (k == QLatin1String("select")) {
        p.setBrush(acc);
        p.drawPolygon(QPolygonF{{9, 4}, {9, 25}, {14, 20}, {18, 28}, {21, 26.5}, {17, 19}, {24, 19}});
        return;
    }
    if (k == QLatin1String("connector")) {
        cylinder(p, 14, 9, 22, 8, ink, QColor(190, 194, 202));
        p.setPen(Qt::NoPen);
        p.setBrush(acc);
        for (int i = 0; i < 3; ++i) p.drawEllipse(QRectF(9.5 + i * 3.5, 8, 2, 2));
        p.setPen(QPen(acc, 3.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(14, 27, 14, 31);
        p.drawLine(14, 29, 28, 29);
        return;
    }
    if (k == QLatin1String("branch")) {
        p.setPen(accLine);
        p.drawLine(4, 22, 16, 18);
        p.drawLine(16, 18, 28, 26);
        p.drawLine(16, 18, 26, 5);
        p.setPen(QPen(ink, 1.4));
        p.setBrush(QColor(250, 210, 60));
        p.drawRect(QRectF(12.5, 14.5, 7, 7));
        return;
    }
    if (k == QLatin1String("clip")) {
        p.setPen(accLine);
        p.drawLine(3, 20, 29, 20);
        p.setPen(QPen(ink, 2.0));
        p.setBrush(QColor(70, 190, 110));
        p.drawRoundedRect(QRectF(11, 14, 10, 12), 2, 2);
        p.drawLine(8, 28, 24, 28);
        p.drawLine(16, 26, 16, 28);
        return;
    }
    if (k == QLatin1String("route")) {
        p.setPen(accLine);
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(4, 26);
        path.cubicTo(10, 26, 10, 10, 16, 12);
        path.cubicTo(22, 14, 22, 6, 28, 6);
        p.drawPath(path);
        p.setPen(QPen(ink, 1.4));
        p.setBrush(ink);
        for (const QPointF& c : {QPointF(4, 26), QPointF(16, 12), QPointF(28, 6)}) p.drawEllipse(c, 2.4, 2.4);
        return;
    }
    if (k == QLatin1String("move")) {
        p.setPen(QPen(acc, 2.2, Qt::SolidLine, Qt::RoundCap));
        arrow(p, QPointF(16, 16), QPointF(16, 3), 5);
        arrow(p, QPointF(16, 16), QPointF(16, 29), 5);
        arrow(p, QPointF(16, 16), QPointF(3, 16), 5);
        arrow(p, QPointF(16, 16), QPointF(29, 16), 5);
        p.setPen(QPen(ink, 1.4));
        p.setBrush(QColor(205, 208, 214));
        p.drawEllipse(QPointF(16, 16), 4, 4);
        return;
    }
    if (k == QLatin1String("loose")) {
        // A cable out of a connector with a loop of slack in it, a plus over it.
        p.setPen(QPen(ink, 1.4));
        p.setBrush(QColor(205, 208, 214));
        p.drawRect(QRectF(2, 19, 6, 9));
        p.setPen(accLine);
        p.setBrush(Qt::NoBrush);
        QPainterPath slack(QPointF(8, 23.5));
        slack.cubicTo(QPointF(16, 23.5), QPointF(22, 14), QPointF(17, 12));
        slack.cubicTo(QPointF(12, 10), QPointF(12, 23.5), QPointF(29, 23.5));
        p.drawPath(slack);
        p.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(25, 4), QPointF(25, 12));
        p.drawLine(QPointF(21, 8), QPointF(29, 8));
        return;
    }
    if (k == QLatin1String("stretch")) {
        p.setPen(accLine);
        p.drawLine(4, 22, 18, 22);
        p.setPen(QPen(ink, 1.4));
        p.setBrush(QColor(70, 190, 110));
        p.drawRect(QRectF(2, 18.5, 5, 7));
        p.setBrush(QColor(205, 208, 214));
        p.drawRect(QRectF(18, 17, 8, 10));
        p.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap));
        arrow(p, QPointF(12, 9), QPointF(28, 9), 5);
        arrow(p, QPointF(28, 9), QPointF(10, 9), 5);
        return;
    }
    if (k == QLatin1String("lengths")) {
        p.save();
        p.translate(16, 16);
        p.rotate(-30);
        p.setBrush(QColor(240, 200, 90));
        p.drawRect(QRectF(-14, -5, 28, 10));
        p.setPen(QPen(ink, 1.2));
        for (int i = -12; i <= 12; i += 3) p.drawLine(QPointF(i, -5), QPointF(i, i % 6 == 0 ? 0 : -2));
        p.restore();
        return;
    }
    if (k == QLatin1String("link") || k == QLatin1String("unlink")) {
        p.setPen(QPen(ink, 2.4));
        p.setBrush(Qt::NoBrush);
        const bool apart = k == QLatin1String("unlink");
        p.drawRoundedRect(QRectF(3, 11, 14, 10), 5, 5);
        p.setPen(QPen(acc, 2.4));
        p.drawRoundedRect(QRectF(apart ? 17 : 15, 11, 14, 10), 5, 5);
        if (apart) {
            p.setPen(QPen(QColor(220, 70, 60), 2.0, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(15, 5, 15, 9);
            p.drawLine(15, 23, 15, 27);
        }
        return;
    }
    // ---- Solids ----
    if (k == QLatin1String("box")) return cube(p, 16, 16, 12, ink, acc, 1);
    if (k == QLatin1String("cylinder")) return cylinder(p, 16, 8, 25, 10, ink, acc);
    if (k == QLatin1String("tube")) return cylinder(p, 16, 8, 25, 10, ink, acc, true);
    if (k == QLatin1String("cone")) {
        p.setBrush(acc);
        QPainterPath path;
        path.moveTo(16, 4);
        path.lineTo(5, 24);
        path.arcTo(QRectF(5, 20, 22, 8), 180, 180);
        path.closeSubpath();
        p.drawPath(path);
        return;
    }
    if (k == QLatin1String("sphere")) {
        QRadialGradient g(QPointF(12, 11), 15);
        g.setColorAt(0, acc.lighter(160));
        g.setColorAt(1, acc.darker(130));
        p.setBrush(g);
        p.drawEllipse(QRectF(4, 4, 24, 24));
        return;
    }
    if (k == QLatin1String("torus")) {
        p.setBrush(acc);
        QPainterPath ring;
        ring.addEllipse(QRectF(3, 8, 26, 16));
        ring.addEllipse(QRectF(11, 13, 10, 6));
        ring.setFillRule(Qt::OddEvenFill);
        p.drawPath(ring);
        return;
    }
    if (k == QLatin1String("hex")) {
        p.setBrush(acc);
        QPolygonF hex;
        for (int i = 0; i < 6; ++i) hex << QPointF(16 + 12 * std::cos(i * M_PI / 3.0), 16 + 12 * std::sin(i * M_PI / 3.0));
        p.drawPolygon(hex);
        p.setBrush(QColor(0, 0, 0, 90));
        p.drawEllipse(QPointF(16, 16), 5, 5);
        return;
    }
    if (k == QLatin1String("turned")) {
        // A profile turned round an axis.
        p.setPen(QPen(ink, 1.0, Qt::DashLine));
        p.drawLine(16, 2, 16, 30);
        p.setPen(QPen(ink, 1.6));
        p.setBrush(acc);
        p.drawPolygon(QPolygonF{{16, 5}, {24, 5}, {24, 10}, {20, 13}, {20, 22}, {26, 25}, {26, 28}, {16, 28}});
        p.setBrush(soft);
        p.drawPolygon(QPolygonF{{16, 5}, {8, 5}, {8, 10}, {12, 13}, {12, 22}, {6, 25}, {6, 28}, {16, 28}});
        p.setPen(QPen(acc, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(4, 14, 24, 8), 200 * 16, 140 * 16);
        return;
    }
    if (k == QLatin1String("extruded") || k == QLatin1String("extrude")) {
        p.setBrush(soft);
        const QPolygonF base{{4, 26}, {16, 26}, {16, 18}, {10, 18}, {10, 14}, {4, 14}};
        p.drawPolygon(base);
        p.setBrush(acc);
        p.drawPolygon(base.translated(9, -8));
        p.setPen(QPen(ink, 1.0));
        for (const QPointF& c : base) p.drawLine(c, c + QPointF(9, -8));
        return;
    }
    if (k == QLatin1String("pins")) {
        cylinder(p, 16, 8, 26, 12, ink, QColor(150, 158, 170));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(212, 175, 80));
        for (int i = 0; i < 6; ++i) {
            const double a = i * M_PI / 3.0;
            p.drawEllipse(QPointF(16 + 6.5 * std::cos(a), 8 + 2.4 * std::sin(a)), 1.6, 1.0);
        }
        p.drawEllipse(QPointF(16, 8), 1.6, 1.0);
        return;
    }
    // ---- Modifying ----
    if (k == QLatin1String("duplicate")) {
        cube(p, 12, 19, 8, ink, acc, 0);
        cube(p, 20, 13, 8, ink, acc, 1);
        return;
    }
    if (k.startsWith(QLatin1String("mirror"))) {
        p.setPen(QPen(ink, 1.2, Qt::DashLine));
        p.drawLine(16, 3, 16, 29);
        p.setPen(QPen(ink, 1.6));
        p.setBrush(soft);
        p.drawPolygon(QPolygonF{{4, 24}, {13, 24}, {13, 10}});
        p.setBrush(acc);
        p.drawPolygon(QPolygonF{{28, 24}, {19, 24}, {19, 10}});
        letter(p, k.mid(6).toUpper(), QRectF(18, 1, 13, 10), ink);
        return;
    }
    if (k.startsWith(QLatin1String("turn"))) {
        cube(p, 14, 18, 8, ink, acc, 0);
        p.setPen(QPen(acc, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(3, 4, 22, 22), 30 * 16, 120 * 16);
        p.setBrush(acc);
        arrowHead(p, QPointF(23.5, 8), QPointF(20, 5.5), 5);
        letter(p, k.mid(4).toUpper(), QRectF(20, 18, 12, 12), ink);
        return;
    }
    if (k == QLatin1String("array")) {
        for (int i = 0; i < 3; ++i) cube(p, 7 + i * 9, 17, 4.6, ink, acc, i == 0 ? 1 : 0);
        p.setPen(QPen(acc, 1.6, Qt::SolidLine, Qt::RoundCap));
        arrow(p, QPointF(5, 27), QPointF(28, 27), 4);
        return;
    }
    if (k == QLatin1String("middle")) {
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(16, 16), 11, 11);
        p.drawLine(16, 2, 16, 9);
        p.drawLine(16, 23, 16, 30);
        p.drawLine(2, 16, 9, 16);
        p.drawLine(23, 16, 30, 16);
        p.setBrush(acc);
        p.drawEllipse(QPointF(16, 16), 3.5, 3.5);
        return;
    }
    if (k == QLatin1String("export") || k == QLatin1String("save")) {
        p.setBrush(soft);
        p.drawRect(QRectF(6, 4, 20, 24));
        if (k == QLatin1String("save")) {
            p.setBrush(acc);
            p.drawRect(QRectF(10, 4, 12, 8));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(10, 17, 12, 11));
        } else {
            cube(p, 16, 18, 6, ink, acc, 1);
            p.setPen(QPen(acc, 2.2, Qt::SolidLine, Qt::RoundCap));
            arrow(p, QPointF(22, 8), QPointF(30, 2), 4);
        }
        return;
    }
    if (k == QLatin1String("fillet") || k == QLatin1String("chamfer")) {
        p.setBrush(soft);
        QPainterPath path;
        path.moveTo(4, 28);
        path.lineTo(4, 6);
        path.lineTo(14, 6);
        if (k == QLatin1String("fillet")) path.quadTo(26, 6, 26, 18);
        else path.lineTo(26, 18);
        path.lineTo(26, 28);
        path.closeSubpath();
        p.drawPath(path);
        p.setPen(accLine);
        p.setBrush(Qt::NoBrush);
        QPainterPath edge;
        edge.moveTo(14, 6);
        if (k == QLatin1String("fillet")) edge.quadTo(26, 6, 26, 18);
        else edge.lineTo(26, 18);
        p.drawPath(edge);
        return;
    }
    if (k == QLatin1String("sketch")) {
        p.setBrush(soft);
        p.drawPolygon(QPolygonF{{3, 24}, {12, 15}, {29, 15}, {20, 24}});
        p.setPen(accLine);
        p.drawPolyline(QPolygonF{{8, 21}, {13, 18}, {18, 21}, {24, 18}});
        p.setPen(QPen(ink, 1.6));
        p.setBrush(QColor(240, 200, 90));
        p.drawPolygon(QPolygonF{{20, 4}, {27, 11}, {24, 13}, {17, 6}});
        return;
    }
    if (k == QLatin1String("pin") || k == QLatin1String("socket")) {
        // A face, and a pin standing on it - or a socket in it.
        p.setBrush(soft);
        p.drawPolygon(QPolygonF{{3, 24}, {13, 17}, {29, 17}, {19, 24}});
        if (k == QLatin1String("pin")) {
            cylinder(p, 16, 6, 20, 3.2, ink, QColor(212, 175, 80));
        } else {
            p.setBrush(QColor(20, 20, 24));
            p.drawEllipse(QRectF(11.5, 18.5, 9, 4));
            p.setPen(QPen(acc, 2.0, Qt::SolidLine, Qt::RoundCap));
            arrow(p, QPointF(16, 3), QPointF(16, 15), 4.5);
        }
        return;
    }
    if (k == QLatin1String("renumber")) {
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(ink);
        p.drawText(QRectF(1, 2, 12, 12), Qt::AlignCenter, QStringLiteral("1"));
        p.drawText(QRectF(10, 10, 12, 12), Qt::AlignCenter, QStringLiteral("2"));
        p.setPen(acc);
        p.drawText(QRectF(19, 18, 12, 12), Qt::AlignCenter, QStringLiteral("3"));
        return;
    }
    if (k == QLatin1String("cut")) {
        cube(p, 14, 18, 10, ink, acc, 0);
        p.setPen(QPen(QColor(220, 70, 60), 1.8));
        p.setBrush(QColor(220, 70, 60, 120));
        p.drawEllipse(QRectF(17, 4, 12, 12));
        return;
    }
    cube(p, 16, 16, 12, ink, acc, 0);
}

} // namespace

QIcon icon3D(const QString& key, const QColor& ink, const QColor& accent) {
    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pm(32 * scale, 32 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        draw(p, key, ink, accent);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace Edraw_Desktop
