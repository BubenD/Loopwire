#include "render/EntityRenderer.h"

#include "geometry/Geometry.h"
#include "harness/Harness.h"

#include <QFont>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QtMath>

#include <algorithm>

namespace Edraw_Desktop {

namespace {
int g_dimDecimals = 2; // decimals on dimension labels (configurable via Options)
}
void EntityRenderer::setDimensionDecimals(int d) { g_dimDecimals = std::clamp(d, 0, 6); }
int EntityRenderer::dimensionDecimals() { return g_dimDecimals; }

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
        // Two parallel lines offset +/- gap/2 perpendicular to the centerline.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 dir = (e.b - e.a).normalized();
        const Vec2 off = dir.perp() * (gap / 2.0);
        p.drawLine(vt.toScreen(e.a + off), vt.toScreen(e.b + off));
        p.drawLine(vt.toScreen(e.a - off), vt.toScreen(e.b - off));
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
                const std::vector<Vec2> center =
                    (cr > 0.0) ? roundedPolyline(e.points, cr) : e.points;
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
    case EntityType::Hatch:
    case EntityType::Wipeout:
        if (e.points.size() >= 2)
            p.drawPolygon(toScreenPoly(e.points, vt));
        break;

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
        const QPointF c = vt.toScreen(e.center);
        const double rx = e.radius * vt.scale();
        const double ry = e.radiusY * vt.scale();
        p.save();
        p.translate(c);
        p.rotate(-qRadiansToDegrees(e.rotation)); // screen Y is flipped
        p.drawEllipse(QPointF(0, 0), rx, ry);
        p.restore();
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
        f.setPixelSize(std::max(1, static_cast<int>(heightWorld * vt.scale())));
        p.setFont(f);
        p.setPen(color);
        const QFontMetricsF fm(f);
        const double lineH = fm.height();
        const QString al = e.meta.value("align").toString();
        // MTEXT container: word-wrap each paragraph to the box width (world units).
        const double boxPx = e.meta.value("boxWidth").toDouble(0.0) * vt.scale();
        QStringList lines;
        for (const QString& para : text.split(QLatin1Char('\n'))) {
            if (boxPx <= 1.0) { lines << para; continue; }
            QString cur;
            for (const QString& w : para.split(QLatin1Char(' '))) {
                const QString test = cur.isEmpty() ? w : cur + QLatin1Char(' ') + w;
                if (!cur.isEmpty() && fm.horizontalAdvance(test) > boxPx) {
                    lines << cur;
                    cur = w;
                } else {
                    cur = test;
                }
            }
            lines << cur;
        }
        // Stack lines downward from the anchor. With a box, drop the first baseline
        // by the ascent so the text sits inside the container's top edge.
        double y = anchor.y() + (boxPx > 1.0 ? fm.ascent() : 0.0);
        for (const QString& ln : lines) {
            double x = anchor.x();
            if (al == QLatin1String("center")) x -= fm.horizontalAdvance(ln) / 2.0;
            else if (al == QLatin1String("right")) x -= fm.horizontalAdvance(ln);
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
