#include "model/Entity.h"

#include "harness/Harness.h"

#include <QJsonArray>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Edraw_Desktop {

namespace {
struct TypeName { EntityType type; const char* name; };
const std::array<TypeName, 29> kTypeNames = {{
    {EntityType::Line, "line"},
    {EntityType::DoubleLine, "doubleline"},
    {EntityType::Polyline, "polyline"},
    {EntityType::Rect, "rect"},
    {EntityType::Polygon, "polygon"},
    {EntityType::Circle, "circle"},
    {EntityType::Arc, "arc"},
    {EntityType::Ellipse, "ellipse"},
    {EntityType::Spline, "spline"},
    {EntityType::Text, "text"},
    {EntityType::Dimension, "dimension"},
    {EntityType::Hatch, "hatch"},
    {EntityType::Wipeout, "wipeout"},
    {EntityType::Image, "image"},
    {EntityType::Wire, "wire"},
    {EntityType::Splice, "splice"},
    {EntityType::LengthBreak, "lengthbreak"},
    {EntityType::TwistedPair, "twistedpair"},
    {EntityType::Leader, "leader"},
    {EntityType::DetailView, "detailview"},
    {EntityType::Connector, "connector"},
    {EntityType::Breakout, "breakout"},
    {EntityType::Bundle, "bundle"},
    {EntityType::NetLabel, "netlabel"},
    {EntityType::NotConnected, "notconnected"},
    {EntityType::Twist, "twist"},
    {EntityType::Screen, "screen"},
    {EntityType::ConnectionPin, "connectionpin"},
    {EntityType::BlockRef, "blockref"},
}};
} // namespace

EntityType entityTypeFromString(const QString& s) {
    const QString key = s.toLower();
    for (const auto& tn : kTypeNames)
        if (key == QLatin1String(tn.name)) return tn.type;
    return EntityType::Unknown;
}

QString entityTypeDisplayName(EntityType t) {
    // Only where the stored name would mislead; everything else is called what it has always been
    // called, so this cannot quietly rename half the application.
    if (t == EntityType::TwistedPair) return QStringLiteral("harness");
    return entityTypeToString(t);
}

QString entityTypeToString(EntityType t) {
    for (const auto& tn : kTypeNames)
        if (t == tn.type) return QString::fromLatin1(tn.name);
    return QStringLiteral("unknown");
}

namespace {
// The three points a linear or aligned dimension is built from: the two it measures between and the one
// its line stands off through. A dimension saved before those were written has only a and b, and then
// the stand-off point is the midpoint - the dimension line lying on what it measures.
bool dimStandOff(const Entity& e, Vec2& p1, Vec2& p2, Vec2& off) {
    const QString ty = e.meta.value("dimType").toString(QStringLiteral("linear"));
    if (ty != QLatin1String("linear") && ty != QLatin1String("aligned") &&
        ty != QLatin1String("arclength"))
        return false;
    if (e.points.size() >= 3) {
        p1 = e.points[0];
        p2 = e.points[1];
        off = e.points[2];
    } else {
        p1 = e.a;
        p2 = e.b;
        off = lerp(e.a, e.b, 0.5);
    }
    return true;
}
} // namespace

bool dimensionAcrossX(const Entity& e) {
    // What it was PLACED measuring along, when the object says so. It has to be remembered rather than
    // read back out of the geometry every time, because the geometry moves: dragging one of the points a
    // dimension measures between moves the middle it would be compared against, and a dimension that
    // works out its own direction turns itself round and measures something else the moment that
    // happens. Only a drawing saved before this was written has to fall back on working it out.
    if (e.meta.contains("dimAcross")) return e.meta.value("dimAcross").toBool();
    Vec2 p1, p2, off;
    if (!dimStandOff(e, p1, p2, off)) return true;
    const Vec2 mid = lerp(p1, p2, 0.5);
    const double dx = std::abs(off.x - mid.x), dy = std::abs(off.y - mid.y);
    if (dx < 1e-9 && dy < 1e-9) return std::abs(p2.x - p1.x) >= std::abs(p2.y - p1.y);
    return dy >= dx;
}

double tableTextWidth(const QString& text, double textHeight) {
    if (text.isEmpty()) return 0.0;
    return std::max(1e-9, textHeight) * 0.62 * text.size();
}

double tableLongestWord(const QString& text, double textHeight) {
    double widest = 0.0;
    for (const QString& line : text.split(QLatin1Char('\n')))
        for (const QString& word : line.split(QLatin1Char(' ')))
            widest = std::max(widest, tableTextWidth(word, textHeight));
    return widest;
}

