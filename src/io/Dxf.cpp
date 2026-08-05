#include "io/Dxf.h"
#include <functional>
#include <QRegularExpression>

#include "geometry/Geometry.h"
#include "model/Document.h"

#include <QFile>
#include <QHash>
#include <QStringList>
#include <QTextStream>
#include <QUuid>
#include <QtMath>
#include <cmath>

namespace Edraw_Desktop::dxf {

namespace {

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8); }

QString pair(int code, const QString& value) {
    return QStringLiteral("%1\n%2").arg(code).arg(value);
}
QString pair(int code, double value) {
    return QStringLiteral("%1\n%2").arg(code).arg(value, 0, 'g', 12);
}

// hex -> DXF ACI color index (matches Edraw dxfColor()).
int colorToIndex(const QString& hex, int fallback = 7) {
    static const QHash<QString, int> map = {
        {"#ff0000", 1}, {"#ffff00", 2}, {"#00ff00", 3}, {"#00ffff", 4},
        {"#0000ff", 5}, {"#ff00ff", 6}, {"#ffffff", 7}, {"#00f0ff", 4},
        {"#f5c66d", 2}, {"#8bdc65", 3}, {"#b28cff", 6}, {"#ff6b6b", 1}};
    const QString k = hex.toLower();
    return map.value(k, (fallback % 7) + 1);
}

// DXF ACI color index -> hex (matches Edraw colorIndexToHex()).
QString indexToColor(int index, int fallback) {
    static const QHash<int, QString> map = {
        {1, "#ff0000"}, {2, "#ffff00"}, {3, "#00ff00"}, {4, "#00ffff"},
        {5, "#0000ff"}, {6, "#ff00ff"}, {7, "#ffffff"}, {8, "#808080"},
        {9, "#c0c0c0"}};
    static const QString defaults[] = {"#ffffff", "#00f0ff", "#f5c66d"};
    const auto it = map.find(std::abs(index));
    if (it != map.end()) return it.value();
    return defaults[fallback % 3];
}

double radToDxf(double v) {
    double deg = std::fmod(v * 180.0 / M_PI, 360.0);
    if (deg < 0) deg += 360.0;
    return deg;
}

QString resolveEntityColor(const Entity& e, const Document& doc) {
    if (e.hasExplicitColor()) return e.color;
    if (const Layer* l = doc.findLayer(e.layer)) return l->color;
    return QStringLiteral("#ffffff");
}

QString commonDxf(const Entity& e, const Document& doc) {
    return pair(8, e.layer.isEmpty() ? QStringLiteral("0") : e.layer) + "\n" +
           pair(62, QString::number(colorToIndex(resolveEntityColor(e, doc))));
}

QString dxfLine(const Entity& e, const Document& doc, const Vec2& a, const Vec2& b) {
    return pair(0, "LINE") + "\n" + commonDxf(e, doc) + "\n" + pair(10, a.x) + "\n" +
           pair(20, a.y) + "\n" + pair(30, 0.0) + "\n" + pair(11, b.x) + "\n" +
           pair(21, b.y) + "\n" + pair(31, 0.0);
}

QString dxfPolyline(const Entity& e, const Document& doc,
                    const std::vector<Vec2>& pts, bool closed) {
    QStringList out;
    out << pair(0, "POLYLINE") << commonDxf(e, doc) << pair(66, QString::number(1))
        << pair(10, 0.0) << pair(20, 0.0) << pair(30, 0.0)
        << pair(70, QString::number(closed ? 1 : 0));
    for (const auto& p : pts) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
        out << pair(0, "VERTEX") << commonDxf(e, doc) << pair(10, p.x) << pair(20, p.y)
            << pair(30, 0.0);
    }
    out << pair(0, "SEQEND") << commonDxf(e, doc);
    return out.join("\n");
}

QString dxfText(const Entity& e, const Document& doc, const Vec2& pos,
                const QString& text, double size) {
    return pair(0, "TEXT") + "\n" + commonDxf(e, doc) + "\n" + pair(10, pos.x) + "\n" +
           pair(20, pos.y) + "\n" + pair(30, 0.0) + "\n" +
           pair(40, size > 0 ? size : 3.0) + "\n" + pair(1, text);
}

