#include "render/EntityRenderer.h"

#include "geometry/Geometry.h"
#include "harness/Harness.h"

#include <QFont>
#include <QJsonArray>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QtMath>

#include <algorithm>

namespace Edraw_Desktop {

namespace {
int g_dimDecimals = 2; // decimals on dimension labels (configurable via Options)
QColor g_bgColor{"#1e2832"}; // canvas background, used to paint Wipeout masks
}
void EntityRenderer::setDimensionDecimals(int d) { g_dimDecimals = std::clamp(d, 0, 6); }
int EntityRenderer::dimensionDecimals() { return g_dimDecimals; }
void EntityRenderer::setBackgroundColor(const QColor& c) { g_bgColor = c; }
QColor EntityRenderer::backgroundColor() { return g_bgColor; }

QColor EntityRenderer::resolveColor(const Entity& e, const Document& doc) {
    QString layerHex = QStringLiteral("#ffffff");
    if (const Layer* layer = doc.findLayer(e.layer); layer && QColor(layer->color).isValid())
        layerHex = layer->color;

    // Wires take their color from the wire color code (WH/BK/RD/...).
    if (e.type == EntityType::Wire) {
        const QString mode = e.meta.value("wireColorMode").toString();
        if (mode != "bylayer")
            return QColor(wireColorHex(e.meta.value("wireColor").toString(), layerHex));
        return QColor(layerHex);
    }
    if (e.hasExplicitColor()) {
        QColor c(e.color);
        if (c.isValid()) return c;
    }
    return QColor(layerHex);
}

namespace {

double effectiveWeight(const Entity& e, const Document& doc) {
    if (e.type == EntityType::Wire)
        return wireGaugeWeight(e.meta.value("wireGauge").toString());
    if (e.weight > 0.0) return e.weight;
    if (const Layer* layer = doc.findLayer(e.layer)) return layer->weight;
    return 1.0;
}

// Draw a small text label at a screen point, scaled to world height.
void drawWorldText(QPainter& p, const QPointF& at, const QString& text,
                   double worldHeight, double scale, const QColor& color,
                   Qt::Alignment align = Qt::AlignLeft) {
    if (text.isEmpty()) return;
    QFont f = p.font();
    f.setPixelSize(std::max(1, static_cast<int>(worldHeight * scale)));
    p.setFont(f);
    p.setPen(color);
    if (align & Qt::AlignHCenter) {
        const QFontMetricsF fm(f);
        p.drawText(QPointF(at.x() - fm.horizontalAdvance(text) / 2.0, at.y()), text);
    } else {
        p.drawText(at, text);
    }
}

QPolygonF toScreenPoly(const std::vector<Vec2>& pts, const ViewTransform& vt) {
    QPolygonF poly;
    poly.reserve(static_cast<int>(pts.size()));
    for (const auto& p : pts) poly.append(vt.toScreen(p));
    return poly;
}

// Draw a family of equally spaced parallel lines (screen px) at `ang` radians,
// covering `bb`. The caller clips to the hatch boundary.
void drawParallelLines(QPainter& p, const QRectF& bb, double ang, double gap) {
    if (gap < 1.5) gap = 1.5;
    const double cx = bb.center().x(), cy = bb.center().y();
    const double diag = std::hypot(bb.width(), bb.height()) + 4.0;
    const double dx = std::cos(ang), dy = std::sin(ang); // line direction
    const double nx = -dy, ny = dx;                       // normal (line stepping)
    const double half = diag / 2.0;
    const int n = static_cast<int>(std::ceil(diag / gap)) + 2;
    for (int i = -n; i <= n; ++i) {
        const double off = i * gap;
        const double px = cx + nx * off, py = cy + ny * off;
        p.drawLine(QPointF(px - dx * half, py - dy * half),
                   QPointF(px + dx * half, py + dy * half));
    }
}

// Fill a closed boundary (screen-space path) with a hatch pattern.
void drawHatch(QPainter& p, const QPainterPath& path, const Entity& e, const QColor& base,
               const ViewTransform& vt) {
    const QString pat = e.meta.value("pattern").toString(QStringLiteral("ansi31")).toLower();
    const double userAngle = e.meta.value("hatchAngle").toDouble(0.0) * M_PI / 180.0;
    const double scale = std::max(0.05, e.meta.value("hatchScale").toDouble(1.0));
    const int transp = std::clamp(e.meta.value("transparency").toInt(0), 0, 95);

    QColor c = base;
    c.setAlpha(static_cast<int>(255 * (1.0 - transp / 100.0)));

    p.save();
    if (pat == QLatin1String("solid")) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(path);
        p.restore();
        return;
    }