QStringList tableWrapText(const QString& text, double avail, double textHeight) {
    QStringList out;
    // How many characters fit across. A column may be taken all the way down to ONE letter, at which point
    // the words run down the cell a letter at a time - which is what a column squeezed to nothing looks
    // like in every drawing package, and is honest: the text is all still there and still readable, just
    // very tall. Nothing is hidden and nothing hangs out of the cell.
    const int perLine = avail <= 1e-9 ? 0
                                      : std::max(1, static_cast<int>(avail / std::max(1e-9, textHeight * 0.62)));
    for (const QString& para : text.split(QLatin1Char('\n'))) {
        if (avail <= 1e-9 || tableTextWidth(para, textHeight) <= avail) {
            out << para;
            continue;
        }
        QString cur;
        auto flush = [&] {
            if (!cur.isEmpty()) { out << cur; cur.clear(); }
        };
        for (const QString& word : para.split(QLatin1Char(' '))) {
            // A word too long for the whole line is BROKEN across lines rather than left to hang out of
            // the cell. Only then: a word that fits is never split, so ordinary text still reads as words.
            if (word.size() > perLine) {
                flush();
                for (int i = 0; i < word.size(); i += perLine) out << word.mid(i, perLine);
                continue;
            }
            const QString test = cur.isEmpty() ? word : cur + QLatin1Char(' ') + word;
            if (!cur.isEmpty() && tableTextWidth(test, textHeight) > avail) {
                out << cur;
                cur = word;
            } else {
                cur = test;
            }
        }
        flush();
    }
    if (out.isEmpty()) out << QString();
    return out;
}

std::vector<TableMerge> tableMerges(const Entity& e) {
    std::vector<TableMerge> out;
    for (const auto v : e.meta.value(QStringLiteral("merges")).toArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() < 4) continue;
        TableMerge m;
        m.row = a[0].toInt();
        m.col = a[1].toInt();
        m.rowSpan = std::max(1, a[2].toInt());
        m.colSpan = std::max(1, a[3].toInt());
        if (m.rowSpan == 1 && m.colSpan == 1) continue;   // a block of one is not a merge
        out.push_back(m);
    }
    return out;
}

void setTableMerges(Entity& e, const std::vector<TableMerge>& merges) {
    QJsonArray arr;
    for (const TableMerge& m : merges) {
        if (m.rowSpan == 1 && m.colSpan == 1) continue;
        arr.append(QJsonArray{m.row, m.col, m.rowSpan, m.colSpan});
    }
    if (arr.isEmpty()) e.meta.remove(QStringLiteral("merges"));
    else e.meta.insert(QStringLiteral("merges"), arr);
}

bool tableMergeAt(const Entity& e, int row, int col, TableMerge& out) {
    for (const TableMerge& m : tableMerges(e))
        if (m.covers(row, col)) {
            out = m;
            return true;
        }
    return false;
}

bool dimensionLineEnds(const Entity& e, Vec2& d1, Vec2& d2) {
    Vec2 p1, p2, off;
    if (!dimStandOff(e, p1, p2, off)) return false;
    if (e.meta.value("dimType").toString(QStringLiteral("linear")) == QLatin1String("linear")) {
        if (dimensionAcrossX(e)) {
            d1 = {p1.x, off.y};
            d2 = {p2.x, off.y};
        } else {
            d1 = {off.x, p1.y};
            d2 = {off.x, p2.y};
        }
    } else {
        const Vec2 n = (p2 - p1).normalized().perp();
        const double t = dot(off - p1, n);
        d1 = p1 + n * t;
        d2 = p2 + n * t;
    }
    return true;
}

namespace {

// THE OUTLINE A HEATSHRINK SLEEVE ACTUALLY HAS.
//
// A sleeve stores its ROUTE - the centreline it was drawn along - and works its outline out from that
// when it is drawn: a body of one width, recovered down to a narrower face at each end over a taper.
// The shape on the screen is nowhere in the stored points.
//
// So everything that asks "where is this object" - a click, a green zone, what a cursor can snap to,
// the box it occupies - was answering about a line down the middle that is never drawn. A zone laid
// across the end of a sleeve caught nothing, because the lines it was touching were not in the model,
// and a snap found the centreline rather than the edge the eye is on.
//
// The reading of the numbers moved here from the renderer, which had it, so that the drawing and the
// picking cannot disagree about what shape it is: the renderer asks this too now.
bool cableWithEndSleeves(const Entity& e) {
    return e.type == EntityType::Polyline &&
           e.meta.value(QLatin1String("shrunkEnds")).toBool() &&
           e.meta.value(QLatin1String("shrinkEndsOnly")).toBool();
}

double sleeveAcross(const Entity& e) {
    return e.meta.contains(QLatin1String("gap"))
               ? e.meta.value(QLatin1String("gap")).toDouble(6.0)
               : e.meta.value(QLatin1String("shrinkWidth")).toDouble(6.0);
}

double sleeveFaceAcross(const Entity& e, int end) {
    const double def = std::max(0.2, sleeveAcross(e)) * 0.82;
    const QString key = end == 0 ? QStringLiteral("shrinkFaceStart")
                                 : QStringLiteral("shrinkFaceEnd");
    return std::clamp(e.meta.value(key).toDouble(def), 0.1, std::max(0.1, sleeveAcross(e)));
}

double sleeveTaper(const Entity& e, int end) {
    const QString key = end == 0 ? QStringLiteral("shrinkTaperStart")
                                 : QStringLiteral("shrinkTaperEnd");
    return std::max(0.0, e.meta.value(key).toDouble(4.0));
}

// A slice of the route, with every vertex it kept still saying which route vertex it came from. The
// two cut points are not vertices of the source route (SIZE_MAX), so a fillet on a real knee cannot
// wander onto the sleeve/cable split.
struct RouteSlice {
    std::vector<Vec2> points;
    std::vector<size_t> source;
};

RouteSlice routeSlice(const std::vector<Vec2>& route, double from, double to) {
    RouteSlice out;
    if (route.size() < 2 || to - from < 1e-7) return out;
    Vec2 p, tangent;
    if (samplePolyline(route, from, false, p, tangent)) {
        out.points.push_back(p);
        out.source.push_back(std::numeric_limits<size_t>::max());
    }
    double walked = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        const double seg = distance(route[i - 1], route[i]);
        const double at = walked + seg;
        if (at > from + 1e-7 && at < to - 1e-7) {
            out.points.push_back(route[i]);
            out.source.push_back(i);
        }
        walked = at;
    }
    if (samplePolyline(route, to, false, p, tangent)) {
        out.points.push_back(p);
        out.source.push_back(std::numeric_limits<size_t>::max());
    }
    return out;
}

