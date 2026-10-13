#include "tools/EntityTransforms.h"

#include "harness/Harness.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace Loopwire {

namespace {

Vec2 rotatePoint(const Vec2& p, const Vec2& pivot, double c, double s) {
    const Vec2 d = p - pivot;
    return {pivot.x + d.x * c - d.y * s, pivot.y + d.x * s + d.y * c};
}

Vec2 scalePoint(const Vec2& p, const Vec2& base, double f) {
    return base + (p - base) * f;
}

// Reflect point p across the line through a with unit direction u.
Vec2 mirrorPoint(const Vec2& p, const Vec2& a, const Vec2& u) {
    const Vec2 d = p - a;
    const double proj = dot(d, u);
    const Vec2 onLine = a + u * proj;
    return onLine * 2.0 - p;
}

// Apply a per-point function to every geometric point of an entity — including
// world-space geometry stored in meta (a double line's line-start reference and
// trim cut lines/circles), so a moved/rotated/mirrored double line keeps its caps.
template <typename F>
void mapPoints(Entity& e, F fn) {
    e.a = fn(e.a);
    e.b = fn(e.b);
    e.center = fn(e.center);
    for (auto& p : e.points) p = fn(p);

    auto xf = [&](const char* kx, const char* ky) {
        if (e.meta.contains(QLatin1String(kx))) {
            const Vec2 p = fn({e.meta.value(QLatin1String(kx)).toDouble(),
                               e.meta.value(QLatin1String(ky)).toDouble()});
            e.meta.insert(QLatin1String(kx), p.x);
            e.meta.insert(QLatin1String(ky), p.y);
        }
    };
    xf("lsRefAx", "lsRefAy");
    xf("lsRefBx", "lsRefBy");
    xf("textPosX", "textPosY"); // moved dimension-text position follows the entity
    xf("fromTextX", "fromTextY"); // an axis angle's two labels, where they have been moved to
    xf("toTextX", "toTextY");
    xf("leaderX", "leaderY");   // legacy single balloon leader tip follows the entity
    // Every balloon leader arrow tip (array form) follows the entity too.
    if (e.meta.contains(QLatin1String("leaders"))) {
        QJsonArray arr = e.meta.value(QLatin1String("leaders")).toArray();
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonArray a = arr[i].toArray();
            if (a.size() < 2) continue;
            const Vec2 q = fn({a[0].toDouble(), a[1].toDouble()});
            QJsonArray np;
            np.append(q.x);
            np.append(q.y);
            arr[i] = np;
        }
        e.meta.insert(QLatin1String("leaders"), arr);
    }
    // Every trim cap slot, and there are FOUR of them: each end of a run can be cut twice - once per line
    // of a double run - so the keys are cutStart, cutStart2, cutEnd and cutEnd2. Only the first of each pair
    // was being carried, and the second stayed behind at the coordinates it was cut at: a copied or moved run
    // then drew its cap from where it is now to where it used to be, which is a line straight across the
    // sheet. That is the ghost line. The slots are named the same way everywhere else that touches them
    // (applyDoubleRunCuts, and the grip edit that drops them), so they are named the same way here.
    for (const char* slot : {"cutStart", "cutStart2", "cutEnd", "cutEnd2"}) {
        const QString base = QString::fromLatin1(slot);
        const QByteArray ax = (base + QStringLiteral("Ax")).toLatin1();
        const QByteArray ay = (base + QStringLiteral("Ay")).toLatin1();
        const QByteArray bx = (base + QStringLiteral("Bx")).toLatin1();
        const QByteArray by = (base + QStringLiteral("By")).toLatin1();
        const QByteArray cx = (base + QStringLiteral("Cx")).toLatin1();
        const QByteArray cy = (base + QStringLiteral("Cy")).toLatin1();
        xf(ax.constData(), ay.constData());
        xf(bx.constData(), by.constData());
        xf(cx.constData(), cy.constData());   // circle-cut centre
    }
    // A hole trimmed out of one line of a double run is kept as the two points where the line stops
    // and starts again ([side, ax, ay, bx, by]); they follow the object like the caps do.
    if (e.meta.contains(QLatin1String("holes"))) {
        QJsonArray arr = e.meta.value(QLatin1String("holes")).toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonArray h = arr[i].toArray();
            if (h.size() < 5) continue;
            const Vec2 p1 = fn({h[1].toDouble(), h[2].toDouble()});
            const Vec2 p2 = fn({h[3].toDouble(), h[4].toDouble()});
            h[1] = p1.x; h[2] = p1.y; h[3] = p2.x; h[4] = p2.y;
            arr[i] = h;
        }
        e.meta.insert(QLatin1String("holes"), arr);
    }
    // Extend cut-cap edge sets (flat [ax,ay,bx,by,...]) follow the entity too - all four slots again.
    for (const char* k : {"cutStartPoly", "cutStart2Poly", "cutEndPoly", "cutEnd2Poly", "entryFaces"}) {
        if (!e.meta.contains(QLatin1String(k))) continue;
        QJsonArray arr = e.meta.value(QLatin1String(k)).toArray();
        for (int i = 0; i + 1 < arr.size(); i += 2) {
            const Vec2 q = fn({arr[i].toDouble(), arr[i + 1].toDouble()});
            arr[i] = q.x;
            arr[i + 1] = q.y;
        }
        e.meta.insert(QLatin1String(k), arr);
    }
}

} // namespace

