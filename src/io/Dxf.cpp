#include "io/Dxf.h"

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

} // namespace

bool parse(Document& doc, const QString& text, QString*) {
    QString clean = text;
    clean.replace("\r", "");
    const QStringList raw = clean.split('\n');
    std::vector<Pair> pairs;
    for (int i = 0; i + 1 < raw.size(); i += 2)
        pairs.push_back({raw[i].trimmed(), raw[i + 1].trimmed()});

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

    for (int i = 0; i < static_cast<int>(pairs.size()); ++i) {
        if (pairs[i].code == "0" && pairs[i].value == "LAYER") {
            const auto g = collectGroup(pairs, i + 1);
            ensureLayer(findVal(g, "2", "0"),
                        std::abs(findVal(g, "62", "0").toInt()));
        }
    }

    std::vector<Entity> entities;
    for (int i = 0; i < static_cast<int>(pairs.size()); ++i) {
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
            for (; j < static_cast<int>(pairs.size()); ++j) {
                if (pairs[j].code == "0" && pairs[j].value == "VERTEX") {
                    const auto v = collectGroup(pairs, j + 1);
                    e.points.push_back({findNum(v, "10"), findNum(v, "20")});
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

        if (type != "LINE" && type != "LWPOLYLINE" && type != "CIRCLE" &&
            type != "ARC" && type != "TEXT" && type != "MTEXT")
            continue;

        const auto g = collectGroup(pairs, i + 1);
        const QString layer = findVal(g, "8", "0");
        ensureLayer(layer, 0);
        const int eci = findVal(g, "62", "256").toInt();
        const QString color = (eci != 256 && eci != 0) ? indexToColor(std::abs(eci), 0)
                                                       : QString();
        Entity e;
        e.id = newId();
        e.layer = layer;
        e.color = color;

        if (type == "LINE") {
            e.type = EntityType::Line;
            e.a = {findNum(g, "10"), findNum(g, "20")};
            e.b = {findNum(g, "11"), findNum(g, "21")};
            entities.push_back(std::move(e));
        } else if (type == "LWPOLYLINE") {
            e.type = EntityType::Polyline;
            Vec2 cur; bool have = false;
            for (const auto& p : g) {
                if (p.code == "10") { if (have) e.points.push_back(cur); cur = {p.value.toDouble(), 0}; have = true; }
                else if (p.code == "20" && have) cur.y = p.value.toDouble();
            }
            if (have) e.points.push_back(cur);
            if (e.points.size() > 1) entities.push_back(std::move(e));
        } else if (type == "CIRCLE") {
            e.type = EntityType::Circle;
            e.center = {findNum(g, "10"), findNum(g, "20")};
            e.radius = findNum(g, "40", 1);
            entities.push_back(std::move(e));
        } else if (type == "ARC") {
            e.type = EntityType::Arc;
            e.center = {findNum(g, "10"), findNum(g, "20")};
            e.radius = findNum(g, "40", 1);
            e.startAngle = findNum(g, "50") * M_PI / 180.0;
            e.endAngle = findNum(g, "51") * M_PI / 180.0;
            entities.push_back(std::move(e));
        } else { // TEXT / MTEXT
            e.type = EntityType::Text;
            e.a = {findNum(g, "10"), findNum(g, "20")};
            e.meta.insert("text", findVal(g, "1", ""));
            e.meta.insert("size", findNum(g, "40", 8));
            entities.push_back(std::move(e));
        }
    }

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