RouteSlice sleeveSectionRoute(const Entity& e, int end) {
    double start = 0.0, finish = 0.0;
    if (!sleeveEndCoverageOf(e, start, finish)) return {};
    const double total = polylineLength(e.points);
    return routeSlice(e.points, end == 0 ? 0.0 : total - finish,
                      end == 0 ? start : total);
}

std::vector<double> routeSliceRadii(const Entity& e, const RouteSlice& slice, double add) {
    std::vector<double> radii(slice.points.size(), 0.0);
    const QJsonArray rr = e.meta.value(QLatin1String("cornerRadii")).toArray();
    for (size_t i = 0; i < slice.source.size(); ++i) {
        const size_t at = slice.source[i];
        if (at != std::numeric_limits<size_t>::max() && at < static_cast<size_t>(rr.size()))
            radii[i] = std::max(0.0, rr[static_cast<int>(at)].toDouble()) + add;
    }
    return radii;
}

std::vector<Vec2> rawEndOutline(const Entity& e, int end) {
    const RouteSlice slice = sleeveSectionRoute(e, end);
    if (slice.points.size() < 2) return {};
    const double bodyHalf = std::max(0.2, sleeveAcross(e)) / 2.0;
    return end == 0 ? shrunkSleeveOutline(slice.points, bodyHalf, sleeveFaceAcross(e, 0) / 2.0,
                                          bodyHalf, sleeveTaper(e, 0), 0.0)
                    : shrunkSleeveOutline(slice.points, bodyHalf, bodyHalf,
                                          sleeveFaceAcross(e, 1) / 2.0, 0.0, sleeveTaper(e, 1));
}

std::vector<Vec2> filletedEndOutline(const Entity& e, int end) {
    const RouteSlice slice = sleeveSectionRoute(e, end);
    std::vector<Vec2> outline = rawEndOutline(e, end);
    if (outline.size() != slice.points.size() * 2 + 4) return outline;

    const std::vector<double> sliceRadii = routeSliceRadii(e, slice, 0.0);
    const size_t n = slice.points.size();
    std::vector<double> radii(outline.size(), 0.0);
    bool any = false;
    for (size_t i = 1; i + 1 < n; ++i) {
        const double r = sliceRadii[i];
        if (r <= 0.0) continue;
        const size_t outA = 1 + i;
        const size_t outB = 2 * n + 2 - i;
        if (outA < radii.size()) radii[outA] = r;
        if (outB < radii.size()) radii[outB] = r;
        any = true;
    }
    if (any) {
        const std::vector<double> none(outline.size(), 0.0);
        outline = filletChamferPolyline(outline, radii, none, none, true);
    }
    return outline;
}

} // namespace

bool sleeveEndCoverageOf(const Entity& e, double& start, double& end) {
    if (!cableWithEndSleeves(e) || e.points.size() < 2) return false;
    const double total = polylineLength(e.points);
    if (total < 1e-7) return false;
    // Twenty millimetres of sleeve on each end is the starting figure; a short run proportions it
    // down rather than letting the two sleeves pass through one another.
    const double def = std::min(20.0, total * 0.25);
    start = std::clamp(e.meta.value(QStringLiteral("shrinkCoverStart")).toDouble(def), 0.0, total);
    end = std::clamp(e.meta.value(QStringLiteral("shrinkCoverEnd")).toDouble(def), 0.0, total);
    if (start + end > total && start + end > 1e-9) {
        const double k = total / (start + end);
        start *= k;
        end *= k;
    }
    return true;
}

