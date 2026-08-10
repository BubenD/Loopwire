#include "harness/MechanicalHarness.h"

#include "harness/CableSpec.h"
#include "harness/Harness.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Edraw_Desktop {
namespace {

constexpr double kPi = 3.14159265358979323846;
double rad(double deg) { return deg * kPi / 180.0; }
double deg(double radians) { return radians * 180.0 / kPi; }

// Wire ends land on pin ends exactly; a fraction of a millimetre of rounding must not split a
// junction into two.
bool samePoint(const Vec2& a, const Vec2& b) { return distanceSq(a, b) < 1e-6; }

// mm2 of an AWG size, for a gauge written in AWG.
double awgToMm2(int awg) {
    static const QHash<int, double> kAwgMm2 = {
        {30, 0.0507}, {28, 0.0810}, {26, 0.1288}, {24, 0.2047}, {22, 0.3247}, {20, 0.5188},
        {18, 0.8230}, {16, 1.3090}, {14, 2.0810}, {12, 3.3090}, {10, 5.2610}, {8, 8.3670},
        {6, 13.300},  {4, 21.150},  {2, 33.630},  {1, 42.410}};
    return kAwgMm2.value(awg, 0.0);
}

double gaugeMm2(const QString& gauge) {
    const QString g = gauge.trimmed();
    if (g.isEmpty()) return 0.0;
    if (g.contains(QLatin1String("awg"), Qt::CaseInsensitive)) {
        const int awg = g.split(QLatin1Char(' ')).value(0).toInt();
        return awgToMm2(awg);
    }
    return g.toDouble();
}

// Nominal outside diameter over thin-wall insulation, by cross-section. In the region of the ISO
// 6722 class B maxima - the figures a harness is actually laid up from.
struct OdRow { double mm2; double od; };
const OdRow kOd[] = {
    {0.13, 0.9}, {0.22, 1.1}, {0.35, 1.3}, {0.50, 1.5}, {0.75, 1.8}, {1.00, 2.1},
    {1.50, 2.5}, {2.00, 2.9}, {2.50, 3.1}, {3.00, 3.4}, {4.00, 3.9}, {5.00, 4.3},
    {6.00, 4.7}, {10.0, 6.2}, {16.0, 7.6}, {25.0, 9.6}, {35.0, 11.2}, {50.0, 13.2},
    {70.0, 15.6}, {95.0, 18.0},
};

// A part's own figure for a key, as a number. 0 when it does not say.
double partNumber(const LibraryPart& part, const QString& key) {
    return part.value(key).trimmed().toDouble();
}

// The min/max of a Range parameter ("<key>Min"/"<key>Max"), as the library stores it.
void partWindow(const LibraryPart& part, const QString& key, double& lo, double& hi) {
    lo = partNumber(part, key + QStringLiteral("Min"));
    hi = partNumber(part, key + QStringLiteral("Max"));
}

// Which Range parameter of a kind is the one a BUNDLE passes through. A backshell and a boot are
// entered from the cable side; a sleeve, a conduit and a braid are sized by the bundle itself.
QString bundleWindowKey(PartKind kind) {
    switch (kind) {
        case PartKind::Backshell:
        case PartKind::HeatshrinkBoot:   return QStringLiteral("cableEntry");
        case PartKind::YSplit:           return QStringLiteral("main");
        case PartKind::Heatshrink:       return QStringLiteral("entry");
        case PartKind::Sleeve:
        case PartKind::ShieldBraid:      return QStringLiteral("diameter");
        case PartKind::Conduit:          return QStringLiteral("id");
        case PartKind::CableTie:         return QStringLiteral("bundle");
        case PartKind::Grommet:
        case PartKind::CableGland:       return QStringLiteral("cableEntry");
        default:                         return QString();
    }
}

// A fitting's length and outside diameter: what it says about itself, else what the library part
// does, else something proportionate to the bundle so it is at least drawn.
void fittingSize(const MechFitting& f, const LibraryPart* part, double bundleMm, double& lengthMm,
                 double& odMm) {
    lengthMm = f.lengthMm;
    odMm = f.odMm;
    if (part) {
        if (lengthMm <= 0.0) lengthMm = partNumber(*part, QStringLiteral("length"));
        if (odMm <= 0.0) {
            double lo = 0.0, hi = 0.0;
            partWindow(*part, bundleWindowKey(part->kind), lo, hi);
            if (hi > 0.0) odMm = hi + 2.0;         // over the entry it takes
        }
    }
    if (odMm <= 0.0) odMm = bundleMm + 3.0;
    if (lengthMm <= 0.0) lengthMm = std::max(12.0, bundleMm * 2.0);
}

Entity baseEntity(const MechDrawOptions& opt, const QString& role) {
    Entity e;
    e.layer = opt.layer;
    e.meta.insert(QStringLiteral("mechRole"), role);
    return e;
}

// One EDGE of a bundle body. The body is its two edges rather than a single double line, because a
// breakout has to be mitred: at a junction each edge stops where its neighbour's edge crosses it,
// which is what makes the outline of the harness continuous instead of three bodies overlapping. The
// bundle's diameter is recorded on the edge so the width is still a figure and not just a picture.
//
// A rail is not always straight from one end to the other. Where a cable's jacket has been cut back
// the run narrows to the bundle inside it, with a cone between the two, so the edge carries a knee at
// every change of diameter - which is the shape a stripped cable has and the shape a boot has to go
// over.
Entity makeBundleEdge(const MechDrawOptions& opt, const std::vector<Vec2>& pts, double diameterMm,
                      const QString& segId, int side) {
    Entity e = baseEntity(opt, QStringLiteral("bundle"));
    e.type = EntityType::Polyline;
    e.points = pts;
    if (!pts.empty()) {
        e.a = pts.front();
        e.b = pts.back();
    }
    e.meta.insert(QStringLiteral("mechSeg"), segId);
    e.meta.insert(QStringLiteral("bundleDia"), diameterMm);
    e.meta.insert(QStringLiteral("side"), side);
    return e;
}

// Where two lines cross: p + t*d and q + t*e. False when they are parallel, or when the crossing is
// so far off that mitring there would be nonsense (a pair of nearly-parallel legs).
bool lineCross(const Vec2& p, const Vec2& d, const Vec2& q, const Vec2& e, double maxDist,
               Vec2& out) {
    const double den = cross(d, e);
    if (std::abs(den) < 1e-9) return false;
    const double t = cross(q - p, e) / den;
    const Vec2 at = p + d * t;
    if (maxDist > 0.0 && distance(at, p) > maxDist) return false;
    out = at;
    return true;
}

Entity makeText(const MechDrawOptions& opt, const Vec2& at, const QString& text, double height,
                double rotationRad, const QString& role) {
    Entity e = baseEntity(opt, role);
    e.type = EntityType::Text;
    e.a = at;
    e.rotation = rotationRad;
    e.meta.insert(QStringLiteral("text"), text);
    e.meta.insert(QStringLiteral("height"), height);
    return e;
}

Entity makeDimension(const MechDrawOptions& opt, const Vec2& a, const Vec2& b, const Vec2& textAt,
                     const QString& segId) {
    Entity e = baseEntity(opt, QStringLiteral("dim"));
    e.type = EntityType::Dimension;
    e.a = a;
    e.b = b;
    e.points = {a, b, textAt};
    e.meta.insert(QStringLiteral("dimType"), QStringLiteral("aligned"));
    e.meta.insert(QStringLiteral("textHeight"), opt.textHeight);
    e.meta.insert(QStringLiteral("arrowSize"), opt.textHeight * 0.66);
    e.meta.insert(QStringLiteral("extOffset"), 2.0);
    e.meta.insert(QStringLiteral("decimals"), 1);
    e.meta.insert(QStringLiteral("mechSeg"), segId);
    return e;
}

// An axis-aligned box in the frame of a run: `along` is its length, `across` its width, centred on
// `mid` and turned to `dirRad`.
Entity makeBox(const MechDrawOptions& opt, const Vec2& mid, double dirRad, double along,
               double across, const QString& role) {
    Entity e = baseEntity(opt, role);
    e.type = EntityType::Rect;
    const Vec2 u{std::cos(dirRad), std::sin(dirRad)};
    const Vec2 v = u.perp();
    const Vec2 hu = u * (along / 2.0);
    const Vec2 hv = v * (across / 2.0);
    e.points = {mid - hu - hv, mid + hu - hv, mid + hu + hv, mid - hu + hv};
    return e;
}

// A body fitted at an end - a backshell, a boot - as the shape one actually is: a neck the conductors
// are squeezed into facing the connector, a cone, then the body over the cable behind it. Drawn as one
// closed outline, so the two bars that close it (across the neck and across the back) come with it.
// A box would be the same size and say nothing about which way round it goes on.
Entity makeTaperBody(const MechDrawOptions& opt, const Vec2& neckAt, const Vec2& u, double lengthAlong,
                     double neckAcross, double bodyAcross, const QString& role) {
    Entity e = baseEntity(opt, role);
    e.type = EntityType::Polygon;
    const Vec2 v = u.perp();
    // Stations along it: the neck, the end of the straight neck, the top of the cone, the back.
    const double s1 = lengthAlong * 0.25, s2 = lengthAlong * 0.55;
    const double hn = neckAcross / 2.0, hb = bodyAcross / 2.0;
    const Vec2 p0 = neckAt, p1 = neckAt + u * s1, p2 = neckAt + u * s2, p3 = neckAt + u * lengthAlong;
    e.points = {p0 + v * hn, p1 + v * hn, p2 + v * hb, p3 + v * hb,
                p3 - v * hb, p2 - v * hb, p1 - v * hn, p0 - v * hn};
    return e;
}

Entity makeBalloon(const MechDrawOptions& opt, const Vec2& tip, const Vec2& at,
                   const QString& text) {
    Entity e = baseEntity(opt, QStringLiteral("balloon"));
    e.type = EntityType::Circle;
    e.center = at;
    e.a = at;
    e.radius = std::max(4.0, opt.textHeight * 1.2);
    e.meta.insert(QStringLiteral("balloon"), true);
    e.meta.insert(QStringLiteral("text"), text);
    e.meta.insert(QStringLiteral("landing"), 8.0);
    e.meta.insert(QStringLiteral("landingSide"), (tip.x >= at.x) ? 1.0 : -1.0);
    QJsonArray leaders;
    QJsonArray p;
    p.append(tip.x);
    p.append(tip.y);
    leaders.append(p);
    e.meta.insert(QStringLiteral("leaders"), leaders);
    return e;
}

} // namespace

QString mechNodeKindName(MechNodeKind kind) {
    switch (kind) {
        case MechNodeKind::Root:      return QStringLiteral("Root");
        case MechNodeKind::Breakout:  return QStringLiteral("Breakout");
        case MechNodeKind::Connector: return QStringLiteral("Connector");
        case MechNodeKind::FreeEnd:   return QStringLiteral("Free end");
    }
    return QStringLiteral("Breakout");
}

// ---- Definition accessors ----------------------------------------------------------------------