std::vector<Vec2> ellipsePoints(const Entity& e, int steps = 48) {
    std::vector<Vec2> pts;
    const double c = std::cos(e.rotation), s = std::sin(e.rotation);
    for (int i = 0; i <= steps; ++i) {
        const double t = 2.0 * M_PI * i / steps;
        const double ex = e.radius * std::cos(t);
        const double ey = e.radiusY * std::sin(t);
        pts.push_back({e.center.x + ex * c - ey * s, e.center.y + ex * s + ey * c});
    }
    return pts;
}

} // namespace

QString toDxf(const Document& doc) {
    QStringList L;
    L << pair(0, "SECTION") << pair(2, "HEADER") << pair(9, "$ACADVER")
      << pair(1, "AC1009") << pair(0, "ENDSEC");
    L << pair(0, "SECTION") << pair(2, "TABLES");
    L << pair(0, "TABLE") << pair(2, "LTYPE") << pair(70, QString::number(1));
    L << pair(0, "LTYPE") << pair(2, "CONTINUOUS") << pair(70, QString::number(0))
      << pair(3, "Solid line") << pair(72, QString::number(65))
      << pair(73, QString::number(0)) << pair(40, 0.0);
    L << pair(0, "ENDTAB");
    L << pair(0, "TABLE") << pair(2, "LAYER")
      << pair(70, QString::number(doc.layers().size()));
    int li = 0;
    for (const auto& layer : doc.layers()) {
        const int ci = colorToIndex(layer.color, li);
        L << pair(0, "LAYER") << pair(2, layer.name)
          << pair(70, QString::number(layer.visible ? 0 : 1))
          << pair(62, QString::number(layer.visible ? ci : -ci))
          << pair(6, "CONTINUOUS");
        ++li;
    }
    L << pair(0, "ENDTAB") << pair(0, "ENDSEC");
    L << pair(0, "SECTION") << pair(2, "ENTITIES");

    for (const auto& e : doc.entities()) {
        switch (e.type) {
        case EntityType::Line:
        case EntityType::Dimension:
            L << dxfLine(e, doc, e.a, e.b);
            if (e.type == EntityType::Dimension) {
                const Vec2 mid = lerp(e.a, e.b, 0.5);
                const QString label = QStringLiteral("%1 %2")
                                          .arg(distance(e.a, e.b), 0, 'f', 2)
                                          .arg(doc.unit());
                L << dxfText(e, doc, mid, label, 3.5);
            }
            break;
        case EntityType::DoubleLine: {
            const double gap = e.meta.value("gap").toDouble(10.0);
            const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
            L << dxfLine(e, doc, e.a + off, e.b + off);
            L << dxfLine(e, doc, e.a - off, e.b - off);
            break;
        }
        case EntityType::Polygon:
            L << dxfPolyline(e, doc, e.points, true);
            break;
        case EntityType::Wire:
        case EntityType::Spline:
            L << dxfPolyline(e, doc, e.points, false);
            break;
        case EntityType::Polyline: {
            const double cr = e.meta.value("cornerRadius").toDouble(0.0);
            if (e.meta.value("double").toBool()) {
                const double gap = e.meta.value("gap").toDouble(10.0);
                // Round centerline at cr+gap/2 so both runs fillet (inner radius = cr).
                const std::vector<Vec2> center =
                    cr > 0.0 ? roundedPolyline(e.points, cr + gap / 2.0) : e.points;
                L << dxfPolyline(e, doc, offsetPolyline(center, gap / 2.0), false);
                L << dxfPolyline(e, doc, offsetPolyline(center, -gap / 2.0), false);
            } else {
                L << dxfPolyline(e, doc, cr > 0.0 ? roundedPolyline(e.points, cr) : e.points, false);
            }
            break;
        }
        case EntityType::Rect:
        case EntityType::Hatch:
        case EntityType::Wipeout:
        // A picture exports as the rectangle it occupies: a DXF image is a reference to a file
        // beside the drawing, and an embedded one has nowhere to go. The frame at least keeps the
        // layout right rather than silently dropping part of the sheet.
        case EntityType::Image:
            L << dxfPolyline(e, doc, e.points, true);
            break;
        case EntityType::Circle:
            L << pair(0, "CIRCLE") << commonDxf(e, doc) << pair(10, e.center.x)
              << pair(20, e.center.y) << pair(40, e.radius);
            break;
        case EntityType::Arc: {
            double start = std::isfinite(e.startAngle) ? e.startAngle : 0.0;
            double end = std::isfinite(e.endAngle) ? e.endAngle : start;
            const double sweep = shortAngleDelta(start, end);
            if (sweep < 0) { const double s2 = start + sweep; end = start; start = s2; }
            else end = start + sweep;
            L << pair(0, "ARC") << commonDxf(e, doc) << pair(10, e.center.x)
              << pair(20, e.center.y) << pair(40, e.radius)
              << pair(50, radToDxf(start)) << pair(51, radToDxf(end));
            break;
        }
        case EntityType::Ellipse:
            L << dxfPolyline(e, doc, ellipsePoints(e), false);
            break;
        case EntityType::Text:
            L << dxfText(e, doc, e.a, e.meta.value("text").toString(),
                         e.meta.value("size").toDouble(3.0));
            break;
        default:
            break;
        }
    }
    L << pair(0, "ENDSEC") << pair(0, "EOF");
    return L.join("\r\n") + "\r\n";
}