std::vector<Vec2> shrunkSleeveOutlineOf(const Entity& e) {
    if (e.type != EntityType::Polyline ||
        !e.meta.value(QLatin1String("shrunkEnds")).toBool() || e.points.size() < 2 ||
        cableWithEndSleeves(e))
        return {};
    const double bodyHalf = std::max(0.2, sleeveAcross(e)) / 2.0;
    // THE TWO ENDS, EACH WITH ITS OWN TWO NUMBERS. Not a share of the body: what a sleeve recovers
    // to, and over what length, is set by whatever is under that end, and the two ends are usually
    // over different things. Absent on a sleeve drawn before the numbers existed, and those fall back
    // to what was always drawn - 82% across, 4 long, both ends the same - so an old drawing opens as
    // it did.
    return shrunkSleeveOutline(e.points, bodyHalf,
                               std::clamp(sleeveFaceAcross(e, 0) / 2.0, 0.05, bodyHalf),
                               std::clamp(sleeveFaceAcross(e, 1) / 2.0, 0.05, bodyHalf),
                               sleeveTaper(e, 0), sleeveTaper(e, 1));
}

std::vector<Vec2> shrunkSleeveEndOutlineOf(const Entity& e, int end) {
    if (end != 0 && end != 1) return {};
    if (cableWithEndSleeves(e)) return rawEndOutline(e, end);
    return shrunkSleeveOutlineOf(e);
}

std::vector<std::vector<Vec2>> shrunkSleeveRunsOf(const Entity& e) {
    std::vector<std::vector<Vec2>> runs;
    if (e.type != EntityType::Polyline ||
        !e.meta.value(QLatin1String("shrunkEnds")).toBool() || e.points.size() < 2) return runs;
    if (!cableWithEndSleeves(e)) {
        std::vector<Vec2> outline = shrunkSleeveOutlineOf(e);
        if (!outline.empty()) {
            outline.push_back(outline.front());
            runs.push_back(std::move(outline));
        }
        return runs;
    }

    double coverStart = 0.0, coverEnd = 0.0;
    if (!sleeveEndCoverageOf(e, coverStart, coverEnd)) return runs;
    const double total = polylineLength(e.points);
    const double body = std::max(0.2, sleeveAcross(e));
    const double cable =
        std::clamp(e.meta.value(QStringLiteral("shrinkCableWidth")).toDouble(body * 0.82),
                   0.1, body);
    const RouteSlice middle = routeSlice(e.points, coverStart, total - coverEnd);
    if (middle.points.size() >= 2 && polylineLength(middle.points) > 1e-7) {
        // The cable keeps its own narrower width between the sleeves. A fillet set on a knee of the
        // run rounds its centreline first, exactly as the ordinary double-polyline rails do.
        const std::vector<double> radii = routeSliceRadii(e, middle, cable / 2.0);
        const std::vector<Vec2> centre = roundedPolylinePerCorner(middle.points, radii);
        const std::vector<Vec2> above = offsetPolyline(centre, cable / 2.0);
        const std::vector<Vec2> below = offsetPolyline(centre, -cable / 2.0);
        if (above.size() >= 2) runs.push_back(above);
        if (below.size() >= 2) runs.push_back(below);
    }
    for (int end = 0; end < 2; ++end) {
        std::vector<Vec2> outline = filletedEndOutline(e, end);
        if (outline.size() >= 3) {
            outline.push_back(outline.front());
            runs.push_back(std::move(outline));
        }
    }
    return runs;
}

std::vector<Vec2> entityOutline(const Entity& e) {
    if (e.meta.value(QLatin1String("shrunkEnds")).toBool()) {
        std::vector<Vec2> points;
        for (const auto& run : shrunkSleeveRunsOf(e))
            points.insert(points.end(), run.begin(), run.end());
        if (!points.empty()) return points;
    }
    if (e.meta.value(QLatin1String("volumeArrow")).toBool())
        return volumeArrowOutline(e.a, e.b, e.meta.value(QLatin1String("arrowWidth")).toDouble(0.0),
                                  e.meta.value(QLatin1String("arrowHead")).toDouble(0.0));
    return e.points;
}

double axisAngleStartAngle(const Entity& e) {
    // Spelled out rather than taken from M_PI: that name only exists under MSVC when the math defines
    // are asked for before <cmath>, and by this point Qt has already included it.
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double clicked = std::atan2(e.b.y - e.a.y, e.b.x - e.a.x);
    return clicked + e.meta.value(QLatin1String("axisZero")).toDouble(0.0) * kDegToRad;
}

double axisAngleCrossAngle(const Entity& e) {
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const QJsonValue own = e.meta.value(QLatin1String("axisCross"));
    if (own.isDouble()) return own.toDouble() * kDegToRad;
    return axisAngleStartAngle(e);
}

bool axisAngleNeedsStop(double angleRad, double crossRad) {
    constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
    const double off = std::fmod(std::abs((angleRad - crossRad) * kRadToDeg), 90.0);
    return std::min(off, 90.0 - off) > 0.05;
}

double axisAngleArm(const Entity& e) {
    const double own = e.meta.value(QLatin1String("axisSize")).toDouble(0.0);
    if (own > 1e-9) return own;
    // No size of its own: the proportional arms it would have had before there was one.
    return std::max(distance(e.a, e.b) * 1.18,
                    e.meta.value(QLatin1String("crossSize")).toDouble(4.0));
}

