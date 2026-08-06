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

    int balloonNo = 0;

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
        if (profile.size() >= 2 && seg && seg->lengthMm > 0.0) {
            for (const MechStation& st : profile) {
                const Vec2 c = ps.a + dir * along(st.atMm);
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
            if (a.atFrom) ba.plus.front() = v;
            else ba.minus.back() = v;
            if (b.atFrom) bb.minus.front() = v;
            else bb.plus.back() = v;
        }
    }
    for (const MechPlacedSegment& ps : layout.segments) {
        const Body& b = bodies.value(ps.id);
        out.push_back(makeBundleEdge(opt, b.plus, ps.diameterMm, ps.id, +1));
        out.push_back(makeBundleEdge(opt, b.minus, ps.diameterMm, ps.id, -1));
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
            if (opt.balloons) {
                ++balloonNo;
                out.push_back(makeBalloon(opt, c, c + v * (across(odMm) / 2.0 + 22.0),
                                          QString::number(balloonNo)));
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
            if (opt.balloons) {
                ++balloonNo;
                out.push_back(makeBalloon(opt, pn->at,
                                          pn->at + v * (across(odMm) / 2.0 + 22.0),
                                          QString::number(balloonNo)));
            }
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
        double fittedBackMm = 0.0;
        int overWires = 0;
        for (const MechFitting& f : n->fittings) {
            const LibraryPart* part = catalog ? catalog->byId(f.libPart) : nullptr;
            const bool overTheWires = mechOverTheWires(f.kind);
            double lengthMm = 0.0, odMm = 0.0;
            fittingSize(f, part, overTheWires ? endMm : bundleMm, lengthMm, odMm);
            double startMm = fittedBackMm + f.atMm;
            if (overTheWires) {
                // Over the bare wires: from where the bodies end, along as much of the stripped
                // length as is left. Nothing said about its length means all of it.
                startMm = fittedBackMm + f.atMm;
                const double left = std::max(0.0, bareMm - startMm);
                if (f.lengthMm <= 0.0 &&
                    (!part || partNumber(*part, QStringLiteral("length")) <= 0.0) && left > 0.0)
                    lengthMm = left;
                else if (left > 0.0)
                    lengthMm = std::min(lengthMm, left);
                if (f.odMm <= 0.0 && (!part || odMm <= endMm))
                    odMm = endMm + 1.0 + 1.5 * overWires;   // each layer over the last
                ++overWires;
            }
            const Vec2 c = pn.at + u * (into * along(startMm + lengthMm / 2.0));
            if (part && part->hasMechanical()) {
                for (Entity& e : placedPartDrawing(*part, c, dirRad, opt,
                                                   QStringLiteral("fitting")))
                    out.push_back(e);
            } else {
                Entity box = makeBox(opt, c, dirRad, along(lengthMm), across(odMm),
                                     QStringLiteral("fitting"));
                box.meta.insert(QStringLiteral("mechNode"), pn.id);
                out.push_back(box);
            }
            if (opt.balloons) {
                ++balloonNo;
                out.push_back(makeBalloon(opt, c, c + v * (across(odMm) / 2.0 + 22.0),
                                          QString::number(balloonNo)));
            }
            // A body takes up room for the next one; a layer over the wires does not.
            if (!overTheWires) fittedBackMm = startMm + lengthMm;
        }

        if (n->kind != MechNodeKind::Connector) {
            if (n->kind == MechNodeKind::FreeEnd && ps) {
                // A cut end: a bar across the bundle, so the drawing does not read as unfinished.
                const double half = across(bundleMm) / 2.0;
                Entity cap = baseEntity(opt, QStringLiteral("end"));
                cap.type = EntityType::Line;
                cap.a = pn.at + v * half;
                cap.b = pn.at - v * half;
                cap.meta.insert(QStringLiteral("mechNode"), pn.id);
                out.push_back(cap);
            }
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
        if (!n->name.isEmpty()) {
            const double h = n->bodyHeightMm > 0.0 ? n->bodyHeightMm : bundleMm + 4.0;
            out.push_back(makeText(opt, pn.at - u * (into * along(26.0)) +
                                            v * (across(h) / 2.0 + 3.0),
                                   n->name, opt.textHeight * 1.2, 0.0, QStringLiteral("label")));
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
