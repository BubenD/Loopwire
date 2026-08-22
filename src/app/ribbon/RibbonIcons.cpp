#include "app/ribbon/RibbonIcons.h"

#include "model/PartSymbols.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>

#include <cmath>

namespace Edraw_Desktop {

// Draw a simple monochrome glyph for a tool so the ribbon reads like Edraw's
// icon grid without external assets.
QIcon makeToolIcon(ToolId id, const QColor& ink) {
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (id) {
    case ToolId::Select: {
        QPolygonF a({{6, 4}, {6, 18}, {10, 14}, {13, 20}, {15, 19}, {12, 13}, {17, 13}});
        p.setBrush(ink);
        p.drawPolygon(a);
        break;
    }
    case ToolId::Line: p.drawLine(5, 18, 19, 6); break;
    case ToolId::DoubleLine: p.drawLine(4, 16, 17, 5); p.drawLine(7, 19, 20, 8); break;
    case ToolId::Polyline: { QPolygonF a({{4, 16}, {9, 9}, {14, 14}, {20, 6}}); p.drawPolyline(a); break; }
    case ToolId::DoublePolyline: {
        QPolygonF a({{4, 14}, {9, 7}, {14, 12}, {20, 4}});
        QPolygonF b({{4, 19}, {9, 12}, {14, 17}, {20, 9}});
        p.drawPolyline(a); p.drawPolyline(b); break;
    }
    case ToolId::Rect: p.drawRect(5, 7, 14, 10); break;
    case ToolId::Polygon: {
        QPolygonF h; for (int i = 0; i < 6; ++i) { double an = M_PI / 6 + i * M_PI / 3; h << QPointF(12 + 8 * std::cos(an), 12 + 8 * std::sin(an)); } p.drawPolygon(h); break;
    }
    case ToolId::Circle: p.drawEllipse(QPointF(12, 12), 7, 7); break;
    case ToolId::Arc: { QPainterPath pa; pa.moveTo(5, 17); pa.arcTo(5, 5, 16, 16, 180, -120); p.drawPath(pa); break; }
    case ToolId::Ellipse: p.drawEllipse(QPointF(12, 12), 8, 5); break;
    case ToolId::Spline: { QPainterPath pa; pa.moveTo(4, 16); pa.cubicTo(8, 2, 16, 22, 20, 8); p.drawPath(pa); break; }
    case ToolId::Text: { QFont f = p.font(); f.setPixelSize(16); f.setBold(true); p.setFont(f); p.drawText(pm.rect(), Qt::AlignCenter, "A"); break; }
    case ToolId::MText: { QFont f = p.font(); f.setPixelSize(11); f.setBold(true); p.setFont(f); p.drawText(QRect(2, 2, 20, 20), Qt::AlignLeft | Qt::AlignTop, "A"); p.drawLine(9, 8, 20, 8); p.drawLine(5, 14, 20, 14); p.drawLine(5, 19, 20, 19); break; }
    case ToolId::Dimension: p.drawLine(5, 12, 19, 12); p.drawLine(5, 8, 5, 16); p.drawLine(19, 8, 19, 16); break;
    case ToolId::Hatch: {
        p.drawRect(4, 5, 16, 14);
        // diagonal hatch lines clipped to the box
        p.save();
        p.setClipRect(4, 5, 16, 14);
        for (int o = -14; o <= 16; o += 4) p.drawLine(4 + o, 19, 4 + o + 14, 5);
        p.restore();
        break;
    }
    case ToolId::Wipeout: {
        p.drawRect(4, 5, 16, 14);
        p.drawLine(4, 19, 20, 5); // crossed-out (masked) box
        p.drawLine(4, 5, 20, 19);
        break;
    }
    case ToolId::Move: {
        // Four-headed move arrow (clear 4-way cross).
        p.drawLine(12, 4, 12, 20); p.drawLine(4, 12, 20, 12);
        p.drawLine(12, 4, 9, 7);   p.drawLine(12, 4, 15, 7);    // up
        p.drawLine(12, 20, 9, 17); p.drawLine(12, 20, 15, 17);  // down
        p.drawLine(4, 12, 7, 9);   p.drawLine(4, 12, 7, 15);    // left
        p.drawLine(20, 12, 17, 9); p.drawLine(20, 12, 17, 15);  // right
        break;
    }
    case ToolId::Rotate: {
        // Near-full circular arrow.
        QPainterPath pa; pa.arcMoveTo(5, 5, 14, 14, 70); pa.arcTo(5, 5, 14, 14, 70, 250);
        p.drawPath(pa);
        const QPointF tip(12 + 7 * std::cos(-70 * M_PI / 180), 12 + 7 * std::sin(-70 * M_PI / 180));
        p.drawLine(tip, tip + QPointF(-4, -1)); p.drawLine(tip, tip + QPointF(0, -4));
        break;
    }
    case ToolId::Mirror: {
        // Dashed mirror axis with a solid shape and its reflection.
        QPen dash = pen; dash.setStyle(Qt::DashLine); p.setPen(dash);
        p.drawLine(12, 3, 12, 21);
        p.setPen(pen);
        QPolygonF a({{4, 7}, {10, 12}, {4, 17}}); QPolygonF b({{20, 7}, {14, 12}, {20, 17}});
        p.drawPolygon(a); p.drawPolygon(b);
        break;
    }
    case ToolId::Scale: {
        // Small square growing into a larger one along a diagonal arrow.
        p.drawRect(5, 14, 5, 5);
        p.drawRect(5, 6, 13, 13);
        p.drawLine(7, 17, 16, 8);
        p.drawLine(16, 8, 12, 8); p.drawLine(16, 8, 16, 12); // arrowhead
        break;
    }
    case ToolId::Offset: {
        // Two parallel corner polylines (the offset of a shape).
        QPolygonF a({{5, 6}, {5, 19}, {19, 19}}); p.drawPolyline(a);
        QPolygonF b({{9, 6}, {9, 15}, {19, 15}}); p.drawPolyline(b);
        break;
    }
    case ToolId::Trim: {
        // A line trimmed at a boundary: a vertical cutting edge, the kept part solid
        // up to it, and the removed part beyond shown dashed (AutoCAD TRIM sense).
        p.drawLine(14, 4, 14, 20);          // boundary / cutting edge
        p.drawLine(3, 12, 14, 12);          // kept segment
        QPen dash = pen;
        dash.setStyle(Qt::DashLine);
        p.setPen(dash);
        p.drawLine(14, 12, 21, 12);         // trimmed-away segment
        p.setPen(pen);
        break;
    }
    case ToolId::Extend: {
        // Arrow extending to a boundary wall.
        p.drawLine(19, 4, 19, 20);          // wall
        p.drawLine(4, 12, 16, 12);          // shaft
        p.drawLine(16, 12, 13, 9); p.drawLine(16, 12, 13, 15); // arrowhead
        break;
    }
    case ToolId::Fillet: { QPainterPath pa; pa.moveTo(5, 19); pa.lineTo(5, 12); pa.arcTo(5, 5, 14, 14, 180, -90); pa.lineTo(19, 5); p.drawPath(pa); break; }
    case ToolId::Chamfer: { QPolygonF a({{5, 19}, {5, 10}, {10, 5}, {19, 5}}); p.drawPolyline(a); break; }
    case ToolId::Wire: { pen.setWidthF(2.4); p.setPen(pen); p.drawLine(4, 17, 20, 7); break; }
    case ToolId::Connector: p.drawRect(6, 5, 8, 14); p.drawLine(14, 9, 20, 9); p.drawLine(14, 15, 20, 15); break;
    case ToolId::Splice: p.setBrush(ink); p.drawEllipse(QPointF(12, 12), 4, 4); break;
    case ToolId::NotConnected: p.drawLine(7, 7, 17, 17); p.drawLine(17, 7, 7, 17); break;
    // A ring, not a filled dot (Splice) - a connection pin is a place things attach TO, not a
    // joint made between them.
    case ToolId::ConnectionPin: p.drawEllipse(QPointF(12, 12), 5, 5); break;
    case ToolId::LengthBreak:
        // A run with the break mark across it: the run, then the pair of strokes.
        p.drawLine(3, 12, 21, 12);
        p.drawLine(13, 5, 8, 19);
        p.drawLine(18, 5, 13, 19);
        break;
    case ToolId::TwistedPair: {
        // The thing itself: two wires straight at both ends, crossing twice in the middle.
        for (double s : {1.0, -1.0}) {
            QPainterPath pa(QPointF(2, 12 - 4 * s));
            pa.lineTo(QPointF(6, 12 - 4 * s));
            for (int k = 0; k < 2; ++k) {
                const double x = 6 + k * 6.0;
                pa.cubicTo(QPointF(x + 2, 12 - 4 * s), QPointF(x + 4, 12 + 4 * s),
                           QPointF(x + 6, 12 + 4 * s));
                s = -s;
            }
            pa.lineTo(QPointF(22, 12 + 4 * s));
            p.drawPath(pa);
        }
        break;
    }
    case ToolId::TwistedRoute: {
        // The same pair, with a knee in it: along, round the corner, and up.
        for (double s : {1.0, -1.0}) {
            QPainterPath pa(QPointF(2, 15 - 3 * s));
            pa.cubicTo(QPointF(7, 15 - 3 * s), QPointF(8, 15 + 3 * s), QPointF(13, 15 + 3 * s));
            pa.lineTo(QPointF(16 + 3 * s, 15 + 3 * s));   // the corner
            pa.lineTo(QPointF(16 + 3 * s, 3));
            p.drawPath(pa);
        }
        break;
    }
    case ToolId::TwistedHarness: {
        // A bundle of them: four wires laid up together, fanned out at both ends.
        for (int k = 0; k < 4; ++k) {
            const double y0 = 6.0 + k * 4.0;                 // where it starts, fanned out
            const double ph = M_PI * k / 3.0;                 // its place in the lay-up
            QPainterPath pa(QPointF(2, y0));
            for (int i = 0; i <= 12; ++i) {
                const double x = 6.0 + i * 1.33;
                pa.lineTo(QPointF(x, 12.0 + 5.0 * std::cos(2.0 * M_PI * i / 12.0 + ph)));
            }
            pa.lineTo(QPointF(26, y0));
            p.drawPath(pa);
        }
        break;
    }
    case ToolId::TwistedHarnessRoute: {
        // The bundle (three strands stand for it, same as the fan below), routed round a knee
        // instead of straight - Twisted route's corner, laid up three wide.
        for (double s : {-1.0, 0.0, 1.0}) {
            QPainterPath pa(QPointF(2, 15 - 2.4 * s));
            pa.cubicTo(QPointF(7, 15 - 2.4 * s), QPointF(8, 15 + 2.4 * s), QPointF(13, 15 + 2.4 * s));
            pa.lineTo(QPointF(16 + 2.4 * s, 15 + 2.4 * s));   // the corner
            pa.lineTo(QPointF(16 + 2.4 * s, 3));
            p.drawPath(pa);
        }
        break;
    }
    case ToolId::NetLabel: { QPolygonF a({{4, 8}, {15, 8}, {20, 12}, {15, 16}, {4, 16}}); p.drawPolygon(a); break; }
    case ToolId::Balloon: {
        // Numbered callout: a circle with a leader stub and a "1" inside.
        p.drawEllipse(QPointF(9, 9), 6, 6);
        p.drawLine(13, 13, 20, 20); // leader
        QFont f = p.font(); f.setPixelSize(8); f.setBold(true); p.setFont(f);
        p.drawText(QRectF(3, 3, 12, 12), Qt::AlignCenter, QStringLiteral("1"));
        break;
    }
    case ToolId::DetailView: {
        // The thing itself, and the thing drawn again bigger: a small circle with a couple of marks in it and
        // a large circle with the same marks larger. That is what a detail view IS, so the icon is the thing
        // and not a magnifying glass - which is what every other program draws and says nothing about
        // enlarging one part of a drawing on to another part of the same drawing.
        p.drawEllipse(QPointF(6.5, 7), 4.5, 4.5);
        p.drawLine(5, 6, 8, 6);
        p.drawLine(5, 8, 7, 8);
        p.drawEllipse(QPointF(15, 16), 7.5, 7.5);
        p.drawLine(12, 14, 18, 14);
        p.drawLine(12, 18, 16, 18);
        break;
    }
    case ToolId::CenterMark: {
        // A ring with the little cross at its middle, overshooting it - which is what a centre mark IS,
        // so the icon is the thing rather than a symbol for it.
        p.drawEllipse(QPointF(12, 12), 6, 6);
        p.drawLine(12, 3, 12, 21);
        p.drawLine(3, 12, 21, 12);
        break;
    }
    case ToolId::CenterLine: {
        // Two parallel runs with the dashed middle between them: the two solid lines are the geometry, the
        // broken one is what the tool adds.
        p.drawLine(3, 5, 21, 5);
        p.drawLine(3, 19, 21, 19);
        QPen dash = p.pen();
        dash.setStyle(Qt::DashLine);
        p.save();
        p.setPen(dash);
        p.drawLine(3, 12, 21, 12);
        p.restore();
        break;
    }
    case ToolId::TextLeader: {
        // A leader arrow up to a short landing, with text lines (no circle).
        p.drawLine(4, 20, 12, 12);          // leader
        p.drawLine(4, 20, 4, 16); p.drawLine(4, 20, 8, 20); // arrowhead
        p.drawLine(12, 12, 20, 12);         // horizontal landing
        p.drawLine(13, 8, 20, 8);           // text lines above the landing
        p.drawLine(13, 5, 18, 5);
        break;
    }
    case ToolId::Note: {
        // Bordered note box with a few text lines.
        p.drawRect(4, 5, 16, 14);
        for (int i = 0; i < 3; ++i) p.drawLine(7, 9 + i * 3, 17, 9 + i * 3);
        break;
    }
    case ToolId::Revision: {
        // Titled box: a top strip + a small revision triangle.
        p.drawRect(4, 5, 16, 14);
        p.drawLine(4, 10, 20, 10); // title strip divider
        QPolygonF tri({{12, 12}, {16, 18}, {8, 18}}); // rev triangle
        p.drawPolyline(tri); p.drawLine(8, 18, 12, 12);
        break;
    }
    case ToolId::Table: {
        // Grid of cells.
        p.drawRect(4, 5, 16, 14);
        p.drawLine(4, 10, 20, 10); p.drawLine(4, 15, 20, 15); // rows
        p.drawLine(9, 5, 9, 19); p.drawLine(15, 5, 15, 19);   // cols
        break;
    }
    }
    p.end();
    return QIcon(pm);
}

QIcon makePartSymbolIcon(PartSymbol s, const QColor& ink) {
    // Drawn from the part's own drawing rather than by hand: the icon is then the part, scaled to fit
    // the button, and a menu entry cannot come to show something other than what it places.
    const std::vector<Entity> ents = partSymbolEntities(s, Vec2{0.0, 0.0});
    Bounds b;
    for (const Entity& e : ents) b.expand(e.bounds());
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    if (!b.valid) return QIcon(pm);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const double w = std::max(1e-6, b.max.x - b.min.x), h = std::max(1e-6, b.max.y - b.min.y);
    const double k = std::min(24.0 / w, 24.0 / h);
    // A symbol with its leads is long and shallow - a resistor is 18 by 3 - and scaled to fit, evenly, it
    // comes out as a 2-pixel-tall squiggle on a line: the box reads as two dashes and the zigzag reads as
    // the same two dashes. So the HEIGHT is exaggerated, up to two and a half times, for a drawing flatter
    // than three to one. An icon is a pictogram of the thing, not a scale drawing of it; the drawing on the
    // sheet is untouched, and anything squarer than 2:1 is left exactly as it is.
    const double flat = w / h;
    const double ky = k * std::min(3.5, std::max(1.0, flat / 2.0));
    auto to = [&](const Vec2& v) {
        return QPointF(14.0 + (v.x - (b.min.x + b.max.x) / 2.0) * k,
                       14.0 - (v.y - (b.min.y + b.max.y) / 2.0) * ky);
    };
    for (const Entity& e : ents) {
        if (e.meta.value(QStringLiteral("lineType")).toString() == QLatin1String("dashed")) {
            QPen dash = pen;
            dash.setStyle(Qt::DashLine);
            p.setPen(dash);
        } else {
            p.setPen(pen);
        }
        switch (e.type) {
        case EntityType::Line:
            p.drawLine(to(e.a), to(e.b));
            break;
        case EntityType::Circle:
            // Round things stay round: the stretch is for the flat symbols, and a stretched circle in an
            // icon reads as a mistake rather than as emphasis.
            p.drawEllipse(to(e.center), e.radius * k, e.radius * k);
            break;
        case EntityType::Arc: {
            const double r = e.radius * k;
            const QPointF c = to(e.center);
            const QRectF box(c.x() - r, c.y() - r, 2 * r, 2 * r);
            const double from = e.startAngle * 180.0 / M_PI;
            double span = (e.endAngle - e.startAngle) * 180.0 / M_PI;
            while (span <= 0.0) span += 360.0;
            p.drawArc(box, static_cast<int>(from * 16), static_cast<int>(span * 16));
            break;
        }
        case EntityType::Polyline: {
            // A POLYLINE. Left out, and so drawn as nothing: the zigzag resistor came out as a bare pair of
            // leads with a gap between them, and the axial resistor's waisted body had been missing from its
            // icon since it was drawn. An icon made from the part's own drawing has to be able to draw all
            // of it, or it quietly shows something else.
            if (e.points.size() < 2) break;
            QPolygonF poly;
            for (const Vec2& v : e.points) poly << to(v);
            p.drawPolyline(poly);
            break;
        }
        case EntityType::Rect:
        case EntityType::Polygon: {
            // Both are their corners, and both close.
            if (e.points.size() < 3) break;
            QPolygonF poly;
            for (const Vec2& v : e.points) poly << to(v);
            p.drawPolygon(poly);
            break;
        }
        default:
            break;
        }
    }
    p.end();
    return QIcon(pm);
}

} // namespace Edraw_Desktop