namespace {

struct Pair { QString code; QString value; };

QString findVal(const std::vector<Pair>& pairs, const QString& code,
                const QString& fallback = QString()) {
    for (const auto& p : pairs)
        if (p.code == code) return p.value;
    return fallback;
}
double findNum(const std::vector<Pair>& pairs, const QString& code, double fallback = 0.0) {
    bool ok = false;
    const double v = findVal(pairs, code, QString::number(fallback)).toDouble(&ok);
    return ok ? v : fallback;
}

std::vector<Pair> collectGroup(const std::vector<Pair>& pairs, int start) {
    std::vector<Pair> out;
    for (int i = start; i < static_cast<int>(pairs.size()); ++i) {
        if (pairs[i].code == "0") break;
        out.push_back(pairs[i]);
    }
    return out;
}

// ---- Text as AutoCAD writes it ---------------------------------------------------------------
//
// A TEXT or MTEXT string is not plain text. It carries the old %% escapes (%%d for a degree sign,
// %%c for a diameter, %%p for plus/minus), \U+ escapes for anything else, and - in MTEXT - inline
// formatting runs: a font is declared as \fArial|b0|i0|c0|p34;, a height as \H2.5x;, a colour as
// \C3;, a paragraph break as \P, and runs are grouped in braces. Read literally, a label reads
// "{\fArial|b0|i0|c0|p34;+0.5%%d}" instead of "+0.5°", which is what the whole drawing looked like.
// MTEXT longer than 250 characters is also SPLIT: the leading parts arrive as repeated group 3 and
// only the tail as group 1, so a note read from group 1 alone lost everything but its last line.
QString decodeDxfText(const QString& in, bool mtext) {
    QString s = in;
    // The %% escapes first: they are the same in TEXT and MTEXT.
    s.replace(QLatin1String("%%d"), QString(QChar(0x00B0)));   // degree
    s.replace(QLatin1String("%%c"), QString(QChar(0x2300)));   // diameter
    s.replace(QLatin1String("%%p"), QString(QChar(0x00B1)));   // plus/minus
    s.replace(QLatin1String("%%%"), QStringLiteral("%"));
    // %%nnn: a character by its code.
    {
        static const QRegularExpression re(QStringLiteral("%%(\\d{1,3})"));
        QRegularExpressionMatch m;
        while ((m = re.match(s)).hasMatch())
            s.replace(m.capturedStart(), m.capturedLength(),
                      QString(QChar(m.captured(1).toInt())));
    }
    // \U+XXXX: a character by its code point.
    {
        static const QRegularExpression re(QStringLiteral("\\\\U\\+([0-9A-Fa-f]{4})"));
        QRegularExpressionMatch m;
        while ((m = re.match(s)).hasMatch())
            s.replace(m.capturedStart(), m.capturedLength(),
                      QString(QChar(m.captured(1).toUShort(nullptr, 16))));
    }
    if (!mtext) return s;

    // The inline runs. Everything here is formatting, not content: it is dropped, leaving the words.
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\') && i + 1 < s.size()) {
            const QChar k = s.at(i + 1);
            // A paragraph break is real content: it is where the line ends.
            if (k == QLatin1Char('P')) { out += QLatin1Char('\n'); ++i; continue; }
            if (k == QLatin1Char('~')) { out += QLatin1Char(' '); ++i; continue; }
            // An escaped brace or backslash stands for itself.
            if (k == QLatin1Char('\\') || k == QLatin1Char('{') || k == QLatin1Char('}')) {
                out += k;
                ++i;
                continue;
            }
            // A stacked run - \S over^under; - is written as over/under, which is what it means.
            if (k == QLatin1Char('S')) {
                int j = i + 2;
                QString run;
                while (j < s.size() && s.at(j) != QLatin1Char(';')) run += s.at(j++);
                run.replace(QLatin1Char('^'), QLatin1Char('/'));
                run.replace(QLatin1Char('#'), QLatin1Char('/'));
                out += run;
                i = j;
                continue;
            }
            // Everything else with a value runs to its semicolon: \f font, \H height, \C colour,
            // \W width, \Q slant, \T tracking, \A alignment, \p paragraph settings.
            if (QStringLiteral("fFHhCcWwQqTtAap").contains(k)) {
                int j = i + 2;
                while (j < s.size() && s.at(j) != QLatin1Char(';')) ++j;
                i = j;
                continue;
            }
            // The switches that carry no value: under/over/strike-through on and off.
            if (QStringLiteral("LlOoKkXN").contains(k)) { ++i; continue; }
            ++i;                                     // an escape this build does not know
            continue;
        }
        if (c == QLatin1Char('{') || c == QLatin1Char('}')) continue;   // run grouping
        out += c;
    }
    return out;
}