    p.setClipPath(path);
    QPen hp(c);
    hp.setCosmetic(true);
    hp.setWidthF(1.0);
    p.setPen(hp);
    p.setBrush(Qt::NoBrush);

    const QRectF bb = path.boundingRect();
    const double gap = std::max(3.0, 6.0 * scale * vt.scale()); // world ~6 units * scale

    // Rotate the whole tiled pattern about the boundary centre by the user Angle.
    if (std::abs(userAngle) > 1e-9) {
        p.translate(bb.center());
        p.rotate(userAngle * 180.0 / M_PI);
        p.translate(-bb.center());
    }
    // Tiled patterns need a box large enough to still cover the region once rotated.
    const double diag = std::hypot(bb.width(), bb.height());
    const QRectF tb(bb.center().x() - diag, bb.center().y() - diag, 2 * diag, 2 * diag);

    if (pat == QLatin1String("dots")) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        for (double y = tb.top(); y <= tb.bottom(); y += gap)
            for (double x = tb.left(); x <= tb.right(); x += gap)
                p.drawEllipse(QPointF(x, y), 1.2, 1.2);
    } else if (pat == QLatin1String("brick")) {
        const double bw = gap * 2.0, bh = gap;
        for (double y = tb.top(); y <= tb.bottom(); y += bh)
            p.drawLine(QPointF(tb.left(), y), QPointF(tb.right(), y));
        int row = 0;
        for (double y = tb.top(); y < tb.bottom(); y += bh, ++row) {
            const double x0 = tb.left() + ((row & 1) ? bw / 2.0 : 0.0);
            for (double x = x0; x <= tb.right(); x += bw)
                p.drawLine(QPointF(x, y), QPointF(x, y + bh));
        }
    } else if (pat == QLatin1String("checker")) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const double cs = gap;
        int r = 0;
        for (double y = tb.top(); y < tb.bottom(); y += cs, ++r) {
            int col = 0;
            for (double x = tb.left(); x < tb.right(); x += cs, ++col)
                if (((r + col) & 1) == 0) p.drawRect(QRectF(x, y, cs, cs));
        }
    } else if (pat == QLatin1String("herringbone")) {
        const double L = gap * 2.0;
        int r = 0;
        for (double y = tb.top(); y < tb.bottom(); y += L, ++r) {
            int col = 0;
            for (double x = tb.left(); x < tb.right(); x += L, ++col) {
                if (((r + col) & 1) == 0)
                    p.drawLine(QPointF(x, y), QPointF(x + L, y + L));
                else
                    p.drawLine(QPointF(x, y + L), QPointF(x + L, y));
            }
        }
    } else if (pat == QLatin1String("honeycomb")) {
        const double rr = gap * 1.2;
        const double hx = rr * std::sqrt(3.0), vy = rr * 1.5;
        int row = 0;
        for (double y = tb.top(); y < tb.bottom() + rr; y += vy, ++row) {
            const double x0 = tb.left() + ((row & 1) ? hx / 2.0 : 0.0);
            for (double x = x0 - hx; x < tb.right() + hx; x += hx) {
                QPolygonF hexp;
                for (int k = 0; k < 6; ++k) {
                    const double a = M_PI / 180.0 * (60.0 * k + 30.0);
                    hexp << QPointF(x + rr * std::cos(a), y + rr * std::sin(a));
                }
                p.drawPolygon(hexp);
            }
        }
    } else if (pat == QLatin1String("braid")) {
        // Woven rope: tilted ovals alternating lean, brick-offset per row.
        const double cw = gap * 2.0, ch = gap * 1.4;
        int row = 0;
        for (double y = tb.top(); y < tb.bottom(); y += ch, ++row) {
            int col = 0;
            const double x0 = tb.left() + ((row & 1) ? cw / 2.0 : 0.0);
            for (double x = x0; x < tb.right(); x += cw, ++col) {
                p.save();
                p.translate(x + cw / 2.0, y + ch / 2.0);
                p.rotate(((row + col) & 1) ? 35.0 : -35.0);
                p.drawEllipse(QPointF(0, 0), cw * 0.5, ch * 0.28);
                p.restore();
            }
        }
    } else if (pat == QLatin1String("zigzag")) {
        const double w = gap, h = gap;
        for (double y = tb.top(); y < tb.bottom(); y += h * 1.6) {
            QPolygonF zz;
            bool up = false;
            for (double x = tb.left(); x <= tb.right() + w; x += w) {
                zz << QPointF(x, up ? y : y + h);
                up = !up;
            }
            p.drawPolyline(zz);
        }
    } else if (pat == QLatin1String("cable")) {
        // 3D cable emulation: shaded rounded tubes (dark edges, bright centre).
        const double h = std::max(4.0, gap * 1.7); // tube diameter
        for (double y = tb.top(); y < tb.bottom(); y += h) {
            QLinearGradient grad(0, y, 0, y + h);
            grad.setColorAt(0.0, c.darker(200));
            grad.setColorAt(0.42, c.lighter(165));
            grad.setColorAt(0.58, c.lighter(165));
            grad.setColorAt(1.0, c.darker(200));
            p.fillRect(QRectF(tb.left(), y, tb.width(), h), grad);
        }
        QPen sep(c.darker(260));
        sep.setCosmetic(true);
        p.setPen(sep);
        for (double y = tb.top(); y <= tb.bottom(); y += h)
            p.drawLine(QPointF(tb.left(), y), QPointF(tb.right(), y));
    } else if (pat == QLatin1String("weave")) {
        // Basket weave: alternating cells of horizontal / vertical strokes.
        const double cs = std::max(5.0, gap * 1.6);
        int r = 0;
        for (double y = tb.top(); y < tb.bottom(); y += cs, ++r) {
            int col = 0;
            for (double x = tb.left(); x < tb.right(); x += cs, ++col) {
                const bool horiz = ((r + col) & 1) == 0;
                for (int k = 1; k <= 3; ++k) {
                    if (horiz) {
                        const double t = y + cs * k / 4.0;
                        p.drawLine(QPointF(x + 1, t), QPointF(x + cs - 1, t));
                    } else {
                        const double u = x + cs * k / 4.0;
                        p.drawLine(QPointF(u, y + 1), QPointF(u, y + cs - 1));
                    }
                }
            }
        }
    } else if (pat == QLatin1String("scales")) {
        // Fish scales: rows of overlapping lower-semicircle arcs.
        const double rr = std::max(4.0, gap * 1.2);
        int row = 0;
        for (double y = tb.top(); y < tb.bottom(); y += rr, ++row) {
            const double x0 = tb.left() + ((row & 1) ? rr : 0.0);
            for (double x = x0 - rr; x < tb.right() + rr; x += 2 * rr)
                p.drawArc(QRectF(x - rr, y - rr, 2 * rr, 2 * rr), 0, -180 * 16);
        }
    } else {
        // Straight line families (lines / grid / cross / ansi31 / ansi32 / angle).
        std::vector<double> angles; // degrees, relative to the (already rotated) box
        double g = gap;
        if (pat == QLatin1String("lines")) angles = {0};
        else if (pat == QLatin1String("grid")) angles = {0, 90};
        else if (pat == QLatin1String("cross")) angles = {45, 135};
        else if (pat == QLatin1String("ansi32")) { angles = {45, 50}; g = gap * 0.6; }
        else if (pat == QLatin1String("angle")) angles = {0};
        else angles = {45}; // ansi31 (default)
        for (double aDeg : angles)
            drawParallelLines(p, tb, aDeg * M_PI / 180.0, g);
    }
    p.restore();
}