// A LABEL CARRIES A NOTE OF WHERE IT WAS LAID, and the note has to go with it.
//
// The label's SHAPE is points and moves with any transform, because everything here maps points. Its
// note is numbers in the meta - the middle of it, the run it was laid along - and those sat still: a
// label moved by anything but a drag along its own run left its joints, and the run it is coupled
// to, at the place it had been moved from. The joints are drawn off that middle, so they stayed
// behind on the paper; and the next slide walked the label back onto the old run.
//
// A label under a clear sleeve is where it shows up first, because that model is several objects and
// every one of them carries the note - but it is the note that is wrong, not the sleeve.
void moveLabelNote(Entity& e, const Vec2& delta) {
    if (!e.meta.contains(QLatin1String("labelMidX"))) return;
    e.meta.insert(QLatin1String("labelMidX"),
                  e.meta.value(QLatin1String("labelMidX")).toDouble() + delta.x);
    e.meta.insert(QLatin1String("labelMidY"),
                  e.meta.value(QLatin1String("labelMidY")).toDouble() + delta.y);
    // AND THE RUN IT WAS LAID ALONG, point by point. Left where it was, a label slid after being
    // moved would set off from the old cable: the middle it walks from is on the path, and the two
    // have to stay one thing.
    const QJsonArray was = e.meta.value(QLatin1String("labelPath")).toArray();
    if (was.size() < 4) return;
    QJsonArray now;
    for (int i = 0; i + 1 < was.size(); i += 2) {
        now.append(was[i].toDouble() + delta.x);
        now.append(was[i + 1].toDouble() + delta.y);
    }
    e.meta.insert(QLatin1String("labelPath"), now);
}

void translateEntity(Entity& e, const Vec2& delta) {
    mapPoints(e, [&](const Vec2& p) { return p + delta; });
    moveLabelNote(e, delta);
}