// Where a group 1 / group 3 text lives: MTEXT longer than 250 characters arrives as any number of
// group 3 continuations followed by the final group 1.
QString gatherText(const std::vector<Pair>& g) {
    QString s;
    for (const auto& p : g)
        if (p.code == QLatin1String("3")) s += p.value;
    for (const auto& p : g)
        if (p.code == QLatin1String("1")) s += p.value;
    return s;
}

// A block's placement: scale, turn, then move. Composed for nested inserts.
struct Xf {
    double sx = 1.0, sy = 1.0, rot = 0.0;   // rot in radians
    Vec2 t{0.0, 0.0};

    Vec2 apply(const Vec2& p) const {
        const double x = p.x * sx, y = p.y * sy;
        const double c = std::cos(rot), s = std::sin(rot);
        return {t.x + x * c - y * s, t.y + x * s + y * c};
    }
    // A length, scaled. A non-uniform insert cannot keep a circle a circle, so the mean is used -
    // the honest approximation for the one case (sx != sy) where no single radius is right.
    double len(double v) const { return v * (std::abs(sx) + std::abs(sy)) * 0.5; }
    double angle(double a) const {
        // A mirrored insert (one negative scale) reverses the sense of an angle.
        return (sx * sy < 0.0) ? -a + rot : a + rot;
    }
    Xf then(const Xf& parent) const {
        Xf out;
        out.sx = sx * parent.sx;
        out.sy = sy * parent.sy;
        out.rot = rot + parent.rot;
        out.t = parent.apply(t);
        return out;
    }
};

} // namespace