Bounds Entity::bounds() const {
    Bounds bnds;
    // A shape that works its outline out is measured from that outline. Without this the arrow had no
    // box at all, so a window drag never caught it, "zoom to fit" could not see it, and it counted as
    // touching nothing when the draw order was asked which objects overlap.
    if (meta.value(QLatin1String("volumeArrow")).toBool()) {
        for (const Vec2& p : entityOutline(*this)) bnds.expand(p);
        if (bnds.valid) return bnds;
    }
    // A sleeve's stored points are its route, not the drawing: include the cable and the two sleeve
    // sections when that is the sort it is, or zoom-fit and overlap checks measure only the wire down
    // the middle.
    if (meta.value(QLatin1String("shrunkEnds")).toBool()) {
        for (const Vec2& p : entityOutline(*this)) bnds.expand(p);
        if (bnds.valid) return bnds;
    }
    // AN AXIS ANGLE is a centre and a point, drawn as an arc with centre lines running out past it -
    // so the room it takes on the sheet is the reach of those lines, not the gap between the two
    // stored points. Measured from the reach in every direction: the sweep may put the arc in any
    // quadrant, and the centre lines cross the whole of it in both.
    if (meta.value(QLatin1String("axisAngle")).toBool()) {
        // The arms and the arc are set apart now, so the room it takes is whichever reaches further.
        const double reach = std::max(axisAngleArm(*this), distance(a, b));
        bnds.expand({a.x - reach, a.y - reach});
        bnds.expand({a.x + reach, a.y + reach});
        if (bnds.valid) return bnds;
    }
    switch (type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
        bnds.expand(a);
        bnds.expand(b);
        break;
    case EntityType::TwistedPair: {
        // The path it runs along, out by half the cable's width - worked out from the path rather than
        // by measuring the weave. How far an object reaches is asked constantly: once per object per
        // repaint, per hit test, per selection zone. Answered by building a woven pair each time, a
        // sheet with a few pairs on it made the cursor lag behind the mouse. Rounding the knees only
        // ever cuts a corner inwards, so this still contains the whole of it.
        const double reach =
            std::max(0.2, meta.value("twistWidth").toDouble(kTwistedPairWidth)) / 2.0;
        const std::vector<Vec2>& along = points.size() >= 2 ? points : std::vector<Vec2>{a, b};
        for (const auto& p : along) {
            bnds.expand({p.x - reach, p.y - reach});
            bnds.expand({p.x + reach, p.y + reach});
        }
        break;
    }
    case EntityType::Hatch:
        // A circle-boundary hatch carries center/radius rather than points.
        if (meta.value("shape").toString() == QLatin1String("circle")) {
            bnds.expand({center.x - radius, center.y - radius});
            bnds.expand({center.x + radius, center.y + radius});
            break;
        }
        for (const auto& p : points) bnds.expand(p);
        break;
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Wipeout:
    case EntityType::Image:
    case EntityType::BlockRef:
        for (const auto& p : points) bnds.expand(p);
        break;
    case EntityType::Circle:
    case EntityType::Arc: {
        // The piece that is DRAWN, not the whole circle it is part of. A drawing package writes an
        // almost-straight segment as an arc of enormous radius sweeping a thousandth of a degree - four of
        // them in one released sample sheet - and taking the circle's box for those made the drawing's
        // extents 86 metres across. Everything that asks how big a drawing is then gets the wrong answer:
        // a fit-to-view shows a sheet the size of a full stop.
        //
        // So: the two ends, and then each quarter point the sweep actually passes through.
        auto at = [this](double a) {
            return Vec2{center.x + radius * std::cos(a), center.y + radius * std::sin(a)};
        };
        double sweep = endAngle - startAngle;
        while (sweep < 0.0) sweep += 2.0 * M_PI;
        // No sweep at all, or the whole way round, is a full circle - which is how one is written here.
        if (sweep <= 1e-9 || sweep >= 2.0 * M_PI - 1e-9) {
            bnds.expand({center.x - radius, center.y - radius});
            bnds.expand({center.x + radius, center.y + radius});
            break;
        }
        bnds.expand(at(startAngle));
        bnds.expand(at(endAngle));
        for (int q = 0; q < 4; ++q) {
            const double quarter = q * M_PI / 2.0;
            double from = quarter - startAngle;
            while (from < 0.0) from += 2.0 * M_PI;
            if (from <= sweep) bnds.expand(at(quarter));
        }
        break;
    }
    case EntityType::Ellipse: {
        const double rx = std::abs(radius), ry = std::abs(radiusY);
        const double c = std::cos(rotation), s = std::sin(rotation);
        const double extentX = std::hypot(rx * c, ry * s);
        const double extentY = std::hypot(rx * s, ry * c);
        bnds.expand({center.x - extentX, center.y - extentY});
        bnds.expand({center.x + extentX, center.y + extentY});
        break;
    }
    case EntityType::Text: {
        // The actual TEXT extent (estimated glyph rectangle), NOT the container —
        // so selection/hit-testing target the text itself. Lines wrap to boxWidth;
        // the block is offset within the box by the justification.
        const QString txt = meta.value("text").toString();
        const double size = meta.value("size").toDouble(meta.value("height").toDouble(2.5));
        const double spacing = std::max(1.0, meta.value("lineSpacing").toDouble(1.0));
        const double bw = meta.value("boxWidth").toDouble(0.0);
        const double bh = meta.value("boxHeight").toDouble(0.0);
        const double charW = size * 0.6, lineH = size * spacing * 1.3;
        const QStringList paras = txt.split(QLatin1Char('\n'));
        int wrapped = 0;
        double textW = 0.0;
        for (const QString& para : paras) {
            const double pw = para.size() * charW;
            if (bw > 0.0 && pw > bw) {
                wrapped += std::max<int>(1, static_cast<int>(std::ceil(pw / bw)));
                textW = std::max(textW, bw);
            } else {
                wrapped += 1;
                textW = std::max(textW, pw);
            }
        }
        if (wrapped == 0) { wrapped = 1; }
        if (textW < 1e-6) textW = size; // empty text: a small caret-ish box
        const double textH = wrapped * lineH;

        // Justification offset of the block within the container box.
        QString just = meta.value("justify").toString();
        if (just.size() < 2) {
            const QString al = meta.value("align").toString();
            just = QStringLiteral("T") + (al == QLatin1String("center") ? QStringLiteral("C")
                                          : al == QLatin1String("right") ? QStringLiteral("R")
                                                                         : QStringLiteral("L"));
        }
        // With no box the anchor is the point the text sits ABOUT, sideways as well as vertically:
        // centre shifts it left by half its width, right by all of it. This is what the renderer does,
        // and the two have to agree or the box that selects a label is not the label.
        // See the renderer: text read from a DXF is justified ABOUT its anchor, not inside its box.
        const bool anchorJust = meta.value(QStringLiteral("anchorJustify")).toBool();
        double hOff = 0.0, vOff = 0.0;
        {
            const double avail = (bw > 0.0 && !anchorJust) ? bw : 0.0;
            if (just[1] == QLatin1Char('C')) hOff = (avail - textW) / 2.0;
            else if (just[1] == QLatin1Char('R')) hOff = avail - textW;
        }
        // No box height: the anchor is the point the block sits ABOUT, so a middle attachment lifts
        // it by half and a bottom one by all of it. (See the renderer.)
        {
            const double avail = (bh > 0.0 && !anchorJust) ? bh : 0.0;
            if (just[0] == QLatin1Char('M')) vOff = (avail - textH) / 2.0;
            else if (just[0] == QLatin1Char('B')) vOff = avail - textH;
        }
        bnds.expand({a.x + hOff, a.y - vOff});
        bnds.expand({a.x + hOff + textW, a.y - vOff - textH});
        break;
    }
    case EntityType::Splice:
        bnds.expand(a);
        // A two-conductor splice reaches down to its second joint.
        if (meta.value("pair").toBool(false)) bnds.expand(b);
        break;
    case EntityType::LengthBreak: {
        // The pair of strokes, whichever way the run points: a square box of the mark's own reach is
        // right for either style and for any rotation.
        const double reach = meta.value("size").toDouble(6.0) +
                             meta.value("gap").toDouble(meta.value("size").toDouble(6.0) * 0.9);
        bnds.expand({a.x - reach, a.y - reach});
        bnds.expand({a.x + reach, a.y + reach});
        break;
    }
    case EntityType::NetLabel:
    case EntityType::NotConnected:
    case EntityType::ConnectionPin:
        bnds.expand(a);
        break;
    case EntityType::Connector: {
        const ConnectorSpec spec = connectorSpecFromMeta(meta);
        const ConnectorGeometry g = connectorGeometry(a, spec);
        bnds.expand(g.rectMin);
        bnds.expand(g.rectMax);
        for (const auto& pe : g.pinEnd) bnds.expand(pe);
        break;
    }
    case EntityType::Twist: {
        // The rendered braid/motif spans a wider band than the raw crossing points
        // (matches EntityRenderer's twist band formula) — use that for bounds/selection
        // so a region that only touches the drawn glyph (not the narrow crossing points)
        // still counts.
        if (points.empty()) break;
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (const auto& p : points) {
            cx += p.x;
            yMin = std::min(yMin, p.y);
            yMax = std::max(yMax, p.y);
        }
        cx /= static_cast<double>(points.size());
        const double band = std::max(1.0, yMax - yMin);
        const double w = std::max(28.0, band * 1.4);
        bnds.expand({cx - w / 2.0, yMin});
        bnds.expand({cx + w / 2.0, yMax});
        break;
    }
    case EntityType::Screen: {
        // The ellipse body + the drain leg's full extent (not just the stored joints).
        const int k = std::min(meta.value("catch").toInt(static_cast<int>(points.size())),
                               static_cast<int>(points.size()));
        if (k < 1) break;
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (int i = 0; i < k; ++i) {
            cx += points[i].x;
            yMin = std::min(yMin, points[i].y);
            yMax = std::max(yMax, points[i].y);
        }
        cx /= k;
        const double cyMid = (yMin + yMax) / 2.0;
        const double rx = meta.value("rx").toDouble(12.0);
        const double ry = meta.contains("ry") ? meta.value("ry").toDouble()
                                              : (yMax - yMin) / 2.0 + 6.0;
        bnds.expand({cx - rx, cyMid - ry});
        bnds.expand({cx + rx, cyMid + ry});
        if (static_cast<int>(points.size()) > k) bnds.expand(points.back()); // drain point
        break;
    }
    case EntityType::Dimension: {
        // Which stored points a dimension is made of depends on the kind of one it is. A radius,
        // diameter or jogged one is a leader from the RING'S CENTRE out, and `points` holds only the far
        // end of it; an ordinate one runs from a to b and has no points at all; an angular one is its
        // three points, and never sets a or b - so reading a and b on one of those reaches back to the
        // origin of the drawing. Answering with less than the object left dimensions out of Zoom All and
        // let the canvas cull one whose leader was on screen.
        const QString dty = meta.value("dimType").toString(QStringLiteral("linear"));
        for (const auto& p : points) bnds.expand(p);
        if (dty == QLatin1String("angular")) break;
        bnds.expand(b);
        if (dty == QLatin1String("radius") || dty == QLatin1String("jogged") ||
            dty == QLatin1String("diameter")) {
            bnds.expand(center);
            // A diameter one is drawn right across the ring, so it reaches as far the other side.
            if (dty == QLatin1String("diameter") && radius > 0.0) {
                bnds.expand({center.x - radius, center.y - radius});
                bnds.expand({center.x + radius, center.y + radius});
            }
            break;
        }
        bnds.expand(a);
        break;
    }
    case EntityType::DetailView: {
        // BOTH circles, and the caption under the enlarged one. A detail view is one object in two places -
        // the circle round the thing and the circle it is drawn bigger in - and answering with less than
        // both leaves half of it out of Zoom All and lets the canvas cull the half that is on screen.
        bnds.expand({center.x - radius, center.y - radius});
        bnds.expand({center.x + radius, center.y + radius});
        const double scale = std::max(0.01, meta.value(QStringLiteral("detailScale")).toDouble(2.0));
        const double big = radius * scale;
        bnds.expand({b.x - big, b.y - big});
        bnds.expand({b.x + big, b.y + big});
        // The words under it, roughly: two short lines at the caption height.
        const double h = std::max(1e-9, meta.value(QStringLiteral("detailTextSize")).toDouble(5.0));
        bnds.expand({b.x - big, b.y - big - h * 3.0});
        break;
    }
    case EntityType::Leader: {
        // The line it turns along, and the words on the end of it. The words are part of the object -
        // answering with less than an object covers leaves it out of Zoom All and lets the canvas cull one
        // whose text is on screen. Measured roughly, from the height and the number of characters, which
        // is all that can be said without a font to hand.
        for (const auto& p : points) bnds.expand(p);
        if (points.empty()) {
            bnds.expand(a);
            break;
        }
        const QString says = meta.value(QStringLiteral("text")).toString();
        if (!says.isEmpty()) {
            const double h = std::max(1e-9, meta.value(QStringLiteral("size")).toDouble(2.5));
            const Vec2 at = points.back();
            bnds.expand({at.x + h * 0.6 * says.size(), at.y + h});
            bnds.expand({at.x, at.y - h});
        }
        break;
    }
    default:
        for (const auto& p : points) bnds.expand(p);
        if (!points.empty()) break;
        bnds.expand(a);
        break;
    }
    return bnds;
}