void rotateEntity(Entity& e, const Vec2& pivot, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    mapPoints(e, [&](const Vec2& p) { return rotatePoint(p, pivot, c, s); });
    // The label's note turns with it too - the middle, the run it was laid along, and which way that
    // run goes, which is what says where the label's own length lies.
    if (e.meta.contains(QLatin1String("labelMidX"))) {
        const Vec2 mid = rotatePoint({e.meta.value(QLatin1String("labelMidX")).toDouble(),
                                      e.meta.value(QLatin1String("labelMidY")).toDouble()},
                                     pivot, c, s);
        e.meta.insert(QLatin1String("labelMidX"), mid.x);
        e.meta.insert(QLatin1String("labelMidY"), mid.y);
        const Vec2 dir{e.meta.value(QLatin1String("labelDirX")).toDouble(1.0),
                       e.meta.value(QLatin1String("labelDirY")).toDouble(0.0)};
        e.meta.insert(QLatin1String("labelDirX"), dir.x * c - dir.y * s);
        e.meta.insert(QLatin1String("labelDirY"), dir.x * s + dir.y * c);
        const QJsonArray was = e.meta.value(QLatin1String("labelPath")).toArray();
        if (was.size() >= 4) {
            QJsonArray now;
            for (int i = 0; i + 1 < was.size(); i += 2) {
                const Vec2 q =
                    rotatePoint({was[i].toDouble(), was[i + 1].toDouble()}, pivot, c, s);
                now.append(q.x);
                now.append(q.y);
            }
            e.meta.insert(QLatin1String("labelPath"), now);
        }
    }
    // A linear dimension measures along an axis of the sheet and remembers which; turning it a quarter
    // turn turns which axis that is, so the note of it turns with the object.
    if (e.type == EntityType::Dimension && e.meta.contains(QLatin1String("dimAcross")) &&
        std::abs(c) < std::abs(s))
        e.meta.insert(QLatin1String("dimAcross"), !e.meta.value(QLatin1String("dimAcross")).toBool());
    e.rotation += angle;
    e.startAngle += angle;
    e.endAngle += angle;
    // AND AN AXIS ANGLE'S CROSS TURNS WITH IT. The cross is the datum the sweep is measured from, and
    // once it has been pinned (see the start handle) it is an absolute bearing rather than something
    // read back off the points - so turning the object has to turn it too, or the centre lines stay
    // where they were while everything drawn against them swings round.
    if (e.meta.contains(QLatin1String("axisCross")))
        e.meta.insert(QLatin1String("axisCross"),
                      e.meta.value(QLatin1String("axisCross")).toDouble() +
                          angle * 180.0 / 3.14159265358979323846);
}

