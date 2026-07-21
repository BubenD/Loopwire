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
bool g_lengthInch = false;   // dimension unit: false = mm (world), true = inch
bool g_dualUnits = false;    // append the other unit in parentheses
}
void EntityRenderer::setDimensionDecimals(int d) { g_dimDecimals = std::clamp(d, 0, 6); }
int EntityRenderer::dimensionDecimals() { return g_dimDecimals; }
void EntityRenderer::setBackgroundColor(const QColor& c) { g_bgColor = c; }
QColor EntityRenderer::backgroundColor() { return g_bgColor; }
void EntityRenderer::setLengthUnitInch(bool inch) { g_lengthInch = inch; }
bool EntityRenderer::lengthUnitInch() { return g_lengthInch; }
void EntityRenderer::setDualUnits(bool on) { g_dualUnits = on; }
bool EntityRenderer::dualUnits() { return g_dualUnits; }

QString EntityRenderer::formatLength(double world, int decimals) {
    const int dec = decimals >= 0 ? decimals : g_dimDecimals;
    const double mm = world;         // world unit == 1 mm
    const double in = world / 25.4;  // inch conversion
    const QString mmStr = QStringLiteral("%1").arg(mm, 0, 'f', dec);
    const QString inStr = QStringLiteral("%1\"").arg(in, 0, 'f', dec);
    if (g_lengthInch)
        return g_dualUnits ? QStringLiteral("%1 (%2)").arg(inStr, mmStr) : inStr;
    return g_dualUnits ? QStringLiteral("%1 (%2)").arg(mmStr, inStr) : mmStr;
}

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
    QPointF pos = at;
    if (align & (Qt::AlignHCenter | Qt::AlignRight | Qt::AlignVCenter)) {
        const QFontMetricsF fm(f);
        if (align & Qt::AlignHCenter) pos.setX(at.x() - fm.horizontalAdvance(text) / 2.0);
        else if (align & Qt::AlignRight) pos.setX(at.x() - fm.horizontalAdvance(text));
        // Centre the glyph body on `at.y` (baseline sits below the visual middle).
        if (align & Qt::AlignVCenter) pos.setY(at.y() + (fm.ascent() - fm.descent()) / 2.0);
    }
    p.drawText(pos, text);
}