namespace {
QJsonObject pointToJson(const Vec2& p) {
    return QJsonObject{{"x", p.x}, {"y", p.y}};
}
Vec2 pointFromJson(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return {o.value("x").toDouble(), o.value("y").toDouble()};
}
} // namespace

namespace {
bool usesPosition(EntityType t) {
    return t == EntityType::Text || t == EntityType::Splice ||
           t == EntityType::NotConnected || t == EntityType::NetLabel ||
           t == EntityType::Connector || t == EntityType::LengthBreak ||
           t == EntityType::ConnectionPin;
}
} // namespace

QJsonObject Entity::toJson() const {
    QJsonObject obj = meta; // start from preserved extras, then write known fields
    obj["type"] = entityTypeToString(type);
    if (!id.isEmpty()) obj["id"] = id;
    obj["layer"] = layer;
    if (hasExplicitColor()) obj["color"] = color;
    if (weight != 0.0) obj["weight"] = weight;

    // Field names mirror Edraw's DWX schema exactly so files round-trip.
    switch (type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
        obj["a"] = pointToJson(a);
        obj["b"] = pointToJson(b);
        break;
    case EntityType::Dimension:
        // A dimension is more than the two points it measures between. Where its line stands off the
        // geometry is a third point; an angular one is three points; a radius, diameter or jogged one is
        // a centre and a radius. Writing only a and b saved none of that, so a dimension placed and
        // saved came back flattened onto what it measured - the stand-off lost, a radius dimension drawn
        // at the origin. It only ever looked right on a file it had been RE-saved from, because then the
        // points came back through the preserved extras.
        obj["a"] = pointToJson(a);
        obj["b"] = pointToJson(b);
        if (!points.empty()) {
            QJsonArray arr;
            for (const auto& p : points) arr.append(pointToJson(p));
            obj["points"] = arr;
        } else {
            obj.remove("points");
        }
        if (center.x != 0.0 || center.y != 0.0) obj["center"] = pointToJson(center);
        else obj.remove("center");
        if (radius != 0.0) obj["radius"] = radius;
        else obj.remove("radius");
        if (startAngle != 0.0 || endAngle != 0.0) {
            obj["start"] = startAngle;
            obj["end"] = endAngle;
        }
        if (rotation != 0.0) obj["rotation"] = rotation;
        break;
    case EntityType::TwistedPair: {
        obj["a"] = pointToJson(a);
        obj["b"] = pointToJson(b);
        // A pair routed through knees is those knees; a straight one has none to write.
        if (points.size() >= 2) {
            QJsonArray arr;
            for (const auto& p : points) arr.append(pointToJson(p));
            obj["points"] = arr;
        }
        break;
    }
    case EntityType::Circle:
        obj["center"] = pointToJson(center);
        obj["radius"] = radius;
        break;
    case EntityType::Arc:
        obj["center"] = pointToJson(center);
        obj["radius"] = radius;
        obj["start"] = startAngle;
        obj["end"] = endAngle;
        break;
    case EntityType::Ellipse:
        obj["center"] = pointToJson(center);
        obj["rx"] = radius;
        obj["ry"] = radiusY;
        obj["rotation"] = rotation;
        if (startAngle != 0.0 || endAngle != 0.0) {
            obj["start"] = startAngle;
            obj["end"] = endAngle;
        }
        break;
    case EntityType::LengthBreak:
        // Which way it lies is the run's direction, and a mark that forgets it comes back lying
        // across the horizontal whatever it was placed on.
        obj["rotation"] = rotation;
        obj["position"] = pointToJson(a);
        break;
    case EntityType::Text:
    case EntityType::Splice:
    case EntityType::NotConnected:
    case EntityType::NetLabel:
    case EntityType::Connector:
    case EntityType::ConnectionPin:
        obj["position"] = pointToJson(a);
        break;
    default: { // polyline / spline / wire / rect / polygon / hatch / wipeout
        // A circle-boundary hatch stores its geometry as center/radius (fromJson
        // already reads these back).
        if (type == EntityType::Hatch && meta.value("shape").toString() == QLatin1String("circle")) {
            obj["center"] = pointToJson(center);
            obj["radius"] = radius;
            break;
        }
        QJsonArray arr;
        for (const auto& p : points) arr.append(pointToJson(p));
        obj["points"] = arr;
        break;
    }
    }
    return obj;
}