void scaleEntity(Entity& e, const Vec2& base, double factor) {
    const double s = std::abs(factor);
    mapPoints(e, [&](const Vec2& p) { return scalePoint(p, base, factor); });
    e.radius *= s;
    e.radiusY *= s;
    // Scale every length-valued meta so the shape scales uniformly: the double-line
    // gap, trim-cap radii, the uniform corner radius, and the shield ellipse radii (rx/ry)
    // — so a shield resizes with the wires/harness when they are scaled together.
    for (const char* k : {"gap", "cutStartR", "cutEndR", "cornerRadius", "landing", "rx", "ry",
                          "twistWidth", "twistPitch", "twistEnds", "shrinkWidth",
                          "shrinkFaceStart", "shrinkFaceEnd", "shrinkTaperStart",
                          "shrinkTaperEnd", "shrinkCoverStart", "shrinkCoverEnd",
                          "shrinkCableWidth", "pinScale"})
        if (e.meta.contains(QLatin1String(k)))
            e.meta.insert(QLatin1String(k), e.meta.value(QLatin1String(k)).toDouble() * s);
    // Per-corner fillet radii / chamfer setbacks are lengths too — scale each entry.
    for (const char* k : {"cornerRadii", "cornerChamfers", "cornerChamfers2"}) {
        if (!e.meta.contains(QLatin1String(k))) continue;
        QJsonArray arr = e.meta.value(QLatin1String(k)).toArray();
        for (int i = 0; i < arr.size(); ++i) arr[i] = arr[i].toDouble() * s;
        e.meta.insert(QLatin1String(k), arr);
    }
    // AND THE WRITING SCALES WITH IT. A text height is a length like any other, and it was being left behind: a
    // table scaled up kept its cells' words at the size they were, so the words no longer fitted the boxes they
    // were in and spilled over the lines into the row below. The same was true of a piece of text, a dimension,
    // a balloon, a marking on a wire - everything on a drawing that has words on it.
    //
    // Scaling something is asking for the same drawing at another size. Words that stay put are not the same
    // drawing; they are a drawing with the wrong writing on it.
    for (const char* k : {"height", "size", "textHeight", "textSize", "arrowSize", "extOffset",
                          "detailTextSize", "detailTagGap", "detailBigGap", "colorSize", "gaugeSize",
                          "labelSize", "dimTextHeight"})
        if (e.meta.contains(QLatin1String(k)))
            e.meta.insert(QLatin1String(k), e.meta.value(QLatin1String(k)).toDouble() * s);
    // A TABLE keeps a height per cell where a cell has been given one of its own, and the table's own height is
    // the fallback for the rest. Both have to go, or the cells that were left alone scale and the ones somebody
    // had set by hand do not.
    if (e.meta.contains(QStringLiteral("cellTextSize"))) {
        QJsonArray arr = e.meta.value(QStringLiteral("cellTextSize")).toArray();
        for (int i = 0; i < arr.size(); ++i)
            if (arr[i].toDouble() > 0.0) arr[i] = arr[i].toDouble() * s;
        e.meta.insert(QStringLiteral("cellTextSize"), arr);
    }
    // A row that has been given a fixed height is a length too: left alone, a scaled table's rows would keep
    // their old height while its outline grew, and the writing would no longer sit in them.
    if (e.meta.contains(QStringLiteral("rowFixed"))) {
        QJsonArray arr = e.meta.value(QStringLiteral("rowFixed")).toArray();
        for (int i = 0; i < arr.size(); ++i)
            if (arr[i].toDouble() > 0.0) arr[i] = arr[i].toDouble() * s;
        e.meta.insert(QStringLiteral("rowFixed"), arr);
    }
    // A connector is parametric (anchor + size fields in meta): scale its dimensions so
    // the whole body / pins / text resize UNIFORMLY, keeping its structure. mapPoints
    // already moved the anchor about the base, so it scales about the scale base point.
    if (e.type == EntityType::Connector) {
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta); // pre-scale sizes
        const ConnectorGeometry g = connectorGeometry(e.a, spec); // current body W/H
        // textSize is NOT here: it is in the general list above, which every entity goes through. Scaling it in
        // both places doubled it, so a connector's words came out the square of the factor.
        for (const char* k : {"pitch", "pinLength", "pinSize", "desSize", "pnSize", "pinTextDX",
                              "pinTextDY", "desTextDX", "desTextDY", "pnTextDX", "pnTextDY"})
            if (e.meta.contains(QLatin1String(k)))
                e.meta.insert(QLatin1String(k), e.meta.value(QLatin1String(k)).toDouble() * s);
        // Scaling the connector scales the pin "cup" glyph too (unlike a pitch change,
        // which leaves it at the default size). Accumulate the factor in cupScale.
        e.meta.insert(QStringLiteral("cupScale"),
                      e.meta.value(QStringLiteral("cupScale")).toDouble(1.0) * s);
        // Bake the effective body into a custom W/H scaled by the same factor so the
        // WIDTH scales too — dynamic mode otherwise pins the width at 40, which would
        // stretch the connector out of proportion.
        e.meta.insert(QStringLiteral("rectMode"), QStringLiteral("custom"));
        e.meta.insert(QStringLiteral("rectWidth"), g.effectiveWidth * s);
        e.meta.insert(QStringLiteral("rectHeight"), g.effectiveHeight * s);
    }
}