const MechNode* MechHarnessDef::node(const QString& id) const {
    for (const MechNode& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const MechSegment* MechHarnessDef::segment(const QString& id) const {
    for (const MechSegment& s : segments)
        if (s.id == id) return &s;
    return nullptr;
}

const MechWire* MechHarnessDef::wire(const QString& id) const {
    for (const MechWire& w : wires)
        if (w.id == id) return &w;
    return nullptr;
}

const MechNode* MechHarnessDef::rootNode() const {
    for (const MechNode& n : nodes)
        if (n.parentSegment.isEmpty()) return &n;
    return nodes.empty() ? nullptr : &nodes.front();
}

std::vector<const MechSegment*> MechHarnessDef::childSegments(const QString& nodeId) const {
    std::vector<const MechSegment*> out;
    for (const MechSegment& s : segments)
        if (s.fromNode == nodeId) out.push_back(&s);
    return out;
}

QString MechHarnessDef::nextNodeId() const {
    for (int i = 1;; ++i) {
        const QString id = QStringLiteral("n%1").arg(i);
        if (!node(id)) return id;
    }
}

QString MechHarnessDef::nextSegmentId() const {
    for (int i = 1;; ++i) {
        const QString id = QStringLiteral("s%1").arg(i);
        if (!segment(id)) return id;
    }
}

// ---- Sizes --------------------------------------------------------------------------------------

double wireOuterDiameterMm(const QString& gauge) {
    const double mm2 = gaugeMm2(gauge);
    if (mm2 <= 0.0) return 0.0;
    const int n = static_cast<int>(std::size(kOd));
    if (mm2 <= kOd[0].mm2) return kOd[0].od;
    if (mm2 >= kOd[n - 1].mm2) {
        // Past the table: the conductor's own diameter plus a wall, which is what the table itself
        // amounts to at that size.
        return std::sqrt(4.0 * mm2 / kPi) + 1.6;
    }
    for (int i = 1; i < n; ++i) {
        if (mm2 <= kOd[i].mm2) {
            const double t = (mm2 - kOd[i - 1].mm2) / (kOd[i].mm2 - kOd[i - 1].mm2);
            return kOd[i - 1].od + t * (kOd[i].od - kOd[i - 1].od);
        }
    }
    return kOd[n - 1].od;
}

double bundleDiameterMm(const std::vector<double>& wireOds, double factor, double minMm) {
    std::vector<double> ds;
    for (double d : wireOds)
        if (d > 0.0) ds.push_back(d);
    double result = 0.0;
    if (ds.empty()) {
        result = 0.0;
    } else if (ds.size() == 1) {
        result = ds.front();                       // one wire is its own bundle
    } else if (ds.size() == 2) {
        result = ds[0] + ds[1];                    // two lie side by side
    } else if (ds.size() == 3) {
        // Three in a triangle: the circle round them is 2.155 times one wire's radius, which for
        // unequal wires is well approximated from the two largest.
        std::sort(ds.begin(), ds.end(), std::greater<double>());
        result = (ds[0] + ds[1]) * 1.077;
    } else {
        double sumSq = 0.0;
        for (double d : ds) sumSq += d * d;
        result = factor * std::sqrt(sumSq);
    }
    return std::max(result, minMm);
}

double autoPackingFactor(int wireCount) {
    // Loose at the small end, tight at the large one. A handful of conductors cannot help leaving air
    // between them; thirty of them settle into something much closer to a solid round bundle.
    if (wireCount <= 3) return 1.25;
    if (wireCount <= 6) return 1.22;
    if (wireCount <= 12) return 1.20;
    if (wireCount <= 30) return 1.18;
    return 1.15;
}

QString BundleCalc::explanation() const {
    QStringList out;
    if (rule == QLatin1String("random-lay")) {
        out << QStringLiteral(
            "The conductors are treated as a random lay: their areas add, so the diameter goes as the "
            "square root of the sum of the squares. Doubling the number of identical wires therefore "
            "multiplies the bundle by √2, not by 2.");
        out << QStringLiteral(
            "k_packing is the allowance for the air between them - a bundle is never solid copper. "
            "%1 was used here%2. The usual range is 1.15 to 1.25: loose at the small end, tighter as "
            "the bundle grows and the conductors settle.")
                   .arg(QString::number(packingFactor, 'f', 3),
                        packingAuto ? QStringLiteral(", from the %1 conductors counted")
                                          .arg(counted)
                                    : QStringLiteral(", entered by hand"));
    } else if (rule == QLatin1String("single")) {
        out << QStringLiteral(
            "One conductor is its own bundle, so there is nothing to pack and k_packing does not "
            "come into it.");
    } else if (rule == QLatin1String("pair")) {
        out << QStringLiteral(
            "Two conductors lie side by side, and the circle round them is simply the two diameters "
            "added. The square-root formula would give %1 mm here - about 12 %% under - because it "
            "describes a random lay, and two wires have no room to be random.")
                   .arg(root * packingFactor, 0, 'f', 2);
    } else if (rule == QLatin1String("triple")) {
        out << QStringLiteral(
            "Three conductors settle into a triangle, and the circle round three equal ones is "
            "2.155 times one diameter. That geometry is exact, so it is used instead of the "
            "square-root formula.");
    }
    if (unknown > 0)
        out << QStringLiteral(
            "%1 conductor(s) have no diameter and no cross-section to work one out from. They are "
            "LEFT OUT rather than counted as nothing, so this figure is short by however much they "
            "would have added.")
                   .arg(unknown);
    if (clampedToMinimum)
        out << QStringLiteral(
            "Nothing was counted, so the answer is the smallest body the drawing will draw rather "
            "than a calculated diameter.");
    out << QStringLiteral(
        "What this does NOT allow for: a screen or a braid over the bundle, the wall of a sleeve or "
        "a conduit, tape, or the way a bundle flattens where it is tied. Add those to the figure "
        "before choosing a boot or a gland.");
    return out.join(QStringLiteral("\n\n"));
}

BundleCalc bundleCalculation(const std::vector<double>& wireOds, double packingFactor,
                             double minMm) {
    BundleCalc c;
    for (double d : wireOds) {
        if (d > 0.0) c.wireOds.push_back(d);
        else ++c.unknown;
    }
    std::sort(c.wireOds.begin(), c.wireOds.end(), std::greater<double>());
    c.counted = static_cast<int>(c.wireOds.size());
    for (double d : c.wireOds) c.sumOfSquares += d * d;
    c.root = std::sqrt(c.sumOfSquares);
    c.packingAuto = packingFactor <= 0.0;
    c.packingFactor = c.packingAuto ? autoPackingFactor(c.counted) : packingFactor;

    auto mm = [](double v) { return QString::number(v, 'f', 3); };
    if (c.counted == 0) {
        c.rule = QStringLiteral("none");
        c.result = minMm;
        c.clampedToMinimum = true;
        c.workings << QStringLiteral("no conductor has a diameter, so there is nothing to add up");
    } else if (c.counted == 1) {
        c.rule = QStringLiteral("single");
        c.result = c.wireOds[0];
        c.workings << QStringLiteral("D_bundle = D_wire  (one conductor is its own bundle)")
                   << QStringLiteral("         = %1 mm").arg(mm(c.result));
    } else if (c.counted == 2) {
        c.rule = QStringLiteral("pair");
        c.result = c.wireOds[0] + c.wireOds[1];
        c.workings << QStringLiteral("D_bundle = D₁ + D₂  (two lie side by side)")
                   << QStringLiteral("         = %1 + %2").arg(mm(c.wireOds[0]), mm(c.wireOds[1]))
                   << QStringLiteral("         = %1 mm").arg(mm(c.result));
    } else if (c.counted == 3) {
        c.rule = QStringLiteral("triple");
        c.result = (c.wireOds[0] + c.wireOds[1]) * 1.077;
        c.workings << QStringLiteral("D_bundle = (D₁ + D₂) × 1.077  (three settle into a triangle)")
                   << QStringLiteral("         = (%1 + %2) × 1.077")
                          .arg(mm(c.wireOds[0]), mm(c.wireOds[1]))
                   << QStringLiteral("         = %1 mm").arg(mm(c.result));
    } else {
        c.rule = QStringLiteral("random-lay");
        c.result = c.root * c.packingFactor;
        QStringList squares;
        for (double d : c.wireOds) squares << mm(d);
        c.workings << QStringLiteral("D_bundle = √(Σ D²_wire) × k_packing")
                   << QStringLiteral("Σ D²_wire = %1²  (mm)")
                          .arg(squares.join(QStringLiteral("² + ")))
                   << QStringLiteral("         = %1 mm²").arg(mm(c.sumOfSquares))
                   << QStringLiteral("√(Σ D²_wire) = %1 mm").arg(mm(c.root))
                   << QStringLiteral("k_packing = %1  (%2)")
                          .arg(QString::number(c.packingFactor, 'f', 3),
                               c.packingAuto
                                   ? QStringLiteral("automatic, for %1 conductors").arg(c.counted)
                                   : QStringLiteral("entered"))
                   << QStringLiteral("D_bundle = %1 × %2 = %3 mm")
                          .arg(mm(c.root), QString::number(c.packingFactor, 'f', 3),
                               mm(c.result));
    }
    if (c.result < minMm) {
        c.result = minMm;
        c.clampedToMinimum = true;
    }
    return c;
}

double segmentBundleDiameterMm(const MechHarnessDef& def, const MechSegment& seg) {
    std::vector<double> ods;
    for (const QString& id : seg.wireIds) {
        const MechWire* w = def.wire(id);
        if (!w) continue;
        ods.push_back(w->odMm > 0.0 ? w->odMm : wireOuterDiameterMm(w->gauge));
    }
    return bundleDiameterMm(ods, def.draw.bundleFactor, def.draw.minBundleMm);
}

double segmentBodyDiameterMm(const MechHarnessDef& def, const MechSegment& seg) {
    if (seg.jacketOdMm > 0.0) return seg.jacketOdMm;
    return segmentBundleDiameterMm(def, seg);
}

double segmentEndDiameterMm(const MechHarnessDef& def, const MechSegment& seg, bool atToEnd) {
    if (seg.jacketOdMm <= 0.0) return segmentBundleDiameterMm(def, seg);
    return seg.stripAt(atToEnd) > 0.0 ? segmentBundleDiameterMm(def, seg) : seg.jacketOdMm;
}

const MechSegment* nodeRunSegment(const MechHarnessDef& def, const MechNode& node, bool* atToEnd) {
    if (atToEnd) *atToEnd = true;
    if (const MechSegment* seg = def.segment(node.parentSegment)) {
        if (atToEnd) *atToEnd = seg->toNode == node.id;
        return seg;
    }
    // The root: no run ARRIVES here, one leaves. A connector at the start of a trunk is still a
    // connector - it takes a backshell, and the jacket is cut back so its contacts can be reached - so
    // the run that leaves is the run at this point, taken by its own start.
    const std::vector<const MechSegment*> kids = def.childSegments(node.id);
    if (kids.size() != 1) return nullptr;    // a breakout: no single run is "the" run at it
    if (atToEnd) *atToEnd = false;
    return kids.front();
}

double nodeBareLengthMm(const MechHarnessDef& def, const MechNode& node) {
    bool atToEnd = true;
    const MechSegment* seg = nodeRunSegment(def, node, &atToEnd);
    if (!seg || seg->jacketOdMm <= 0.0) return 0.0;
    // A node sits at one END of its run, so the strip that concerns it is the one at that end.
    return seg->stripAt(atToEnd);
}

bool mechOverTheWires(PartKind kind) {
    // Pulled over the conductors themselves rather than being a body in its own right. A braid is
    // combed over the bare wires and heatshrink is recovered onto whatever is under it; a backshell or
    // a boot is a thing with a bore, and it stacks back from the connector.
    return kind == PartKind::ShieldBraid || kind == PartKind::Heatshrink ||
           kind == PartKind::Tape || kind == PartKind::Marker;
}

double fittingFitDiameterMm(const MechHarnessDef& def, const MechSegment* seg, PartKind kind,
                            bool atEnd, bool atToEnd) {
    if (!seg) return def.draw.minBundleMm;
    // Over the bare conductors, the bundle is the figure whether the jacket is known or not. Anything
    // else on a cable is sized on the jacket - so if that is missing, the answer is "not known", not
    // the bundle. The validator asks for it by name.
    const bool overWires = atEnd && mechOverTheWires(kind);
    if (!overWires && seg->cableWithoutJacket()) return 0.0;
    if (!atEnd) return segmentBodyDiameterMm(def, *seg);     // along the run: over the body
    if (mechOverTheWires(kind)) return segmentEndDiameterMm(def, *seg, atToEnd);
    // A backshell, a boot, a gland: the cable goes INTO it, so its entry has to take the jacket even
    // when the last few millimetres in front of it are bare conductors. Sizing one of these on the
    // stripped bundle is how a boot that cannot be got over the cable ends up on a drawing.
    return segmentBodyDiameterMm(def, *seg);
}

std::vector<MechStation> segmentProfile(const MechHarnessDef& def, const MechSegment& seg) {
    const double len = std::max(0.0, seg.lengthMm);
    const double bundle = segmentBundleDiameterMm(def, seg);
    std::vector<MechStation> out;
    if (seg.jacketOdMm <= 0.0) {
        out.push_back({0.0, bundle});
        out.push_back({len, bundle});
        return out;
    }
    const double jacket = seg.jacketOdMm;
    double sFrom = std::max(0.0, seg.stripFromMm);
    double sTo = std::max(0.0, seg.stripToMm);
    // Stripped back further than the run is long, from both ends: there is no jacket left, so it is
    // bare wire from end to end rather than a cone that crosses itself.
    if (sFrom + sTo >= len) {
        out.push_back({0.0, bundle});
        out.push_back({len, bundle});
        return out;
    }
    // The cone from the bundle out to the jacket. A cut jacket does not step: the wires spread out of
    // it over about the difference in diameter, and never over more than the strip itself.
    auto cone = [&](double strip) {
        return std::min(strip, std::max(1.0, std::abs(jacket - bundle)));
    };
    out.push_back({0.0, sFrom > 0.0 ? bundle : jacket});
    if (sFrom > 0.0) {
        out.push_back({sFrom - cone(sFrom), bundle});
        out.push_back({sFrom, jacket});
    }
    if (sTo > 0.0) {
        out.push_back({len - sTo, jacket});
        out.push_back({len - sTo + cone(sTo), bundle});
    }
    out.push_back({len, sTo > 0.0 ? bundle : jacket});
    // Two stations at the same place say nothing; drop the duplicates a zero-length cone leaves.
    std::vector<MechStation> tidy;
    for (const MechStation& s : out)
        if (tidy.empty() || std::abs(s.atMm - tidy.back().atMm) > 1e-9 ||
            std::abs(s.diameterMm - tidy.back().diameterMm) > 1e-9)
            tidy.push_back(s);
    return tidy;
}

// ---- Where a conductor changes gauge ------------------------------------------------------------

namespace {

// The sizes a set of conductors is made up of, largest first, as a line of text - "1.0, 0.5, 0.5".
QString gaugeList(const MechHarnessDef& def, const QStringList& wireIds) {
    std::vector<double> mm2;
    for (const QString& id : wireIds) {
        const MechWire* w = def.wire(id);
        if (!w) continue;
        const double a = gaugeMm2(w->gauge);
        if (a > 0.0) mm2.push_back(a);
    }
    std::sort(mm2.begin(), mm2.end(), std::greater<double>());
    QStringList parts;
    for (double a : mm2) parts << QString::number(a, 'g', 3);
    return parts.join(QStringLiteral(", "));
}

} // namespace

std::vector<MechGaugeChange> mechGaugeChanges(const MechHarnessDef& def) {
    std::vector<MechGaugeChange> out;
    for (const MechNode& n : def.nodes) {
        const MechSegment* in = def.segment(n.parentSegment);
        if (!in) continue;
        for (const MechSegment* child : def.childSegments(n.id)) {
            // Which conductors stop here, and which start. A breakout carries the SAME conductors on
            // into its branches, so nothing starts or stops and there is no transition; a splice ends
            // one wire and begins another, which is what this finds.
            QStringList ended, started;
            for (const QString& id : in->wireIds)
                if (!child->wireIds.contains(id)) ended << id;
            for (const QString& id : child->wireIds)
                if (!in->wireIds.contains(id)) started << id;
            if (ended.isEmpty() || started.isEmpty()) continue;
            const QString a = gaugeList(def, ended);
            const QString b = gaugeList(def, started);
            if (a.isEmpty() || b.isEmpty() || a == b) continue;   // a re-route, not a re-gauge
            MechGaugeChange g;
            g.nodeId = n.id;
            g.nodeName = n.name;
            g.fromSegment = in->id;
            g.toSegment = child->id;
            g.fromGauges = a;
            g.toGauges = b;
            g.diameterMm = std::max(segmentBodyDiameterMm(def, *in),
                                    segmentBodyDiameterMm(def, *child));
            out.push_back(g);
            break;    // one transition per junction: it is the junction that gets the heatshrink
        }
    }
    return out;
}

// ---- The bill of materials ----------------------------------------------------------------------

namespace {

// The order the released drawings list things in: what a harness is built FROM, in the order it is
// built. Connectors and their contacts, the backshells that dress them, then the cable and wire, then
// what goes over it, then what is written on it, then what it is stuck together with.
int bomRank(PartKind kind) {
    switch (kind) {
        case PartKind::Connector:     return 0;
        case PartKind::ContactPin:
        case PartKind::Crimp:
        case PartKind::Ferrule:
        case PartKind::Terminal:
        case PartKind::Splice:        return 1;
        case PartKind::Backshell:
        case PartKind::CableGland:
        case PartKind::Grommet:
        case PartKind::Overmould:     return 2;
        case PartKind::Cable:         return 3;
        case PartKind::Wire:          return 4;
        case PartKind::Heatshrink:
        case PartKind::HeatshrinkBoot:
        case PartKind::YSplit:        return 5;
        case PartKind::ShieldBraid:
        case PartKind::Sleeve:
        case PartKind::Conduit:       return 6;
        case PartKind::CableTie:
        case PartKind::Tape:          return 7;
        case PartKind::Marker:        return 8;
        default:                      return 9;
    }
}

// Anything cut off a reel is bought by length; a thing is bought by the piece.
bool boughtByLength(PartKind kind) {
    switch (kind) {
        case PartKind::Wire:
        case PartKind::Cable:
        case PartKind::Heatshrink:
        case PartKind::ShieldBraid:
        case PartKind::Sleeve:
        case PartKind::Conduit:
        case PartKind::Tape:
            return true;
        default:
            return false;
    }
}

// What makes two of these ONE line: the part number when there is one, else what it is and what it is
// called. A part used at four ends is one row whose quantity is the total.
QString bomKey(const QString& libPart, PartKind kind, const QString& description) {
    if (!libPart.trimmed().isEmpty()) return QStringLiteral("p:") + libPart;
    return QStringLiteral("k:%1|%2").arg(static_cast<int>(kind)).arg(description.trimmed().toLower());
}

} // namespace

std::vector<MechBomLine> mechBom(const MechHarnessDef& def, const PartsCatalog* catalog) {
    std::vector<MechBomLine> lines;
    QHash<QString, int> at;      // key -> index in `lines`

    auto add = [&](const QString& libPart, PartKind kind, const QString& description, double qty,
                   const QString& where) {
        const LibraryPart* part = catalog && !libPart.isEmpty() ? catalog->byId(libPart) : nullptr;
        MechBomLine line;
        line.libPart = libPart;
        line.kind = part ? part->kind : kind;
        line.description = part && !part->description.isEmpty() ? part->description : description;
        if (part) {
            line.mfrPn = part->mfrPn;
            line.mfrName = part->mfrName;
            line.unit = part->unit;
        }
        // Nothing said: a thing is counted each, a length in the millimetres the drawing is in.
        if (line.unit.trimmed().isEmpty())
            line.unit = boughtByLength(line.kind) ? QStringLiteral("mm") : QStringLiteral("each");
        const QString key = bomKey(line.libPart, line.kind, line.description);
        auto it = at.find(key);
        if (it == at.end()) {
            line.qty = qty;
            if (!where.isEmpty()) line.where << where;
            at.insert(key, static_cast<int>(lines.size()));
            lines.push_back(line);
            return;
        }
        MechBomLine& have = lines[static_cast<size_t>(it.value())];
        have.qty += qty;
        if (!where.isEmpty() && !have.where.contains(where)) have.where << where;
    };

    auto runName = [&def](const MechSegment& s) {
        const MechNode* to = def.node(s.toNode);
        return to && !to->name.isEmpty() ? QStringLiteral("the run to %1").arg(to->name) : s.id;
    };

    // The connectors, and what dresses each of them.
    for (const MechNode& n : def.nodes) {
        if (n.kind == MechNodeKind::Connector && !n.libPart.isEmpty())
            add(n.libPart, PartKind::Connector, QStringLiteral("Connector"), 1.0, n.name);
        bool atToEnd = true;
        const MechSegment* seg = nodeRunSegment(def, n, &atToEnd);
        const double bundleMm = seg ? segmentBodyDiameterMm(def, *seg) : def.draw.minBundleMm;
        for (const MechFitting& f : n.fittings) {
            const LibraryPart* part = catalog ? catalog->byId(f.libPart) : nullptr;
            double lengthMm = 0.0, odMm = 0.0;
            fittingSize(f, part, bundleMm, lengthMm, odMm);
            const PartKind kind = part ? part->kind : f.kind;
            add(f.libPart, kind, f.name.isEmpty() ? partKindName(kind) : f.name,
                boughtByLength(kind) ? lengthMm : 1.0, n.name);
        }
    }

    // The cable, the conductors, and what is fitted along each run.
    for (const MechSegment& s : def.segments) {
        const QString where = runName(s);
        if (s.isCable() || !s.cablePart.isEmpty())
            add(s.cablePart, PartKind::Cable,
                s.cableName.isEmpty() ? QStringLiteral("Cable") : s.cableName, s.lengthMm, where);
        for (const MechFitting& f : s.coverings) {
            const LibraryPart* part = catalog ? catalog->byId(f.libPart) : nullptr;
            double lengthMm = 0.0, odMm = 0.0;
            fittingSize(f, part, segmentBodyDiameterMm(def, s), lengthMm, odMm);
            const PartKind kind = part ? part->kind : f.kind;
            // A covering with no length of its own runs the length of the run, as it is drawn.
            if (f.lengthMm <= 0.0 && (!part || partNumber(*part, QStringLiteral("length")) <= 0.0))
                lengthMm = s.lengthMm;
            add(f.libPart, kind, f.name.isEmpty() ? partKindName(kind) : f.name,
                boughtByLength(kind) ? lengthMm : 1.0, where);
        }
    }

    // The conductors themselves, by part or by gauge: a cable's cores are inside the cable and are not
    // bought again, so only the loose wire is listed.
    for (const MechWire& w : def.wires) {
        double lengthMm = 0.0;
        for (const MechSegment& s : def.segments)
            if (s.wireIds.contains(w.id) && !s.isCable() && s.cablePart.isEmpty())
                lengthMm += s.lengthMm;
        if (lengthMm <= 0.0) continue;
        const QString desc = w.gauge.isEmpty()
                                 ? QStringLiteral("Wire")
                                 : QStringLiteral("Wire, %1 mm²").arg(w.gauge);
        for (const MechSegment& s : def.segments)
            if (s.wireIds.contains(w.id) && !s.isCable() && s.cablePart.isEmpty())
                add(w.libPart, PartKind::Wire, desc, 0.0, runName(s));
        add(w.libPart, PartKind::Wire, desc, lengthMm, QString());
    }

    // In the order a drawing lists them, and only then numbered - so the item numbers read down the
    // table rather than following whatever order the harness happened to be typed in.
    std::stable_sort(lines.begin(), lines.end(),
                     [](const MechBomLine& a, const MechBomLine& b) {
                         if (bomRank(a.kind) != bomRank(b.kind))
                             return bomRank(a.kind) < bomRank(b.kind);
                         return a.description.localeAwareCompare(b.description) < 0;
                     });
    for (size_t i = 0; i < lines.size(); ++i) lines[i].item = static_cast<int>(i) + 1;
    return lines;
}

int mechBomItem(const std::vector<MechBomLine>& bom, const QString& libPart, PartKind kind,
                const QString& description) {
    const QString key = bomKey(libPart, kind, description);
    for (const MechBomLine& l : bom)
        if (bomKey(l.libPart, l.kind, l.description) == key) return l.item;
    // A description that was replaced by the library part's own: match on the part alone.
    if (!libPart.trimmed().isEmpty())
        for (const MechBomLine& l : bom)
            if (l.libPart == libPart) return l.item;
    return 0;
}

// ---- Process notes ------------------------------------------------------------------------------

std::vector<MechDeltaNote> mechDeltaNotes(const MechHarnessDef& def) {
    std::vector<MechDeltaNote> out;
    // The one the definition can work out for itself. The sample drawings write it exactly this way,
    // and it is note 2 on SMP-00001.
    MechDeltaNote gauge;
    gauge.text = QStringLiteral("WIRES GAUGE TRANSITION LOCATION.");
    for (const MechGaugeChange& g : mechGaugeChanges(def))
        if (!gauge.atNodes.contains(g.nodeId)) gauge.atNodes << g.nodeId;
    if (!gauge.atNodes.isEmpty()) out.push_back(gauge);

    // A Y-splitter boot is filled with epoxy where it meets the heatshrink on each leg. That is a thing
    // done rather than a thing drawn, so it is a note - and where it applies follows from the boot
    // being fitted at all.
    MechDeltaNote ysplit;
    ysplit.text = QStringLiteral("APPLY EPOXY OVER Y-SPLITTER, OVERLAP 2 MM ONTO HEATSHRINK ON EACH "
                                 "LEG.");
    for (const MechNode& n : def.nodes)
        for (const MechFitting& f : n.fittings)
            if (f.kind == PartKind::YSplit && !ysplit.atNodes.contains(n.id)) ysplit.atNodes << n.id;
    if (!ysplit.atNodes.isEmpty()) out.push_back(ysplit);

    for (size_t i = 0; i < out.size(); ++i) out[i].number = static_cast<int>(i) + 1;
    return out;
}

// ---- Layout -------------------------------------------------------------------------------------

const MechPlacedNode* MechLayout::node(const QString& id) const {
    for (const MechPlacedNode& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const MechPlacedSegment* MechLayout::segment(const QString& id) const {
    for (const MechPlacedSegment& s : segments)
        if (s.id == id) return &s;
    return nullptr;
}

MechLayout layoutMechanicalHarness(const MechHarnessDef& def) {
    MechLayout out;
    const MechNode* root = def.rootNode();
    if (!root) return out;
    out.nodes.push_back({root->id, def.draw.origin, 0.0});

    // Breadth-first from the root, so a branch of a branch is placed once its parent is. A node
    // whose parent never gets placed is simply not placed - the validator says so.
    bool grew = true;
    while (grew) {
        grew = false;
        for (const MechNode& n : def.nodes) {
            if (out.node(n.id)) continue;
            const MechSegment* seg = def.segment(n.parentSegment);
            if (!seg) continue;
            const MechPlacedNode* parent = out.node(seg->fromNode);
            if (!parent) continue;
            const double dirDeg = parent->dirDeg + n.angleDeg;
            const double r = rad(dirDeg);
            const double len = seg->lengthMm * def.draw.scale;
            const Vec2 at{parent->at.x + std::cos(r) * len, parent->at.y + std::sin(r) * len};
            out.nodes.push_back({n.id, at, dirDeg});
            out.segments.push_back({seg->id, parent->at, at, dirDeg,
                                    segmentBodyDiameterMm(def, *seg),
                                    segmentEndDiameterMm(def, *seg, /*atToEnd=*/false),
                                    segmentEndDiameterMm(def, *seg, /*atToEnd=*/true)});
            grew = true;
        }
    }
    return out;
}

// ---- Fit ----------------------------------------------------------------------------------------

bool mechFittingFits(const LibraryPart& part, double bundleMm, QString& why, bool* known) {
    why.clear();
    const QString key = bundleWindowKey(part.kind);
    if (key.isEmpty()) {
        if (known) *known = true;      // nothing a bundle passes through: nothing to check
        return true;
    }
    double lo = 0.0, hi = 0.0;
    partWindow(part, key, lo, hi);
    if (lo <= 0.0 && hi <= 0.0) {
        if (known) *known = false;
        return true;                   // unstated: a gap, not a failure
    }
    if (known) *known = true;
    if (hi > 0.0 && bundleMm > hi) {
        why = QStringLiteral("the bundle is %1 mm and this takes up to %2 mm")
                  .arg(bundleMm, 0, 'f', 1)
                  .arg(hi, 0, 'f', 1);
        return false;
    }
    if (lo > 0.0 && bundleMm < lo) {
        // Heatshrink recovers onto something smaller; anything rigid does not grip at all.
        const bool shrinks = part.kind == PartKind::Heatshrink ||
                             part.kind == PartKind::HeatshrinkBoot || part.kind == PartKind::YSplit;
        why = shrinks ? QStringLiteral("the bundle is %1 mm and this recovers to %2 mm at best")
                            .arg(bundleMm, 0, 'f', 1)
                            .arg(lo, 0, 'f', 1)
                      : QStringLiteral("the bundle is %1 mm and this needs at least %2 mm")
                            .arg(bundleMm, 0, 'f', 1)
                            .arg(lo, 0, 'f', 1);
        return false;
    }
    return true;
}

std::vector<MechIssue> validateMechanicalHarness(const MechHarnessDef& def,
                                                 const PartsCatalog* catalog) {
    std::vector<MechIssue> out;
    auto gap = [&out](const QString& id, const QString& name, const QString& prop,
                      const QString& msg) {
        out.push_back({MechIssueKind::Gap, id, name, prop, msg});
    };
    auto violation = [&out](const QString& id, const QString& name, const QString& msg) {
        out.push_back({MechIssueKind::Violation, id, name, QString(), msg});
    };
    auto warning = [&out](const QString& id, const QString& name, const QString& msg) {
        out.push_back({MechIssueKind::Warning, id, name, QString(), msg});
    };

    if (!def.rootNode())
        violation(QString(), def.name, QStringLiteral("Nothing to draw: the harness has no root."));

    // Conductors whose size is not known: the bundle cannot be worked out without them, and a
    // guessed diameter is a drawing nobody can build to.
    for (const MechWire& w : def.wires)
        if (w.odMm <= 0.0 && wireOuterDiameterMm(w.gauge) <= 0.0)
            gap(w.id, w.label.isEmpty() ? w.id : w.label, QStringLiteral("Diameter"),
                QStringLiteral("neither a cross-section nor an outside diameter"));

    const MechLayout layout = layoutMechanicalHarness(def);

    for (const MechSegment& s : def.segments) {
        const MechNode* to = def.node(s.toNode);
        const QString name = to ? to->name : s.id;
        if (s.lengthMm <= 0.0)
            gap(s.id, name, QStringLiteral("Length"),
                QStringLiteral("how long this run is, in millimeters"));
        if (!def.node(s.fromNode) || !to)
            violation(s.id, name, QStringLiteral("This run does not join two points."));
        if (s.wireIds.isEmpty())
            warning(s.id, name, QStringLiteral("No conductors run through it, so it is drawn at "
                                               "the smallest body there is."));
        for (const QString& wid : s.wireIds)
            if (!def.wire(wid))
                violation(s.id, name, QStringLiteral("It routes a conductor that is not in the "
                                                     "list (%1).").arg(wid));
        if (s.cableWithoutJacket()) {
            // The one figure that decides what can be fitted over this run, and the drawing does not
            // have it. Said as a gap naming the cable, because the answer is on the drum: a cable is
            // bought by its jacket diameter, and everything that goes over it is chosen against that
            // and not against what its cores add up to.
            gap(s.id, name, QStringLiteral("Jacket diameter"),
                QStringLiteral("every conductor here is a core of %1: what does it measure over the "
                               "jacket? Until that is in, a boot or heatshrink for this run cannot be "
                               "checked").arg(s.cableName));
        }
        if (s.isCable()) {
            // A jacket that will not hold what is inside it is not a cable, it is a mistake in one of
            // the two figures - and it would otherwise be drawn as a cable narrower than its own cores.
            const double bundle = segmentBundleDiameterMm(def, s);
            if (bundle > s.jacketOdMm + 1e-6)
                violation(s.id, name,
                          QStringLiteral("The jacket is Ø%1 mm and the conductors in it make Ø%2 mm: "
                                         "one of the two figures is wrong.")
                              .arg(s.jacketOdMm, 0, 'f', 1)
                              .arg(bundle, 0, 'f', 1));
            if (s.stripFromMm + s.stripToMm >= s.lengthMm && s.lengthMm > 0.0)
                warning(s.id, name,
                        QStringLiteral("Stripped back %1 mm at one end and %2 mm at the other, on a "
                                       "%3 mm run: no jacket is left, so it is drawn as bare wires.")
                            .arg(s.stripFromMm, 0, 'f', 1)
                            .arg(s.stripToMm, 0, 'f', 1)
                            .arg(s.lengthMm, 0, 'f', 1));
            const MechNode* toNode = def.node(s.toNode);
            if (s.stripToMm <= 0.0 && toNode && toNode->kind == MechNodeKind::Connector)
                gap(s.id, name, QStringLiteral("Wires without jacket"),
                    QStringLiteral("the jacket runs into %1: say how far back it is cut so the "
                                   "conductors can reach the contacts").arg(toNode->name));
        }
    }

    // Heatshrink that runs up to the connector face. The conductors have to fan out to the contacts and
    // be inspected, so a released drawing always stops the covering short by a stated distance - and
    // leaving that distance to whoever builds it is exactly the thing a drawing is for.
    for (const MechNode& n : def.nodes) {
        if (n.kind != MechNodeKind::Connector || n.openLengthMm > 0.0) continue;
        bool covered = false;
        for (const MechFitting& f : n.fittings)
            if (mechOverTheWires(f.kind)) covered = true;
        if (covered)
            gap(n.id, n.name, QStringLiteral("Wires open"),
                QStringLiteral("something covers the conductors right up to the face of %1: say how "
                               "far back it stops so they can reach the contacts").arg(n.name));
    }

    // A splice at a gauge change, and whether it is covered.
    for (const MechGaugeChange& g : mechGaugeChanges(def)) {
        if (def.draw.spliceHeatshrink)
            warning(g.nodeId, g.nodeName,
                    QStringLiteral("The conductors change size here (%1 → %2 mm²), so there is a "
                                   "splice: heatshrink is drawn over it.")
                        .arg(g.fromGauges, g.toGauges));
        else
            violation(g.nodeId, g.nodeName,
                      QStringLiteral("The conductors change size here (%1 → %2 mm²), so there is a "
                                     "splice, and nothing covers it.")
                          .arg(g.fromGauges, g.toGauges));
    }

    for (const MechNode& n : def.nodes) {
        if (!n.parentSegment.isEmpty() && !layout.node(n.id))
            violation(n.id, n.name,
                      QStringLiteral("It hangs off a run that leads nowhere, so it is not drawn."));
    }

    // Fit: every fitting against the bundle it is fitted over.
    auto checkFittings = [&](const std::vector<MechFitting>& fittings, double bundleMm,
                             const QString& whereId, const QString& whereName) {
        for (const MechFitting& f : fittings) {
            const QString what = f.name.isEmpty() ? partKindName(f.kind) : f.name;
            const LibraryPart* part = catalog ? catalog->byId(f.libPart) : nullptr;
            if (!f.libPart.isEmpty() && !part) {
                violation(whereId, whereName,
                          QStringLiteral("%1: its library part is not in any connected library.")
                              .arg(what));
                continue;
            }
            if (!part) {
                // Typed on the spot: check what it does say about itself.
                if (f.idMm > 0.0 && bundleMm > f.idMm)
                    violation(whereId, whereName,
                              QStringLiteral("%1 at %2: the bundle is %3 mm and its bore is %4 mm.")
                                  .arg(what, whereName)
                                  .arg(bundleMm, 0, 'f', 1)
                                  .arg(f.idMm, 0, 'f', 1));
                else if (f.idMm <= 0.0)
                    gap(whereId, whereName, QStringLiteral("Bore"),
                        QStringLiteral("%1 has no inside diameter, so fit cannot be checked")
                            .arg(what));
                continue;
            }
            QString why;
            bool known = false;
            if (!mechFittingFits(*part, bundleMm, why, &known))
                violation(whereId, whereName,
                          QStringLiteral("%1 (%2): %3.").arg(what, part->mfrPn, why));
            else if (!known)
                gap(whereId, whereName, QStringLiteral("Entry size"),
                    QStringLiteral("%1 (%2) does not state what it fits over")
                        .arg(what, part->mfrPn.isEmpty() ? part->description : part->mfrPn));
        }
    };

    // Every fitting against what IT has to go over: the bare bundle for a braid, the whole cable for a
    // backshell, the body for a sleeve along the run. A figure of 0 means the jacket is not known, and
    // then there is nothing to check against - the gap above has already asked for it, and checking
    // against the cores instead would report a fit that is not the one being made.
    for (const MechSegment& s : def.segments) {
        const MechNode* to = def.node(s.toNode);
        for (const MechFitting& f : s.coverings) {
            const double d = fittingFitDiameterMm(def, &s, f.kind, /*atEnd=*/false, true);
            if (d > 0.0) checkFittings({f}, d, s.id, to ? to->name : s.id);
        }
    }
    for (const MechNode& n : def.nodes) {
        const MechSegment* seg = def.segment(n.parentSegment);
        const bool atToEnd = seg ? seg->toNode == n.id : true;
        for (const MechFitting& f : n.fittings) {
            const double d = fittingFitDiameterMm(def, seg, f.kind, /*atEnd=*/true, atToEnd);
            if (d > 0.0) checkFittings({f}, d, n.id, n.name);
        }
    }
    return out;
}

// ---- The drawing --------------------------------------------------------------------------------

namespace {

// The library part's own mechanical drawing, moved and turned onto the run. Empty when it has none.
std::vector<Entity> placedPartDrawing(const LibraryPart& part, const Vec2& at, double dirRad,
                                      const MechDrawOptions& opt, const QString& role) {
    std::vector<Entity> out;
    if (!part.hasMechanical()) return out;
    out = partDrawingEntities(part.mechanical);
    if (out.empty()) return out;
    // Centre what was drawn on the point it is fitted at, then turn it along the run.
    Bounds b;
    for (const Entity& e : out) {
        const Bounds eb = e.bounds();
        if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
    }
    const Vec2 mid = b.valid ? Vec2{(b.min.x + b.max.x) / 2.0, (b.min.y + b.max.y) / 2.0}
                             : Vec2{0.0, 0.0};
    for (Entity& e : out) {
        translateEntity(e, at - mid);
        if (std::abs(dirRad) > 1e-9) rotateEntity(e, at, dirRad);
        e.layer = opt.layer;
        e.meta.insert(QStringLiteral("mechRole"), role);
    }
    return out;
}

} // namespace

std::vector<Entity> buildMechanicalHarness(const MechHarnessDef& def, const PartsCatalog* catalog) {
    std::vector<Entity> out;
    const MechDrawOptions& opt = def.draw;
    const MechLayout layout = layoutMechanicalHarness(def);
    if (layout.segments.empty() && layout.nodes.empty()) return out;

    // Lengths run along the harness at the drawing's scale; widths get the exaggeration as well, so
    // the body can be seen without the lengths being touched.
    const double xg = std::max(0.01, opt.diameterExaggeration);
    auto along = [&opt](double mm) { return mm * opt.scale; };
    auto across = [&opt, xg](double mm) { return mm * opt.scale * xg; };

    // The balloons carry BOM ITEM numbers, not a running count. A balloon that says 11 means "item 11
    // of the table", so the sheet and the table are one document rather than two that have to be kept
    // in step by hand.
    const std::vector<MechBomLine> bom = mechBom(def, catalog);
    auto itemOf = [&bom, catalog](const QString& libPart, PartKind kind, const QString& name) {
        const LibraryPart* part = catalog && !libPart.isEmpty() ? catalog->byId(libPart) : nullptr;
        const QString desc = part && !part->description.isEmpty() ? part->description : name;
        return mechBomItem(bom, libPart, part ? part->kind : kind, desc);
    };
    auto balloonText = [](int item) {
        return item > 0 ? QString::number(item) : QStringLiteral("?");
    };

    // Where the conductors run OPEN, the bundle has no body: it is not a bundle there, it is so many
    // separate wires on their way to the contacts. So the outline stops short of the connector by that
    // distance and the wires themselves are drawn across the gap.
    auto openAtNode = [&def](const QString& nodeId) {
        const MechNode* n = def.node(nodeId);
        if (!n) return 0.0;
        if (n->kind != MechNodeKind::Connector && n->kind != MechNodeKind::FreeEnd) return 0.0;
        return std::max(0.0, n->openLengthMm);
    };

    // ---- The outline of the harness ----
    // Every run has two edges, half its bundle either side of its centreline. At a junction an edge
    // stops where the next run's facing edge crosses it, so the outline runs continuously from one
    // branch into the next with a notch between them - the shape a breakout actually has. A run with
    // nothing at its end keeps a square cap there.
    //
    // A run whose jacket is stripped back is not one width from end to end: it is the bundle at the
    // stripped end, a cone, then the jacket. So each edge is a RAIL of points - one per change of
    // diameter - rather than two, and the mitring at a junction moves the point at the end without
    // touching the knees between.
    struct Body {
        std::vector<Vec2> plus, minus;   // from the run's start to its end
        double gap;                      // across the body, for placing what is written beside it
        // Whether each end runs on into another run. An end that does is not closed off across its
        // width; an end that stops is (see the bars below).
        bool mitredStart = false, mitredEnd = false;
    };
    QHash<QString, Body> bodies;
    for (const MechPlacedSegment& ps : layout.segments) {
        const MechSegment* seg = def.segment(ps.id);
        const double gap = across(ps.diameterMm);
        const Vec2 dir = (ps.b - ps.a).normalized();
        const Vec2 v = dir.perp();
        Body body;
        body.gap = gap;
        const std::vector<MechStation> profile =
            seg ? segmentProfile(def, *seg) : std::vector<MechStation>{};
        // The body stops short at either end where the conductors are open there.
        const double openFrom = seg ? openAtNode(seg->fromNode) : 0.0;
        const double openTo = seg ? openAtNode(seg->toNode) : 0.0;
        const double bodyFrom = openFrom;
        const double bodyTo = seg ? std::max(bodyFrom, seg->lengthMm - openTo) : 0.0;
        if (profile.size() >= 2 && seg && seg->lengthMm > 0.0 && bodyTo > bodyFrom) {
            for (const MechStation& st : profile) {
                const double atMm = std::clamp(st.atMm, bodyFrom, bodyTo);
                const Vec2 c = ps.a + dir * along(atMm);
                const double half = across(st.diameterMm) / 2.0;
                body.plus.push_back(c + v * half);
                body.minus.push_back(c - v * half);
            }
        } else {
            body.plus = {ps.a + v * (gap / 2.0), ps.b + v * (gap / 2.0)};
            body.minus = {ps.a - v * (gap / 2.0), ps.b - v * (gap / 2.0)};
        }
        bodies.insert(ps.id, body);
    }
    for (const MechPlacedNode& pn : layout.nodes) {
        // The runs meeting here, each as the way it LEAVES this node.
        struct Leg { QString segId; bool atFrom; Vec2 dir; double half; double angle; };
        std::vector<Leg> legs;
        for (const MechPlacedSegment& ps : layout.segments) {
            const MechSegment* seg = def.segment(ps.id);
            if (!seg) continue;
            // Half the width AT THIS END - which is the bundle, not the jacket, on a cable that has
            // come out of its jacket before it reaches the junction.
            if (seg->fromNode == pn.id) {
                const Vec2 d = (ps.b - ps.a).normalized();
                legs.push_back({ps.id, true, d, across(ps.fromDiameterMm) / 2.0,
                                std::atan2(d.y, d.x)});
            } else if (seg->toNode == pn.id) {
                const Vec2 d = (ps.a - ps.b).normalized();
                legs.push_back({ps.id, false, d, across(ps.toDiameterMm) / 2.0,
                                std::atan2(d.y, d.x)});
            }
        }
        if (legs.size() < 2) continue;   // one run: the square cap it already has
        std::sort(legs.begin(), legs.end(),
                  [](const Leg& a, const Leg& b) { return a.angle < b.angle; });

        // ---- A breakout is a SPREAD CONE, not a set of mitred corners ----
        // Where one run arrives and two or more leave, the accurate drawing is a funnel: the run stops
        // short and is closed off across its width, two lines converge from that bar to a narrower bar
        // at the junction - the throat, as wide as the runs leaving it laid side by side - and the
        // branches start on that bar. Mitring the outlines into one another instead drew the branches
        // as a sharp V from a single point with their inner edges CROSSING behind it, which is not a
        // shape a harness has.
        {
            const MechNode* node = def.node(pn.id);
            const Leg* in = nullptr;
            int leaving = 0;
            for (const Leg& l : legs) {
                if (l.atFrom) ++leaving;
                else in = &l;
            }
            if (node && in && leaving >= 2 && node->kind != MechNodeKind::Connector) {
                double throat = 0.0;
                for (const Leg& l : legs)
                    if (l.atFrom) throat += l.half * 2.0;
                throat = std::min(throat, in->half * 2.0);       // never wider than what arrives
                const Vec2 vin = in->dir.perp();
                const Vec2 t0 = pn.at + vin * (throat / 2.0);
                const Vec2 t1 = pn.at - vin * (throat / 2.0);

                // Pull the run that arrives back by the length of the funnel. Three times its own width
                // is what the released drawings use, bounded so a short run is not swallowed by it.
                const MechSegment* inSeg = def.segment(in->segId);
                const double inDiaMm = inSeg ? segmentEndDiameterMm(def, *inSeg, true) : 0.0;
                double spreadMm = std::clamp(3.0 * inDiaMm, 6.0, 40.0);
                if (inSeg) spreadMm = std::min(spreadMm, inSeg->lengthMm * 0.3);
                Body& bi = bodies[in->segId];
                if (!bi.plus.empty() && !bi.minus.empty() && spreadMm > 0.0) {
                    // in->dir points away from the node, back along the run that arrives.
                    const Vec2 back = in->dir * along(spreadMm);
                    // Whichever end of that body is here is the one that moves; a knee left beyond the
                    // new end goes with it, so the outline does not double back on itself.
                    const bool hereIsEnd = !in->atFrom;
                    if (hereIsEnd) {
                        const Vec2 np = bi.plus.back() + back, nm = bi.minus.back() + back;
                        // in->dir runs back along the arriving run, so a station the new end has moved
                        // PAST lies in that direction from it - and it is that one that goes.
                        while (bi.plus.size() > 2 &&
                               dot(np - bi.plus[bi.plus.size() - 2], in->dir) > 0.0) {
                            bi.plus.pop_back();
                            bi.minus.pop_back();
                        }
                        bi.plus.back() = np;
                        bi.minus.back() = nm;
                    }
                }
                // The cone itself: from the bar across the run to the bar across the throat.
                for (int side : {+1, -1}) {
                    const Vec2 from = side > 0 ? bi.plus.back() : bi.minus.back();
                    const Vec2 to = side > 0 ? t0 : t1;
                    Entity e = baseEntity(opt, QStringLiteral("bundle"));
                    e.type = EntityType::Line;
                    e.a = from;
                    e.b = to;
                    e.meta.insert(QStringLiteral("mechNode"), pn.id);
                    e.meta.insert(QStringLiteral("spread"), true);
                    e.meta.insert(QStringLiteral("side"), side);
                    out.push_back(e);
                }
                // The bar across the throat, once for the whole junction.
                {
                    Entity e = baseEntity(opt, QStringLiteral("bar"));
                    e.type = EntityType::Line;
                    e.a = t0;
                    e.b = t1;
                    e.meta.insert(QStringLiteral("mechNode"), pn.id);
                    e.meta.insert(QStringLiteral("sectionEnd"), true);
                    e.meta.insert(QStringLiteral("throat"), true);
                    out.push_back(e);
                }
                // Every branch starts ON that bar, and each has its OWN STRETCH of it: the runs come
                // out of the throat side by side, in the order they fan out, so a branch going one way
                // takes the near side of the bar and a branch going the other takes the far side.
                // Starting them all on the middle of the bar - which is where the junction point is -
                // put half of each branch on the wrong side of the other, and their outlines crossed.
                std::vector<const Leg*> outLegs;
                for (const Leg& l : legs)
                    if (l.atFrom) outLegs.push_back(&l);
                // Ordered across the bar the way they head off it.
                std::sort(outLegs.begin(), outLegs.end(), [&vin](const Leg* a, const Leg* b) {
                    return dot(a->dir, vin) > dot(b->dir, vin);
                });
                double wanted = 0.0;
                for (const Leg* l : outLegs) wanted += l->half * 2.0;
                const double barLen = distance(t0, t1);
                // The throat is never wider than the run that arrives, so where the branches laid side
                // by side would be wider than that they share it in proportion instead.
                const double squeeze = wanted > barLen && wanted > 1e-9 ? barLen / wanted : 1.0;
                double at = 0.0;   // how much of the bar is taken, measured from t0
                for (const Leg* l : outLegs) {
                    Body& bl = bodies[l->segId];
                    const double slot = l->half * 2.0 * squeeze;
                    const double slotFrom = at, slotTo = at + slot;
                    at += slot;
                    if (bl.plus.empty() || bl.minus.empty()) continue;
                    const Vec2 vl = l->dir.perp();
                    // Where a point lands on the bar, and kept WITHIN THIS BRANCH'S OWN STRETCH of it.
                    // Projecting onto the bar's line is not enough on its own: a branch leaving at an
                    // angle meets that line a little past its own stretch, and the two branches then
                    // overlap across the throat by that much - a millimetre of one drawn inside the
                    // other, which is exactly the crossing the funnel is there to do away with.
                    auto onBar = [&](const Vec2& railPoint) {
                        Vec2 hit = railPoint;
                        lineCross(railPoint, l->dir, t0, (t1 - t0).normalized(), 0.0, hit);
                        const double s = std::clamp(dot(t0 - hit, vin), slotFrom, slotTo);
                        return t0 - vin * s;
                    };
                    const Vec2 centre = t0 - vin * ((slotFrom + slotTo) / 2.0);
                    bl.plus.front() = onBar(centre + vl * (l->half * squeeze));
                    bl.minus.front() = onBar(centre - vl * (l->half * squeeze));
                    bl.mitredStart = true;   // the throat bar closes them, not a bar of their own
                }
                continue;   // no mitring at a junction that spreads
            }
        }

        double maxHalf = 0.0;
        for (const Leg& l : legs) maxHalf = std::max(maxHalf, l.half);
        // Going round the node, each leg's anticlockwise edge meets the next leg's clockwise one.
        for (size_t i = 0; i < legs.size(); ++i) {
            const Leg& a = legs[i];
            const Leg& b = legs[(i + 1) % legs.size()];
            const Vec2 pa = pn.at + a.dir.perp() * a.half;    // a's anticlockwise edge
            const Vec2 pb = pn.at - b.dir.perp() * b.half;    // b's clockwise edge
            Vec2 v;
            if (!lineCross(pa, a.dir, pb, b.dir, maxHalf * 30.0, v)) continue;
            // A leg's anticlockwise edge is the "+" side of its own segment when it leaves from the
            // segment's start, and the "-" side when it leaves from the end.
            Body& ba = bodies[a.segId];
            Body& bb = bodies[b.segId];
            if (ba.plus.empty() || bb.plus.empty()) continue;
            if (a.atFrom) { ba.plus.front() = v; ba.mitredStart = true; }
            else { ba.minus.back() = v; ba.mitredEnd = true; }
            if (b.atFrom) { bb.minus.front() = v; bb.mitredStart = true; }
            else { bb.plus.back() = v; bb.mitredEnd = true; }
        }
    }
    for (const MechPlacedSegment& ps : layout.segments) {
        const Body& b = bodies.value(ps.id);
        out.push_back(makeBundleEdge(opt, b.plus, ps.diameterMm, ps.id, +1));
        out.push_back(makeBundleEdge(opt, b.minus, ps.diameterMm, ps.id, -1));
        // A section of the outline is closed off ACROSS ITS WIDTH wherever it stops: at the end of a
        // cable, at the neck a boot squeezes the conductors down to, at the small end of a spread cone.
        // A released drawing has a bar at every one of them - a quarter of the objects in the hand-drawn
        // view of test1.dwx are these - and without them the outline reads as two loose lines running
        // off the end of the object. An end that runs ON into another run is not barred: there is
        // nothing there to close, the outline carries through the junction.
        if (b.plus.size() == b.minus.size() && b.plus.size() >= 2) {
            auto bar = [&](const Vec2& p, const Vec2& q) {
                if (distance(p, q) < 1e-9) return;
                // Its own role: a bar is a feature of the drawing in its own right, not another length
                // of the body, and anything counting bodies should not have to sort them out again.
                Entity e = baseEntity(opt, QStringLiteral("bar"));
                e.type = EntityType::Line;
                e.a = p;
                e.b = q;
                e.meta.insert(QStringLiteral("mechSeg"), ps.id);
                e.meta.insert(QStringLiteral("sectionEnd"), true);
                out.push_back(e);
            };
            if (!b.mitredStart) bar(b.plus.front(), b.minus.front());
            if (!b.mitredEnd) bar(b.plus.back(), b.minus.back());
            // And at a STEP in the diameter - two stations at the same place along the run, one wider
            // than the other. A change with a cone between them needs no bar: the cone closes it.
            for (size_t i = 1; i < b.plus.size(); ++i) {
                const Vec2 c0 = lerp(b.plus[i - 1], b.minus[i - 1], 0.5);
                const Vec2 c1 = lerp(b.plus[i], b.minus[i], 0.5);
                if (distance(c0, c1) > 1e-6) continue;                   // there is a cone here
                if (std::abs(distance(b.plus[i], b.minus[i]) -
                             distance(b.plus[i - 1], b.minus[i - 1])) < 1e-6)
                    continue;                                            // same width: nothing to close
                bar(b.plus[i], b.minus[i]);
            }
        }
    }

    // ---- What is written on each run, and what is fitted to it ----
    for (const MechPlacedSegment& ps : layout.segments) {
        const MechSegment* seg = def.segment(ps.id);
        if (!seg) continue;
        const double gap = across(ps.diameterMm);
        const Vec2 u = (ps.b - ps.a).normalized();
        const Vec2 v = u.perp();
        const Vec2 mid = lerp(ps.a, ps.b, 0.5);
        // Text reads along the run, and is turned the right way up rather than upside down on a
        // branch that heads left.
        double textDir = ps.dirDeg;
        while (textDir > 90.0) textDir -= 180.0;
        while (textDir < -90.0) textDir += 180.0;

        if (opt.diameterLabels) {
            QString label = QStringLiteral("Ø%1").arg(ps.diameterMm, 0, 'f', 1);
            // A cable says both figures: the jacket it is bought as, and the bundle it becomes where
            // the jacket is cut back. One without the other is the figure someone sizes a boot on.
            if (seg->isCable()) {
                const double bundle = segmentBundleDiameterMm(def, *seg);
                label = QStringLiteral("Ø%1 cable").arg(seg->jacketOdMm, 0, 'f', 1);
                if (seg->stripFromMm > 0.0 || seg->stripToMm > 0.0)
                    label += QStringLiteral(" / Ø%1 bundle").arg(bundle, 0, 'f', 1);
            }
            if (opt.wireCounts) label += QStringLiteral("  (%1)").arg(seg->wireIds.size());
            out.push_back(makeText(opt, mid + v * (gap / 2.0 + opt.textHeight * 0.7), label,
                                   opt.textHeight, rad(textDir), QStringLiteral("dia")));
        }
        if (opt.dimensions) {
            const Vec2 off = v * -(gap / 2.0 + opt.textHeight * 3.0);
            out.push_back(makeDimension(opt, ps.a, ps.b, mid + off, ps.id));
        }

        // What the run is MADE of, ballooned on the run itself: the cable, or the conductors by size.
        // They are bought parts like any other and the released drawings balloon them exactly here -
        // without which a harness of nothing but wire has a table and no callouts at all.
        if (opt.balloons) {
            std::vector<int> items;
            auto want = [&items](int item) {
                if (item > 0 && std::find(items.begin(), items.end(), item) == items.end())
                    items.push_back(item);
            };
            if (seg->isCable() || !seg->cablePart.isEmpty()) {
                want(itemOf(seg->cablePart, PartKind::Cable,
                            seg->cableName.isEmpty() ? QStringLiteral("Cable") : seg->cableName));
            } else {
                // One balloon per SIZE, not per conductor: six wires of one gauge are one item.
                for (const QString& wid : seg->wireIds) {
                    const MechWire* w = def.wire(wid);
                    if (!w) continue;
                    want(itemOf(w->libPart, PartKind::Wire,
                                w->gauge.isEmpty()
                                    ? QStringLiteral("Wire")
                                    : QStringLiteral("Wire, %1 mm²").arg(w->gauge)));
                }
            }
            const Vec2 tip = lerp(ps.a, ps.b, 0.35);
            const double step = std::max(9.0, opt.textHeight * 2.6);
            for (size_t i = 0; i < items.size(); ++i)
                out.push_back(makeBalloon(opt, tip,
                                          tip + v * (gap / 2.0 + 24.0 + step * static_cast<double>(i)),
                                          QString::number(items[i])));
        }

        // Anything fitted along the run: a sleeve, a conduit, a tie.
        for (const MechFitting& f : seg->coverings) {
            const LibraryPart* part = catalog ? catalog->byId(f.libPart) : nullptr;
            double lengthMm = 0.0, odMm = 0.0;
            fittingSize(f, part, ps.diameterMm, lengthMm, odMm);
            // A covering with no length of its own runs the whole way.
            if (f.lengthMm <= 0.0 && (!part || partNumber(*part, QStringLiteral("length")) <= 0.0))
                lengthMm = seg->lengthMm;
            const double startMm = std::clamp(f.atMm, 0.0, std::max(0.0, seg->lengthMm - 1e-9));
            const double runMm = std::min(lengthMm, std::max(0.0, seg->lengthMm - startMm));
            const Vec2 c = ps.a + u * along(startMm + runMm / 2.0);
            if (part && part->hasMechanical()) {
                for (Entity& e : placedPartDrawing(*part, c, rad(ps.dirDeg), opt,
                                                   QStringLiteral("fitting")))
                    out.push_back(e);
            } else {
                Entity box = makeBox(opt, c, rad(ps.dirDeg), along(runMm), across(odMm),
                                     QStringLiteral("fitting"));
                box.meta.insert(QStringLiteral("mechSeg"), ps.id);
                out.push_back(box);
            }
            if (opt.balloons)
                out.push_back(makeBalloon(opt, c, c + v * (across(odMm) / 2.0 + 22.0),
                                          balloonText(itemOf(f.libPart, f.kind, f.name))));
            // Its own length dimension: a sleeve is as long as it is, where it is, and the length on
            // the drawing is the length in the table.
            if (opt.dimensions && runMm > 0.0) {
                const Vec2 a = ps.a + u * along(startMm);
                const Vec2 b = ps.a + u * along(startMm + runMm);
                out.push_back(makeDimension(opt, a, b,
                                            lerp(a, b, 0.5) + v * (across(odMm) / 2.0 +
                                                                   opt.textHeight * 1.4),
                                            ps.id));
            }
        }
    }

    // ---- The breakouts, as objects rather than as outline ----
    // The outline above already runs continuously through a junction. What is still worth putting on
    // the drawing is the junction ITSELF - so it can be picked, named and dimensioned to.
    for (const MechPlacedNode& pn : layout.nodes) {
        const MechNode* n = def.node(pn.id);
        if (!n || n->kind != MechNodeKind::Breakout) continue;
        int touching = 0;
        double maxHalf = 0.0;
        for (const MechPlacedSegment& ps : layout.segments) {
            const MechSegment* seg = def.segment(ps.id);
            if (!seg) continue;
            if (seg->fromNode != pn.id && seg->toNode != pn.id) continue;
            ++touching;
            maxHalf = std::max(maxHalf, across(ps.diameterMm) / 2.0);
        }
        if (touching < 3) continue;   // a bend is not a breakout
        Entity e = baseEntity(opt, QStringLiteral("breakout"));
        e.type = EntityType::Breakout;
        e.a = pn.at;
        e.center = pn.at;
        e.points = {pn.at};
        e.meta.insert(QStringLiteral("mechNode"), pn.id);
        e.meta.insert(QStringLiteral("legs"), touching);
        out.push_back(e);
        if (!n->name.isEmpty())
            out.push_back(makeText(opt, pn.at + Vec2{0.0, maxHalf + opt.textHeight * 0.8}, n->name,
                                   opt.textHeight, 0.0, QStringLiteral("label")));
    }

    // ---- Heatshrink where a conductor changes gauge ----
    // A junction where one wire ends and a bigger one carries on has a splice in it, and a splice is
    // not left bare. It is not asked for either: the transition is worked out from the conductors
    // either side, so re-gauging a wire moves the heatshrink with it.
    if (opt.spliceHeatshrink) {
        for (const MechGaugeChange& g : mechGaugeChanges(def)) {
            const MechPlacedNode* pn = layout.node(g.nodeId);
            if (!pn) continue;
            const double odMm = g.diameterMm + 2.0;
            const double lenMm = std::max(20.0, g.diameterMm * 3.0);
            const double dirRad = rad(pn->dirDeg);
            Entity box = makeBox(opt, pn->at, dirRad, along(lenMm), across(odMm),
                                 QStringLiteral("splice"));
            box.meta.insert(QStringLiteral("mechNode"), g.nodeId);
            box.meta.insert(QStringLiteral("gaugeChange"),
                            QStringLiteral("%1 mm² → %2 mm²").arg(g.fromGauges, g.toGauges));
            out.push_back(box);
            const Vec2 v = Vec2{std::cos(dirRad), std::sin(dirRad)}.perp();
            out.push_back(makeText(opt, pn->at - v * (across(odMm) / 2.0 + opt.textHeight * 1.6),
                                   QStringLiteral("heatshrink over the splice %1 → %2 mm²")
                                       .arg(g.fromGauges, g.toGauges),
                                   opt.textHeight, 0.0, QStringLiteral("label")));
            if (opt.balloons)
                out.push_back(makeBalloon(opt, pn->at, pn->at + v * (across(odMm) / 2.0 + 22.0),
                                          balloonText(itemOf(QString(), PartKind::Heatshrink,
                                                             partKindName(PartKind::Heatshrink)))));
        }
    }

    // ---- The ends: connectors, free ends, and what is fitted at them ----
    for (const MechPlacedNode& pn : layout.nodes) {
        const MechNode* n = def.node(pn.id);
        if (!n) continue;
        // The run at this point, and which end of it this is. Usually the run that arrives; at the root,
        // the one that leaves - and then everything fitted here goes FORWARD along it instead of back,
        // because the harness is in front of the node rather than behind it.
        bool atToEnd = true;
        const MechSegment* seg = nodeRunSegment(def, *n, &atToEnd);
        const MechPlacedSegment* ps = seg ? layout.segment(seg->id) : nullptr;
        const double bundleMm = ps ? ps->diameterMm : def.draw.minBundleMm;
        // What is at this end of the run: the bare bundle where the jacket is stripped, the body where
        // it is not. What goes over the WIRES is drawn on that; what the cable enters is drawn on the
        // body, because the cable is behind it.
        const double endMm = seg ? segmentEndDiameterMm(def, *seg, atToEnd) : bundleMm;
        const double bareMm = seg ? nodeBareLengthMm(def, *n) : 0.0;
        // Into the run: -1 when it arrives here (fittings stack back along it), +1 when it leaves.
        const double into = atToEnd ? -1.0 : 1.0;
        const double dirRad = rad(atToEnd || !ps ? pn.dirDeg : ps->dirDeg);
        const Vec2 u{std::cos(dirRad), std::sin(dirRad)};
        const Vec2 v = u.perp();

        // What is fitted at the end sits on the run, just back from the node. The bodies - a backshell,
        // then a boot behind it - stack back one after another, in the order they are fitted. What is
        // pulled over the conductors themselves does not stack: a braid covers the bare stretch that
        // is left, and heatshrink goes over the braid rather than after it, which is how it is built.
        //
        // And nothing covers the conductors for the first `openLengthMm` behind the bodies: they run
        // OPEN there so they can fan out to the contacts and be inspected. A drawing that runs the
        // heatshrink up to the connector face is wrong, so that distance is kept and dimensioned.
        double fittedBackMm = 0.0;
        int overWires = 0;
        int stacked = 0;   // how many bodies are already on, so each steps down from the last
        const double openMm = openAtNode(pn.id);   // 0 at a breakout: nothing comes out there
        double firstCoverAtMm = -1.0;      // where the first thing over the wires starts
        for (const MechFitting& f : n->fittings) {
            const LibraryPart* part = catalog ? catalog->byId(f.libPart) : nullptr;
            const bool overTheWires = mechOverTheWires(f.kind);
            double lengthMm = 0.0, odMm = 0.0;
            fittingSize(f, part, overTheWires ? endMm : bundleMm, lengthMm, odMm);
            double startMm = fittedBackMm + f.atMm;
            if (overTheWires) {
                // Over the bare wires: past the open length, from where the bodies end, along as much
                // of the stripped length as is left. Nothing said about its length means all of it.
                startMm = fittedBackMm + openMm + f.atMm;
                if (firstCoverAtMm < 0.0) firstCoverAtMm = startMm;
                const double left = std::max(0.0, bareMm - startMm);
                if (f.lengthMm <= 0.0 &&
                    (!part || partNumber(*part, QStringLiteral("length")) <= 0.0) && left > 0.0)
                    lengthMm = left;
                else if (left > 0.0)
                    lengthMm = std::min(lengthMm, left);
                if (f.odMm <= 0.0 && (!part || odMm <= endMm))
                    odMm = endMm + 1.0 + 1.5 * overWires;   // each layer over the last
                ++overWires;
            } else if (f.odMm <= 0.0 && !part) {
                // A body typed on the spot rather than chosen: step it down as the stack goes back, the
                // way a released drawing shows a backshell, then its boot, then the cable - each one
                // narrower than the one in front of it.
                odMm = std::max(bundleMm + 1.0, bundleMm + 4.0 - 1.2 * stacked);
                ++stacked;
            }
            const Vec2 c = pn.at + u * (into * along(startMm + lengthMm / 2.0));
            if (part && part->hasMechanical()) {
                for (Entity& e : placedPartDrawing(*part, c, dirRad, opt,
                                                   QStringLiteral("fitting")))
                    out.push_back(e);
            } else if (f.kind == PartKind::YSplit) {
                // A Y-splitter boot is a Y, not a box across the junction: a body over the run that
                // arrives, and a leg over each run that leaves. Drawn as a box it would say nothing
                // about which legs it dresses, and which legs it dresses is what it is for.
                const Vec2 body = pn.at + u * (into * along(lengthMm * 0.35));
                Entity trunk = makeBox(opt, body, dirRad, along(lengthMm * 0.7), across(odMm),
                                       QStringLiteral("fitting"));
                trunk.meta.insert(QStringLiteral("mechNode"), pn.id);
                trunk.meta.insert(QStringLiteral("ysplit"), true);
                out.push_back(trunk);
                for (const MechSegment* leg : def.childSegments(pn.id)) {
                    const MechPlacedSegment* lp = layout.segment(leg->id);
                    if (!lp) continue;
                    // Each arm is sized on the LEG it dresses, not on the trunk: an arm as wide as the
                    // trunk on a leg half its size reads as a bowtie across the junction rather than as
                    // a boot going onto that leg.
                    const double legOd = across(lp->fromDiameterMm + 1.6);
                    const double legLen = std::max(along(lengthMm * 0.5), legOd * 1.6);
                    const Vec2 ld = (lp->b - lp->a).normalized();
                    Entity arm = makeBox(opt, pn.at + ld * (legLen / 2.0),
                                         std::atan2(ld.y, ld.x), legLen, legOd,
                                         QStringLiteral("fitting"));
                    arm.meta.insert(QStringLiteral("mechNode"), pn.id);
                    arm.meta.insert(QStringLiteral("ysplitLeg"), leg->id);
                    out.push_back(arm);
                }
            } else if (!overTheWires && lengthMm > 0.0 && odMm > endMm) {
                // A shell or a boot: tapered, neck towards the connector. What passes through the neck
                // is the conductors at that end, so that is what the neck is sized on.
                const Vec2 neck = pn.at + u * (into * along(startMm));
                Entity body = makeTaperBody(opt, neck, u * into, along(lengthMm),
                                            std::max(across(endMm), across(odMm) * 0.45),
                                            across(odMm), QStringLiteral("fitting"));
                body.meta.insert(QStringLiteral("mechNode"), pn.id);
                body.meta.insert(QStringLiteral("neckDia"), endMm);
                body.meta.insert(QStringLiteral("bodyDia"), odMm);
                out.push_back(body);
            } else {
                Entity box = makeBox(opt, c, dirRad, along(lengthMm), across(odMm),
                                     QStringLiteral("fitting"));
                box.meta.insert(QStringLiteral("mechNode"), pn.id);
                out.push_back(box);
            }
            if (opt.balloons)
                out.push_back(makeBalloon(opt, c, c + v * (across(odMm) / 2.0 + 22.0),
                                          balloonText(itemOf(f.libPart, f.kind, f.name))));
            // A body takes up room for the next one; a layer over the wires does not.
            if (!overTheWires) fittedBackMm = startMm + lengthMm;
        }

        // ---- The open conductors themselves ----
        // Where the wires are open they are not a bundle, they are so many separate wires fanning out
        // to the contacts, and that is how a released drawing shows them: the body stops short and each
        // conductor is a line from the bundle to the connector. It is also the check a builder makes on
        // the drawing - that the wire count going into the connector is the wire count in the run.
        if (openMm > 0.0 && seg && !seg->wireIds.isEmpty() &&
            (n->kind == MechNodeKind::Connector || n->kind == MechNodeKind::FreeEnd)) {
            const int count = static_cast<int>(seg->wireIds.size());
            // Every conductor, up to the point where drawing more lines says less rather than more.
            const int drawn = std::min(count, 24);
            // At the connector they are spread across its face; at the far end they close into the
            // bundle they come out of.
            const double faceHalf =
                across(n->bodyHeightMm > 0.0 ? n->bodyHeightMm : std::max(endMm * 1.4, endMm + 2.0)) /
                2.0;
            const double bundleHalf = across(endMm) / 2.0;
            const Vec2 a = pn.at;                                        // the connector face
            const Vec2 b = pn.at + u * (into * along(openMm));           // where the body starts
            // Each conductor at ITS OWN diameter, with a knee where it fans: out of the bundle at an
            // angle, then straight into the face. That is what a released drawing shows and what the
            // hand-drawn view of test1.dwx has - a double run per wire with the middle vertex rounded
            // (cornerRadii [0, r, 0]) - rather than a bare line from the bundle to the contact, which
            // says nothing about the size of the wire or how it is dressed into the shell.
            const double straightMm = openMm * 0.45;                     // the run straight into the face
            for (int i = 0; i < drawn; ++i) {
                // Evenly spaced across each end, so two wires do not lie on top of one another.
                const double t = drawn == 1 ? 0.0
                                            : (static_cast<double>(i) / (drawn - 1)) * 2.0 - 1.0;
                const MechWire* mw = def.wire(seg->wireIds[i]);
                const double odMm = mw ? wireOuterDiameterMm(mw->gauge) : 1.5;
                const Vec2 face = a + v * (t * faceHalf * 0.8);
                const Vec2 knee = face + u * (into * along(straightMm));
                Entity w = baseEntity(opt, QStringLiteral("wires"));
                w.type = EntityType::Polyline;
                w.points = {b + v * (t * bundleHalf), knee, face};
                w.a = w.points.front();
                w.b = w.points.back();
                w.meta.insert(QStringLiteral("double"), true);
                w.meta.insert(QStringLiteral("gap"), std::max(0.4, across(odMm)));
                // Rounded at the knee and nowhere else, so the ends stay square on the bundle and on
                // the face. The radius is kept inside the fan it is turning through.
                QJsonArray radii;
                radii.append(0.0);
                radii.append(std::max(1.0, std::min(along(straightMm), across(odMm) * 3.0)));
                radii.append(0.0);
                w.meta.insert(QStringLiteral("cornerRadii"), radii);
                w.meta.insert(QStringLiteral("mechNode"), pn.id);
                w.meta.insert(QStringLiteral("mechSeg"), seg->id);
                w.meta.insert(QStringLiteral("wireOd"), odMm);
                out.push_back(w);
            }
            if (drawn < count)
                out.push_back(makeText(opt, b - v * (bundleHalf + opt.textHeight * 1.2),
                                       QStringLiteral("%1 conductors").arg(count),
                                       opt.textHeight * 0.9, 0.0, QStringLiteral("label")));
        }

        // The open conductors, dimensioned: from the back of the bodies to whatever covers them first,
        // or to the end of the open length when nothing does. It is one of the figures a harness is
        // actually built to, and the sample drawings carry it at every connector.
        if (opt.dimensions && openMm > 0.0) {
            const double toMm = firstCoverAtMm >= 0.0 ? firstCoverAtMm : fittedBackMm + openMm;
            const Vec2 a = pn.at + u * (into * along(fittedBackMm));
            const Vec2 b = pn.at + u * (into * along(toMm));
            Entity dim = makeDimension(opt, a, b,
                                       lerp(a, b, 0.5) - v * (across(endMm) / 2.0 +
                                                              opt.textHeight * 2.2),
                                       seg ? seg->id : pn.id);
            dim.meta.insert(QStringLiteral("mechNode"), pn.id);
            dim.meta.insert(QStringLiteral("openWires"), true);
            out.push_back(dim);
        }

        if (n->kind != MechNodeKind::Connector) {
            if (!n->name.isEmpty() && n->kind != MechNodeKind::Root)
                out.push_back(makeText(opt, pn.at + v * (across(bundleMm) / 2.0 + 4.0), n->name,
                                       opt.textHeight, 0.0, QStringLiteral("label")));
            continue;
        }
        if (!opt.connectorBodies) continue;

        const LibraryPart* part = catalog ? catalog->byId(n->libPart) : nullptr;
        if (part && part->hasMechanical()) {
            // The connector as its own drawing says it looks, placed on the end of the run.
            for (Entity& e : placedPartDrawing(*part, pn.at, dirRad, opt,
                                              QStringLiteral("connector")))
                out.push_back(e);
        } else {
            // A body proportionate to what arrives at it: wide enough for the pins, deep enough to
            // read as a shell.
            const double h = n->bodyHeightMm > 0.0
                                 ? n->bodyHeightMm
                                 : std::max(bundleMm + 4.0, n->pinCount * 2.5 + 6.0);
            const double w = n->bodyWidthMm > 0.0 ? n->bodyWidthMm : 20.0;
            // Outward: away from the run, whichever side of the node the run is on.
            Entity body = makeBox(opt, pn.at - u * (into * along(w) / 2.0), dirRad, along(w),
                                  across(h), QStringLiteral("connector"));
            body.meta.insert(QStringLiteral("mechNode"), pn.id);
            out.push_back(body);
        }
        if (opt.balloons && !n->libPart.isEmpty()) {
            const double h = n->bodyHeightMm > 0.0 ? n->bodyHeightMm : bundleMm + 4.0;
            out.push_back(makeBalloon(opt, pn.at,
                                      pn.at + v * (across(h) / 2.0 + 22.0),
                                      balloonText(itemOf(n->libPart, PartKind::Connector,
                                                         QStringLiteral("Connector")))));
        }
        if (!n->name.isEmpty()) {
            const double h = n->bodyHeightMm > 0.0 ? n->bodyHeightMm : bundleMm + 4.0;
            out.push_back(makeText(opt, pn.at - u * (into * along(26.0)) +
                                            v * (across(h) / 2.0 + 3.0),
                                   n->name, opt.textHeight * 1.2, 0.0, QStringLiteral("label")));
        }
    }

    // ---- The overall length ----
    // The figure the harness is bought and inspected by, from the first end to the last. The per-run
    // dimensions say how the inside of it is built; none of them says how long the thing is, and a
    // released drawing always carries that - it is the one dimension on the sheet a stores man reads.
    if (opt.dimensions && layout.segments.size() >= 2) {
        const MechPlacedNode* fa = nullptr;
        const MechPlacedNode* fb = nullptr;
        double best = 0.0;
        for (const MechPlacedNode& p1 : layout.nodes) {
            const MechNode* n1 = def.node(p1.id);
            if (!n1 || (n1->kind != MechNodeKind::Connector && n1->kind != MechNodeKind::FreeEnd))
                continue;
            for (const MechPlacedNode& p2 : layout.nodes) {
                const MechNode* n2 = def.node(p2.id);
                if (&p2 == &p1 || !n2 ||
                    (n2->kind != MechNodeKind::Connector && n2->kind != MechNodeKind::FreeEnd))
                    continue;
                const double d = distance(p1.at, p2.at);
                if (d > best) { best = d; fa = &p1; fb = &p2; }
            }
        }
        if (fa && fb && best > 1e-6) {
            // Below everything on the sheet, clear of the per-run dimensions that already stand off it.
            double lowest = std::min(fa->at.y, fb->at.y);
            for (const MechPlacedSegment& ps : layout.segments) {
                const Body& b = bodies.value(ps.id);
                for (const Vec2& q : b.plus) lowest = std::min(lowest, q.y);
                for (const Vec2& q : b.minus) lowest = std::min(lowest, q.y);
            }
            const Vec2 textAt{(fa->at.x + fb->at.x) / 2.0, lowest - opt.textHeight * 8.0};
            Entity dim = makeDimension(opt, fa->at, fb->at, textAt, QString());
            dim.meta.insert(QStringLiteral("overall"), true);
            out.push_back(dim);
        }
    }

    // ---- The process notes: a numbered triangle where each applies, and the list beside them ----
    // A balloon says what to BUY; a triangle says what to DO. They are different callouts on a released
    // drawing and they are different here.
    if (opt.deltaNotes) {
        const std::vector<MechDeltaNote> notes = mechDeltaNotes(def);
        for (const MechDeltaNote& note : notes) {
            for (const QString& nodeId : note.atNodes) {
                const MechPlacedNode* pn = layout.node(nodeId);
                if (!pn) continue;
                double half = def.draw.minBundleMm;
                for (const MechPlacedSegment& ps : layout.segments) {
                    const MechSegment* s = def.segment(ps.id);
                    if (!s || (s->fromNode != nodeId && s->toNode != nodeId)) continue;
                    half = std::max(half, across(ps.diameterMm) / 2.0);
                }
                // An equilateral triangle with the number in it, standing clear of the harness - and
                // clear of the balloons, which stand off the same side.
                const double side = std::max(6.0, opt.textHeight * 2.2);
                const Vec2 at{pn->at.x, pn->at.y + half + side * 1.4 + opt.textHeight * 4.0};
                Entity tri = baseEntity(opt, QStringLiteral("note"));
                tri.type = EntityType::Polyline;
                tri.points = {{at.x - side / 2.0, at.y - side * 0.433},
                              {at.x + side / 2.0, at.y - side * 0.433},
                              {at.x, at.y + side * 0.577},
                              {at.x - side / 2.0, at.y - side * 0.433}};
                tri.meta.insert(QStringLiteral("mechNode"), nodeId);
                tri.meta.insert(QStringLiteral("deltaNote"), note.number);
                out.push_back(tri);
                out.push_back(makeText(opt, Vec2{at.x, at.y - side * 0.30},
                                       QString::number(note.number), opt.textHeight * 0.9, 0.0,
                                       QStringLiteral("note")));
                // A leader down onto the harness, so it is clear what the note is about.
                Entity leader = baseEntity(opt, QStringLiteral("note"));
                leader.type = EntityType::Line;
                leader.a = Vec2{at.x, at.y - side * 0.433};
                leader.b = Vec2{pn->at.x, pn->at.y + half};
                out.push_back(leader);
            }
        }
        // The list itself, above the harness: "1 - WIRES GAUGE TRANSITION LOCATION."
        if (!notes.empty()) {
            Bounds b;
            for (const Entity& e : out) {
                const Bounds eb = e.bounds();
                if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
            }
            if (b.valid) {
                double y = b.max.y + opt.textHeight * 3.0;
                out.push_back(makeText(opt, Vec2{b.min.x, y}, QStringLiteral("NOTES:"),
                                       opt.textHeight, 0.0, QStringLiteral("note")));
                for (const MechDeltaNote& note : notes) {
                    y -= opt.textHeight * 1.8;
                    out.push_back(makeText(opt, Vec2{b.min.x, y},
                                           QStringLiteral("%1 — %2").arg(note.number).arg(note.text),
                                           opt.textHeight, 0.0, QStringLiteral("note")));
                }
            }
        }
    }

    // A width that has been exaggerated must say so on the face of the drawing: someone will scale
    // one off the print otherwise, and the lengths beside it are true.
    if (std::abs(xg - 1.0) > 1e-9 && !out.empty()) {
        Bounds b;
        for (const Entity& e : out) {
            const Bounds eb = e.bounds();
            if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
        }
        if (b.valid)
            out.push_back(makeText(
                opt, Vec2{b.min.x, b.min.y - opt.textHeight * 3.0},
                QStringLiteral("Bundle diameters shown ×%1 for clarity; lengths are to scale.")
                    .arg(xg, 0, 'g', 3),
                opt.textHeight, 0.0, QStringLiteral("note")));
    }
    // ---- Keeping the figures apart ----
    // Two dimensions whose figures land on top of one another say nothing: "8.18" and "11.76" written
    // across each other is worse than either alone. Each one already stands off its geometry in a known
    // direction, so where a figure would sit on one already placed it is stood further off along that
    // same direction until it is clear. Done at the end, because it is only when they are all made that
    // it can be seen which of them collide.
    if (opt.dimensions) {
        std::vector<Entity*> dims;
        for (Entity& e : out)
            if (e.type == EntityType::Dimension &&
                e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("dim"))
                dims.push_back(&e);
        // Where the figure sits, and how much room it takes: estimated off the glyph height, which is
        // all that is available without a font to measure with.
        auto figure = [](const Entity& e, Vec2& at, double& halfW, double& halfH) {
            Vec2 d1, d2;
            if (dimensionLineEnds(e, d1, d2)) at = lerp(d1, d2, 0.5);
            else at = lerp(e.a, e.b, 0.5);
            const double h = e.meta.value(QStringLiteral("textHeight")).toDouble(5.0);
            // How many characters the figure will be: the length it measures, to the decimals it is
            // set to. The renderer works the same figure out from the same geometry.
            const int dec = e.meta.value(QStringLiteral("decimals")).toInt(1);
            const int chars = QString::number(distance(e.a, e.b), 'f', dec).size();
            halfW = std::max(1, chars) * h * 0.62 / 2.0;
            halfH = h * 0.9;
        };
        auto standOff = [](const Entity& e) {
            // The way it is already standing off: from what it measures towards where its line is.
            if (e.points.size() < 3) return Vec2{0.0, 1.0};
            const Vec2 v = e.points[2] - lerp(e.points[0], e.points[1], 0.5);
            if (v.length() < 1e-9) return (e.b - e.a).normalized().perp();
            return v.normalized();
        };
        for (size_t i = 0; i < dims.size(); ++i) {
            for (int guard = 0; guard < 8; ++guard) {
                Vec2 ai;
                double wi = 0.0, hi = 0.0;
                figure(*dims[i], ai, wi, hi);
                bool clash = false;
                for (size_t j = 0; j < i && !clash; ++j) {
                    Vec2 aj;
                    double wj = 0.0, hj = 0.0;
                    figure(*dims[j], aj, wj, hj);
                    clash = std::abs(ai.x - aj.x) < wi + wj && std::abs(ai.y - aj.y) < hi + hj;
                }
                if (!clash) break;
                const Vec2 push = standOff(*dims[i]) * (hi * 2.4);
                dims[i]->points[2] = dims[i]->points[2] + push;
            }
        }
    }

    return out;
}

// ---- From what is already drawn -----------------------------------------------------------------

MechHarnessDef mechFromDrawing(const Document& doc, const PartsCatalog* catalog) {
    MechHarnessDef def;
    def.name = QStringLiteral("Harness");
    // Which library part each conductor came out of, so a run of cable cores can be recognised as the
    // cable it is rather than as so many loose wires that happen to run together.
    QHash<QString, QString> wirePart;

    // Every connector, and where each of its pins ends.
    struct Conn { QString id, designator; int pinCount; std::vector<Vec2> pinEnds; Vec2 at; };
    std::vector<Conn> conns;
    for (const Entity& e : doc.entities()) {
        if (e.type != EntityType::Connector) continue;
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry geom = connectorGeometry(e.a, spec);
        Conn c;
        c.id = e.id;
        c.designator = spec.designator.isEmpty() ? e.id : spec.designator;
        c.pinCount = spec.pinCount;
        c.pinEnds = geom.pinEnd;
        c.at = e.a;
        conns.push_back(c);
    }

    // Every wire, and which connectors its two ends land on.
    struct Routed { QString wireId; int fromConn = -1, toConn = -1; };
    std::vector<Routed> routed;
    auto connAt = [&conns](const Vec2& p) {
        for (int i = 0; i < static_cast<int>(conns.size()); ++i)
            for (const Vec2& pe : conns[static_cast<size_t>(i)].pinEnds)
                if (samePoint(pe, p)) return i;
        return -1;
    };
    for (const Entity& e : doc.entities()) {
        if (e.type != EntityType::Wire || e.points.size() < 2) continue;
        MechWire w;
        w.id = e.id;
        w.label = e.meta.value(QStringLiteral("wireLabel")).toString();
        if (w.label.isEmpty()) w.label = e.id;
        w.gauge = e.meta.value(QStringLiteral("wireGauge")).toString();
        if (w.gauge.isEmpty()) w.gauge = e.meta.value(QStringLiteral("awg")).toString();
        w.bundle = e.meta.value(QStringLiteral("bundle")).toString();
        wirePart.insert(w.id, e.meta.value(QStringLiteral("libPart")).toString());
        def.wires.push_back(w);
        routed.push_back({e.id, connAt(e.points.front()), connAt(e.points.back())});
    }

    if (conns.empty()) return def;

    // A star: one breakout in the middle of the connectors, a branch out to each of them. It is the
    // shape a first pass has to be - where the real breakouts go is what the mechanical drawing is
    // for, and that is the user's to say.
    Vec2 centre{0.0, 0.0};
    for (const Conn& c : conns) centre += c.at;
    centre = centre / static_cast<double>(conns.size());

    MechNode root;
    root.id = def.nextNodeId();
    root.name = QStringLiteral("BR1");
    root.kind = MechNodeKind::Breakout;
    def.nodes.push_back(root);

    // Ordered by the angle they sit at, so the drawing comes out in the shape the schematic is in.
    std::vector<int> order(conns.size());
    for (int i = 0; i < static_cast<int>(conns.size()); ++i) order[static_cast<size_t>(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Vec2 da = conns[static_cast<size_t>(a)].at - centre;
        const Vec2 db = conns[static_cast<size_t>(b)].at - centre;
        return std::atan2(da.y, da.x) < std::atan2(db.y, db.x);
    });

    for (int idx : order) {
        const Conn& c = conns[static_cast<size_t>(idx)];
        MechNode n;
        n.id = def.nextNodeId();
        n.name = c.designator;
        n.kind = MechNodeKind::Connector;
        n.pinCount = c.pinCount;
        const Vec2 d = c.at - centre;
        n.angleDeg = deg(std::atan2(d.y, d.x));
        MechSegment s;
        s.id = def.nextSegmentId();
        s.fromNode = root.id;
        s.toNode = n.id;
        // The length the schematic happens to be drawn at: a starting figure, and the first thing
        // to be replaced by a real one.
        s.lengthMm = std::max(10.0, d.length());
        for (const Routed& r : routed)
            if ((r.fromConn == idx || r.toConn == idx) && !s.wireIds.contains(r.wireId))
                s.wireIds.push_back(r.wireId);
        // Every conductor in it a core of one and the same cable? Then the run IS that cable: it is
        // drawn at the jacket, and the bundle inside only shows where the jacket is cut back. A cable
        // whose five cores add up to Ø2.4 is still a Ø5.0 cable.
        // The cable is recognised by the NAME its cores share, not by the library part: a drain or a
        // screen written into the same cable does not always carry the part id its cores do, and one
        // conductor short of a match is still the same cable.
        QString bundle;
        QString partId;
        bool oneCable = !s.wireIds.isEmpty();
        for (const QString& wid : s.wireIds) {
            const MechWire* w = def.wire(wid);
            const QString b = w ? w->bundle : QString();
            const QString p = wirePart.value(wid);
            if (b.isEmpty()) { oneCable = false; break; }
            if (bundle.isEmpty()) bundle = b;
            else if (bundle != b) { oneCable = false; break; }
            if (p.isEmpty()) continue;
            if (partId.isEmpty()) partId = p;
            else if (partId != p) partId = QStringLiteral("-");   // they disagree: take none of them
        }
        if (partId == QLatin1String("-")) partId.clear();
        if (oneCable) {
            s.cableName = bundle;
            const LibraryPart* part = catalog ? catalog->byId(partId) : nullptr;
            if (part && part->kind == PartKind::Cable) {
                s.cablePart = part->id;
                const CableSpec spec = cableSpecOf(*part);
                // Only what the part actually states. How far the jacket is cut back at each end is
                // not in the part and is not guessed at - the validator asks for it.
                if (spec.odMm > 0.0) s.jacketOdMm = spec.odMm;
            }
        }
        n.parentSegment = s.id;
        def.nodes.push_back(n);
        def.segments.push_back(s);
    }
    return def;
}

} // namespace Edraw_Desktop