Entity Entity::fromJson(const QJsonObject& obj) {
    Entity e;
    e.type = entityTypeFromString(obj.value("type").toString());
    e.id = obj.value("id").toString();
    e.layer = obj.value("layer").toString("0");
    e.color = obj.value("color").toString();
    e.weight = obj.value("weight").toDouble(0.0);

    if (usesPosition(e.type) && obj.contains("position"))
        e.a = pointFromJson(obj.value("position"));
    if (obj.contains("a")) e.a = pointFromJson(obj.value("a"));
    if (obj.contains("b")) e.b = pointFromJson(obj.value("b"));
    if (obj.contains("center")) e.center = pointFromJson(obj.value("center"));

    e.radius = obj.value("radius").toDouble(0.0);
    // Ellipse uses rx/ry; fall back to radius/radiusY for our own files.
    if (obj.contains("rx")) e.radius = obj.value("rx").toDouble(0.0);
    e.radiusY = obj.value("ry").toDouble(obj.value("radiusY").toDouble(0.0));
    // Arc/ellipse sweep uses start/end; tolerate startAngle/endAngle too.
    e.startAngle = obj.value("start").toDouble(obj.value("startAngle").toDouble(0.0));
    e.endAngle = obj.value("end").toDouble(obj.value("endAngle").toDouble(0.0));
    e.rotation = obj.value("rotation").toDouble(0.0);

    if (obj.contains("points")) {
        for (const auto& v : obj.value("points").toArray())
            e.points.push_back(pointFromJson(v));
    }

    // Keep everything else (text content, wire/connector metadata, etc.).
    e.meta = obj;

    // A linear dimension out of a drawing saved before the axis was written down has it worked out from
    // its geometry, once, here - and then it is the object's, like a newly placed one's. Left to be
    // worked out afresh every time, the answer changed as the drawing was edited: dragging one of the
    // points the dimension measures between moves the middle, and past a certain point the dimension
    // decided it was measuring the other way about and showed a different number.
    if (e.type == EntityType::Dimension && !e.meta.contains(QStringLiteral("dimAcross")) &&
        e.meta.value(QStringLiteral("dimType")).toString(QStringLiteral("linear")) ==
            QLatin1String("linear"))
        e.meta.insert(QStringLiteral("dimAcross"), dimensionAcrossX(e));
    return e;
}

} // namespace Edraw_Desktop
