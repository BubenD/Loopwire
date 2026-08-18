#include "render/EntityRenderer.h"

#include "geometry/Geometry.h"
#include "harness/Harness.h"
#include "model/Picture.h"

#include <QFont>
#include <QHash>
#include <QJsonArray>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QtMath>

#include <algorithm>
#include <cstdio>

namespace Edraw_Desktop {

namespace {
int g_dimDecimals = 2; // decimals on dimension labels (configurable via Options)
QColor g_bgColor{"#1e2832"}; // canvas background, used to paint Wipeout masks
// Drawing the inside of a detail view. A detail drawn inside a detail would draw itself for ever, and it is
// not something a drawing office asks for - so the inner one is simply left out.
bool g_inDetailView = false;
} // namespace

Vec2 detailViewTextAt(const Entity& e, bool big) {
    const double scale = std::max(0.01, e.meta.value(QStringLiteral("detailScale")).toDouble(2.0));
    const double capH = std::max(1e-9, e.meta.value(QStringLiteral("detailTextSize")).toDouble(3.5));
    const Vec2 c = big ? e.b : e.center;
    const double r = big ? e.radius * scale : e.radius;
    const double ang =
        e.meta.value(big ? QStringLiteral("detailBigAngle") : QStringLiteral("detailTagAngle"))
            .toDouble(big ? -90.0 : 45.0);
    const double gap =
        e.meta.value(big ? QStringLiteral("detailBigGap") : QStringLiteral("detailTagGap"))
            .toDouble(big ? capH * 1.1 : capH * 0.6);
    const double a = ang * M_PI / 180.0;
    return {c.x + (r + gap) * std::cos(a), c.y + (r + gap) * std::sin(a)};
}

namespace {
bool g_monochrome = false;
bool g_plotLineweights = true;   // a plot may be asked for without them
double g_annotationScale = 1.0;  // how much bigger annotation is drawn than it is written
bool g_whitePaper = false;
bool g_lengthInch = false;   // dimension unit: false = mm (world), true = inch
bool g_dualUnits = false;    // append the other unit in parentheses

// Decoded pictures, by content hash. A picture is decoded once and then drawn from the cache: without
// this, every repaint would re-decode every picture on the sheet, and panning a drawing with a logo on
// it would crawl. The cache is bounded, so a session that places dozens of different pictures does not
// hold all of them in memory for ever.
QHash<QString, QPixmap> g_pictures;

QPixmap picturePixmap(const Entity& e) {
    const QString key = e.meta.value(QLatin1String(PictureKeys::kKey)).toString();
    if (!key.isEmpty()) {
        const auto it = g_pictures.constFind(key);
        if (it != g_pictures.constEnd()) return *it;
    }
    QPixmap pm;
    const QByteArray bytes = pictureBytes(e);
    if (bytes.isEmpty() || !pm.loadFromData(bytes)) return QPixmap();
    if (!key.isEmpty()) {
        if (g_pictures.size() > 64) g_pictures.clear();
        g_pictures.insert(key, pm);
    }
    return pm;
}
}
void EntityRenderer::setDimensionDecimals(int d) { g_dimDecimals = std::clamp(d, 0, 6); }
int EntityRenderer::dimensionDecimals() { return g_dimDecimals; }

double EntityRenderer::shieldRotation(const std::vector<Vec2>& crossings) {
    // Major (ry) axis runs from the first to the last caught wire. Level wires give a
    // vertical shield (rot = +90 deg); angled wires tilt it to match. The axis is
    // normalized to point "up" (world +y) so the leg's top/bottom stay absolute
    // regardless of the order the wires were caught in.
    if (crossings.size() < 2) return M_PI / 2.0;
    const Vec2 d = crossings.back() - crossings.front();
    if (d.x * d.x + d.y * d.y < 1e-12) return M_PI / 2.0;
    double rot = std::atan2(d.y, d.x);
    if (std::sin(rot) < 0.0) rot += M_PI; // flip so the major axis points upward
    return rot;
}

std::vector<Vec2> EntityRenderer::shieldOutline(const Vec2& center, double rx, double ry,
                                                double rot, bool capsule) {
    // major = along ry (the wire-spread direction), minor = along rx (perpendicular).
    const Vec2 major{std::cos(rot), std::sin(rot)};
    const Vec2 minor{-std::sin(rot), std::cos(rot)};
    std::vector<Vec2> out;
    if (!capsule) {
        constexpr int kN = 48;
        out.reserve(kN + 1);
        for (int i = 0; i <= kN; ++i) {
            const double t = 2.0 * M_PI * i / kN;
            const double u = rx * std::cos(t), v = ry * std::sin(t);
            out.push_back({center.x + u * minor.x + v * major.x,
                           center.y + u * minor.y + v * major.y});
        }
        return out;
    }
    // Capsule (stadium): straight sides of half-length (ry-rx) capped by rx semicircles.
    const double straight = std::max(0.0, ry - rx);
    const Vec2 cTop{center.x + straight * major.x, center.y + straight * major.y};
    const Vec2 cBot{center.x - straight * major.x, center.y - straight * major.y};
    constexpr int kCap = 16;
    auto arc = [&](const Vec2& c, double a0, double a1) {
        for (int i = 0; i <= kCap; ++i) {
            const double a = a0 + (a1 - a0) * i / kCap;
            const double u = rx * std::cos(a), v = rx * std::sin(a);
            out.push_back({c.x + u * minor.x + v * major.x, c.y + u * minor.y + v * major.y});
        }
    };
    arc(cTop, 0.0, M_PI);            // top cap: +minor -> +major -> -minor
    arc(cBot, M_PI, 2.0 * M_PI);     // bottom cap: -minor -> -major -> +minor
    out.push_back(out.front());      // close
    return out;
}
void EntityRenderer::setBackgroundColor(const QColor& c) { g_bgColor = c; }
QColor EntityRenderer::backgroundColor() { return g_bgColor; }
void EntityRenderer::setMonochrome(bool on) { g_monochrome = on; }
void EntityRenderer::setPlotLineweights(bool on) { g_plotLineweights = on; }
void EntityRenderer::setAnnotationScale(double factor) {
    g_annotationScale = factor > 1e-6 ? factor : 1.0;
}
double EntityRenderer::annotationScale() { return g_annotationScale; }
bool EntityRenderer::plotLineweights() { return g_plotLineweights; }
bool EntityRenderer::monochrome() { return g_monochrome; }
void EntityRenderer::setWhitePaper(bool on) { g_whitePaper = on; }
bool EntityRenderer::whitePaper() { return g_whitePaper; }
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

QVector<qreal> linetypeDashes(const QString& name, double scale, double penWidth) {
    const QString n = name.trimmed().toUpper();
    // Solid, and the two that mean "whatever it inherits" - which, until there is a linetype on a layer or
    // a block to inherit, is solid.
    if (n.isEmpty() || n == QLatin1String("CONTINUOUS") || n == QLatin1String("BYLAYER") ||
        n == QLatin1String("BYBLOCK") || n == QLatin1String("SOLID"))
        return {};
    QVector<qreal> dashes;
    if (n == QLatin1String("CENTER")) dashes = {12, 3, 2, 3};
    else if (n == QLatin1String("CENTERX2")) dashes = {20, 4, 3, 4};
    else if (n == QLatin1String("DOT2") || n == QLatin1String("DOTTED")) dashes = {1, 3};
    else if (n == QLatin1String("HIDDEN") || n == QLatin1String("DASHED")) dashes = {5, 3};
    else if (n == QLatin1String("PHANTOM")) dashes = {14, 3, 3, 3, 3, 3};
    else if (n == QLatin1String("DASHDOT")) dashes = {8, 3, 1, 3};
    else return {};   // a name from a file that means nothing here draws solid rather than guessed at
    // Qt measures a dash pattern in units of the PEN WIDTH, so a raw pattern on a hairline collapses to
    // nothing. Normalising by the width makes a dash the same length on screen whatever it is drawn with.
    const double w = penWidth > 0.0 ? penWidth : 1.0;
    const double s = std::max(0.05, scale);
    for (qreal& d : dashes) d = d * s / w;
    return dashes;
}

QColor EntityRenderer::resolveColor(const Entity& e, const Document& doc) {
    QString layerHex = QStringLiteral("#ffffff");
    if (const Layer* layer = doc.findLayer(e.layer); layer && QColor(layer->color).isValid())
        layerHex = layer->color;

    // A wire is drawn in its ordinary COLOUR property, like every other entity: that is the
    // one the Properties panel and the ribbon set, so changing it must change the drawing. The
    // harness colour CODE (WH/BK/RD/...) is information about the wire, and only stands in as
    // the colour when no colour of its own has been set - which is what keeps wires drawn
    // before this, and wires whose code was set but never painted, looking as they did.
    if (e.type == EntityType::Wire) {
        if (e.hasExplicitColor()) {
            const QColor own(e.color);
            if (own.isValid()) return own;
        }
        if (e.meta.value("wireColorMode").toString() == QLatin1String("bylayer"))
            return QColor(layerHex);
        return QColor(wireColorHex(e.meta.value("wireColor").toString(), layerHex));
    }
    if (e.hasExplicitColor()) {
        QColor c(e.color);
        if (c.isValid()) return c;
    }
    return QColor(layerHex);
}

namespace {

double effectiveWeight(const Entity& e, const Document& doc) {
    if (e.weight > 0.0) return e.weight;
    if (const Layer* layer = doc.findLayer(e.layer)) return layer->weight;
    return 0.25;   // no such layer: the same hairline a layer gives by default
}

// The pen width to draw with, in screen pixels - which is NOT the same as the lineweight.
//
// A lineweight is a number off the standard list (0.25 is a normal line). Handed straight to Qt as a
// cosmetic pen width, anything under 1 is drawn as a FRACTION of a pixel: Qt renders it by lowering the
// coverage, so a 0.25 line comes out as a quarter-lit grey smear instead of a thin white line. Which is
// exactly what a hairline default did to every drawing - thin, but washed out.
//
// So: a line is never drawn narrower than one whole pixel. Thinner than that is not thinner, it is
// fainter, and a drawing is meant to be read.
double penPixels(double weight) {
    // A plot can be asked NOT to plot object lineweights - a check print, where what is wanted is the
    // geometry and not the emphasis. Then every line is the same thin one.
    if (!EntityRenderer::plotLineweights()) return 1.0;
    return std::max(1.0, weight);
}

// Is this shape made of straight edges? A hairline one is drawn without antialiasing (see the guard
// below); a curve is not, because a jagged circle is worse than a slightly soft one.
bool straightEdged(EntityType t) {
    switch (t) {
    case EntityType::Line:
    case EntityType::DoubleLine:
    case EntityType::Polyline:
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Wire:
    case EntityType::Dimension:
    case EntityType::Wipeout:
    case EntityType::NetLabel:
    case EntityType::Bundle:
        return true;
    default:
        return false;
    }
}

// Antialiasing turned off while a hairline straight edge is drawn, and put back afterwards.
//
// A one-pixel antialiased line that falls between two rows of pixels is drawn as two half-lit rows -
// which is grey, not white. That is what made a thin line look washed out: the width was right and the
// brightness was not. Without antialiasing the same line is one row, fully lit, which is what a drawing
// is meant to look like and what every CAD application does with a hairline.
class HairlineCrisp {
public:
    HairlineCrisp(QPainter& p, bool apply) : p_(p), apply_(apply) {
        if (!apply_) return;
        was_ = p_.testRenderHint(QPainter::Antialiasing);
        p_.setRenderHint(QPainter::Antialiasing, false);
    }
    ~HairlineCrisp() {
        if (apply_) p_.setRenderHint(QPainter::Antialiasing, was_);
    }
    HairlineCrisp(const HairlineCrisp&) = delete;
    HairlineCrisp& operator=(const HairlineCrisp&) = delete;

private:
    QPainter& p_;
    bool apply_ = false;
    bool was_ = true;
};

// How wide a WIRE is drawn, in screen pixels. The base is the weight the layer gives it - a net
// wire is as thin as anything else on that layer - and its cross-section moves it a little
// either way, bounded at both ends: the gauge should be legible in the line, not shout.
double wireDrawWidth(const Entity& e, const Document& doc) {
    // A conductor is the subject of a harness drawing, so it is drawn heavier than the geometry around
    // it. The floor is exactly what a wire used to get from the old default layer weight, so every
    // harness drawing looks the same as it did while ordinary lines became hairlines. A layer set
    // heavier than this still carries its wires heavier.
    const double base = std::max(2.0, effectiveWeight(e, doc));
    const QString gauge = e.meta.value("wireGauge").toString();
    if (gauge.isEmpty()) return base;
    // wireGaugeWeight runs 0.7 (32 AWG) to 10 (1 AWG); 1.5 is 0.5 mm2, the everyday size, and
    // sits at the layer's own weight.
    const double delta = 0.28 * (wireGaugeWeight(gauge) - 1.5);
    return std::clamp(base + delta, 0.6, base + 1.9);
}

// Draw a small text label at a screen point, scaled to world height.
// Measuring a font is not free, and a drawing's writing is very nearly all set in the SAME font: one
// family, one or two sizes. Qt keeps the font engine, but a QFontMetricsF still has to be built for every
// string, and on a sheet of several hundred labels plus a couple of hundred table cells that came to over
// half the time spent redrawing the whole drawing.
//
// Remembering the last one answers almost every ask without building anything, because the next string is
// nearly always in the same font as the one before. QFont's own comparison is a cheap check of what the
// font IS, so a miss costs almost nothing either. Per thread, since a painter belongs to one.
const QFontMetricsF& metricsFor(const QFont& f) {
    thread_local QFont known;
    thread_local QFontMetricsF cached{QFont()};
    thread_local bool have = false;
    if (!have || known != f) {
        known = f;
        cached = QFontMetricsF(f);
        have = true;
    }
    return cached;
}

// Setting a painter's font throws away what it had worked out about the old one, so it is worth not
// doing when the font has not changed - which, string after string, it usually has not.
void useFont(QPainter& p, const QFont& f) {
    if (p.font() != f) p.setFont(f);
}

// The lines a string breaks into when wrapped to a box.
//
// Not cached, and it is worth saying why, because caching it was the obvious next thing and it did not
// work. Removing the wrap boxes from the imported sheet takes the redraw from 23 ms to 15, which reads
// like eight milliseconds of wrapping to be saved - but it is not. Unwrapped text is FEWER LINES, and the
// saving is in the glyphs not drawn, not in the measuring not done. Remembering the line breaks bought
// nothing at all, twice: keyed on the string, then keyed on the object's id. The floor here is Qt putting
// glyphs on the screen, and no amount of not-re-measuring gets under it.
QStringList wrappedLines(const QFontMetricsF& fm, const QString& text, double boxWpx,
                         const QString& list) {
    QStringList lines;
    int num = 1;
    for (const QString& para : text.split(QLatin1Char('\n'))) {
        QString full = para;
        if (list == QLatin1String("bullet")) full = QString::fromUtf8("\xE2\x80\xA2 ") + para;
        else if (list == QLatin1String("number")) full = QString::number(num++) + ". " + para;
        if (boxWpx <= 1.0) {
            lines << full;
            continue;
        }
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
    return lines;
}

void drawWorldText(QPainter& p, const QPointF& at, const QString& text,
                   double worldHeight, double scale, const QColor& color,
                   Qt::Alignment align = Qt::AlignLeft) {
    if (text.isEmpty()) return;
    const double px = worldHeight * scale;
    // Text too small to be a letter is not drawn. Below about two pixels a glyph is not small writing,
    // it is a smear the width of a word - nothing can be read from it, and it costs as much to draw as
    // readable text does. On a real imported sheet that is 352 pieces of text plus a couple of hundred
    // table cells, over half the time spent redrawing the whole drawing, all of it on marks nobody can
    // read until they zoom in - at which point it is drawn again, properly. Which is how every drawing
    // package behaves.
    if (px < 2.0) return;
    QFont f = p.font();
    f.setPixelSize(std::max(1, static_cast<int>(px)));
    useFont(p, f);
    p.setPen(color);
    QPointF pos = at;
    if (align & (Qt::AlignHCenter | Qt::AlignRight | Qt::AlignVCenter)) {
        const QFontMetricsF& fm = metricsFor(f);
        if (align & Qt::AlignHCenter) pos.setX(at.x() - fm.horizontalAdvance(text) / 2.0);
        else if (align & Qt::AlignRight) pos.setX(at.x() - fm.horizontalAdvance(text));
        // Centre the glyph body on `at.y` (baseline sits below the visual middle).
        if (align & Qt::AlignVCenter) pos.setY(at.y() + (fm.ascent() - fm.descent()) / 2.0);
    }
    p.drawText(pos, text);
}

// Text that reads ALONG a wire: centred on `at`, rotated to the wire's direction, and
// lifted clear of the line (above it, or below when `below`). Screen y runs downward, so
// the world direction's y is negated to get the on-screen angle. This is what makes a
// vertical wire's marking turn with the wire instead of staying horizontal.
void drawTextAlongWire(QPainter& p, const ViewTransform& vt, const Vec2& at, const Vec2& dir,
                       const QString& text, double worldHeight, const QColor& color,
                       bool below, double lift = 0.0) {
    if (text.isEmpty()) return;
    QFont f = p.font();
    f.setPixelSize(std::max(1, static_cast<int>(worldHeight * vt.scale())));
    const QFontMetricsF& fm = metricsFor(f);
    // `lift` is the height above the line the text is drawn at, in world units; the small screen
    // floor keeps a zero-lift marking off the line itself at any zoom.
    const double gap = std::max(2.0, worldHeight * vt.scale() * 0.35) + lift * vt.scale();
    p.save();
    p.setFont(f);
    p.setPen(color);
    p.translate(vt.toScreen(at));
    p.rotate(std::atan2(-dir.y, dir.x) * 180.0 / M_PI);
    p.drawText(QPointF(-fm.horizontalAdvance(text) / 2.0, below ? gap + fm.ascent() : -gap), text);
    p.restore();
}

// "ls" line-start: read the stored reference line, if any.
bool lsReferenceLine(const Entity& e, Vec2& a, Vec2& b) {
    if (!e.meta.value("lsRef").toBool()) return false;
    a = {e.meta.value("lsRefAx").toDouble(), e.meta.value("lsRefAy").toDouble()};
    b = {e.meta.value("lsRefBx").toDouble(), e.meta.value("lsRefBy").toDouble()};
    return true;
}
// Bring a run's START up to the reference line. Where the line actually crosses the run is looked for
// along the WHOLE run, not only in its first segment: a boundary that cuts a run past one of its knees
// is an ordinary thing to trim to, and clipping the first segment alone left the knee sitting there
// beyond the cut - which is why anything further along was refused rather than drawn.
void extendRunStartToLine(std::vector<Vec2>& run, const Vec2& a, const Vec2& b) {
    if (run.size() < 2) return;
    for (int i = static_cast<int>(run.size()) - 1; i >= 1; --i) {
        if (auto x = segmentSegmentIntersection(run[i - 1], run[i], a, b)) {
            run.erase(run.begin(), run.begin() + i);   // everything before the crossing goes
            run.insert(run.begin(), *x);
            return;
        }
    }
    // The line does not cross the run at all: extend the first segment out to meet it, as before.
    if (auto x = lineLineIntersection(run[0], run[1], a, b)) run[0] = *x;
}
// The same at the other end, so a trimmed run meets the cutting line exactly (a diagonal cap).
void extendRunEndToLine(std::vector<Vec2>& run, const Vec2& a, const Vec2& b) {
    const int n = static_cast<int>(run.size());
    if (n < 2) return;
    for (int i = 1; i < n; ++i) {
        if (auto x = segmentSegmentIntersection(run[i - 1], run[i], a, b)) {
            run.resize(static_cast<size_t>(i));        // everything after the crossing goes
            run.push_back(*x);
            return;
        }
    }
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
//
// `side` is which of the two physical runs this is (+1 / -1). A cut may belong to ONE of them: trimming
// a single line of a double run back to a reference leaves the other at its own length, and a run whose
// two lines end in different places cannot be described by a centreline and a gap - so the end that
// moved is recorded as a cap on that side alone. A cut with no side on it belongs to both, which is
// what every cut made before this did.
void applyRunCut(std::vector<Vec2>& run, const Entity& e, const QString& key, bool atEnd,
                 int side = 0) {
    if (!e.meta.value(key).toBool()) return;
    const int onlySide = e.meta.value(key + QStringLiteral("Side")).toInt();
    if (onlySide != 0 && side != 0 && onlySide != side) return;
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

} // namespace

void applyDoubleRunCuts(std::vector<Vec2>& rail, const Entity& e, int side) {
    if (rail.size() < 2) return;
    for (const QString& k : {QStringLiteral("cutStart"), QStringLiteral("cutStart2")})
        applyRunCut(rail, e, k, false, side);
    for (const QString& k : {QStringLiteral("cutEnd"), QStringLiteral("cutEnd2")})
        applyRunCut(rail, e, k, true, side);
}

// The part of a run between two distances along it.
static std::vector<Vec2> railSlice(const std::vector<Vec2>& run, double from, double to) {
    std::vector<Vec2> out;
    if (run.size() < 2 || to - from <= 1e-9) return out;
    double at = 0.0;
    for (size_t i = 1; i < run.size(); ++i) {
        const double segLen = distance(run[i - 1], run[i]);
        if (segLen <= 1e-12) continue;
        const double s0 = at;
        at += segLen;
        const double lo = std::max(from, s0), hi = std::min(to, at);
        if (hi - lo <= 1e-9) continue;
        const Vec2 dir = (run[i] - run[i - 1]).normalized();
        const Vec2 p0 = run[i - 1] + dir * (lo - s0);
        const Vec2 p1 = run[i - 1] + dir * (hi - s0);
        if (out.empty() || distance(out.back(), p0) > 1e-9) out.push_back(p0);
        out.push_back(p1);
    }
    return out;
}

std::vector<std::vector<Vec2>> splitRailAtHoles(const std::vector<Vec2>& rail, const Entity& e,
                                                int side) {
    std::vector<std::vector<Vec2>> pieces;
    if (rail.size() < 2) {
        if (!rail.empty()) pieces.push_back(rail);
        return pieces;
    }
    // A hole is kept as the two points on the line where it starts and ends, in world coordinates, so
    // it follows the object through a move, a turn or a mirror the way the end caps do.
    std::vector<std::pair<double, double>> spans;
    for (const auto& hv : e.meta.value(QStringLiteral("holes")).toArray()) {
        const QJsonArray h = hv.toArray();
        if (h.size() < 5 || h[0].toInt() != side) continue;
        const Vec2 p1{h[1].toDouble(), h[2].toDouble()};
        const Vec2 p2{h[3].toDouble(), h[4].toDouble()};
        double d1 = 0.0, d2 = 0.0, along = 0.0, best1 = 1e299, best2 = 1e299;
        for (size_t i = 1; i < rail.size(); ++i) {
            const auto c1 = closestPointOnSegment(p1, rail[i - 1], rail[i]);
            if (c1.distance < best1) { best1 = c1.distance; d1 = along + distance(rail[i - 1], c1.point); }
            const auto c2 = closestPointOnSegment(p2, rail[i - 1], rail[i]);
            if (c2.distance < best2) { best2 = c2.distance; d2 = along + distance(rail[i - 1], c2.point); }
            along += distance(rail[i - 1], rail[i]);
        }
        if (d2 < d1) std::swap(d1, d2);
        if (d2 - d1 > 1e-9) spans.push_back({d1, d2});
    }
    if (spans.empty()) {
        pieces.push_back(rail);
        return pieces;
    }
    std::sort(spans.begin(), spans.end());
    const double total = polylineLength(rail);
    double at = 0.0;
    for (const auto& s : spans) {
        std::vector<Vec2> piece = railSlice(rail, at, s.first);
        if (piece.size() >= 2) pieces.push_back(std::move(piece));
        at = std::max(at, s.second);
    }
    std::vector<Vec2> tail = railSlice(rail, at, total);
    if (tail.size() >= 2) pieces.push_back(std::move(tail));
    return pieces;
}

Bounds drawnBounds(const Entity& e) {
    Bounds b = e.bounds();
    if (!b.valid) return b; // nothing there to reach out from
    const bool doubleRun = e.type == EntityType::DoubleLine ||
                           (e.type == EntityType::Polyline && e.meta.value("double").toBool());
    if (doubleRun) {
        // The two lines themselves, cap for cap. Built rather than padded because a cap can stretch a
        // line a long way past the end of its own run - to the far side of whatever it was extended to
        // meet - and no fixed pad would contain that.
        const double gap = e.meta.value("gap").toDouble(10.0);
        std::vector<Vec2> centre;
        if (e.type == EntityType::DoubleLine) centre = {e.a, e.b};
        else centre = e.points;
        if (centre.size() >= 2) {
            for (int side : {+1, -1}) {
                std::vector<Vec2> rail;
                if (e.type == EntityType::DoubleLine) {
                    const Vec2 off = (e.b - e.a).normalized().perp() * (side * gap / 2.0);
                    rail = {e.a + off, e.b + off};
                } else {
                    rail = offsetPolyline(centre, side * gap / 2.0);
                }
                Vec2 ra, rb;
                if (lsReferenceLine(e, ra, rb)) extendRunStartToLine(rail, ra, rb);
                applyDoubleRunCuts(rail, e, side);
                for (const Vec2& p : rail) b.expand(p);
            }
            // The lines are taken off the path with its corners sharp, which is the wider of the two:
            // rounding a corner only ever cuts it inwards, so this contains the rounded run as well.
        }
        return b;
    }
    if (e.type == EntityType::Dimension) {
        // The dimension LINE, which is where the arrows are: it runs between the two measured points
        // carried out to the stand-off, and on an aligned one that puts its far end well past the
        // stand-off point itself. Then the text stands off the line again, and the extension lines
        // overshoot it.
        Vec2 d1, d2;
        if (dimensionLineEnds(e, d1, d2)) {
            b.expand(d1);
            b.expand(d2);
        }
        if (e.meta.contains("textPosX"))
            b.expand({e.meta.value("textPosX").toDouble(), e.meta.value("textPosY").toDouble()});
        // The label is centred on its anchor, so it reaches out sideways by half of its own width -
        // which is what a radius dimension's "R35.00" does past the end of its leader. Estimated from
        // the glyph height rather than measured: this is a bound to paint against, and being a little
        // generous with it only means painting an object that was going to be painted anyway.
        const double textH2 = e.meta.value("textHeight").toDouble(5.0);
        const double pad = textH2 * 2.0 + std::abs(e.meta.value("extOffset").toDouble(2.0)) +
                           EntityRenderer::dimensionText(e).size() * textH2 * 0.6;
        b.expand({b.min.x - pad, b.min.y - pad});
        b.expand({b.max.x + pad, b.max.y + pad});
        return b;
    }
    if (e.type == EntityType::Wire) {
        // Markings sit above and below the conductor, and a bridge hop arcs over it.
        const double pad = std::max(6.0, e.meta.value("markingHeight").toDouble(2.5) * 3.0);
        b.expand({b.min.x - pad, b.min.y - pad});
        b.expand({b.max.x + pad, b.max.y + pad});
        return b;
    }
    return b;
}

namespace {

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
    double gap = std::max(3.0, 6.0 * scale * vt.scale()); // world ~6 units * scale
    // A spacing of six world units means nothing to a fill that is under one unit across. On an imported
    // drawing whose fills are a fraction of a millimetre wide, every ruled hatch came out as a SINGLE
    // stroke through the middle - a stray line sitting where a fill should be, with nothing to say it was
    // meant to be hatching. The spacing is capped so that any fill big enough to see gets ruled rather
    // than crossed once.
    {
        const QRectF pbb = path.boundingRect();
        const double across = std::min(pbb.width(), pbb.height());
        if (across > 8.0) gap = std::min(gap, across / 4.0);
    }

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
    // Written in world units, DRAWN at the annotation scale: a dimension on a drawing plotted at 1:10 is
    // written 5 mm high and drawn 50, so that it comes off the paper 5 mm high. The geometry it measures is
    // untouched - only the writing about it grows.
    const double annot = EntityRenderer::annotationScale();
    const double textH = e.meta.value("textHeight").toDouble(5.0) * annot;   // world units
    const double arrowW = e.meta.value("arrowSize").toDouble(3.3) * annot;   // world units
    const double arrowPx = std::clamp(arrowW * vt.scale(), 5.0, 40.0);
    const double extOff = e.meta.value("extOffset").toDouble(2.0) * annot; // ext-line gap (world)
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
    // What the dimension says is worked out in one place, EntityRenderer::dimensionText - the
    // measurement, or the text override in place of it - so what is drawn and what anything else
    // reports are the same words.
    const QString says = EntityRenderer::dimensionText(e);
    auto label = [&](const QPointF& at) {
        drawWorldText(p, at + QPointF(0, -4), says, textH, vt.scale(), color, Qt::AlignHCenter);
    };

    if (ty == QLatin1String("radius") || ty == QLatin1String("jogged")) {
        const QPointF c = vt.toScreen(e.center);
        const QPointF tp = vt.toScreen(e.b);
        const QPointF dir = unit(tp - c);
        const QPointF on = c + dir * (e.radius * vt.scale());
        p.drawLine(c, tp);
        drawArrowHead(p, on, dir, color, arrowPx); // tip on the ring, pointing outward
        label(tp);
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
        label(tp);
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
        const double am = a0 + (a1 - a0) * 0.5;
        label(vt.toScreen(pointOnCircle(v, rr, am)));
        return;
    }
    if (ty == QLatin1String("ordinate")) {
        const QPointF f = vt.toScreen(e.a), le = vt.toScreen(e.b);
        p.drawLine(f, le);
        label(le);
        return;
    }

    // ---- linear / aligned / arclength: extension lines + dim line + arrows ----
    const Vec2 p1 = e.a, p2 = e.b;
    Vec2 d1, d2;
    if (!dimensionLineEnds(e, d1, d2)) {
        d1 = p1;
        d2 = p2;
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
    const QString txt = says;
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

// The world x-range spanned by a Twist symbol's band (leads + crossings). A wire the
// twist references must not draw its own straight segment through this range — the
// twist symbol takes over there — so the wire's actual structure stays intact (no
// separate mask painted over it).
std::pair<double, double> twistBandX(const Entity& twist) {
    double cx = 0.0, yMin = 1e18, yMax = -1e18;
    for (const Vec2& pt : twist.points) {
        cx += pt.x;
        yMin = std::min(yMin, pt.y);
        yMax = std::max(yMax, pt.y);
    }
    if (!twist.points.empty()) cx /= twist.points.size();
    const double band = std::max(1.0, yMax - yMin);
    // Width grows with the number of crossings (repeats) and the flat length between them,
    // so the braid isn't cramped when more twists / longer flats are requested.
    const int repeats = std::max(1, twist.meta.value("repeats").toInt(2));
    const double flat = twist.meta.value("flatLen").toDouble(6.0);
    const double w = twistBandWidth(repeats, band, flat);
    return {cx - w / 2.0, cx + w / 2.0};
}

// Every Twist band that references `wireId` (via its "wires" meta), so the wire's own
// render can skip drawing through those x-ranges.
std::vector<std::pair<double, double>> twistBandsForWire(const QString& wireId,
                                                          const Document& doc) {
    std::vector<std::pair<double, double>> bands;
    for (const auto& t : doc.entities()) {
        if (t.type != EntityType::Twist) continue;
        // "crossed" just overlays a line across the wires — the wires stay continuous.
        if (t.meta.value("twistType").toString() == QLatin1String("crossed")) continue;
        const QJsonArray ids = t.meta.value("wires").toArray();
        bool refs = false;
        for (const auto& v : ids)
            if (v.toString() == wireId) { refs = true; break; }
        if (refs) bands.push_back(twistBandX(t));
    }
    return bands;
}

// Split a polyline into sub-polylines with the given world-x ranges removed (assumes
// near-horizontal segments, matching the Twist symbol's own horizontal-run geometry).
// Fragments shorter than 2 points are dropped.
std::vector<std::vector<Vec2>> clipPolylineByXBands(
    const std::vector<Vec2>& pts, const std::vector<std::pair<double, double>>& bands) {
    if (bands.empty() || pts.size() < 2) return {pts};
    std::vector<std::vector<Vec2>> result;
    std::vector<Vec2> cur = {pts[0]};
    auto flush = [&] {
        if (cur.size() >= 2) result.push_back(cur);
        cur.clear();
    };
    for (size_t i = 1; i < pts.size(); ++i) {
        const Vec2 a = pts[i - 1], b = pts[i];
        // The twist takes over a stretch of the wire's HORIZONTAL run. A leg that climbs
        // through the same x-range is not what the symbol replaces, so it is drawn whole -
        // otherwise a wire that turns a corner near a twist loses the whole leg.
        if (std::abs(b.y - a.y) > std::abs(b.x - a.x)) {
            cur.push_back(b);
            continue;
        }
        const double xLo = std::min(a.x, b.x), xHi = std::max(a.x, b.x);
        std::vector<std::pair<double, double>> cuts;
        for (const auto& bd : bands) {
            const double c0 = std::max(xLo, bd.first), c1 = std::min(xHi, bd.second);
            if (c0 < c1) cuts.push_back({c0, c1});
        }
        if (cuts.empty()) {
            cur.push_back(b);
            continue;
        }
        std::sort(cuts.begin(), cuts.end());
        const bool reversed = a.x > b.x;
        if (reversed) std::reverse(cuts.begin(), cuts.end());
        auto atX = [&](double x) -> Vec2 {
            const double t = (b.x != a.x) ? (x - a.x) / (b.x - a.x) : 0.0;
            return {x, a.y + (b.y - a.y) * t};
        };
        for (const auto& cut : cuts) {
            const double startX = reversed ? cut.second : cut.first;
            const double endX = reversed ? cut.first : cut.second;
            cur.push_back(atX(startX));
            flush();
            cur = {atX(endX)};
        }
        cur.push_back(b);
    }
    flush();
    return result;
}

} // namespace

QString EntityRenderer::dimensionText(const Entity& e) {
    const QString ty = e.meta.value("dimType").toString(QStringLiteral("linear"));
    const int dec = e.meta.contains("decimals") ? e.meta.value("decimals").toInt() : g_dimDecimals;
    QString measured;
    if (ty == QLatin1String("radius") || ty == QLatin1String("jogged")) {
        measured = QStringLiteral("R%1").arg(formatLength(e.radius, dec));
    } else if (ty == QLatin1String("diameter")) {
        measured = QString::fromUtf8("\xC3\x98%1").arg(formatLength(e.radius * 2.0, dec));
    } else if (ty == QLatin1String("angular") && e.points.size() >= 3) {
        const Vec2 v = e.points[0], a = e.points[1], b = e.points[2];
        double deg = (std::atan2(b.y - v.y, b.x - v.x) - std::atan2(a.y - v.y, a.x - v.x)) * 180.0 /
                     M_PI;
        while (deg < 0) deg += 360.0;
        while (deg > 360) deg -= 360.0;
        measured = QStringLiteral("%1\xC2\xB0").arg(deg, 0, 'f', 1);
    } else if (ty == QLatin1String("ordinate")) {
        const bool xOrd = std::abs(e.b.x - e.a.x) >= std::abs(e.b.y - e.a.y);
        measured = formatLength(xOrd ? e.a.x : e.a.y, dec);
    } else {
        // linear / aligned / arclength: a linear one measures along whichever axis its line stands off
        // on, the others measure between the points themselves.
        double measure = 0.0;
        if (ty == QLatin1String("linear")) {
            measure = dimensionAcrossX(e) ? std::abs(e.b.x - e.a.x) : std::abs(e.b.y - e.a.y);
        } else {
            measure = distance(e.a, e.b);
        }
        measured = ty == QLatin1String("arclength")
                       ? QString::fromUtf8("\xE2\x8C\x92%1").arg(formatLength(measure, dec))
                       : formatLength(measure, dec);
    }
    const QString overrideText = e.meta.value("textOverride").toString();
    if (overrideText.isEmpty()) return measured;
    QString said = overrideText;
    return said.replace(QLatin1String("<>"), measured);
}

void EntityRenderer::draw(QPainter& p, const Entity& e, const Document& doc,
                          const ViewTransform& vt, const QColor& overrideColor) {
    // The line a part is attached to the harness by shows ORANGE WHILE THE PART IS SELECTED, and is an
    // ordinary line the rest of the time. It is a note about the part, not a feature of the drawing:
    // orange at all times would put a coloured line through every connector on a finished sheet, and
    // nowhere is it more wanted than when the connector it belongs to has just been picked.
    const bool connLine = e.meta.value(QLatin1String(kConnectionLineKey)).toBool();
    QColor color = g_monochrome
                       ? QColor(Qt::black)
                       : (connLine && overrideColor.isValid()
                              ? QColor("#ff9f40")
                              : (overrideColor.isValid() ? overrideColor : resolveColor(e, doc)));
    if (g_whitePaper && color == QColor(Qt::white)) color = QColor(Qt::black);
    QPen pen(color);
    pen.setCosmetic(true); // width is set in screen px; for a wire it is derived from the zoom
    const double px = penPixels(e.type == EntityType::Wire ? wireDrawWidth(e, doc)
                                                           : effectiveWeight(e, doc));
    pen.setWidthF(overrideColor.isValid() ? px + 1.4 : px);
    // A hairline straight edge is drawn crisp rather than antialiased, so it comes out white instead of
    // grey. Restored the moment this entity is done with.
    const HairlineCrisp crisp(p, px <= 1.25 && !overrideColor.isValid() && straightEdged(e.type));
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    // Dashed, when the object says so. Qt measures a dash pattern in units of the PEN WIDTH, so a raw
    // {6,4} on a hairline collapses to nothing - it is normalised by the width, which makes the dash
    // the same length on screen whatever the object is drawn with. Only the shield used to honour
    // this; a screen drawn as plain lines, a construction line, a boundary all want it too.
    if (e.meta.value("lineType").toString(QStringLiteral("solid")) == QLatin1String("dashed")) {
        const double ds = std::max(0.1, e.meta.value("dashScale").toDouble(1.0));
        const double we = pen.widthF() > 0.0 ? pen.widthF() : 1.0;
        pen.setDashPattern({6.0 * ds / we, 4.0 * ds / we});
        pen.setCapStyle(Qt::FlatCap);   // a round cap on a short dash fills the gap back in
    } else if (const QVector<qreal> dashes =
                   linetypeDashes(e.meta.value("linetype").toString(),
                                  e.meta.value("ltscale").toDouble(1.0), pen.widthF());
               !dashes.isEmpty()) {
        // A NAMED linetype, which is what the Linetype property sets and what a centre mark and a centre
        // line are drawn with. Nothing read it: the renderer honoured only meta "lineType" (a capital T,
        // and the single value "dashed"), while the property writes meta "linetype" with a name like
        // CENTER. Two keys a letter apart, so choosing a linetype in Properties had never once changed
        // what was drawn, and a centre mark came out as two solid strokes.
        pen.setDashPattern(dashes);
        pen.setCapStyle(Qt::FlatCap);
    }
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
        for (const QString& k : {QStringLiteral("cutStart"), QStringLiteral("cutStart2")}) {
            applyRunCut(run1, e, k, false, +1);
            applyRunCut(run2, e, k, false, -1);
        }
        for (const QString& k : {QStringLiteral("cutEnd"), QStringLiteral("cutEnd2")}) {
            applyRunCut(run1, e, k, true, +1);
            applyRunCut(run2, e, k, true, -1);
        }
        // Each line in the pieces it survives in: a trim that took a bite out of the middle of one of
        // them is kept as a hole, and the line is drawn either side of it.
        for (int side : {+1, -1})
            for (const auto& piece : splitRailAtHoles(side > 0 ? run1 : run2, e, side))
                p.drawPolyline(toScreenPoly(piece, vt));
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
                for (const QString& k : {QStringLiteral("cutStart"), QStringLiteral("cutStart2")}) {
                    applyRunCut(run1, e, k, false, +1);
                    applyRunCut(run2, e, k, false, -1);
                }
                for (const QString& k : {QStringLiteral("cutEnd"), QStringLiteral("cutEnd2")}) {
                    applyRunCut(run1, e, k, true, +1);
                    applyRunCut(run2, e, k, true, -1);
                }
                for (int side : {+1, -1})
                    for (const auto& piece : splitRailAtHoles(side > 0 ? run1 : run2, e, side))
                        p.drawPolyline(toScreenPoly(piece, vt));
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
        // A Twisted-pair symbol (Twist entity) that references this wire takes over
        // drawing within its band — split the wire's own points around that x-range so
        // the wire's actual structure simply doesn't run through it (nothing to mask).
        const auto bands = twistBandsForWire(e.id, doc);
        const auto polys = clipPolylineByXBands(e.points, bands);
        for (const auto& poly : polys) {
            if (poly.size() < 2) continue;
            if (hops.empty()) {
                p.drawPolyline(toScreenPoly(poly, vt));
                continue;
            }
            const double r = 5.0 * vt.scale(); // hop radius (≈5 world units)
            QPainterPath path;
            path.moveTo(vt.toScreen(poly[0]));
            for (size_t s = 1; s < poly.size(); ++s) {
                const QPointF a0 = vt.toScreen(poly[s - 1]);
                const QPointF a1 = vt.toScreen(poly[s]);
                const QPointF d = a1 - a0;
                const double L = std::hypot(d.x(), d.y());
                if (L < 1e-6) { path.lineTo(a1); continue; }
                const QPointF dir(d.x() / L, d.y() / L);
                QPointF perp(-dir.y(), dir.x());
                // The hop always arcs UPWARD on the sheet (screen y grows downward, so the
                // bulge side is the one with negative y). Taking the segment's own
                // perpendicular made the arc flip with the direction the wire happened to be
                // drawn in, so some bridges bulged down and read as a dip rather than a hop.
                if (perp.y() > 0.0) perp = -perp;
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
        // Wire markings: the colour code / name and the cross-section, each switched on
        // independently and repeated along the run. They read along the wire (colour
        // above it, gauge below) and sit wherever their grip was dragged to.
        // The three markings each read on their OWN line, stacked above the run: the label
        // topmost, then the colour, then the cross-section. One line holding "RD 0.5 mm2" made
        // them impossible to tell apart or to point at; a line each can be read - and clicked -
        // on its own.
        {
            // Three separate markings: each at its OWN places along the run, as many times as it
            // is set to, at its OWN height above it and in its OWN text height. Nothing is stacked
            // relative to anything else, so a height set here is the height it is drawn at.
            const int tone[] = {130, 150, 160};
            int which = 0;
            for (const QString& part : wireAnnotParts()) {
                const QString text = wireAnnotPartText(e, part);
                const int lighten = tone[std::min(which++, 2)];
                if (text.isEmpty()) continue;
                const double h = wireAnnotSizeFor(e, part);
                for (const WireAnnot& a : wireAnnotsFor(e, part))
                    drawTextAlongWire(p, vt, a.pos, a.dir, text, h, color.lighter(lighten),
                                      /*below=*/a.flip, /*lift=*/wireAnnotGapFor(e, part));
            }
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
                const std::vector<TableMerge> merges = tableMerges(e);
                const int vTop = hasTitle ? 1 : 0; // verticals skip the merged title row
                if (merges.empty()) {
                    // The whole way across and the whole way down: two lines per boundary rather than one
                    // per cell edge, which on a thirty-four row table is 38 lines instead of 400.
                    for (int r = 1; r < rows; ++r)
                        p.drawLine(vt.toScreen({tb.min.x, yb[r]}), vt.toScreen({tb.max.x, yb[r]}));
                    for (int c = 1; c < cols; ++c)
                        p.drawLine(vt.toScreen({xb[c], yb[vTop]}), vt.toScreen({xb[c], yb[rows]}));
                } else {
                    // With cells joined, a boundary is drawn a cell edge at a time, and the edges INSIDE
                    // a joined block are left out - that absence is what makes the block read as one cell.
                    auto joined = [&merges](int r, int c, int r2, int c2) {
                        for (const TableMerge& m : merges)
                            if (m.covers(r, c) && m.covers(r2, c2)) return true;
                        return false;
                    };
                    for (int r = 1; r < rows; ++r)
                        for (int c = 0; c < cols; ++c) {
                            if (joined(r - 1, c, r, c)) continue;   // same block above and below
                            p.drawLine(vt.toScreen({xb[c], yb[r]}), vt.toScreen({xb[c + 1], yb[r]}));
                        }
                    for (int c = 1; c < cols; ++c)
                        for (int r = vTop; r < rows; ++r) {
                            if (joined(r, c - 1, r, c)) continue;   // same block left and right
                            p.drawLine(vt.toScreen({xb[c], yb[r]}), vt.toScreen({xb[c], yb[r + 1]}));
                        }
                }
                const int headerIdx = hasTitle ? 1 : 0;
                const QFont normal = p.font();
                const QJsonArray cellAlign = e.meta.value("cellAlign").toArray();
                const QJsonArray cellText = e.meta.value("cellTextSize").toArray();
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
                    // Only the FONT changes from cell to cell, so only the font is put back. A full
                    // painter save and restore per cell hands back the pen, the brush, the clip and the
                    // transform as well - two hundred and four times over on a bill of materials, which
                    // was most of what a table cost to draw.
                    QFont f = normal; f.setBold(bold);
                    const QFont keep = p.font();
                    useFont(p, f);
                    // A cell narrower than its words WRAPS, and the row was made tall enough to hold the
                    // lines when the column was narrowed. Written a line at a time down from the top of the
                    // block the cell was given, so the words stay inside the cell instead of running out
                    // through the column beside it.
                    const QStringList lines = tableWrapText(s, std::max(0.0, x1 - x0 - 2 * pad), h);
                    const double pitch = h * 1.35;
                    double ly = y;
                    if (lines.size() > 1 && v == 'M') ly = yTop - pad - h;   // a wrapped cell reads down
                    for (const QString& ln : lines) {
                        drawWorldText(p, vt.toScreen({x, ly}), ln, h, vt.scale(), color,
                                      lines.size() > 1 ? (flags & ~Qt::AlignVCenter) : flags);
                        ly -= pitch;
                    }
                    useFont(p, keep);
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
                        // A cell may carry its own text height; the table's is the fallback.
                        const double own = idx < cellText.size() ? cellText[idx].toDouble() : 0.0;
                        // A joined block writes its words ONCE, across the whole of it, from the cell it
                        // starts at. The cells it covers write nothing - they are not cells any more.
                        double x1 = xb[c + 1], yBot = yb[r + 1];
                        if (TableMerge m; tableMergeAt(e, r, c, m)) {
                            if (!m.isAnchor(r, c)) continue;
                            x1 = xb[std::min(cols, m.col + m.colSpan)];
                            yBot = yb[std::min(rows, m.row + m.rowSpan)];
                        }
                        placeCell(xb[c], x1, yb[r], yBot, s, code,
                                  own > 0.0 ? own : th, isHeader);
                    }
                }
                break;
            }
            // A filled shape is a patch of colour rather than an outline of one: a DXF SOLID, and the
            // head a leader or a dimension points with. Outlined only, an arrowhead read as a hollow
            // triangle resting on the wire it picks out.
            if (e.meta.value("filled").toBool() && e.points.size() >= 3) {
                p.save();
                p.setBrush(color);
                p.drawPolygon(toScreenPoly(e.points, vt));
                p.restore();
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
                const QFontMetricsF& fm = metricsFor(f);
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
        // A faint boundary so an empty/low-density hatch is still selectable - and, for one read out of
        // a file, so that it reads as a FILLED SHAPE at all.
        //
        // A hatch does not normally draw its own edge, because the edge is usually a real object sitting
        // in the same place. A hatch imported from a DXF may have no such object: its boundary was
        // "derived" from geometry that need not be in the file. Drawn without it, a round fill came out as
        // a handful of loose parallel strokes floating in space - a circle with no circle. Where the edge
        // object does exist, this draws exactly on top of it and costs nothing.
        if (overrideColor.isValid() || e.meta.value("drawBoundary").toBool()) {
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        break;
    }

    case EntityType::Wipeout: {
        if (e.points.size() < 3) break;
        const QPolygonF poly = toScreenPoly(e.points, vt);
        // The mask fills to a pixel INSIDE its boundary. A mask is normally drawn with an outline on
        // that very boundary - the sleeve round a cable label is a rectangle with the mask that hides
        // the braid exactly under it - and a fill that reached the boundary ate the outline with it,
        // leaving the cable looking cut rather than labelled.
        QPolygonF fill = poly;
        {
            const QRectF box = poly.boundingRect();
            const double w = box.width(), h = box.height();
            const double sx = w > 4.0 ? (w - 2.0) / w : 1.0;
            const double sy = h > 4.0 ? (h - 2.0) / h : 1.0;
            const QPointF c = box.center();
            for (QPointF& q : fill)
                q = QPointF(c.x() + (q.x() - c.x()) * sx, c.y() + (q.y() - c.y()) * sy);
        }
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(g_bgColor); // opaque mask matching the canvas background
        p.drawPolygon(fill);
        p.restore();
        // Thin frame so the (otherwise invisible) mask can be seen and picked. A mask read from a
        // drawing has an outline of its own where it wants one - the sleeve round a cable label is a
        // polyline over the mask that hides the braid - so framing it as well drew that rectangle
        // twice. Such a mask is framed only while it is selected, so it can still be found.
        if (e.meta.value("noFrame").toBool() && !overrideColor.isValid()) break;
        QPen frame(overrideColor.isValid() || g_monochrome ? color : QColor(120, 130, 145));
        frame.setCosmetic(true);
        frame.setWidthF(overrideColor.isValid() ? 2.4 : 1.0);
        p.setPen(frame);
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(poly);
        break;
    }

    case EntityType::Image: {
        if (e.points.size() < 4) break;
        const QPixmap pm = picturePixmap(e);
        const Bounds b = boundsOf(e.points);
        // The rectangle the picture fills, in screen terms. World y is up and screen y is down, so the
        // world TOP-left corner is the screen top-left one.
        const QPointF tl = vt.toScreen({b.min.x, b.max.y});
        const QPointF br = vt.toScreen({b.max.x, b.min.y});
        const QRectF target(tl, br);
        if (pm.isNull()) {
            // A picture whose bytes did not survive: say so on the sheet rather than leaving a blank,
            // so it is obvious what is missing and where it was.
            p.save();
            QPen pen(color);
            pen.setCosmetic(true);
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(target);
            p.drawLine(target.topLeft(), target.bottomRight());
            p.drawLine(target.topRight(), target.bottomLeft());
            p.restore();
            break;
        }
        p.save();
        const double opacity =
            std::clamp(e.meta.value(QLatin1String(PictureKeys::kOpacity)).toDouble(100.0), 0.0, 100.0);
        if (opacity < 100.0) p.setOpacity(opacity / 100.0);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // Rotated by a grip or by the Rotate tool: turn the painter about the middle and draw it
        // straight, so a rotated picture is not sheared.
        if (std::abs(e.rotation) > 1e-9) {
            p.translate(target.center());
            p.rotate(-e.rotation * 180.0 / M_PI);   // screen y is down, so the sign flips
            p.translate(-target.center());
        }
        p.drawPixmap(target, pm, QRectF(pm.rect()));
        p.restore();
        // A selected picture gets its outline drawn, so its corner grips have something to sit on and
        // an all-white picture on a white sheet can still be seen and grabbed.
        if (overrideColor.isValid()) {
            QPen pen(color);
            pen.setCosmetic(true);
            pen.setWidthF(2.0);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(toScreenPoly(e.points, vt));
        }
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
            // Stepped by how big the curve is ON SCREEN, not by a fixed angle. A fixed three degrees is
            // nothing on a small ellipse and a straight run of a dozen pixels on a large one zoomed
            // into - which is what left a "stub" of straight line at the end of a trimmed ellipse,
            // sticking out where the curve should have turned away.
            const double onScreen = sweep * std::max(e.radius, e.radiusY) * vt.scale();
            const int steps =
                std::clamp(static_cast<int>(std::ceil(onScreen / 3.0)), 16, 4000);
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
        // Nothing legible at this size: stop before the font work, the wrapping and the per-line
        // measuring, all of which happen before a single glyph is put down.
        if (heightWorld * vt.scale() < 2.0) break;
        const QPointF anchor = vt.toScreen(e.a);
        QFont f = p.font();
        const QString family = e.meta.value("font").toString();
        if (!family.isEmpty()) f.setFamily(family);
        f.setBold(e.meta.value("bold").toBool());
        f.setItalic(e.meta.value("italic").toBool());
        f.setUnderline(e.meta.value("underline").toBool());
        f.setStrikeOut(e.meta.value("strike").toBool());
        f.setPixelSize(std::max(1, static_cast<int>(heightWorld * vt.scale())));
        useFont(p, f);
        p.setPen(color);
        const QFontMetricsF& fm = metricsFor(f);
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

        // Bullets / numbering: prefix each paragraph; then word-wrap to the box. Worked out once per
        // distinct string and remembered, because the answer is the same on every redraw.
        const QStringList lines = wrappedLines(fm, text, boxWpx, e.meta.value("list").toString());

        // Vertical placement within the container (screen Y grows downward; anchor
        // is the box top). T = top, M = centre, B = bottom of the box.
        //
        // With NO box - which is how a cell's text, a balloon's number and a pin's number are written,
        // and how most of a real drawing's text arrives - the anchor is the point the block is placed
        // ABOUT, so M has to lift it by half its height and B by all of it. Treating the block as its
        // own box made both of them do nothing, which is why every such label sat low: the anchor was
        // being used as the top whatever it said. Horizontal placement already worked this way (a
        // width of zero makes C shift left by half the line).
        // Text whose ANCHOR is its justification point (everything read from a DXF) is placed about
        // that point; the box, if any, only says where the lines wrap.
        const bool anchorJust = e.meta.value("anchorJustify").toBool();
        const double totalH = lineH * lines.size();
        const double availH = (boxHpx > 1.0 && !anchorJust) ? boxHpx : 0.0;

        // Text can be written at an ANGLE, and this drew every one of it flat. A drawing states the
        // angle (group 50, or the direction vector the text runs along, which is how the sheets that
        // brought this to light state it) and the reader was picking it up correctly all along - it
        // just never reached the paper. A column of labels turned on their side therefore came out
        // horizontal, every one of them landing on the same line and printing over its neighbours.
        //
        // Turned about the ANCHOR, so the justification below still means what it says: the layout is
        // worked out from the origin of a turned frame rather than from a point on the sheet.
        const bool turned = std::abs(e.rotation) > 1e-9;
        if (turned) {
            p.save();
            p.translate(anchor);
            // The drawing turns anticlockwise and the screen's y grows downward, so the sign goes round.
            p.rotate(-e.rotation * 180.0 / M_PI);
        }
        const QPointF at = turned ? QPointF(0.0, 0.0) : anchor;

        double blockTop = at.y();
        if (vJust == QLatin1Char('M')) blockTop += (availH - totalH) / 2.0;
        else if (vJust == QLatin1Char('B')) blockTop += availH - totalH;
        double y = blockTop + fm.ascent();
        for (const QString& ln : lines) {
            const double lw = fm.horizontalAdvance(ln);
            const double avail = (boxWpx > 1.0 && !anchorJust) ? boxWpx : 0.0;
            double x = at.x();
            if (hJust == QLatin1Char('C')) x += (avail - lw) / 2.0;
            else if (hJust == QLatin1Char('R')) x += avail - lw;
            p.drawText(QPointF(x, y), ln);
            y += lineH;
        }
        if (turned) p.restore();
        break;
    }

    case EntityType::Dimension:
        drawDimensionEntity(p, e, vt, color, g_dimDecimals);
        break;

    case EntityType::DetailView: {
        // THE SMALL THING, DRAWN AGAIN BIGGER. Two circles: one round the thing where it actually is, one
        // somewhere there is room for it, with whatever is inside the first drawn inside the second at a
        // magnification.
        //
        // It is a VIEW and not a copy. What goes in the big circle is read out of the document every time it
        // is drawn, so changing the thing changes its detail. A detail that is a copy of the geometry goes
        // stale the first time somebody edits what it was a copy of, and a stale detail on a released sheet
        // is worse than no detail at all.
        if (e.radius <= 1e-9) break;
        const double scale = std::max(0.01, e.meta.value("detailScale").toDouble(2.0));
        const QString tag = e.meta.value("detailTag").toString(QStringLiteral("A"));
        const double big = e.radius * scale;
        const double capH = std::max(1e-9, e.meta.value("detailTextSize").toDouble(3.5));
        const double vs = vt.scale();
        // BOTH CIRCLES SAY "Detail A". The letter alone beside the small circle is the terse convention, but a
        // bare A on a sheet is a thing to be worked out - it could be a note, a zone, a revision - and the two
        // circles reading the same is what ties them together at a glance. Either can be typed over.
        QString smallSays = e.meta.value("detailTagText").toString();
        if (smallSays.isEmpty()) smallSays = QStringLiteral("Detail %1").arg(tag);
        // The font, so a drawing office's details read like the rest of its sheet.
        const QString face = e.meta.value("detailFont").toString();
        QFont saved = p.font();
        if (!face.isEmpty()) {
            QFont f = p.font();
            f.setFamily(face);
            p.setFont(f);
        }

        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // The circle round the thing itself, and its letter beside it. It is an annotation over the drawing
        // rather than part of what is drawn, so it goes on at the pen it was given and no heavier.
        const QPointF sc = vt.toScreen(e.center);
        const double sr = e.radius * vs;
        p.drawEllipse(sc, sr, sr);
        drawWorldText(p, vt.toScreen(detailViewTextAt(e, false)), smallSays, capH, vs, color,
                      Qt::AlignHCenter | Qt::AlignVCenter);

        // The enlargement. Its OUTLINE is a switch: the ring is the convention and says where the view stops,
        // but a detail drawn into a clear part of the sheet reads perfectly well without one, and on a busy
        // sheet the ring is one more circle to be mistaken for geometry.
        const QPointF bc = vt.toScreen(e.b);
        const double br = big * vs;
        if (e.meta.value("detailBoundary").toBool(true)) p.drawEllipse(bc, br, br);
        if (!g_inDetailView && br > 3.0) {
            // NOT RECURSIVE. A detail view drawn inside a detail view would draw itself for ever, and a
            // detail of a detail is not something a drawing office asks for.
            g_inDetailView = true;
            p.save();
            QPainterPath clip;
            clip.addEllipse(bc, br, br);
            p.setClipPath(clip, Qt::IntersectClip);
            // The view is made by moving the VIEW, not the geometry: scale up, and shift so the source
            // centre lands on the target centre. Everything then comes out enlarged exactly as it would if
            // the sheet itself were zoomed in - text heights, line weights, hatch spacing and all - which is
            // what a detail view is, and is not what scaling a pile of copied geometry gives you.
            //
            // toScreen is (x*s + view.x, -y*s + view.y), so with s' = s*scale:
            //   view.x' = b.x*s + view.x - centre.x*s'      view.y' = -b.y*s + view.y + centre.y*s'
            ViewTransform inner = vt;
            inner.view.scale = vs * scale;
            inner.view.x = e.b.x * vs + vt.view.x - e.center.x * inner.view.scale;
            inner.view.y = -e.b.y * vs + vt.view.y + e.center.y * inner.view.scale;
            // WHETHER THE DIMENSIONS COME WITH IT. A detail is usually drawn to show a shape, and the
            // dimensions on the small version are sized for the small version - blown up they are the
            // biggest thing in the circle and they cover the shape they were measuring. But a detail drawn
            // BECAUSE the dimensions would not fit at the small size needs them, and needs them enlarged.
            // Both are ordinary, so it is a switch and not a rule.
            // OFF unless it is asked for. A detail is drawn to show a SHAPE, and dimensions sized for the
            // small version become the biggest thing in the circle and cover the very thing they were
            // measuring. The drawing that wants them is the exception, so it is the one that asks.
            const bool withDims = e.meta.value("detailShowDims").toBool(false);
            // ANNOTATIVE OR NOT decides WHERE THE VIEW IS READ FROM, which is the whole difference between
            // them.
            //
            // Annotative, it is read out of the DOCUMENT every time it is drawn: the enlargement is a live
            // window, so anything added at the small circle appears in the large one and anything drawn in the
            // large one - which the cursor reaches through to the real geometry - appears in the small. The two
            // are the same thing seen twice.
            //
            // Not annotative, it is read from the SNAPSHOT taken when it was placed. The two stop passing
            // anything to each other: draw something new by the small circle and the enlargement does not
            // change, draw something over the enlargement and it stays there and only there. Which is what an
            // ordinary detail view is - a picture of how the thing was - and is why the switch exists.
            const bool live = e.meta.value("detailAnnotative").toBool(false);
            std::vector<Entity> snap;
            if (!live) {
                const QJsonArray arr = e.meta.value("detailSnap").toArray();
                // Entity::fromJson is STATIC and RETURNS the entity. Called as though it were a setter it
                // built one, threw it away, and pushed a DEFAULT entity instead - so the copy an ordinary
                // detail view had faithfully taken came back as a pile of blank objects with no geometry, and
                // the enlargement drew nothing at all.
                for (const auto& v : arr) snap.push_back(Entity::fromJson(v.toObject()));
            }
            // A view with no picture yet - one made before the picture was a thing, or one whose copy was
            // lost - falls back to the drawing. An empty circle is not "not annotative", it is broken.
            const std::vector<Entity>& shown = (live || snap.empty()) ? doc.entities() : snap;
            for (const Entity& x : shown) {
                if (x.id == e.id) continue;                       // never itself
                if (x.type == EntityType::DetailView) continue;   // nor any other detail's circles
                if (!withDims && (x.type == EntityType::Dimension || x.type == EntityType::Leader ||
                                  x.type == EntityType::Text || x.type == EntityType::NetLabel))
                    continue;
                if (const Layer* ly = doc.findLayer(x.layer); ly && !ly->visible) continue;
                // Only what the source circle can reach: its box against the circle's, which is cheap and
                // leaves the exact edge to the clip.
                const Bounds xb = x.bounds();
                if (xb.valid && (xb.max.x < e.center.x - e.radius || xb.min.x > e.center.x + e.radius ||
                                 xb.max.y < e.center.y - e.radius || xb.min.y > e.center.y + e.radius))
                    continue;
                EntityRenderer::draw(p, x, doc, inner, overrideColor);
            }
            p.restore();
            g_inDetailView = false;
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
        }

        // The caption, which is what makes the two circles one drawing instead of two. Two separate texts:
        // the letter goes on the circle round the thing, and this goes on the enlargement, and they are set
        // apart because they say different things - one is a pointer, the other is a heading. It defaults to
        // "DETAIL <letter>", which is the convention, and can be anything an office writes instead.
        QString head = e.meta.value("detailBigText").toString();
        if (head.isEmpty()) head = QStringLiteral("Detail %1").arg(tag);
        const QString sub = scale >= 1.0 ? QStringLiteral("SCALE %1:1").arg(scale, 0, 'g', 3)
                                         : QStringLiteral("SCALE 1:%1").arg(1.0 / scale, 0, 'g', 3);
        // Round the enlargement the same way, so a caption that lands on something can be moved off it.
        // Straight down is the convention, so that is where it starts.
        const QPointF capAt = vt.toScreen(detailViewTextAt(e, true));
        drawWorldText(p, capAt, head, capH, vs, color, Qt::AlignHCenter | Qt::AlignVCenter);
        drawWorldText(p, QPointF(capAt.x(), capAt.y() + capH * vs * 1.25), sub, capH * 0.8, vs, color,
                      Qt::AlignHCenter | Qt::AlignVCenter);
        p.setFont(saved);
        break;
    }

    case EntityType::Leader: {
        // The line, the head on the end it points WITH, the flat landing the words sit on, and the words
        // themselves - drawn together because they are one object. points[0] is the tip: what is pointed at.
        if (e.points.size() < 2) break;
        // The landing runs from the last knee, away from where the leader came in, so the words sit at the
        // far end of it and read away from what is being pointed at.
        const double landing = e.meta.value("landing").toDouble(0.0);
        Vec2 wordsAt = e.points.back();
        if (landing > 1e-9 && e.points.size() >= 2) {
            const Vec2 from = e.points[e.points.size() - 2];
            const Vec2 end = e.points.back();
            const double dir = (end.x >= from.x) ? 1.0 : -1.0;
            wordsAt = {end.x + dir * landing, end.y};
            p.drawLine(vt.toScreen(end), vt.toScreen(wordsAt));
        }
        p.drawPolyline(toScreenPoly(e.points, vt));
        const QPointF tip = vt.toScreen(e.points[0]);
        const QPointF from = vt.toScreen(e.points[1]);
        const double dx = tip.x() - from.x(), dy = tip.y() - from.y();
        const double len = std::hypot(dx, dy);
        if (len > 1e-9) {
            const QPointF dir(dx / len, dy / len);
            const double arrowW = e.meta.value("arrowSize").toDouble(3.3) *
                                  EntityRenderer::annotationScale();
            drawArrowHead(p, tip, dir, color, std::clamp(arrowW * vt.scale(), 5.0, 40.0));
        }
        // The words sit at the far end - where the leader lands - reading away from what it points at.
        const QString says = e.meta.value("text").toString();
        if (!says.isEmpty()) {
            const double h = e.meta.value("size").toDouble(2.5) * EntityRenderer::annotationScale();
            const bool toTheLeft = wordsAt.x < e.points[e.points.size() - 2].x;
            drawWorldText(p, vt.toScreen(wordsAt) + QPointF(toTheLeft ? -4 : 4, -2), says, h,
                          vt.scale(), color, toTheLeft ? Qt::AlignRight : Qt::AlignLeft);
        }
        break;
    }

    case EntityType::Connector: {
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        // Body.
        const QPointF lo = vt.toScreen(g.rectMin), hi = vt.toScreen(g.rectMax);
        p.drawRect(QRectF(lo, hi).normalized());
        // Pins: a small ">" fork ("cup") at the body edge, then the wire stub. The cup
        // stays a FIXED default size when the pin pitch changes, but DOES grow when the
        // connector is scaled (cupScale, accumulated by the Scale tool; 1.0 = default).
        const double xDir = spec.mirrorX ? -1.0 : 1.0;
        const double cupScale = e.meta.value(QStringLiteral("cupScale")).toDouble(1.0);
        const double fork = 8.0 * cupScale;   // cup depth  (world units)
        const double forkY = 5.4 * cupScale;  // cup half-height (world units)
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
        // Each marking is drawn only if it is meant to be seen. The same connector wants its designator on
        // a schematic and often not on the mechanical view beside it; the text is kept either way.
        if (spec.showDesignator) {
            QFont f = baseFont; f.setFamily(spec.desFont); p.setFont(f);
            drawWorldText(p, vt.toScreen(g.designatorPos), spec.designator, spec.desSize, vt.scale(),
                          color, Qt::AlignHCenter | Qt::AlignVCenter);
        }
        if (spec.showPartNumber) {
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
        // A splice that joins the two conductors of one cable is drawn as the whole assembly:
        // each joint in its own housing, and a bar tying the housings into a single splice. `b`
        // holds the second joint, so the pair travels as one entity.
        const bool pair = e.meta.value("pair").toBool(false);
        if (!pair) {
            p.setBrush(color);
            p.drawEllipse(c, r, r);
            p.setBrush(Qt::NoBrush);
            break;
        }
        const QPointF c2 = vt.toScreen(e.b);
        // The housing is elongated along the conductor. Sized in world units when the drawing says
        // so, else from the dot - a joint drawn small still needs a body big enough to read.
        const double houseRx = e.meta.value("houseRx").toDouble(0.0);
        const double houseRy = e.meta.value("houseRy").toDouble(0.0);
        const double hx = houseRx > 0.0 ? std::max(3.0, houseRx * vt.scale()) : r * 2.2;
        const double hy = houseRy > 0.0 ? std::max(2.0, houseRy * vt.scale()) : r * 1.5;
        // The bar spans the gap between the two housings, so they read as one part.
        const double top = std::min(c.y(), c2.y()), bot = std::max(c.y(), c2.y());
        if (bot - top > hy * 2.0 + 1.0) {
            const double bw = hx * 0.55;
            p.drawRect(QRectF((c.x() + c2.x()) / 2.0 - bw, top + hy, bw * 2.0,
                              (bot - hy) - (top + hy)));
        }
        for (const QPointF& j : {c, c2}) {
            p.drawEllipse(j, hx, hy);
            p.setBrush(color);
            p.drawEllipse(j, r, r);
            p.setBrush(Qt::NoBrush);
        }
        // The name goes above the assembly: alongside it would sit in the wires' own markings.
        const QString txt = e.meta.value("spliceText").toString();
        const double th = e.meta.value("textSize").toDouble(3.5);
        if (!txt.isEmpty())
            drawWorldText(p, QPointF((c.x() + c2.x()) / 2.0, top - hy - th * 0.6 * vt.scale()), txt,
                          th, vt.scale(), color, Qt::AlignHCenter);
        break;
    }

    case EntityType::LengthBreak: {
        // "Longer than it is drawn": a pair of strokes across the run, the conventional break mark.
        // Two styles, because both are used - straight strokes on a mechanical layout, S-curves where
        // the run itself is drawn as a curve - and the choice is the draughtsman's.
        //
        // The shape comes from breakStrokes(), which is also what a click measures against, what a
        // selection zone catches and what decides which stretch of run is hidden. One description, so
        // none of them can disagree about where the mark is.
        Entity shown = e;
        const Entity* host = nullptr;
        {
            const QString hostId = e.meta.value("host").toString();
            if (!hostId.isEmpty())
                for (const auto& h : doc.entities())
                    if (h.id == hostId) host = &h;
            if (host) {
                // A mark ATTACHED to a run takes its place and its angle from that run, so it follows
                // the run when the run is moved or re-shaped, and can be slid along it.
                Vec2 pos, dir;
                if (runPointAtFraction(*host, e.meta.value("annT").toDouble(0.5), pos, dir)) {
                    shown.a = pos;
                    shown.rotation = std::atan2(dir.y, dir.x);
                } else {
                    host = nullptr;
                }
            }
        }
        const std::vector<std::vector<Vec2>> strokes = breakStrokes(shown);
        if (strokes.size() != 2) break;
        auto toPath = [&vt](const std::vector<Vec2>& pts) {
            QPainterPath path(vt.toScreen(pts.front()));
            for (size_t i = 1; i < pts.size(); ++i) path.lineTo(vt.toScreen(pts[i]));
            return path;
        };
        const QPainterPath s0 = toPath(strokes[0]);
        const QPainterPath s1 = toPath(strokes[1]);

        // The run is INTERRUPTED between the two strokes. That is what a break means: the drawing does
        // not claim to show that stretch. The mask is bounded BY THE STROKES - the strokes lean, so a
        // rectangle hid the run past one of them and left it showing past the other - and the run
        // itself is untouched, still one object, still dimensioned, still editable.
        if (e.meta.value("mask").toBool(true)) {
            QPainterPath cover = s0;
            cover.lineTo(s1.currentPosition());
            cover.connectPath(s1.toReversed());
            cover.closeSubpath();
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(g_bgColor);
            p.drawPath(cover);
            p.restore();
        }
        p.drawPath(s0);
        p.drawPath(s1);

        break;
    }

    case EntityType::TwistedPair: {
        // Two wires laid up round each other. Straight at both ends, because that is where the pair is
        // worked on - stripped, crimped, spliced - and twisted in between, which is the whole point of
        // the drawing: this pair is a twisted pair and is to be made as one.
        //
        // Drawn a stretch at a time, so that at every crossing the wire in FRONT hides the one behind
        // it: that is what makes it read as twisted rather than as two lines that happen to cross. The
        // stretches come from twistedPairWeave(), built from the same measurement a click is tested
        // against, so what is drawn and what can be grabbed cannot drift apart.
        //
        // Painted stretch by stretch, and the pen and brush are swapped WITHOUT save/restore: a pair
        // has a stretch per half crossing, so a save and a restore for each of them is most of what
        // drawing one costs. (Gathering the whole pair into one path instead is slower still - Qt
        // strokes a few large paths less quickly than it draws many small polylines.)
        auto stroke = [&](const std::vector<Vec2>& line) {
            if (line.size() < 2) return;
            QPolygonF poly;
            poly.reserve(static_cast<int>(line.size()));
            for (const Vec2& v : line) poly << vt.toScreen(v);
            p.drawPolyline(poly);
        };
        const QPen wirePen = p.pen();
        for (const auto& s : twistedPairWeave(e))
            for (const auto& wire : s.wires) {   // back to front
                if (wire.outline.size() > 2) {
                    QPolygonF fill;
                    fill.reserve(static_cast<int>(wire.outline.size()));
                    for (const Vec2& v : wire.outline) fill << vt.toScreen(v);
                    p.setPen(Qt::NoPen);
                    p.setBrush(g_bgColor);   // a wire hides whatever runs behind it
                    p.drawPolygon(fill);
                    p.setBrush(Qt::NoBrush);
                    p.setPen(wirePen);
                }
                stroke(wire.edgeA);
                stroke(wire.edgeB);
            }
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
        // A Wire label ATTACHED to a wire is defined by that wire plus a position along
        // it, so it rides the wire: it follows every edit of the wire, reads along the
        // run, and can only be slid up and down it.
        const QString wid = e.meta.value("wireId").toString();
        if (!wid.isEmpty()) {
            for (const auto& w : doc.entities()) {
                if (w.id != wid || w.type != EntityType::Wire) continue;
                Vec2 at, dir;
                if (wirePointAtFraction(w, e.meta.value("annT").toDouble(0.5), at, dir)) {
                    if (dir.x < -1e-9 || (std::abs(dir.x) <= 1e-9 && dir.y < 0.0)) dir = dir * -1.0;
                    drawTextAlongWire(p, vt, at, dir, txt,
                                      e.meta.value("textSize").toDouble(4.0), color,
                                      /*below=*/false);
                    return;
                }
                break;
            }
        }
        drawWorldText(p, vt.toScreen(e.a), txt, e.meta.value("textSize").toDouble(4.0),
                      vt.scale(), color);
        break;
    }

    case EntityType::Twist: {
        // Twisted-pair symbol over the band spanned by the crossed wires. "cross" = a
        // braid of two X crossings; "single" (jog) = one X. The two wires' OWN render
        // (case Wire, via twistBandsForWire) already skips drawing through this band, so
        // no mask is painted here — the wire's actual structure is simply not routed
        // straight through the twist; this case only draws the twist motif itself.
        if (e.points.empty()) break;
        // Draw the braid at the crossed WIRE's thickness (stored gauge), so it reads as part
        // of the wire rather than a thin overlay. Falls back to the default pen if no gauge.
        if (e.meta.contains(QStringLiteral("wireGauge"))) {
            const double ww = penPixels(wireGaugeWeight(e.meta.value("wireGauge").toString()));
            QPen tp = p.pen();
            tp.setWidthF(overrideColor.isValid() ? ww + 1.4 : ww);
            p.setPen(tp);
        }
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (const Vec2& pt : e.points) {
            cx += pt.x;
            yMin = std::min(yMin, pt.y);
            yMax = std::max(yMax, pt.y);
        }
        cx /= e.points.size();
        const QString ty = e.meta.value("twistType").toString();
        // "crossed": a Z / step sign over the wires. A steep diagonal (default 15° off
        // vertical) spans the crossed-wire height, with a truly HORIZONTAL cap at each end
        // (independent of any slant in the crossing line the user drew): the TOP cap runs
        // to the RIGHT, the BOTTOM cap to the LEFT. The diagonal angle and cap length are
        // editable (meta "diagAngle" degrees, "capLen" world units). The wires stay
        // continuous under it (twistBandsForWire does not clip "crossed").
        if (ty == QLatin1String("crossed")) {
            const double span = yMax - yMin;
            if (span < 1e-9) {
                p.drawLine(vt.toScreen(e.points.front()), vt.toScreen(e.points.back()));
            } else {
                // The diagonal starts/ends 30% of the closest wire pitch BEYOND the top and
                // bottom crossed wires (pitch = smallest spacing between adjacent wires).
                std::vector<double> ys;
                for (const Vec2& pt : e.points) ys.push_back(pt.y);
                std::sort(ys.begin(), ys.end());
                double gap = 1e18;
                for (size_t i = 1; i < ys.size(); ++i) gap = std::min(gap, ys[i] - ys[i - 1]);
                if (gap >= 1e17) gap = span; // fallback (single spacing)
                const double ext = 0.30 * gap;
                const double yb = yMin - ext, yt = yMax + ext;    // diagonal ends, extended
                const double ang = e.meta.value("diagAngle").toDouble(15.0) * M_PI / 180.0;
                const double capLen = e.meta.value("capLen").toDouble(8.0);
                const double off = ((yt - yb) / 2.0) * std::tan(ang); // horiz run over half height
                const Vec2 diagBot{cx - off, yb}; // bottom end (30% below), left of centre
                const Vec2 diagTop{cx + off, yt}; // top end (30% above), right of centre
                QPolygonF z;
                z << vt.toScreen({diagBot.x - capLen, yb}) // bottom cap: horizontal, to the left
                  << vt.toScreen(diagBot)
                  << vt.toScreen(diagTop)                   // steep diagonal
                  << vt.toScreen({diagTop.x + capLen, yt}); // top cap: horizontal, to the right
                p.drawPolyline(z);
            }
            break;
        }
        // Type 2 ("jog") = a braid that RESTS FLAT between crossings (a short level run at
        // each peak/trough), Type 3 ("type3") = the same but meeting at a sharp diamond
        // point (no flat). The number of crossings ("repeats") and, for Type 2, the flat
        // length between them are editable (meta "repeats", "flatLen").
        const bool type3 = (ty == QLatin1String("type3"));
        const int n = std::max(1, e.meta.value("repeats").toInt(2));
        const bool flatten = !type3;
        const auto [x0, x1] = twistBandX(e);
        const double lead = std::min((x1 - x0) * 0.18, 8.0); // short horizontal lead lines
        const double mx0 = x0 + lead, mx1 = x1 - lead;       // crossing span
        const double pitch = (mx1 - mx0) / n;
        const double flatHalf =
            flatten ? std::min(e.meta.value("flatLen").toDouble(6.0) / 2.0, pitch * 0.4) : 0.0;
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
        const double pad = 6.0;
        const double rx = e.meta.value("rx").toDouble(12.0);
        // Angle: perpendicular to the wires (stored at placement); older shields fall back
        // to the first->last-wire direction.
        std::vector<Vec2> crossings(e.points.begin(), e.points.begin() + k);
        const double rot = e.meta.contains(QStringLiteral("rot"))
                               ? e.meta.value(QStringLiteral("rot")).toDouble()
                               : shieldRotation(crossings);
        const Vec2 major{std::cos(rot), std::sin(rot)};
        const Vec2 minor{-std::sin(rot), std::cos(rot)};
        // Centre = midpoint of the OUTERMOST caught wires along the major axis, so the
        // outline spans every wire regardless of the order they were caught in.
        double tMin = 1e18, tMax = -1e18;
        Vec2 pMin{}, pMax{};
        for (int i = 0; i < k; ++i) {
            const double t = e.points[i].x * major.x + e.points[i].y * major.y;
            if (t < tMin) { tMin = t; pMin = e.points[i]; }
            if (t > tMax) { tMax = t; pMax = e.points[i]; }
        }
        const Vec2 center{(pMin.x + pMax.x) * 0.5, (pMin.y + pMax.y) * 0.5};
        const double cx = center.x, cyMid = center.y;
        // Large radius: explicit override (Properties) or auto-fit to the caught-wire span.
        const double ry = e.meta.contains("ry") ? e.meta.value("ry").toDouble()
                                                : (tMax - tMin) / 2.0 + pad;
        const QPointF c = vt.toScreen(center);
        // Line type: solid (default) or dashed. Qt measures the dash pattern in units of
        // the pen WIDTH, and the shield's width comes from the (often ~0) line weight — so a
        // raw {6,4} pattern collapses and "dash scale" does almost nothing. Normalise by the
        // effective width so the dash is ~ (6*ds) DEVICE PIXELS and ds scales it directly.
        QPen shieldPen = p.pen();
        if (e.meta.value("lineType").toString(QStringLiteral("solid")) == QLatin1String("dashed")) {
            const double ds = std::max(0.1, e.meta.value("dashScale").toDouble(1.0));
            const double we = shieldPen.widthF() > 0.0 ? shieldPen.widthF() : 1.0;
            shieldPen.setDashPattern({6.0 * ds / we, 4.0 * ds / we});
        }
        p.setPen(shieldPen);
        // Outline: rotated ellipse or capsule (stadium), sampled in world then mapped.
        const bool capsule =
            e.meta.value("shape").toString(QStringLiteral("ellipse")) == QLatin1String("capsule");
        {
            QPolygonF poly;
            for (const Vec2& w : shieldOutline(center, rx, ry, rot, capsule)) poly << vt.toScreen(w);
            p.drawPolyline(poly);
        }
        // Drain lead + endpoint (the point after the k caught wires); the lead exits the
        // outline edge on the meta "legDir" side (relative to the tilted axes). The leg is
        // always SOLID even when the outline is dashed (a dashed drain reads badly).
        if (static_cast<int>(e.points.size()) > k) {
            QPen legPen = shieldPen;
            legPen.setStyle(Qt::SolidLine);
            p.setPen(legPen);
            const Vec2 drain = e.points.back();
            const QString legDir = e.meta.value("legDir").toString(QStringLiteral("bottom"));
            Vec2 edge;
            if (legDir == QLatin1String("top")) edge = {cx + ry * major.x, cyMid + ry * major.y};
            else if (legDir == QLatin1String("left")) edge = {cx + rx * minor.x, cyMid + rx * minor.y};
            else if (legDir == QLatin1String("right")) edge = {cx - rx * minor.x, cyMid - rx * minor.y};
            else edge = {cx - ry * major.x, cyMid - ry * major.y}; // "bottom"
            p.drawLine(vt.toScreen(edge), vt.toScreen(drain));
            // The "X" end tick. A wire actually landing on the leg end means the leg IS
            // connected, so the X goes — what is visibly wired outranks any stored flag,
            // otherwise a leg with a wire on it would still be marked not-connected. With
            // nothing on the leg the explicit "Not Connected" choice decides, and shields
            // saved before that flag existed fall back to the same geometric test.
            // Judged with a small WORLD tolerance rather than exact equality: a leg dragged
            // onto a run without a snap lands a hair off it, and a leg that visibly touches a
            // wire must read as connected either way.
            constexpr double kLegTol = 1.0;
            bool wired = false;
            // A leg that was actually DROPPED on a wire says so, and that outranks the
            // geometry: the connection was made deliberately and holds while the shield is
            // moved (the wire is pulled along with it). Only a wire that has since gone is
            // ignored - then the geometric test below has the last word.
            const QString legWire = e.meta.value("legWireId").toString();
            if (!legWire.isEmpty())
                for (const auto& w : doc.entities())
                    if (w.id == legWire && w.type == EntityType::Wire) { wired = true; break; }
            for (const auto& w : doc.entities()) {
                if (wired) break;
                if (w.type != EntityType::Wire || w.points.empty()) continue;
                for (const Vec2& pt : w.points)
                    if (std::hypot(pt.x - drain.x, pt.y - drain.y) < kLegTol) { wired = true; break; }
                if (wired) break;
                // A leg dropped onto the middle of a run is connected just as much as one
                // dropped on its end.
                for (size_t i = 1; i < w.points.size(); ++i)
                    if (closestPointOnSegment(drain, w.points[i - 1], w.points[i]).distance
                        < kLegTol) {
                        wired = true;
                        break;
                    }
                if (wired) break;
            }
            const bool notConnected =
                !wired && (!e.meta.contains("notConnected") ||
                           e.meta.value("notConnected").toBool());
            if (notConnected) {
                // The SAME X as a standalone NotConnected terminator: world-sized off
                // "ncSize", so it scales with the drawing and reads as the standard
                // not-connected symbol rather than a fixed-pixel tick. A shield's X is
                // half-size 3 (also the fallback for shields saved before it was stored).
                const double halfWorld = e.meta.value("ncSize").toDouble(3.0);
                const double s = std::max(3.0, halfWorld * vt.scale());
                const QPointF d = vt.toScreen(drain);
                p.drawLine(d + QPointF(-s, -s), d + QPointF(s, s));
                p.drawLine(d + QPointF(s, -s), d + QPointF(-s, s));
            }
        }
        if (e.meta.value("showS").toBool(false)) {
            const double sSize = e.meta.value("sSize").toDouble(4.0);
            const Vec2 sPos{center.x + (rx + sSize) * minor.x, center.y + (rx + sSize) * minor.y};
            drawWorldText(p, vt.toScreen(sPos), QStringLiteral("S"), sSize, vt.scale(), color);
        }
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