void mirrorEntity(Entity& e, const Vec2& a, const Vec2& b) {
    const Vec2 u = (b - a).normalized();
    // A connector is parametric (axis-aligned body + horizontal pins): mirror its
    // body box and flip the matching orientation flag (mirrorX for a vertical mirror
    // line, mirrorY for a horizontal one), re-anchoring the top corner. Its text
    // offsets stay relative, so skip the generic point map.
    if (e.type == EntityType::Connector) {
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        const Vec2 m0 = mirrorPoint(g.rectMin, a, u), m1 = mirrorPoint(g.rectMax, a, u);
        const double minX = std::min(m0.x, m1.x), maxX = std::max(m0.x, m1.x);
        const double maxY = std::max(m0.y, m1.y);
        const bool vertical = std::abs(u.x) < std::abs(u.y); // mirror line is vertical-ish
        bool mx = spec.mirrorX, my = spec.mirrorY;
        if (vertical) mx = !mx; else my = !my;
        e.meta["mirrorX"] = mx;
        e.meta["mirrorY"] = my;
        e.a = {mx ? maxX : minX, maxY}; // origin = top-left (mx=false) / top-right (mx=true)
        return;
    }
    mapPoints(e, [&](const Vec2& p) { return mirrorPoint(p, a, u); });

    // AN AXIS ANGLE'S CROSS REFLECTS TOO. It is an absolute bearing once pinned, so the points came
    // back reflected and the centre lines they are measured against did not: a bearing reflected in a
    // line lying at phi is 2*phi minus itself.
    if (e.meta.contains(QLatin1String("axisCross"))) {
        constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
        const double phi = std::atan2(u.y, u.x) * kRadToDeg;
        e.meta.insert(QLatin1String("axisCross"),
                      2.0 * phi - e.meta.value(QLatin1String("axisCross")).toDouble());
    }

    // A MIRROR SWAPS WHICH RAIL IS WHICH, so every cut has to change sides with them.
    //
    // A trim on a double run is recorded against ONE of its two lines - the slot carries a
    // "<slot>Side" of +1 or -1, and applyRunCut ignores the cut unless the rail it is drawing matches.
    // mapPoints reflects where the cut IS, which is right, and says nothing about which line it belongs
    // to, which after a mirror is the other one: left has become right.
    //
    // Left alone, the mirrored copy carried each cut on the rail it used to be on. That rail came out
    // cut where nothing had cut it, and the rail that should have been cut came out running straight
    // through - which reads as the mirror having brought the trimmed-away piece along with it.
    //
    // Only the SIDE is flipped here and not in mapPoints, because a move or a rotate does not swap the
    // rails - only a reflection does.
    for (const char* slot : {"cutStart", "cutStart2", "cutEnd", "cutEnd2"}) {
        const QString key = QString::fromLatin1(slot) + QStringLiteral("Side");
        if (!e.meta.contains(key)) continue;
        const int side = e.meta.value(key).toInt();
        if (side != 0) e.meta.insert(key, -side);
    }
    // A hole trimmed out of one line keeps its side as the first of its five figures - see applyRunCut
    // and the hole reader beside it - so it changes rails the same way.
    if (e.meta.contains(QLatin1String("holes"))) {
        QJsonArray arr = e.meta.value(QLatin1String("holes")).toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonArray h = arr[i].toArray();
            if (h.isEmpty()) continue;
            const int side = h[0].toInt();
            if (side != 0) h[0] = -side;
            arr[i] = h;
        }
        e.meta.insert(QLatin1String("holes"), arr);
    }
    // Reflect arc angles about the mirror line direction.
    const double axis = std::atan2(u.y, u.x);
    if (e.type == EntityType::Arc) {
        const double ns = 2.0 * axis - e.startAngle;
        const double ne = 2.0 * axis - e.endAngle;
        // Mirroring reverses orientation: swap start/end.
        e.startAngle = ne;
        e.endAngle = ns;
    }
    // A PIECE of an ellipse says where it starts and stops in the ellipse's own parameter, not as an
    // angle about its centre - and reflecting the ellipse's frame (below) turns that parameter round.
    // So the piece runs from minus-the-end to minus-the-start. Left alone, the mirrored copy showed the
    // WRONG PIECE of the right ellipse: a half happens to be symmetric about its own parameter and came
    // out looking correct, which is why this only showed on a piece that was not.
    if (e.type == EntityType::Ellipse && e.meta.value(QStringLiteral("ellipticalArc")).toBool()) {
        const double ns = -e.endAngle;
        const double ne = -e.startAngle;
        e.startAngle = ns;
        e.endAngle = ne;
    }
    // A linear dimension measures along an axis of the sheet and remembers which. A mirror line that is
    // itself along an axis reflects x onto x and y onto y, so it stays as it was; one at a diagonal maps
    // the x axis onto the y, and then the dimension measures the other way about.
    if (e.type == EntityType::Dimension && e.meta.contains(QLatin1String("dimAcross"))) {
        const Vec2 mx = mirrorPoint({1, 0}, {0, 0}, u); // where the x axis lands
        if (std::abs(mx.x) < std::abs(mx.y))
            e.meta.insert(QLatin1String("dimAcross"),
                          !e.meta.value(QLatin1String("dimAcross")).toBool());
    }
    e.rotation = 2.0 * axis - e.rotation;
}

} // namespace Loopwire