// "ls" line-start: read the stored reference line, if any.
bool lsReferenceLine(const Entity& e, Vec2& a, Vec2& b) {
    if (!e.meta.value("lsRef").toBool()) return false;
    a = {e.meta.value("lsRefAx").toDouble(), e.meta.value("lsRefAy").toDouble()};
    b = {e.meta.value("lsRefBx").toDouble(), e.meta.value("lsRefBy").toDouble()};
    return true;
}
// Move a run's start vertex onto the reference line (extend along its first segment).
void extendRunStartToLine(std::vector<Vec2>& run, const Vec2& a, const Vec2& b) {
    if (run.size() < 2) return;
    if (auto x = lineLineIntersection(run[0], run[1], a, b)) run[0] = *x;
}
// Move a run's END vertex onto the reference line (along its last segment), so a
// trimmed double line's physical runs meet the cutting line exactly (diagonal cap).
void extendRunEndToLine(std::vector<Vec2>& run, const Vec2& a, const Vec2& b) {
    const int n = static_cast<int>(run.size());
    if (n < 2) return;
    if (auto x = lineLineIntersection(run[n - 1], run[n - 2], a, b)) run[n - 1] = *x;
}
// Trim a run's endpoint to where the run crosses the circle, choosing the crossing
// nearest the KEPT-side neighbour vertex (i.e. where the run enters the ring coming
// from the part that stays). Picking by the neighbour — not by the endpoint — cuts
// BOTH physical runs at their true crossing and never extends a run across the ring
// (which had left one of the two lines uncut). Each line is trimmed at its own
// intersection, so the constant perpendicular gap is preserved.
void extendRunEndToCircle(std::vector<Vec2>& run, const Vec2& c, double r) {
    const int n = static_cast<int>(run.size());
    if (n < 2) return;
    const Vec2 keep = run[n - 2];
    const auto xs = lineCircleIntersection(keep, run[n - 1] - keep, c, r);
    double best = 1e299;
    for (const auto& x : xs) {
        const double d = distance(x, keep);
        if (d < best) { best = d; run[n - 1] = x; }
    }
}
void extendRunStartToCircle(std::vector<Vec2>& run, const Vec2& c, double r) {
    if (run.size() < 2) return;
    const Vec2 keep = run[1];
    const auto xs = lineCircleIntersection(keep, run[0] - keep, c, r);
    double best = 1e299;
    for (const auto& x : xs) {
        const double d = distance(x, keep);
        if (d < best) { best = d; run[0] = x; }
    }
}
// Clip a run's END onto whichever of the target EDGES its line crosses nearest the
// endpoint — so each of a double line's two runs meets the edge it actually reaches
// (a corner/vertex never leaves one run short).
void extendRunEndToPoly(std::vector<Vec2>& run, const QJsonArray& poly) {
    const int n = static_cast<int>(run.size());
    if (n < 2) return;
    const Vec2 from = run[n - 2], to = run[n - 1];
    double best = 1e299;
    Vec2 bestPt = to;
    bool found = false;
    for (int i = 0; i + 3 < poly.size(); i += 4) {
        const Vec2 a{poly[i].toDouble(), poly[i + 1].toDouble()};
        const Vec2 b{poly[i + 2].toDouble(), poly[i + 3].toDouble()};
        auto x = lineLineIntersection(from, to, a, b);
        if (!x) continue;
        if (closestPointOnSegment(*x, a, b).distance > 1e-6) continue; // on the finite edge
        const double d = distance(*x, to);
        if (d < best) { best = d; bestPt = *x; found = true; }
    }
    if (found) run[n - 1] = bestPt;
}
void extendRunStartToPoly(std::vector<Vec2>& run, const QJsonArray& poly) {
    if (run.size() < 2) return;
    const Vec2 from = run[1], to = run[0];
    double best = 1e299;
    Vec2 bestPt = to;
    bool found = false;
    for (int i = 0; i + 3 < poly.size(); i += 4) {
        const Vec2 a{poly[i].toDouble(), poly[i + 1].toDouble()};
        const Vec2 b{poly[i + 2].toDouble(), poly[i + 3].toDouble()};
        auto x = lineLineIntersection(from, to, a, b);
        if (!x) continue;
        if (closestPointOnSegment(*x, a, b).distance > 1e-6) continue;
        const double d = distance(*x, to);
        if (d < best) { best = d; bestPt = *x; found = true; }
    }
    if (found) run[0] = bestPt;
}
// Clip a run's start/end onto the stored cut boundary (a line, circle, or edge set).
void applyRunCut(std::vector<Vec2>& run, const Entity& e, const QString& key, bool atEnd) {
    if (!e.meta.value(key).toBool()) return;
    const QString kind = e.meta.value(key + "Kind").toString();
    if (kind == QLatin1String("circle")) {
        const Vec2 c{e.meta.value(key + "Cx").toDouble(), e.meta.value(key + "Cy").toDouble()};
        const double r = e.meta.value(key + "R").toDouble();
        if (atEnd) extendRunEndToCircle(run, c, r);
        else extendRunStartToCircle(run, c, r);
    } else if (kind == QLatin1String("poly")) {
        const QJsonArray poly = e.meta.value(key + "Poly").toArray();
        if (atEnd) extendRunEndToPoly(run, poly);
        else extendRunStartToPoly(run, poly);
    } else {
        const Vec2 a{e.meta.value(key + "Ax").toDouble(), e.meta.value(key + "Ay").toDouble()};
        const Vec2 b{e.meta.value(key + "Bx").toDouble(), e.meta.value(key + "By").toDouble()};
        if (atEnd) extendRunEndToLine(run, a, b);
        else extendRunStartToLine(run, a, b);
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

// A small filled arrowhead at screen point `tip`, pointing along `dir` (unit).
void drawArrowHead(QPainter& p, const QPointF& tip, const QPointF& dir, const QColor& c,
                   double len = 9.0) {
    const double wide = len * 0.36;
    const QPointF n(-dir.y(), dir.x());
    const QPointF base = tip - dir * len;
    QPolygonF tri;
    tri << tip << (base + n * wide) << (base - n * wide);
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(tri);
    p.restore();
}

// Draw a leader arrowhead of the given TYPE at `tip`, pointing along `dir` (unit).
// Types mirror the AutoCAD arrowhead list (a practical subset is drawn distinctly;
// the rest fall back to the closed-filled triangle).
void drawLeaderArrow(QPainter& p, const QPointF& tip, const QPointF& dir, const QColor& c,
                     const QString& type, double len) {
    if (type == QLatin1String("none")) return;
    const QPointF n(-dir.y(), dir.x());
    if (type == QLatin1String("dot")) {
        p.save(); p.setPen(Qt::NoPen); p.setBrush(c);
        p.drawEllipse(tip, len * 0.32, len * 0.32);
        p.restore();
        return;
    }
    if (type == QLatin1String("tick")) { // architectural / oblique slash across the tip
        p.save();
        QPen pen(c); pen.setCosmetic(true); pen.setWidthF(1.8); p.setPen(pen);
        p.drawLine(tip - dir * (len * 0.5) - n * (len * 0.5),
                   tip + dir * (len * 0.5) + n * (len * 0.5));
        p.restore();
        return;
    }
    if (type == QLatin1String("open")) { // unfilled V
        const QPointF base = tip - dir * len;
        p.save();
        QPen pen(c); pen.setCosmetic(true); pen.setWidthF(1.8); p.setPen(pen); p.setBrush(Qt::NoBrush);
        p.drawLine(tip, base + n * (len * 0.4));
        p.drawLine(tip, base - n * (len * 0.4));
        p.restore();
        return;
    }
    if (type == QLatin1String("blank")) { // closed-but-hollow triangle
        const QPointF base = tip - dir * len;
        QPolygonF tri; tri << tip << (base + n * len * 0.36) << (base - n * len * 0.36);
        p.save();
        QPen pen(c); pen.setCosmetic(true); pen.setWidthF(1.6); p.setPen(pen); p.setBrush(Qt::NoBrush);
        p.drawPolygon(tri);
        p.restore();
        return;
    }
    drawArrowHead(p, tip, dir, c, len); // "closed" (closed filled) — the default
}

// Draw any dimension entity (linear/aligned/radius/diameter/angular/ordinate/...).
void drawDimensionEntity(QPainter& p, const Entity& e, const ViewTransform& vt,
                         const QColor& color, int decimals) {
    const QString ty = e.meta.value("dimType").toString(QStringLiteral("linear"));
    // Per-dimension style (AutoCAD-like), with sensible defaults.
    const double textH = e.meta.value("textHeight").toDouble(5.0);  // world units
    const double arrowW = e.meta.value("arrowSize").toDouble(3.3);  // world units
    const int dec = e.meta.contains("decimals") ? e.meta.value("decimals").toInt() : decimals;
    const double arrowPx = std::clamp(arrowW * vt.scale(), 5.0, 40.0);
    const double extOff = e.meta.value("extOffset").toDouble(2.0); // ext-line gap (world)
    // Dimension lines are drawn thin (but clearly visible), independent of weight.
    {
        QPen dp(color);
        dp.setCosmetic(true);
        dp.setWidthF(1.2);
        p.setPen(dp);
    }
    auto unit = [](QPointF v) {
        const double L = std::hypot(v.x(), v.y());
        return L > 1e-9 ? QPointF(v.x() / L, v.y() / L) : QPointF(1, 0);
    };
    auto label = [&](const QPointF& at, const QString& s) {
        drawWorldText(p, at + QPointF(0, -4), s, textH, vt.scale(), color, Qt::AlignHCenter);
    };

    if (ty == QLatin1String("radius") || ty == QLatin1String("jogged")) {
        const QPointF c = vt.toScreen(e.center);
        const QPointF tp = vt.toScreen(e.b);
        const QPointF dir = unit(tp - c);
        const QPointF on = c + dir * (e.radius * vt.scale());
        p.drawLine(c, tp);
        drawArrowHead(p, on, dir, color, arrowPx); // tip on the ring, pointing outward
        label(tp, QStringLiteral("R%1").arg(EntityRenderer::formatLength(e.radius, dec)));
        return;
    }
    if (ty == QLatin1String("diameter")) {
        const QPointF c = vt.toScreen(e.center);
        const QPointF tp = vt.toScreen(e.b);
        const QPointF dir = unit(tp - c);
        const double r = e.radius * vt.scale();
        const QPointF a1 = c - dir * r, a2 = c + dir * r;
        p.drawLine(a1, a2);
        drawArrowHead(p, a1, QPointF(-dir.x(), -dir.y()), color, arrowPx); // ring, outward
        drawArrowHead(p, a2, dir, color, arrowPx);
        label(tp, QString::fromUtf8("\xC3\x98%1").arg(EntityRenderer::formatLength(e.radius * 2.0, dec)));
        return;
    }
    if (ty == QLatin1String("angular") && e.points.size() >= 3) {
        const Vec2 v = e.points[0], A = e.points[1], B = e.points[2];
        const double a0 = std::atan2(A.y - v.y, A.x - v.x);
        const double a1 = std::atan2(B.y - v.y, B.x - v.x);
        const double rr = std::min(distance(v, A), distance(v, B)) * 0.6;
        // faint rays
        p.drawLine(vt.toScreen(v), vt.toScreen(A));
        p.drawLine(vt.toScreen(v), vt.toScreen(B));
        QPainterPath arc;
        appendArc(arc, v, rr, a0, a1, vt, true);
        p.drawPath(arc);
        double deg = (a1 - a0) * 180.0 / M_PI;
        while (deg < 0) deg += 360.0;
        while (deg > 360) deg -= 360.0;
        const double am = a0 + (a1 - a0) * 0.5;
        label(vt.toScreen(pointOnCircle(v, rr, am)),
              QStringLiteral("%1\xC2\xB0").arg(deg, 0, 'f', 1));
        return;
    }
    if (ty == QLatin1String("ordinate")) {
        const QPointF f = vt.toScreen(e.a), le = vt.toScreen(e.b);
        p.drawLine(f, le);
        const bool xOrd = std::abs(e.b.x - e.a.x) >= std::abs(e.b.y - e.a.y);
        label(le, EntityRenderer::formatLength(xOrd ? e.a.x : e.a.y, dec));
        return;
    }

    // ---- linear / aligned / arclength: extension lines + dim line + arrows ----
    const Vec2 p1 = e.a, p2 = e.b;
    Vec2 off = e.points.size() >= 3 ? e.points[2] : lerp(p1, p2, 0.5);
    Vec2 d1, d2;
    double measure = 0.0;
    if (ty == QLatin1String("linear")) {
        const Vec2 mid = lerp(p1, p2, 0.5);
        const bool horiz = std::abs(off.y - mid.y) >= std::abs(off.x - mid.x);
        if (horiz) { d1 = {p1.x, off.y}; d2 = {p2.x, off.y}; measure = std::abs(p2.x - p1.x); }
        else { d1 = {off.x, p1.y}; d2 = {off.x, p2.y}; measure = std::abs(p2.y - p1.y); }
    } else { // aligned (and arclength as a chord)
        const Vec2 dir = (p2 - p1).normalized();
        const Vec2 n = dir.perp();
        const double t = dot(off - p1, n);
        d1 = p1 + n * t;
        d2 = p2 + n * t;
        measure = distance(p1, p2);
    }
    const QPointF s1 = vt.toScreen(p1), s2 = vt.toScreen(p2);
    const QPointF e1 = vt.toScreen(d1), e2 = vt.toScreen(d2);
    // AutoCAD-style extension lines: a gap from the measured point (the editable
    // "Ext line offset", world units) and a short overshoot past the dimension line.
    const double exGap = std::max(0.0, extOff * vt.scale());
    const double exBeyond = std::max(2.0, extOff * vt.scale() * 0.6);
    const QPointF x1 = unit(e1 - s1), x2 = unit(e2 - s2);
    p.drawLine(s1 + x1 * exGap, e1 + x1 * exBeyond);
    p.drawLine(s2 + x2 * exGap, e2 + x2 * exBeyond);
    p.drawLine(e1, e2); // dimension line
    // Arrowheads point OUTWARD, tips touching the extension lines.
    const QPointF du = unit(e2 - e1);
    drawArrowHead(p, e1, QPointF(-du.x(), -du.y()), color, arrowPx);
    drawArrowHead(p, e2, du, color, arrowPx);
    // Text centred on the dimension line, nudged to the outer side (away from the
    // measured geometry) so it sits clear of the line.
    const QPointF dimMid((e1.x() + e2.x()) / 2.0, (e1.y() + e2.y()) / 2.0);
    const QPointF measMid((s1.x() + s2.x()) / 2.0, (s1.y() + s2.y()) / 2.0);
    const QPointF nOut = unit(dimMid - measMid);
    const QPointF txtAt = dimMid + nOut * (textH * vt.scale() * 0.5 + 3.0);
    const QString txt = (ty == QLatin1String("arclength"))
                            ? QString::fromUtf8("\xE2\x8C\x92%1").arg(EntityRenderer::formatLength(measure, dec))
                            : EntityRenderer::formatLength(measure, dec);
    // If the text was dragged off the dimension line, draw it there and connect it
    // back to the line's midpoint with a dashed leader (AutoCAD-style).
    if (e.meta.contains("textPosX")) {
        const QPointF moved = vt.toScreen(
            {e.meta.value("textPosX").toDouble(), e.meta.value("textPosY").toDouble()});
        QPen dash(color);
        dash.setCosmetic(true);
        dash.setWidthF(1.0);
        dash.setStyle(Qt::DashLine);
        p.save();
        p.setPen(dash);
        p.drawLine(dimMid, moved);
        p.restore();
        drawWorldText(p, moved + QPointF(0, -2), txt, textH, vt.scale(), color, Qt::AlignHCenter);
    } else {
        drawWorldText(p, txtAt, txt, textH, vt.scale(), color, Qt::AlignHCenter);
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
        // Two parallel lines, +/- gap/2 about the clicked line a-b. "ls" extends both
        // runs so they start on a clicked reference line.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 perp = (e.b - e.a).normalized().perp();
        const Vec2 off = perp * (gap / 2.0);
        std::vector<Vec2> run1 = {e.a + off, e.b + off};
        std::vector<Vec2> run2 = {e.a - off, e.b - off};
        Vec2 ra, rb;
        if (lsReferenceLine(e, ra, rb)) {
            extendRunStartToLine(run1, ra, rb);
            extendRunStartToLine(run2, ra, rb);
        }
        // Trim caps: clip both physical lines onto the cut boundary (line or circle)
        // so a trimmed double line ends flush with the reference, not a perpendicular
        // cap that over/under-shoots it.
        applyRunCut(run1, e, QStringLiteral("cutStart"), false);
        applyRunCut(run2, e, QStringLiteral("cutStart"), false);
        applyRunCut(run1, e, QStringLiteral("cutEnd"), true);
        applyRunCut(run2, e, QStringLiteral("cutEnd"), true);
        p.drawLine(vt.toScreen(run1[0]), vt.toScreen(run1[1]));
        p.drawLine(vt.toScreen(run2[0]), vt.toScreen(run2[1]));
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
            // Per-corner fillet radii (newer drawings). Falls back to the uniform
            // cornerRadius for older files. radii[i] = fillet radius at vertex i.
            std::vector<double> radii(e.points.size(), cr);
            bool anyFillet = cr > 0.0;
            if (e.meta.contains("cornerRadii")) {
                const QJsonArray rArr = e.meta.value("cornerRadii").toArray();
                radii.assign(e.points.size(), 0.0);
                anyFillet = false;
                for (int i = 0; i < rArr.size() && i < static_cast<int>(radii.size()); ++i) {
                    radii[i] = rArr[i].toDouble();
                    if (radii[i] > 0.0) anyFillet = true;
                }
            }
            // Per-corner chamfer setbacks (mutually exclusive with a fillet per vertex).
            // cornerChamfers = setback toward the previous vertex ("in"), cornerChamfers2
            // = toward the next ("out"); if the second is absent the chamfer is symmetric.
            std::vector<double> chamfersIn(e.points.size(), 0.0);
            std::vector<double> chamfersOut(e.points.size(), 0.0);
            bool anyChamfer = false;
            if (e.meta.contains("cornerChamfers")) {
                const QJsonArray cArr = e.meta.value("cornerChamfers").toArray();
                const QJsonArray cArr2 = e.meta.value("cornerChamfers2").toArray();
                for (int i = 0; i < cArr.size() && i < static_cast<int>(chamfersIn.size()); ++i) {
                    chamfersIn[i] = cArr[i].toDouble();
                    chamfersOut[i] = (i < cArr2.size()) ? cArr2[i].toDouble() : chamfersIn[i];
                    if (chamfersIn[i] > 0.0 && chamfersOut[i] > 0.0) anyChamfer = true;
                }
            }
            const QJsonArray bulgeArr = e.meta.value("bulges").toArray();
            if (e.type == EntityType::Polyline && e.meta.value("double").toBool()) {
                // Round the CENTERLINE first, THEN offset both sides. The offset of
                // an arc is a concentric arc, so the perpendicular gap stays constant
                // through the bends — the two runs make equal, even turns (offsetting
                // first and rounding each run with the same radius does not).
                const double gap = e.meta.value("gap").toDouble(10.0);
                // The clicked path is always the centerline; both runs are offset
                // +/- gap/2 from it. "ls" (justify=side) just adds start/end caps so
                // both runs start from the clicked reference line.
                // Round each filleted corner of the centerline at radius + gap/2 so
                // BOTH runs fillet: the inner run gets the requested radius, the outer
                // radius+gap — both rounded with a constant gap through the bend.
                std::vector<double> centerRadii(radii.size(), 0.0);
                for (size_t i = 0; i < radii.size(); ++i)
                    if (radii[i] > 0.0) centerRadii[i] = radii[i] + gap / 2.0;
                // Chamfer the centerline too (offsetting a straight cut keeps the gap).
                const std::vector<Vec2> center =
                    (anyFillet || anyChamfer)
                        ? filletChamferPolyline(e.points, centerRadii, chamfersIn, chamfersOut,
                                                false)
                        : e.points;
                std::vector<Vec2> run1 = offsetPolyline(center, gap / 2.0);
                std::vector<Vec2> run2 = offsetPolyline(center, -gap / 2.0);
                // Line-start (ls): extend BOTH physical runs so they start on the
                // clicked reference line (no closing caps).
                Vec2 ra, rb;
                if (lsReferenceLine(e, ra, rb)) {
                    extendRunStartToLine(run1, ra, rb);
                    extendRunStartToLine(run2, ra, rb);
                }
                // Trim cut boundaries: clip both physical runs so they meet the
                // cutting edge (line OR circle) exactly, instead of a perpendicular
                // cap that over/under-shoots it.
                applyRunCut(run1, e, QStringLiteral("cutStart"), false);
                applyRunCut(run2, e, QStringLiteral("cutStart"), false);
                applyRunCut(run1, e, QStringLiteral("cutEnd"), true);
                applyRunCut(run2, e, QStringLiteral("cutEnd"), true);
                p.drawPolyline(toScreenPoly(run1, vt));
                p.drawPolyline(toScreenPoly(run2, vt));
            } else if (!bulgeArr.isEmpty()) {
                // Arc segments (PLINE arc mode): expand bulges into a curved run.
                std::vector<double> bulges;
                for (const auto& v : bulgeArr) bulges.push_back(v.toDouble());
                p.drawPolyline(toScreenPoly(expandBulgePolyline(e.points, bulges), vt));
            } else if (e.type == EntityType::Polyline && (anyFillet || anyChamfer)) {
                p.drawPolyline(toScreenPoly(
                    filletChamferPolyline(e.points, radii, chamfersIn, chamfersOut, false), vt));
            } else {
                p.drawPolyline(toScreenPoly(e.points, vt));
            }
        }
        break;

    case EntityType::Wire: {
        if (e.points.size() < 2) break;
        // Per-crossing "bridge" hops (Cross switch tool): points on this wire where it
        // hops OVER a crossing wire with a small semicircle instead of a plain cross.
        std::vector<Vec2> hops;
        for (const auto& h : e.meta.value("hops").toArray()) {
            const QJsonArray a = h.toArray();
            if (a.size() == 2) hops.push_back({a[0].toDouble(), a[1].toDouble()});
        }
        if (hops.empty()) {
            p.drawPolyline(toScreenPoly(e.points, vt));
        } else {
            const double r = 5.0 * vt.scale(); // hop radius (≈5 world units)
            QPainterPath path;
            path.moveTo(vt.toScreen(e.points[0]));
            for (size_t s = 1; s < e.points.size(); ++s) {
                const QPointF a0 = vt.toScreen(e.points[s - 1]);
                const QPointF a1 = vt.toScreen(e.points[s]);
                const QPointF d = a1 - a0;
                const double L = std::hypot(d.x(), d.y());
                if (L < 1e-6) { path.lineTo(a1); continue; }
                const QPointF dir(d.x() / L, d.y() / L);
                const QPointF perp(-dir.y(), dir.x());
                std::vector<double> ts; // hop centres as distance along this segment
                for (const Vec2& h : hops) {
                    const QPointF hs = vt.toScreen(h);
                    const double t = QPointF::dotProduct(hs - a0, dir);
                    const double pd = std::abs(QPointF::dotProduct(hs - a0, perp));
                    if (pd < 1.5 && t > r && t < L - r) ts.push_back(t);
                }
                std::sort(ts.begin(), ts.end());
                for (double t : ts) {
                    const QPointF hc = a0 + dir * t;
                    path.lineTo(hc - dir * r); // up to the hop entry
                    const int N = 10;          // semicircle bulging to +perp
                    for (int k = 1; k <= N; ++k) {
                        const double th = M_PI - M_PI * k / N;
                        path.lineTo(hc + dir * (r * std::cos(th)) + perp * (r * std::sin(th)));
                    }
                }
                path.lineTo(a1);
            }
            p.drawPath(path);
        }
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
        if (e.points.size() >= 2) {
            // Per-corner fillets / chamfers (set by the Fillet / Chamfer tool) shape
            // the closed outline.
            if (e.meta.contains("cornerRadii") || e.meta.contains("cornerChamfers")) {
                std::vector<double> radii(e.points.size(), 0.0);
                std::vector<double> chamfersIn(e.points.size(), 0.0);
                std::vector<double> chamfersOut(e.points.size(), 0.0);
                bool any = false;
                const QJsonArray rArr = e.meta.value("cornerRadii").toArray();
                for (int i = 0; i < rArr.size() && i < static_cast<int>(radii.size()); ++i) {
                    radii[i] = rArr[i].toDouble();
                    if (radii[i] > 0.0) any = true;
                }
                const QJsonArray cArr = e.meta.value("cornerChamfers").toArray();
                const QJsonArray cArr2 = e.meta.value("cornerChamfers2").toArray();
                for (int i = 0; i < cArr.size() && i < static_cast<int>(chamfersIn.size()); ++i) {
                    chamfersIn[i] = cArr[i].toDouble();
                    chamfersOut[i] = (i < cArr2.size()) ? cArr2[i].toDouble() : chamfersIn[i];
                    if (chamfersIn[i] > 0.0 && chamfersOut[i] > 0.0) any = true;
                }
                if (any) {
                    p.drawPolygon(toScreenPoly(
                        filletChamferPolyline(e.points, radii, chamfersIn, chamfersOut, true), vt));
                    break;
                }
            }
            // Editable table: outer border + grid + per-cell text, with an optional
            // merged Title row and a bold Header row (AutoCAD Insert Table styles).
            if (e.meta.value("table").toBool() && e.points.size() >= 4) {
                const Bounds tb = boundsOf(e.points);
                const QJsonArray cf = e.meta.value("colFrac").toArray();
                const QJsonArray rf = e.meta.value("rowFrac").toArray();
                const QJsonArray cells = e.meta.value("cells").toArray();
                const int cols = cf.size(), rows = rf.size();
                const bool hasTitle = e.meta.value("titleRow").toBool();
                const bool hasHeader = e.meta.value("headerRow").toBool();
                const double th = e.meta.value("textSize").toDouble(4.0);
                p.drawPolygon(toScreenPoly(e.points, vt)); // outer border
                // Column x-boundaries and row y-boundaries (top->down, world y-up).
                std::vector<double> xb(cols + 1), yb(rows + 1);
                xb[0] = tb.min.x;
                for (int c = 0; c < cols; ++c) xb[c + 1] = xb[c] + cf[c].toDouble() * tb.width();
                yb[0] = tb.max.y;
                for (int r = 0; r < rows; ++r) yb[r + 1] = yb[r] - rf[r].toDouble() * tb.height();
                for (int r = 1; r < rows; ++r)
                    p.drawLine(vt.toScreen({tb.min.x, yb[r]}), vt.toScreen({tb.max.x, yb[r]}));
                const int vTop = hasTitle ? 1 : 0; // verticals skip the merged title row
                for (int c = 1; c < cols; ++c)
                    p.drawLine(vt.toScreen({xb[c], yb[vTop]}), vt.toScreen({xb[c], yb[rows]}));
                const int headerIdx = hasTitle ? 1 : 0;
                const QFont normal = p.font();
                const QJsonArray cellAlign = e.meta.value("cellAlign").toArray();
                const double pad = th * 0.35;
                // Place `s` in the cell [x0..x1]x[yTop..yBot] per a 2-char align code
                // (T/M/B + L/C/R); default depends on the row type.
                auto placeCell = [&](double x0, double x1, double yTop, double yBot,
                                     const QString& s, const QString& codeIn, double h, bool bold) {
                    if (s.isEmpty()) return;
                    QString code = codeIn.isEmpty() ? QStringLiteral("TL") : codeIn;
                    const QChar v = code[0], hc = code.size() > 1 ? code[1] : QChar('L');
                    Qt::Alignment flags;
                    double x;
                    if (hc == 'C') { flags = Qt::AlignHCenter; x = (x0 + x1) / 2.0; }
                    else if (hc == 'R') { flags = Qt::AlignRight; x = x1 - pad; }
                    else { flags = Qt::AlignLeft; x = x0 + pad; }
                    double y;
                    if (v == 'M') { y = (yTop + yBot) / 2.0; flags |= Qt::AlignVCenter; }
                    else if (v == 'B') { y = yBot + pad; }
                    else { y = yTop - pad - h; }
                    QFont f = normal; f.setBold(bold);
                    p.save(); p.setFont(f);
                    drawWorldText(p, vt.toScreen({x, y}), s, h, vt.scale(), color, flags);
                    p.restore();
                };
                for (int r = 0; r < rows; ++r) {
                    const bool isTitle = hasTitle && r == 0;
                    const bool isHeader = hasHeader && r == headerIdx && !isTitle;
                    if (isTitle) { // one merged, bold title across all columns
                        const QString code = !cellAlign.isEmpty() && !cellAlign[0].toString().isEmpty()
                                                 ? cellAlign[0].toString() : QStringLiteral("MC");
                        placeCell(tb.min.x, tb.max.x, yb[0], yb[1],
                                  cells.isEmpty() ? QString() : cells[0].toString(), code, th * 1.2, true);
                        continue;
                    }
                    for (int c = 0; c < cols; ++c) {
                        const int idx = r * cols + c;
                        const QString s = idx < cells.size() ? cells[idx].toString() : QString();
                        QString code = idx < cellAlign.size() ? cellAlign[idx].toString() : QString();
                        if (code.isEmpty()) code = isHeader ? QStringLiteral("MC") : QStringLiteral("TL");
                        placeCell(xb[c], xb[c + 1], yb[r], yb[r + 1], s, code, th, isHeader);
                    }
                }
                break;
            }
            p.drawPolygon(toScreenPoly(e.points, vt));
            // Note / Revision annotation boxes carry text inside the rectangle.
            const bool note = e.meta.value("note").toBool();
            const bool rev = e.meta.value("revision").toBool();
            if ((note || rev) && e.points.size() >= 4) {
                const Bounds b = boundsOf(e.points);
                const double th = e.meta.value("textSize").toDouble(4.0);
                const QString body = e.meta.value("text").toString();
                double topY = b.max.y; // world y-up: top edge
                if (rev) {
                    // Title strip along the top with the revision id.
                    const double strip = std::min(th * 1.8, b.height() * 0.4);
                    const double divY = b.max.y - strip;
                    p.drawLine(vt.toScreen({b.min.x, divY}), vt.toScreen({b.max.x, divY}));
                    drawWorldText(p, vt.toScreen({(b.min.x + b.max.x) / 2.0, b.max.y - strip * 0.72}),
                                  QStringLiteral("REV %1").arg(e.meta.value("rev").toString()),
                                  th, vt.scale(), color, Qt::AlignHCenter);
                    topY = divY;
                }
                // Body text, wrapped to the box width, drawn top-down inside a small pad.
                const double pad = th * 0.4;
                QFont f = p.font();
                f.setPixelSize(std::max(1, static_cast<int>(th * vt.scale())));
                const QFontMetricsF fm(f);
                const double maxWpx = std::max(1.0, (b.width() - 2 * pad) * vt.scale());
                QStringList lines;
                for (const QString& para : body.split(QLatin1Char('\n'))) {
                    QString cur;
                    for (const QString& word : para.split(QLatin1Char(' '))) {
                        const QString trial = cur.isEmpty() ? word : cur + QLatin1Char(' ') + word;
                        if (fm.horizontalAdvance(trial) > maxWpx && !cur.isEmpty()) {
                            lines << cur;
                            cur = word;
                        } else {
                            cur = trial;
                        }
                    }
                    lines << cur;
                }
                double y = topY - pad;
                for (const QString& ln : lines) {
                    y -= th; // advance one line (world y-up)
                    if (y < b.min.y) break; // clip to the box
                    drawWorldText(p, vt.toScreen({b.min.x + pad, y}), ln, th, vt.scale(), color);
                }
            }
        }
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
        // Balloon callout: a leader with an arrow at the tip, a horizontal landing
        // into the balloon, then the numbered circle.
        if (e.meta.value("balloon").toBool()) {
            // Collect every leader tip (array form, or the legacy single leaderX/Y).
            std::vector<Vec2> tips;
            if (e.meta.contains("leaders")) {
                for (const auto v : e.meta.value("leaders").toArray()) {
                    const QJsonArray a = v.toArray();
                    if (a.size() >= 2) tips.push_back({a[0].toDouble(), a[1].toDouble()});
                }
            } else if (e.meta.contains("leaderX")) {
                tips.push_back({e.meta.value("leaderX").toDouble(),
                                e.meta.value("leaderY").toDouble()});
            }
            if (!tips.empty()) {
                const double landing = std::max(0.0, e.meta.value("landing").toDouble(8.0));
                Vec2 attach, knee;
                if (landing > 0) {
                    // Horizontal landing shelf on the side toward the tips.
                    int side;
                    if (e.meta.contains("landingSide")) {
                        side = e.meta.value("landingSide").toDouble() >= 0 ? 1 : -1;
                    } else {
                        double sx = 0;
                        for (const auto& t : tips) sx += t.x;
                        side = (sx / tips.size() >= e.center.x) ? 1 : -1;
                    }
                    attach = {e.center.x + side * e.radius, e.center.y};
                    knee = {e.center.x + side * (e.radius + landing), e.center.y};
                    p.drawLine(vt.toScreen(attach), vt.toScreen(knee)); // shelf
                } else {
                    // No shelf: the leader attaches at a free ANGLE around the ring.
                    double a;
                    if (e.meta.contains("landingAngle")) {
                        a = e.meta.value("landingAngle").toDouble();
                    } else {
                        double sx = 0, sy = 0;
                        for (const auto& t : tips) { sx += t.x; sy += t.y; }
                        a = std::atan2(sy / tips.size() - e.center.y, sx / tips.size() - e.center.x);
                    }
                    attach = {e.center.x + std::cos(a) * e.radius, e.center.y + std::sin(a) * e.radius};
                    knee = attach;
                }
                const QString ah = e.meta.value("arrowhead").toString(QStringLiteral("closed"));
                const double alen = std::clamp(e.meta.value("arrowSize").toDouble(3.3) * vt.scale(),
                                               6.0, 40.0);
                const QPointF sKnee = vt.toScreen(knee);
                for (const Vec2& tip : tips) {
                    const QPointF sTip = vt.toScreen(tip);
                    p.drawLine(sKnee, sTip); // leader line from the knee/attach to each tip
                    QPointF d = sTip - sKnee;
                    const double L = std::hypot(d.x(), d.y());
                    if (L > 1e-6) { d /= L; drawLeaderArrow(p, sTip, d, color, ah, alen); }
                }
            }
            const bool textLeader = e.meta.value("textLeader").toBool();
            if (!textLeader) p.drawEllipse(c, r, r); // a text leader has no circle
            const QString label = e.meta.value("text").toString();
            drawWorldText(p, c, label, e.radius * (textLeader ? 1.4 : 1.1), vt.scale(), color,
                          Qt::AlignHCenter | Qt::AlignVCenter);
        } else {
            p.drawEllipse(c, r, r);
        }
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

    case EntityType::Dimension:
        drawDimensionEntity(p, e, vt, color, g_dimDecimals);
        break;

    case EntityType::Connector: {
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        // Body.
        const QPointF lo = vt.toScreen(g.rectMin), hi = vt.toScreen(g.rectMax);
        p.drawRect(QRectF(lo, hi).normalized());
        // Pins: a small ">" fork ("cup") at the body edge, then the wire stub. The cup
        // keeps a FIXED default size — it does NOT grow with the pin pitch or when the
        // connector / harness is scaled, so the contact symbol stays a constant glyph.
        const double xDir = spec.mirrorX ? -1.0 : 1.0;
        constexpr double fork = 8.0;   // cup depth  (world units, default geometry)
        constexpr double forkY = 5.4;  // cup half-height (world units, default geometry)
        for (int i = 0; i < spec.pinCount; ++i) {
            const Vec2 tip = g.pinTip[i];
            const Vec2 root{tip.x - xDir * fork, tip.y};
            p.drawLine(vt.toScreen({root.x, tip.y + forkY}), vt.toScreen(tip));
            p.drawLine(vt.toScreen({root.x, tip.y - forkY}), vt.toScreen(tip));
            p.drawLine(vt.toScreen(tip), vt.toScreen(g.pinEnd[i]));
        }
        // Pin labels (own font/size).
        const QFont baseFont = p.font();
        {
            QFont f = baseFont; f.setFamily(spec.pinFont); p.setFont(f);
            for (int i = 0; i < spec.pinCount; ++i) {
                const QString lbl = i < spec.pins.size() ? spec.pins[i] : QString::number(i + 1);
                drawWorldText(p, vt.toScreen(g.pinLabelPos[i]), lbl, spec.pinSize * 0.7, vt.scale(),
                              color, Qt::AlignHCenter | Qt::AlignVCenter);
            }
        }
        {
            QFont f = baseFont; f.setFamily(spec.desFont); p.setFont(f);
            drawWorldText(p, vt.toScreen(g.designatorPos), spec.designator, spec.desSize, vt.scale(),
                          color, Qt::AlignHCenter | Qt::AlignVCenter);
        }
        {
            QFont f = baseFont; f.setFamily(spec.pnFont); p.setFont(f);
            drawWorldText(p, vt.toScreen(g.partPos), spec.partNumber, spec.pnSize * 0.8, vt.scale(),
                          color, Qt::AlignHCenter);
        }
        p.setFont(baseFont);
        p.setPen(pen);
        break;
    }

    case EntityType::Splice: {
        const QPointF c = vt.toScreen(e.a);
        // World-sized dot so it scales with zoom (screen floor keeps it visible).
        const double r = std::max(2.5, e.meta.value("size").toDouble(4.0) * vt.scale());
        p.setBrush(color);
        p.drawEllipse(c, r, r);
        p.setBrush(Qt::NoBrush);
        break;
    }

    case EntityType::NotConnected: {
        const QPointF c = vt.toScreen(e.a);
        // World-sized so the X scales with the drawing (zooms out as you zoom out),
        // matching the pins it terminates; a small screen floor keeps it visible.
        const double halfWorld = e.meta.value("ncSize").toDouble(6.0);
        const double s = std::max(3.0, halfWorld * vt.scale());
        p.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
        p.drawLine(c + QPointF(s, -s), c + QPointF(-s, s));
        break;
    }

    case EntityType::NetLabel: {
        const QString txt = e.meta.value("text").toString(e.meta.value("netName").toString());
        drawWorldText(p, vt.toScreen(e.a), txt, 4.0, vt.scale(), color);
        break;
    }

    case EntityType::Twist: {
        // Twisted-pair symbol over the band spanned by the crossed wires. "cross" = a
        // braid of two X crossings; "single" (jog) = one X. The straight top/bottom wires
        // are CUT (masked) within the band, and short horizontal leads flank the crossings.
        if (e.points.empty()) break;
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (const Vec2& pt : e.points) {
            cx += pt.x;
            yMin = std::min(yMin, pt.y);
            yMax = std::max(yMax, pt.y);
        }
        cx /= e.points.size();
        const QString ty = e.meta.value("twistType").toString();
        // Type 1 ("cross") = a 3-crossing braid; Type 2 ("jog"/"single") = twice twisted
        // (2 crossings); both now REST FLAT between crossings (a short level run at each
        // peak/trough) instead of meeting at a sharp point, mirrored top/bottom. Type 3
        // ("type3") keeps the original sharp diamond point between 2 crossings.
        const bool type3 = (ty == QLatin1String("type3"));
        const int n = type3 ? 2 : (ty == QLatin1String("jog") || ty == QLatin1String("single")) ? 2 : 3;
        const bool flatten = !type3;
        const double band = std::max(1.0, yMax - yMin);
        const double w = std::max(28.0, band * 1.4);
        const double x0 = cx - w / 2.0, x1 = cx + w / 2.0;
        const double lead = std::min(w * 0.18, 8.0);      // short horizontal lead lines
        const double mx0 = x0 + lead, mx1 = x1 - lead;    // crossing span
        const double pad = std::max(1.5, band * 0.08);    // fully cover the wire strokes
        const double pitch = (mx1 - mx0) / n;
        const double flatHalf = flatten ? std::min(pitch * 0.25, 6.0) : 0.0;
        // Cut the straight top/bottom wires between the twists.
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(g_bgColor);
        p.drawRect(QRectF(vt.toScreen({x0, yMax + pad}), vt.toScreen({x1, yMin - pad})).normalized());
        p.restore();
        // Two conductors: horizontal lead, n diagonal crossings (resting flat between them
        // at each intermediate peak/trough), horizontal lead out.
        QPolygonF A, B;
        A << vt.toScreen({x0, yMin});
        B << vt.toScreen({x0, yMax});
        for (int k = 0; k <= n; ++k) {
            const double xc = mx0 + pitch * k;
            const bool up = (k % 2 == 0);
            const double aY = up ? yMin : yMax, bY = up ? yMax : yMin;
            if (k == 0 || k == n || flatHalf <= 0.0) {
                A << vt.toScreen({xc, aY});
                B << vt.toScreen({xc, bY});
            } else {
                A << vt.toScreen({xc - flatHalf, aY}) << vt.toScreen({xc + flatHalf, aY});
                B << vt.toScreen({xc - flatHalf, bY}) << vt.toScreen({xc + flatHalf, bY});
            }
        }
        const bool aEndsTop = (n % 2 == 0);
        A << vt.toScreen({x1, aEndsTop ? yMin : yMax});
        B << vt.toScreen({x1, aEndsTop ? yMax : yMin});
        p.drawPolyline(A);
        p.drawPolyline(B);
        break;
    }

    case EntityType::Screen: {
        // A shield/screen: a vertical ellipse catching (crossing) the selected wires, a
        // joint on each, and a drain lead to the last point (drag it onto a connector
        // body or an X to connect / terminate).
        const int k = std::min(e.meta.value("catch").toInt(static_cast<int>(e.points.size())),
                               static_cast<int>(e.points.size()));
        if (k < 1) break;
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (int i = 0; i < k; ++i) {
            cx += e.points[i].x;
            yMin = std::min(yMin, e.points[i].y);
            yMax = std::max(yMax, e.points[i].y);
        }
        cx /= k;
        const double pad = 6.0;
        const double rx = e.meta.value("rx").toDouble(12.0);
        const double cyMid = (yMin + yMax) / 2.0;
        // Large radius: explicit override (Properties) or auto-fit to the caught wires.
        const double ry = e.meta.contains("ry") ? e.meta.value("ry").toDouble()
                                                : (yMax - yMin) / 2.0 + pad;
        const QPointF c = vt.toScreen({cx, cyMid});
        p.drawEllipse(c, rx * vt.scale(), ry * vt.scale());
        // Drain lead + endpoint (the point after the k caught wires); the lead exits
        // the ellipse edge on the meta "legDir" side (default "bottom", matching the
        // original behavior before left/right were supported).
        if (static_cast<int>(e.points.size()) > k) {
            const Vec2 drain = e.points.back();
            const QString legDir = e.meta.value("legDir").toString(QStringLiteral("bottom"));
            Vec2 edge;
            if (legDir == QLatin1String("top")) edge = {cx, cyMid + ry};
            else if (legDir == QLatin1String("left")) edge = {cx - rx, cyMid};
            else if (legDir == QLatin1String("right")) edge = {cx + rx, cyMid};
            else edge = {cx, cyMid - ry};
            p.drawLine(vt.toScreen(edge), vt.toScreen(drain));
            const QPointF d = vt.toScreen(drain);
            p.drawLine(d + QPointF(-3, -3), d + QPointF(3, 3)); // small end tick
            p.drawLine(d + QPointF(3, -3), d + QPointF(-3, 3));
        }
        if (e.meta.value("showS").toBool(true))
            drawWorldText(p, c + QPointF(rx * vt.scale() + 3, 0), QStringLiteral("S"), 4.0,
                          vt.scale(), color);
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
