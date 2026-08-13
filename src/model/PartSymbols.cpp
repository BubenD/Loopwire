#include "model/PartSymbols.h"

#include <QtMath>
#include <cmath>

namespace Edraw_Desktop {

namespace {

// A little builder, so each part below reads as the drawing it is rather than as a page of
// boilerplate: every piece is placed about the part's middle and moved to where it is dropped.
class Draw {
public:
    Draw(const Vec2& at, const QString& part) : at_(at), part_(part) {}

    void line(double x1, double y1, double x2, double y2, bool hidden = false) {
        Entity e = start(EntityType::Line);
        e.a = {at_.x + x1, at_.y + y1};
        e.b = {at_.x + x2, at_.y + y2};
        if (hidden) e.meta.insert(QStringLiteral("lineType"), QStringLiteral("dashed"));
        out_.push_back(std::move(e));
    }
    void rect(double x1, double y1, double x2, double y2) {
        line(x1, y1, x2, y1);
        line(x2, y1, x2, y2);
        line(x2, y2, x1, y2);
        line(x1, y2, x1, y1);
    }
    void poly(const std::vector<Vec2>& pts) {
        if (pts.size() < 2) return;
        Entity e = start(EntityType::Polyline);
        for (const Vec2& v : pts) e.points.push_back({at_.x + v.x, at_.y + v.y});
        out_.push_back(std::move(e));
    }
    void circle(double cx, double cy, double r) {
        Entity e = start(EntityType::Circle);
        e.center = {at_.x + cx, at_.y + cy};
        e.radius = r;
        out_.push_back(std::move(e));
    }
    // Angles in degrees, anticlockwise, as a drawing states them.
    void arc(double cx, double cy, double r, double from, double to) {
        Entity e = start(EntityType::Arc);
        e.center = {at_.x + cx, at_.y + cy};
        e.radius = r;
        e.startAngle = from * M_PI / 180.0;
        e.endAngle = to * M_PI / 180.0;
        out_.push_back(std::move(e));
    }
    std::vector<Entity> take() { return std::move(out_); }

private:
    Entity start(EntityType t) {
        Entity e;
        e.type = t;
        e.meta.insert(QLatin1String(kPartSymbolKey), part_);
        return e;
    }
    Vec2 at_;
    QString part_;
    std::vector<Entity> out_;
};

} // namespace

QString partSymbolName(PartSymbol s) {
    switch (s) {
    case PartSymbol::ThResistor: return QStringLiteral("Resistor, through hole");
    case PartSymbol::ThCapacitor: return QStringLiteral("Capacitor, through hole");
    case PartSymbol::ThCoil: return QStringLiteral("Coil, through hole");
    case PartSymbol::SmtResistor: return QStringLiteral("Resistor, SMT (top view)");
    case PartSymbol::SmtCapacitor: return QStringLiteral("Capacitor, SMT (top view)");
    case PartSymbol::SmtCoil: return QStringLiteral("Coil, SMT (top view)");
    case PartSymbol::FerriteRingSide: return QStringLiteral("Ferrite ring (side view)");
    case PartSymbol::FerriteRingTop: return QStringLiteral("Ferrite ring (top view)");
    case PartSymbol::SchResistor: return QStringLiteral("Resistor (box)");
    case PartSymbol::SchResistorZigzag: return QStringLiteral("Resistor (zigzag)");
    case PartSymbol::SchCapacitor: return QStringLiteral("Capacitor");
    case PartSymbol::SchCoil: return QStringLiteral("Coil");
    case PartSymbol::SchFerrite: return QStringLiteral("Ferrite");
    }
    return QString();
}

QString partSymbolTip(PartSymbol s) {
    switch (s) {
    case PartSymbol::ThResistor:
        return QStringLiteral("An axial resistor from the side, at size: 7 by 2.6 mm with its leads.");
    case PartSymbol::ThCapacitor:
        return QStringLiteral("A radial can from the side, at size: 8 mm across, with its two leads "
                              "and the stripe down the negative side.");
    case PartSymbol::ThCoil:
        return QStringLiteral("A wound bobbin from the side, at size: 8 by 6 mm with its two leads.");
    case PartSymbol::SmtResistor:
        return QStringLiteral("An 1206 chip resistor from above, at size: 3.2 by 1.6 mm.");
    case PartSymbol::SmtCapacitor:
        return QStringLiteral("An 1206 chip capacitor from above, at size: 3.2 by 1.6 mm, with the "
                              "end bands it is soldered by.");
    case PartSymbol::SmtCoil:
        return QStringLiteral("A shielded chip inductor from above, at size: 4 by 4 mm.");
    case PartSymbol::FerriteRingSide:
        return QStringLiteral("A ferrite ring from the side: 12 mm across, 5 mm thick, its hole shown "
                              "as hidden edges.");
    case PartSymbol::FerriteRingTop:
        return QStringLiteral("A ferrite ring from above: 12 mm across the outside, 6 mm across the "
                              "hole - what has to fit over the bundle.");
    case PartSymbol::SchResistor:
        return QStringLiteral("The resistor as a box - IEC 60617, which is what a European schematic "
                              "uses - with its two leads.");
    case PartSymbol::SchResistorZigzag:
        return QStringLiteral("The resistor as a zigzag - ANSI/IEEE, which is what an American schematic "
                              "uses. The same part as the box: which one to draw is the drawing office's "
                              "own convention, so both are here.");
    case PartSymbol::SchCapacitor: return QStringLiteral("The capacitor symbol, with its two leads.");
    case PartSymbol::SchCoil: return QStringLiteral("The coil symbol, with its two leads.");
    case PartSymbol::SchFerrite:
        return QStringLiteral("A coil with its core: what a ferrite bead or a choke is drawn as.");
    }
    return QString();
}

bool partSymbolIsSchematic(PartSymbol s) {
    switch (s) {
    case PartSymbol::SchResistor:
    case PartSymbol::SchResistorZigzag:
    case PartSymbol::SchCapacitor:
    case PartSymbol::SchCoil:
    case PartSymbol::SchFerrite:
        return true;
    default:
        return false;
    }
}

std::vector<PartSymbol> mechanicalPartSymbols() {
    return {PartSymbol::ThResistor,  PartSymbol::ThCapacitor,      PartSymbol::ThCoil,
            PartSymbol::SmtResistor, PartSymbol::SmtCapacitor,     PartSymbol::SmtCoil,
            PartSymbol::FerriteRingSide, PartSymbol::FerriteRingTop};
}

std::vector<PartSymbol> schematicPartSymbols() {
    return {PartSymbol::SchResistor, PartSymbol::SchResistorZigzag, PartSymbol::SchCapacitor,
            PartSymbol::SchCoil, PartSymbol::SchFerrite};
}

std::vector<Entity> partSymbolEntities(PartSymbol s, const Vec2& at) {
    Draw d(at, partSymbolName(s));
    switch (s) {
    case PartSymbol::ThResistor: {
        // Axial, from the side, and shaped like one: the body is WAISTED - full at both ends where the
        // caps are, narrower through the middle - with a domed end each side, its bands, and a lead out
        // of each dome. Drawn as a plain 7 by 2.6 box it read as a chip resistor lying on its side.
        const double hx = 3.5;      // half the body
        const double hEnd = 1.3;    // half its height at the ends
        const double hMid = 1.0;    // ... and through the waist
        const double lead = 4.5;
        constexpr int kSteps = 24;
        for (const double side : {1.0, -1.0}) {
            std::vector<Vec2> edge;
            for (int i = 0; i <= kSteps; ++i) {
                const double x = -hx + 2.0 * hx * i / kSteps;
                const double f = x / hx;
                edge.push_back({x, side * (hMid + (hEnd - hMid) * f * f)});
            }
            d.poly(edge);
        }
        d.arc(-hx, 0.0, hEnd, 90.0, 270.0);
        d.arc(hx, 0.0, hEnd, -90.0, 90.0);
        // Four bands: three together and the tolerance band set apart, as they are printed.
        for (const double x : {-1.5, -0.6, 0.3, 1.9}) {
            const double f = x / hx;
            const double h = hMid + (hEnd - hMid) * f * f;
            d.line(x, h, x, -h);
        }
        d.line(-hx - hEnd, 0.0, -hx - hEnd - lead, 0.0);
        d.line(hx + hEnd, 0.0, hx + hEnd + lead, 0.0);
        break;
    }
    case PartSymbol::ThCapacitor: {
        // A radial can from the side: 8 across, 10 tall with a domed top, the stripe down the negative
        // side, and its two leads out of the bottom.
        const double hx = 4.0, top = 4.0, bot = -5.0, lead = 5.0;
        d.line(-hx, bot, -hx, top);
        d.line(hx, bot, hx, top);
        d.line(-hx, bot, hx, bot);
        d.arc(0.0, top, hx, 0.0, 180.0);
        d.line(hx - 1.4, bot, hx - 1.4, top + 1.0);   // the stripe
        d.line(hx - 2.4, bot, hx - 2.4, top + 1.0);
        d.line(-1.75, bot, -1.75, bot - lead);
        d.line(1.75, bot, 1.75, bot - lead);
        break;
    }
    case PartSymbol::ThCoil: {
        // A wound bobbin from the side: the body, the turns across it, and a lead out of each end of
        // the winding.
        const double hx = 4.0, hy = 3.0, lead = 5.0;
        d.rect(-hx, -hy, hx, hy);
        for (int i = 1; i < 6; ++i) d.line(-hx + i * (2 * hx / 6), -hy, -hx + i * (2 * hx / 6), hy);
        d.line(-2.0, -hy, -2.0, -hy - lead);
        d.line(2.0, -hy, 2.0, -hy - lead);
        break;
    }
    case PartSymbol::SmtResistor: {
        // 1206, from above: the body, a terminal at each end, and the printed face between them.
        const double hx = 1.6, hy = 0.8, term = 0.6;
        d.rect(-hx, -hy, hx, hy);
        d.line(-hx + term, -hy, -hx + term, hy);
        d.line(hx - term, -hy, hx - term, hy);
        d.rect(-hx + term + 0.2, -hy + 0.2, hx - term - 0.2, hy - 0.2);   // the printed face
        break;
    }
    case PartSymbol::SmtCapacitor: {
        // 1206 chip capacitor, from above: the body and the two bands it is soldered by. No printed
        // face - that is what tells it from the resistor on a drawing.
        const double hx = 1.6, hy = 0.8, band = 0.8;
        d.rect(-hx, -hy, hx, hy);
        d.line(-hx + band, -hy, -hx + band, hy);
        d.line(hx - band, -hy, hx - band, hy);
        break;
    }
    case PartSymbol::SmtCoil: {
        // A shielded chip inductor from above: the body, the drum inside it, and a terminal each side.
        const double h = 2.0, term = 0.9;
        d.rect(-h, -h, h, h);
        d.circle(0.0, 0.0, 1.3);
        d.line(-h + term, -h, -h + term, h);
        d.line(h - term, -h, h - term, h);
        break;
    }
    case PartSymbol::FerriteRingSide: {
        // From the side: 12 across, 5 thick. The hole cannot be seen from here, so its two walls are
        // drawn as hidden edges - which is how a drawing says "there is a hole through this".
        const double hx = 6.0, hy = 2.5, hole = 3.0;
        d.rect(-hx, -hy, hx, hy);
        d.line(-hole, -hy, -hole, hy, /*hidden=*/true);
        d.line(hole, -hy, hole, hy, /*hidden=*/true);
        break;
    }
    case PartSymbol::FerriteRingTop:
        // From above: the ring. The inside is what has to go over the bundle.
        d.circle(0.0, 0.0, 6.0);
        d.circle(0.0, 0.0, 3.0);
        break;
    case PartSymbol::SchResistor: {
        // The symbol: a plain box with a lead each side.
        const double hx = 4.0, hy = 1.5, lead = 5.0;
        d.rect(-hx, -hy, hx, hy);
        d.line(-hx, 0.0, -hx - lead, 0.0);
        d.line(hx, 0.0, hx + lead, 0.0);
        break;
    }
    case PartSymbol::SchResistorZigzag: {
        // The same part as the box, drawn the American way: a zigzag of six equal legs between two leads.
        // Six because that is what makes three full peaks - the count a draughtsman reads as a resistor
        // rather than as a kink in a wire - and the peaks are as tall as the box is half deep, so the two
        // symbols sit at the same weight on a sheet beside each other.
        const double hx = 4.0;       // the zigzag runs from -hx to +hx, as the box does
        const double peak = 1.5;     // as tall as the box's half depth
        const double lead = 5.0;
        constexpr int kLegs = 6;
        std::vector<Vec2> zig;
        zig.push_back({-hx, 0.0});
        for (int i = 0; i < kLegs; ++i) {
            // Half a step in from each end, so the first and last legs are half legs and the zigzag
            // leaves the leads level rather than starting on a peak.
            const double x = -hx + (2.0 * hx) * (i + 0.5) / kLegs;
            zig.push_back({x, (i % 2 == 0) ? peak : -peak});
        }
        zig.push_back({hx, 0.0});
        d.poly(zig);
        d.line(-hx, 0.0, -hx - lead, 0.0);
        d.line(hx, 0.0, hx + lead, 0.0);
        break;
    }
    case PartSymbol::SchCapacitor: {
        // Two plates, and a lead to each.
        const double gap = 0.9, plate = 2.5, lead = 5.0;
        d.line(-gap, -plate, -gap, plate);
        d.line(gap, -plate, gap, plate);
        d.line(-gap, 0.0, -gap - lead, 0.0);
        d.line(gap, 0.0, gap + lead, 0.0);
        break;
    }
    case PartSymbol::SchCoil: {
        // Four turns, and a lead each side.
        const double r = 1.25, lead = 4.0;
        for (int i = 0; i < 4; ++i) d.arc(-3.0 * r + i * 2.0 * r, 0.0, r, 0.0, 180.0);
        d.line(-4.0 * r, 0.0, -4.0 * r - lead, 0.0);
        d.line(4.0 * r, 0.0, 4.0 * r + lead, 0.0);
        break;
    }
    case PartSymbol::SchFerrite: {
        // The same turns with a core over them: two lines above the winding, which is what a bead or a
        // choke is drawn as.
        const double r = 1.25, lead = 4.0;
        for (int i = 0; i < 4; ++i) d.arc(-3.0 * r + i * 2.0 * r, 0.0, r, 0.0, 180.0);
        d.line(-4.0 * r, 0.0, -4.0 * r - lead, 0.0);
        d.line(4.0 * r, 0.0, 4.0 * r + lead, 0.0);
        d.line(-4.0 * r, r + 0.6, 4.0 * r, r + 0.6);
        d.line(-4.0 * r, r + 1.2, 4.0 * r, r + 1.2);
        break;
    }
    }
    return d.take();
}

} // namespace Edraw_Desktop