bool parse(Document& doc, const QString& text, QString*) {
    QString clean = text;
    clean.replace("\r", "");
    const QStringList raw = clean.split('\n');
    std::vector<Pair> pairs;
    for (int i = 0; i + 1 < raw.size(); i += 2)
        pairs.push_back({raw[i].trimmed(), raw[i + 1].trimmed()});
    const int n = static_cast<int>(pairs.size());

    QHash<QString, Layer> layers;
    auto ensureLayer = [&](const QString& nameIn, int colorIndex) {
        const QString name = nameIn.isEmpty() ? QStringLiteral("0") : nameIn;
        if (!layers.contains(name)) {
            Layer l;
            l.name = name;
            l.color = indexToColor(colorIndex, layers.size());
            layers.insert(name, l);
        }
    };
    for (int i = 0; i < n; ++i) {
        if (pairs[i].code == "0" && pairs[i].value == "LAYER") {
            const auto g = collectGroup(pairs, i + 1);
            ensureLayer(findVal(g, "2", "0"), std::abs(findVal(g, "62", "0").toInt()));
        }
    }

    // ---- The file's sections ------------------------------------------------------------------
    //
    // Every entity used to be read wherever it appeared, which meant the BLOCKS section - the
    // DEFINITIONS of the symbols, drawn about their own origins - was dropped into the drawing as
    // though it were geometry, while the INSERTs that actually place those symbols were ignored. A
    // frame block therefore appeared in a corner of the sheet on its own, and everything really
    // placed by an insert (the braided cable, the logos, the connector symbols) was missing.
    int entFrom = -1, entTo = -1;
    struct BlockDef { int from = -1, to = -1; Vec2 base{0.0, 0.0}; };
    QHash<QString, BlockDef> blocks;
    for (int i = 0; i < n; ++i) {
        if (pairs[i].code != "0" || pairs[i].value != "SECTION") continue;
        const auto head = collectGroup(pairs, i + 1);
        const QString name = findVal(head, "2");
        int end = i + 1;
        while (end < n && !(pairs[end].code == "0" && pairs[end].value == "ENDSEC")) ++end;
        if (name == QLatin1String("ENTITIES")) {
            entFrom = i + 1;
            entTo = end;
        } else if (name == QLatin1String("BLOCKS")) {
            for (int j = i + 1; j < end; ++j) {
                if (pairs[j].code != "0" || pairs[j].value != "BLOCK") continue;
                const auto bh = collectGroup(pairs, j + 1);
                BlockDef def;
                def.base = {findNum(bh, "10"), findNum(bh, "20")};
                int k = j + 1;
                while (k < end && pairs[k].code != "0") ++k;      // past the header
                def.from = k;
                while (k < end && !(pairs[k].code == "0" && pairs[k].value == "ENDBLK")) ++k;
                def.to = k;
                blocks.insert(findVal(bh, "2"), def);
                j = k;
            }
        }
        i = end;
    }
    if (entFrom < 0) { entFrom = 0; entTo = n; }   // a file with no sections: read it all

    std::vector<Entity> entities;
    // One reader, used for the drawing itself and for the inside of every block it places.
    std::function<void(int, int, const Xf&, int)> readRange =
        [&](int from, int to, const Xf& xf, int depth) {
            for (int i = from; i < to; ++i) {
                if (pairs[i].code != "0") continue;
                const QString type = pairs[i].value;

                if (type == "POLYLINE") {
                    // Legacy polyline: gather following VERTEX groups until SEQEND.
                    const auto head = collectGroup(pairs, i + 1);
                    const QString layer = findVal(head, "8", "0");
                    ensureLayer(layer, 0);
                    Entity e;
                    e.id = newId();
                    e.type = EntityType::Polyline;
                    e.layer = layer;
                    int j = i + 1;
                    for (; j < to; ++j) {
                        if (pairs[j].code == "0" && pairs[j].value == "VERTEX") {
                            const auto v = collectGroup(pairs, j + 1);
                            e.points.push_back(xf.apply({findNum(v, "10"), findNum(v, "20")}));
                        } else if (pairs[j].code == "0" && pairs[j].value == "SEQEND") {
                            break;
                        } else if (pairs[j].code == "0" && pairs[j].value != "VERTEX") {
                            break;
                        }
                    }
                    if (e.points.size() > 1) entities.push_back(std::move(e));
                    i = j;
                    continue;
                }

                static const QStringList kKnown = {
                    "LINE",   "LWPOLYLINE", "CIRCLE",  "ARC",    "TEXT",      "MTEXT",
                    "INSERT", "ELLIPSE",    "SOLID",   "LEADER", "DIMENSION", "ACAD_TABLE"};
                if (!kKnown.contains(type)) continue;

                const auto g = collectGroup(pairs, i + 1);
                const QString layer = findVal(g, "8", "0");
                ensureLayer(layer, 0);
                const int eci = findVal(g, "62", "256").toInt();
                const QString color =
                    (eci != 256 && eci != 0) ? indexToColor(std::abs(eci), 0) : QString();
                Entity e;
                e.id = newId();
                e.layer = layer;
                e.color = color;

                if (type == "INSERT" || type == "DIMENSION" || type == "ACAD_TABLE") {
                    // All three are drawn by a BLOCK. An insert places one, with its own scale and
                    // turn; a dimension and a table each name the block that holds the geometry
                    // AutoCAD generated for them, which is already in the drawing's coordinates.
                    if (depth > 8) continue;                    // a block that places itself
                    const QString name = findVal(g, "2");
                    const auto it = blocks.constFind(name);
                    if (it == blocks.constEnd() || it->from < 0) continue;
                    if (type == "ACAD_TABLE") {
                        // A table is a block reference as well, but it states its turn as a
                        // DIRECTION VECTOR rather than an angle, and its 41 is a column width, not a
                        // scale. Expanded with the identity transform - as a dimension rightly is,
                        // since a dimension's block is already in the drawing's coordinates - both
                        // tables landed on top of each other at the origin instead of on the sheet.
                        Xf local;
                        local.t = {findNum(g, "10"), findNum(g, "20")};
                        const Vec2 dir{findNum(g, "11", 1.0), findNum(g, "21", 0.0)};
                        if (std::hypot(dir.x, dir.y) > 1e-12) local.rot = std::atan2(dir.y, dir.x);
                        Xf shift;
                        shift.t = {-it->base.x, -it->base.y};
                        readRange(it->from, it->to, shift.then(local).then(xf), depth + 1);
                        continue;
                    }
                    if (type == "INSERT") {
                        Xf local;
                        local.sx = findNum(g, "41", 1.0);
                        local.sy = findNum(g, "42", 1.0);
                        if (std::abs(local.sx) < 1e-12) local.sx = 1.0;
                        if (std::abs(local.sy) < 1e-12) local.sy = 1.0;
                        local.rot = findNum(g, "50") * M_PI / 180.0;
                        const Vec2 at{findNum(g, "10"), findNum(g, "20")};
                        // The block's own base point is its origin, so the definition is placed
                        // relative to it rather than to the file's origin.
                        Xf shift;
                        shift.t = {-it->base.x, -it->base.y};
                        // A row/column array of the same block: rare, and cheap to honour.
                        const int cols = std::max(1, findVal(g, "70", "1").toInt());
                        const int rows = std::max(1, findVal(g, "71", "1").toInt());
                        const double cs = findNum(g, "44"), rs = findNum(g, "45");
                        for (int c = 0; c < cols; ++c)
                            for (int r = 0; r < rows; ++r) {
                                Xf place = local;
                                place.t = {at.x + c * cs, at.y + r * rs};
                                readRange(it->from, it->to, shift.then(place).then(xf), depth + 1);
                            }
                    } else {
                        readRange(it->from, it->to, xf, depth + 1);
                    }
                    continue;
                }

                if (type == "LINE") {
                    e.type = EntityType::Line;
                    e.a = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    e.b = xf.apply({findNum(g, "11"), findNum(g, "21")});
                    entities.push_back(std::move(e));
                } else if (type == "LWPOLYLINE" || type == "LEADER") {
                    // A leader is a run of points with an arrow on the end: the run is the part
                    // that carries meaning here.
                    e.type = EntityType::Polyline;
                    Vec2 cur;
                    bool have = false;
                    for (const auto& p : g) {
                        if (p.code == "10") {
                            if (have) e.points.push_back(xf.apply(cur));
                            cur = {p.value.toDouble(), 0};
                            have = true;
                        } else if (p.code == "20" && have) {
                            cur.y = p.value.toDouble();
                        }
                    }
                    if (have) e.points.push_back(xf.apply(cur));
                    if (e.points.size() > 1) entities.push_back(std::move(e));
                } else if (type == "CIRCLE") {
                    e.type = EntityType::Circle;
                    e.center = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    e.radius = std::max(1e-6, xf.len(findNum(g, "40", 1)));
                    entities.push_back(std::move(e));
                } else if (type == "ARC") {
                    e.type = EntityType::Arc;
                    e.center = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    e.radius = std::max(1e-6, xf.len(findNum(g, "40", 1)));
                    double a0 = findNum(g, "50") * M_PI / 180.0;
                    double a1 = findNum(g, "51") * M_PI / 180.0;
                    a0 = xf.angle(a0);
                    a1 = xf.angle(a1);
                    if (xf.sx * xf.sy < 0.0) std::swap(a0, a1);   // mirrored: the sweep reverses
                    e.startAngle = a0;
                    e.endAngle = a1;
                    entities.push_back(std::move(e));
                } else if (type == "ELLIPSE") {
                    // Centre, the vector to one end of the major axis, and the minor/major ratio.
                    e.type = EntityType::Ellipse;
                    e.center = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    const Vec2 major{findNum(g, "11"), findNum(g, "21")};
                    const double a = std::hypot(major.x, major.y);
                    const double ratio = findNum(g, "40", 1.0);
                    e.radius = std::max(1e-6, xf.len(a));
                    e.radiusY = std::max(1e-6, xf.len(a * ratio));
                    e.rotation = xf.angle(std::atan2(major.y, major.x));
                    e.startAngle = findNum(g, "41", 0.0);
                    e.endAngle = findNum(g, "42", 2.0 * M_PI);
                    entities.push_back(std::move(e));
                } else if (type == "SOLID") {
                    // Four corners (the last two are given in the other order).
                    e.type = EntityType::Polygon;
                    const Vec2 p1 = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    const Vec2 p2 = xf.apply({findNum(g, "11"), findNum(g, "21")});
                    const Vec2 p3 = xf.apply({findNum(g, "12"), findNum(g, "22")});
                    const Vec2 p4 = xf.apply({findNum(g, "13"), findNum(g, "23")});
                    e.points = {p1, p2, p4, p3};
                    entities.push_back(std::move(e));
                } else { // TEXT / MTEXT
                    e.type = EntityType::Text;
                    const bool mtext = type == QLatin1String("MTEXT");
                    e.a = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    const QString body = decodeDxfText(gatherText(g), mtext);
                    if (body.trimmed().isEmpty()) continue;
                    e.meta.insert("text", body);
                    e.meta.insert("size", std::max(0.1, xf.len(findNum(g, "40", 8))));
                    e.rotation = xf.angle(findNum(g, "50") * M_PI / 180.0);
                    if (mtext) {
                        // The box it wraps in, and where the insertion point sits in it: 1..9 run
                        // top-left, top-centre, top-right, middle-left, ... bottom-right.
                        const double w = xf.len(findNum(g, "41", 0.0));
                        if (w > 1e-6) e.meta.insert("boxWidth", w);
                        static const char* const kJust[9] = {"TL", "TC", "TR", "ML", "MC",
                                                             "MR", "BL", "BC", "BR"};
                        const int at = std::clamp(findVal(g, "71", "1").toInt(), 1, 9);
                        e.meta.insert("justify", QLatin1String(kJust[at - 1]));
                    } else {
                        // TEXT: a justification other than left/baseline measures from the SECOND
                        // alignment point, which is where the text actually sits.
                        const int hj = findVal(g, "72", "0").toInt();
                        const int vj = findVal(g, "73", "0").toInt();
                        if (hj != 0 || vj != 0) {
                            const Vec2 alt{findNum(g, "11"), findNum(g, "21")};
                            if (std::abs(alt.x) > 1e-12 || std::abs(alt.y) > 1e-12)
                                e.a = xf.apply(alt);
                            static const char* const kH[5] = {"L", "C", "R", "L", "C"};
                            static const char* const kV[4] = {"B", "B", "M", "T"};
                            e.meta.insert("justify",
                                          QString(QLatin1String(kV[std::clamp(vj, 0, 3)])) +
                                              QLatin1String(kH[std::clamp(hj, 0, 4)]));
                        }
                    }
                    entities.push_back(std::move(e));
                }
            }
        };
    readRange(entFrom, entTo, Xf{}, 0);

    std::vector<Layer> layerList;
    for (const auto& l : layers) layerList.push_back(l);
    if (layerList.empty()) layerList = Document::defaultLayers();
    doc.layers() = std::move(layerList);
    doc.entities() = std::move(entities);
    doc.setActiveLayer(doc.layers().front().name);
    doc.setDirty(false);
    return true;
}

bool exportFile(const Document& doc, const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(toDxf(doc).toUtf8());
    return true;
}

bool importFile(Document& doc, const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    return parse(doc, QString::fromUtf8(file.readAll()), error);
}

} // namespace Edraw_Desktop::dxf