// Approximate an arc with line segments for direction-agnostic drawing.
void appendArc(QPainterPath& path, const Vec2& center, double radius,
               double a0, double a1, const ViewTransform& vt, bool moveFirst) {
    double sweep = a1 - a0;
    constexpr double kTwoPi = 2.0 * M_PI;
    while (sweep <= 0.0) sweep += kTwoPi;
    const int steps = std::max(8, static_cast<int>(std::ceil(sweep / (M_PI / 60.0))));
    for (int i = 0; i <= steps; ++i) {
        const double ang = a0 + sweep * (static_cast<double>(i) / steps);
        const QPointF s = vt.toScreen(pointOnCircle(center, radius, ang));
        if (i == 0 && moveFirst) path.moveTo(s);
        else path.lineTo(s);
    }
}

} // namespace

void EntityRenderer::draw(QPainter& p, const Entity& e, const Document& doc,
                          const ViewTransform& vt, const QColor& overrideColor) {
    const QColor color = overrideColor.isValid() ? overrideColor : resolveColor(e, doc);
    QPen pen(color);
    pen.setCosmetic(true); // width stays constant in screen px regardless of zoom
    pen.setWidthF(overrideColor.isValid() ? effectiveWeight(e, doc) + 1.4
                                          : effectiveWeight(e, doc));
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (e.type) {
    case EntityType::Line:
        p.drawLine(vt.toScreen(e.a), vt.toScreen(e.b));
        break;

    case EntityType::DoubleLine: {
        // Two parallel lines. Centered: +/- gap/2 about a,b. Line-start justify:
        // a,b is one edge and the other run is offset a full gap to one side.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 perp = (e.b - e.a).normalized().perp();
        if (e.meta.value("justify").toString() == QLatin1String("side")) {
            const Vec2 off = perp * gap;
            p.drawLine(vt.toScreen(e.a), vt.toScreen(e.b));
            p.drawLine(vt.toScreen(e.a + off), vt.toScreen(e.b + off));
        } else {
            const Vec2 off = perp * (gap / 2.0);
            p.drawLine(vt.toScreen(e.a + off), vt.toScreen(e.b + off));
            p.drawLine(vt.toScreen(e.a - off), vt.toScreen(e.b - off));
        }
        break;
    }

    case EntityType::Spline:
        // Fit (default) passes through the points; CV is a B-spline shaped by them.
        if (e.points.size() >= 3) {
            const bool cv = e.meta.value("splineMode").toString() == QLatin1String("cv");
            p.drawPolyline(toScreenPoly(cv ? bsplinePoints(e.points) : splinePoints(e.points), vt));
        } else if (e.points.size() == 2) {
            p.drawLine(vt.toScreen(e.points[0]), vt.toScreen(e.points[1]));
        }
        break;

    case EntityType::Polyline:
        if (e.points.size() >= 2) {
            const double cr = e.meta.value("cornerRadius").toDouble(0.0);
            const QJsonArray bulgeArr = e.meta.value("bulges").toArray();
            if (e.type == EntityType::Polyline && e.meta.value("double").toBool()) {
                // Round the CENTERLINE first, THEN offset both sides. The offset of
                // an arc is a concentric arc, so the perpendicular gap stays constant
                // through the bends — the two runs make equal, even turns (offsetting
                // first and rounding each run with the same radius does not).
                const double gap = e.meta.value("gap").toDouble(10.0);
                // Line-start justify: the stored path is one edge, so the geometric
                // center sits gap/2 to one side; otherwise the path IS the center.
                const std::vector<Vec2> base =
                    (e.meta.value("justify").toString() == QLatin1String("side"))
                        ? offsetPolyline(e.points, gap / 2.0)
                        : e.points;
                // Round the centerline at cr + gap/2 so BOTH runs fillet: the inner
                // run gets radius cr (>0), the outer cr+gap — both rounded, constant
                // gap (rounding at just cr collapses the inner knee when cr<=gap/2).
                const std::vector<Vec2> center =
                    (cr > 0.0) ? roundedPolyline(base, cr + gap / 2.0) : base;
                p.drawPolyline(toScreenPoly(offsetPolyline(center, gap / 2.0), vt));
                p.drawPolyline(toScreenPoly(offsetPolyline(center, -gap / 2.0), vt));
            } else if (!bulgeArr.isEmpty()) {
                // Arc segments (PLINE arc mode): expand bulges into a curved run.
                std::vector<double> bulges;
                for (const auto& v : bulgeArr) bulges.push_back(v.toDouble());
                p.drawPolyline(toScreenPoly(expandBulgePolyline(e.points, bulges), vt));
            } else if (e.type == EntityType::Polyline && cr > 0.0) {
                p.drawPolyline(toScreenPoly(roundedPolyline(e.points, cr), vt));
            } else {
                p.drawPolyline(toScreenPoly(e.points, vt));
            }
        }
        break;

    case EntityType::Wire: {
        if (e.points.size() < 2) break;
        p.drawPolyline(toScreenPoly(e.points, vt));
        // Net label at the polyline midpoint segment.
        const QString net = e.meta.value("netName").toString();
        if (!net.isEmpty() && e.meta.value("showLabel").toBool(true)) {
            const Vec2 mid = lerp(e.points[(e.points.size() - 1) / 2],
                                  e.points[e.points.size() / 2], 0.5);
            drawWorldText(p, vt.toScreen(mid) - QPointF(0, 4), net, 3.0, vt.scale(),
                          color.lighter(140));
        }
        break;
    }

    case EntityType::Rect:
    case EntityType::Polygon:
        if (e.points.size() >= 2)
            p.drawPolygon(toScreenPoly(e.points, vt));
        break;

    case EntityType::Hatch: {
        QPainterPath path;
        if (e.meta.value("shape").toString() == QLatin1String("circle")) {
            const QPointF cc = vt.toScreen(e.center);
            const double r = e.radius * vt.scale();
            path.addEllipse(cc, r, r);
        } else {
            if (e.points.size() < 3) break;
            path.addPolygon(toScreenPoly(e.points, vt));
            path.closeSubpath();
        }
        drawHatch(p, path, e, color, vt);
        // A faint boundary so an empty/low-density hatch is still selectable.
        if (overrideColor.isValid()) {
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        break;
    }

    case EntityType::Wipeout: {
        if (e.points.size() < 3) break;
        const QPolygonF poly = toScreenPoly(e.points, vt);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(g_bgColor); // opaque mask matching the canvas background
        p.drawPolygon(poly);
        p.restore();
        // Thin frame so the (otherwise invisible) mask can be seen and picked.
        QPen frame(overrideColor.isValid() ? color : QColor(120, 130, 145));
        frame.setCosmetic(true);
        frame.setWidthF(overrideColor.isValid() ? 2.4 : 1.0);
        p.setPen(frame);
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(poly);
        break;
    }

    case EntityType::Circle: {
        const QPointF c = vt.toScreen(e.center);
        const double r = e.radius * vt.scale();
        p.drawEllipse(c, r, r);
        break;
    }

    case EntityType::Arc: {
        QPainterPath path;
        appendArc(path, e.center, e.radius, e.startAngle, e.endAngle, vt, true);
        p.drawPath(path);
        break;
    }

    case EntityType::Ellipse: {
        if (e.meta.value("ellipticalArc").toBool()) {
            // Partial ellipse (elliptical arc): sample the rotated parametric curve
            // from startAngle to endAngle (CCW) in world space.
            const double cc = std::cos(e.rotation), ss = std::sin(e.rotation);
            auto wpt = [&](double t) {
                const double lx = e.radius * std::cos(t), ly = e.radiusY * std::sin(t);
                return vt.toScreen({e.center.x + lx * cc - ly * ss, e.center.y + lx * ss + ly * cc});
            };
            double sweep = e.endAngle - e.startAngle;
            while (sweep <= 0.0) sweep += 2.0 * M_PI;
            const int steps = std::max(8, static_cast<int>(std::ceil(sweep / (M_PI / 60.0))));
            QPainterPath path;
            for (int i = 0; i <= steps; ++i) {
                const QPointF sp = wpt(e.startAngle + sweep * i / steps);
                if (i == 0) path.moveTo(sp);
                else path.lineTo(sp);
            }
            p.drawPath(path);
        } else {
            const QPointF c = vt.toScreen(e.center);
            p.save();
            p.translate(c);
            p.rotate(-qRadiansToDegrees(e.rotation)); // screen Y is flipped
            p.drawEllipse(QPointF(0, 0), e.radius * vt.scale(), e.radiusY * vt.scale());
            p.restore();
        }
        break;
    }

    case EntityType::Text: {
        const QString text = e.meta.value("text").toString();
        if (text.isEmpty()) break;
        const double heightWorld = e.meta.value("size").toDouble(
            e.meta.value("height").toDouble(2.5));
        const QPointF anchor = vt.toScreen(e.a);
        QFont f = p.font();
        const QString family = e.meta.value("font").toString();
        if (!family.isEmpty()) f.setFamily(family);
        f.setBold(e.meta.value("bold").toBool());
        f.setItalic(e.meta.value("italic").toBool());
        f.setUnderline(e.meta.value("underline").toBool());
        f.setStrikeOut(e.meta.value("strike").toBool());
        f.setPixelSize(std::max(1, static_cast<int>(heightWorld * vt.scale())));
        p.setFont(f);
        p.setPen(color);
        const QFontMetricsF fm(f);
        const double spacing = e.meta.value("lineSpacing").toDouble(1.0);
        const double lineH = fm.height() * (spacing > 0 ? spacing : 1.0);
        const double boxWpx = e.meta.value("boxWidth").toDouble(0.0) * vt.scale();
        const double boxHpx = e.meta.value("boxHeight").toDouble(0.0) * vt.scale();

        // Justification: "TL".."BR" (vertical T/M/B + horizontal L/C/R). Fall back
        // to the older "align" (left/center/right) when no justify is set.
        QString just = e.meta.value("justify").toString();
        if (just.size() < 2) {
            const QString al = e.meta.value("align").toString();
            just = QStringLiteral("T") + (al == QLatin1String("center") ? QStringLiteral("C")
                                          : al == QLatin1String("right") ? QStringLiteral("R")
                                                                         : QStringLiteral("L"));
        }
        const QChar vJust = just[0], hJust = just[1];

        // Bullets / numbering: prefix each paragraph; then word-wrap to the box.
        const QString list = e.meta.value("list").toString();
        QStringList lines;
        int num = 1;
        for (const QString& para : text.split(QLatin1Char('\n'))) {
            QString full = para;
            if (list == QLatin1String("bullet")) full = QString::fromUtf8("• ") + para;
            else if (list == QLatin1String("number")) full = QString::number(num++) + ". " + para;
            if (boxWpx <= 1.0) { lines << full; continue; }
            QString cur;
            for (const QString& w : full.split(QLatin1Char(' '))) {
                const QString test = cur.isEmpty() ? w : cur + QLatin1Char(' ') + w;
                if (!cur.isEmpty() && fm.horizontalAdvance(test) > boxWpx) {
                    lines << cur;
                    cur = w;
                } else {
                    cur = test;
                }
            }
            lines << cur;
        }

        // Vertical placement within the container (screen Y grows downward; anchor
        // is the box top). T = top, M = centre, B = bottom of the box.
        const double totalH = lineH * lines.size();
        const double availH = boxHpx > 1.0 ? boxHpx : totalH;
        double blockTop = anchor.y();
        if (vJust == QLatin1Char('M')) blockTop += (availH - totalH) / 2.0;
        else if (vJust == QLatin1Char('B')) blockTop += availH - totalH;
        double y = blockTop + fm.ascent();
        for (const QString& ln : lines) {
            const double lw = fm.horizontalAdvance(ln);
            const double avail = boxWpx > 1.0 ? boxWpx : 0.0;
            double x = anchor.x();
            if (hJust == QLatin1Char('C')) x += (avail - lw) / 2.0;
            else if (hJust == QLatin1Char('R')) x += avail - lw;
            p.drawText(QPointF(x, y), ln);
            y += lineH;
        }
        break;
    }

    case EntityType::Dimension: {
        const QPointF a = vt.toScreen(e.a), b = vt.toScreen(e.b);
        p.drawLine(a, b);
        // Small tick marks and a length label.
        const QString label = QStringLiteral("%1").arg(distance(e.a, e.b), 0, 'f', g_dimDecimals);
        drawWorldText(p, QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0 - 4),
                      label, 3.0, vt.scale(), color, Qt::AlignHCenter);
        break;
    }

    case EntityType::Connector: {
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        // Body.
        const QPointF lo = vt.toScreen(g.rectMin), hi = vt.toScreen(g.rectMax);
        p.drawRect(QRectF(lo, hi).normalized());
        // Pins (fork + stub) and labels.
        for (int i = 0; i < spec.pinCount; ++i) {
            p.drawLine(vt.toScreen(g.pinTip[i]), vt.toScreen(g.pinEnd[i]));
            const QString lbl = i < spec.pins.size() ? spec.pins[i] : QString::number(i + 1);
            drawWorldText(p, vt.toScreen(g.pinLabelPos[i]) + QPointF(-6, 3), lbl,
                          spec.textSize * 0.7, vt.scale(), color);
        }
        drawWorldText(p, vt.toScreen(g.designatorPos), spec.designator, spec.textSize,
                      vt.scale(), color, Qt::AlignHCenter);
        drawWorldText(p, vt.toScreen(g.partPos), spec.partNumber, spec.textSize * 0.8,
                      vt.scale(), color);
        p.setPen(pen);
        break;
    }

    case EntityType::Splice: {
        const QPointF c = vt.toScreen(e.a);
        p.setBrush(color);
        p.drawEllipse(c, 4.0, 4.0);
        p.setBrush(Qt::NoBrush);
        break;
    }

    case EntityType::NotConnected: {
        const QPointF c = vt.toScreen(e.a);
        const double s = 5.0;
        p.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
        p.drawLine(c + QPointF(s, -s), c + QPointF(-s, s));
        break;
    }

    case EntityType::NetLabel: {
        const QString txt = e.meta.value("text").toString(e.meta.value("netName").toString());
        drawWorldText(p, vt.toScreen(e.a), txt, 4.0, vt.scale(), color);
        break;
    }

    case EntityType::Breakout:
    case EntityType::Bundle:
    default:
        if (e.points.size() >= 2)
            p.drawPolyline(toScreenPoly(e.points, vt));
        break;
    }
}

} // namespace Edraw_Desktop
