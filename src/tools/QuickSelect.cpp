#include "tools/QuickSelect.h"

#include "geometry/Geometry.h"

#include <QJsonValue>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace Loopwire {

namespace {

constexpr double kQsPi = 3.14159265358979323846;

QuickSelectProp field(const char* key, const char* label, bool numeric = false) {
    return {QString::fromLatin1(key), QString::fromLatin1(label), numeric};
}
// A property stored in the entity's meta bag.
QuickSelectProp metaProp(const char* key, const char* label, bool numeric = false) {
    return {QStringLiteral("meta:") + QString::fromLatin1(key), QString::fromLatin1(label),
            numeric};
}

// Format a number the way the dialog shows and compares it: no trailing zeros, so
// "10" matches a radius of 10.0 typed by hand.
QString num(double v) {
    QString s = QString::number(v, 'f', 4);
    while (s.contains(QLatin1Char('.')) && (s.endsWith(QLatin1Char('0')) || s.endsWith(QLatin1Char('.'))))
        s.chop(1);
    return s.isEmpty() ? QStringLiteral("0") : s;
}

// Total length of an entity's drawn run (lines, polylines, wires, ...).
double runLength(const Entity& e) {
    switch (e.type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
        return distance(e.a, e.b);
    case EntityType::Polyline:
    case EntityType::Wire:
    case EntityType::Spline:
        return polylineLength(e.points);
    case EntityType::Rect:
    case EntityType::Polygon: {
        double len = 0.0;
        for (size_t i = 0; i < e.points.size(); ++i)
            len += distance(e.points[i], e.points[(i + 1) % e.points.size()]);
        return len;
    }
    case EntityType::Circle:
        return 2.0 * kQsPi * e.radius;
    case EntityType::Arc: {
        double sweep = e.endAngle - e.startAngle;
        while (sweep < 0.0) sweep += 2.0 * kQsPi;
        return sweep * e.radius;
    }
    default:
        return 0.0;
    }
}

// Value of a meta key as comparable text (numbers formatted like the fields above).
QString metaText(const Entity& e, const QString& key) {
    const QJsonValue v = e.meta.value(key);
    if (v.isUndefined() || v.isNull()) return QString();
    if (v.isBool()) return v.toBool() ? QStringLiteral("Yes") : QStringLiteral("No");
    if (v.isDouble()) return num(v.toDouble());
    return v.toString();
}

} // namespace

std::vector<QuickSelectProp> quickSelectGeneralProps() {
    return {field("layer", "Layer"),
            field("color", "Colour"),
            field("weight", "Lineweight", true),
            metaProp("linetype", "Linetype")};
}

std::vector<QuickSelectProp> quickSelectProps(const Entity& e) {
    std::vector<QuickSelectProp> props = quickSelectGeneralProps();
    auto add = [&props](const QuickSelectProp& p) { props.push_back(p); };

    // Geometry / technical fields, per type — only what the object actually has.
    switch (e.type) {
    case EntityType::Line:
        add(field("length", "Length", true));
        add(field("angle", "Angle", true));
        add(field("startx", "Start X", true));
        add(field("starty", "Start Y", true));
        break;
    case EntityType::DoubleLine:
        add(field("length", "Length", true));
        add(field("angle", "Angle", true));
        add(metaProp("gap", "Gap", true));
        break;
    case EntityType::Polyline:
    case EntityType::Spline:
        add(field("length", "Length", true));
        add(field("vertices", "Vertices", true));
        if (e.meta.value("double").toBool()) add(metaProp("gap", "Gap", true));
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
        add(field("width", "Width", true));
        add(field("height", "Height", true));
        add(field("area", "Area", true));
        add(field("length", "Perimeter", true));
        add(field("vertices", "Vertices", true));
        break;
    case EntityType::Circle:
        add(field("radius", "Radius", true));
        add(field("diameter", "Diameter", true));
        add(field("area", "Area", true));
        add(field("centerx", "Centre X", true));
        add(field("centery", "Centre Y", true));
        break;
    case EntityType::Arc:
        add(field("radius", "Radius", true));
        add(field("length", "Arc length", true));
        add(field("sweep", "Sweep angle", true));
        break;
    case EntityType::Ellipse:
        add(field("radius", "X radius", true));
        add(field("radiusy", "Y radius", true));
        add(field("rotation", "Rotation", true));
        break;
    case EntityType::Text:
        add(metaProp("text", "Contents"));
        add(metaProp("height", "Text height", true));
        add(metaProp("font", "Font"));
        add(metaProp("justify", "Justification"));
        break;
    case EntityType::Dimension:
        add(metaProp("dimType", "Dimension type"));
        add(field("length", "Measurement", true));
        add(metaProp("arrowSize", "Arrow size", true));
        break;
    case EntityType::Hatch:
    case EntityType::Wipeout:
        add(metaProp("pattern", "Pattern"));
        add(metaProp("angle", "Pattern angle", true));
        add(metaProp("scale", "Pattern scale", true));
        add(metaProp("transparency", "Transparency", true));
        add(field("area", "Area", true));
        break;
    case EntityType::Wire:
        add(metaProp("wireColor", "Wire colour"));
        add(metaProp("wireGauge", "Cross-section"));
        add(metaProp("awg", "AWG"));
        add(metaProp("netName", "Net"));
        add(metaProp("insulation", "Insulation"));
        add(metaProp("insulationVoltage", "Voltage"));
        add(metaProp("conductorType", "Conductor"));
        add(metaProp("resistance", "Resistance", true));
        add(field("length", "Length", true));
        add(metaProp("entNum", "Entity number", true));
        break;
    case EntityType::Connector:
        add(metaProp("designator", "Designator"));
        add(metaProp("partNumber", "Part number"));
        add(metaProp("pinCount", "Pin count", true));
        add(metaProp("pitch", "Pin pitch", true));
        add(metaProp("annotation", "Pin annotation"));
        add(metaProp("pinOrder", "Pin order"));
        add(metaProp("rectMode", "Body mode"));
        add(metaProp("entNum", "Entity number", true));
        break;
    case EntityType::Splice:
    case EntityType::NotConnected:
        add(metaProp("size", "Size", true));
        break;
    case EntityType::NetLabel:
        add(metaProp("netName", "Net"));
        add(metaProp("textSize", "Text size", true));
        break;
    case EntityType::Twist:
        add(metaProp("twistType", "Twist type"));
        add(metaProp("repeats", "Twists", true));
        add(metaProp("diagAngle", "Diagonal angle", true));
        add(metaProp("capLen", "Cap length", true));
        add(metaProp("flatLen", "Flat length", true));
        add(metaProp("wireGauge", "Cross-section"));
        break;
    case EntityType::Screen:
        add(metaProp("rx", "Small radius", true));
        add(metaProp("ry", "Large radius", true));
        add(metaProp("shape", "Shape"));
        add(metaProp("legDir", "Drain leg side"));
        add(metaProp("showS", "Shows \"S\""));
        add(metaProp("notConnected", "Leg not connected"));
        break;
    default:
        break;
    }
    // A table's grid size, and any balloon's number, wherever they are stored.
    if (e.meta.contains(QStringLiteral("cols"))) add(metaProp("cols", "Columns", true));
    if (e.meta.contains(QStringLiteral("rows"))) add(metaProp("rows", "Rows", true));
    if (e.meta.contains(QStringLiteral("balloon"))) add(metaProp("balloon", "Balloon number"));
    return props;
}

QString quickSelectValue(const Entity& e, const QString& key) {
    if (key.isEmpty()) return QString();
    if (key.startsWith(QLatin1String("meta:"))) return metaText(e, key.mid(5));

    if (key == QLatin1String("layer")) return e.layer;
    // An empty colour is ByLayer — name it, so the dialog offers something meaningful.
    if (key == QLatin1String("color"))
        return e.color.isEmpty() ? QStringLiteral("ByLayer") : e.color;
    if (key == QLatin1String("weight"))
        return e.weight <= 0.0 ? QStringLiteral("ByLayer") : num(e.weight);
    if (key == QLatin1String("length")) return num(runLength(e));
    if (key == QLatin1String("angle"))
        return num(std::atan2(e.b.y - e.a.y, e.b.x - e.a.x) * 180.0 / kQsPi);
    if (key == QLatin1String("startx")) return num(e.a.x);
    if (key == QLatin1String("starty")) return num(e.a.y);
    if (key == QLatin1String("radius")) return num(e.radius);
    if (key == QLatin1String("radiusy")) return num(e.radiusY);
    if (key == QLatin1String("diameter")) return num(e.radius * 2.0);
    if (key == QLatin1String("rotation")) return num(e.rotation * 180.0 / kQsPi);
    if (key == QLatin1String("centerx")) return num(e.center.x);
    if (key == QLatin1String("centery")) return num(e.center.y);
    if (key == QLatin1String("vertices")) return num(static_cast<double>(e.points.size()));
    if (key == QLatin1String("sweep")) {
        double sweep = (e.endAngle - e.startAngle) * 180.0 / kQsPi;
        while (sweep < 0.0) sweep += 360.0;
        return num(sweep);
    }
    if (key == QLatin1String("width") || key == QLatin1String("height")) {
        const Bounds b = e.bounds();
        if (!b.valid) return QStringLiteral("0");
        return num(key == QLatin1String("width") ? b.width() : b.height());
    }
    if (key == QLatin1String("area")) {
        if (e.type == EntityType::Circle) return num(kQsPi * e.radius * e.radius);
        // Shoelace over the outline; a bounding box for anything without one.
        if (e.points.size() >= 3) {
            double acc = 0.0;
            for (size_t i = 0; i < e.points.size(); ++i) {
                const Vec2& p1 = e.points[i];
                const Vec2& p2 = e.points[(i + 1) % e.points.size()];
                acc += p1.x * p2.y - p2.x * p1.y;
            }
            return num(std::abs(acc) * 0.5);
        }
        const Bounds b = e.bounds();
        return num(b.valid ? b.width() * b.height() : 0.0);
    }
    return QString();
}

bool quickSelectMatches(const Entity& e, const QuickSelectQuery& q) {
    if (!q.type.isEmpty() && entityTypeToString(e.type) != q.type) return false;
    if (q.op == QsOp::All || q.key.isEmpty()) return true;

    const QString have = quickSelectValue(e, q.key);
    const QString want = q.value;
    // Numeric comparison whenever both sides read as numbers, so "> 5" works on a
    // radius but "= WH" still works on a wire colour.
    bool haveOk = false, wantOk = false;
    const double hv = have.toDouble(&haveOk);
    const double wv = want.toDouble(&wantOk);
    const bool numeric = haveOk && wantOk;

    switch (q.op) {
    case QsOp::Equal:
        return numeric ? std::abs(hv - wv) < 1e-9
                       : have.compare(want, Qt::CaseInsensitive) == 0;
    case QsOp::NotEqual:
        return numeric ? std::abs(hv - wv) >= 1e-9
                       : have.compare(want, Qt::CaseInsensitive) != 0;
    case QsOp::Greater:
        return numeric ? hv > wv : have.compare(want, Qt::CaseInsensitive) > 0;
    case QsOp::Less:
        return numeric ? hv < wv : have.compare(want, Qt::CaseInsensitive) < 0;
    case QsOp::GreaterEqual:
        return numeric ? hv >= wv : have.compare(want, Qt::CaseInsensitive) >= 0;
    case QsOp::LessEqual:
        return numeric ? hv <= wv : have.compare(want, Qt::CaseInsensitive) <= 0;
    case QsOp::Contains:
        return have.contains(want, Qt::CaseInsensitive);
    case QsOp::All:
        break;
    }
    return true;
}

std::vector<QString> quickSelectMatchIds(const std::vector<Entity>& pool,
                                         const QuickSelectQuery& q,
                                         const std::vector<QString>& currentSelection) {
    // "Current selection" narrows the pool to what is already picked.
    QSet<QString> inSelection;
    for (const QString& id : currentSelection) inSelection.insert(id);

    std::vector<QString> out;
    QSet<QString> taken;
    if (q.append)
        for (const QString& id : currentSelection)
            if (!taken.contains(id)) { out.push_back(id); taken.insert(id); }

    for (const Entity& e : pool) {
        if (!q.wholeDrawing && !inSelection.contains(e.id)) continue;
        // Exclude inverts the property test, but the type filter still decides which
        // objects the query is about at all (AutoCAD behaves the same way).
        if (!q.type.isEmpty() && entityTypeToString(e.type) != q.type) continue;
        QuickSelectQuery inner = q;
        inner.type.clear();
        const bool hit = quickSelectMatches(e, inner);
        if (hit == q.exclude) continue;
        if (taken.contains(e.id)) continue;
        out.push_back(e.id);
        taken.insert(e.id);
    }
    return out;
}

std::vector<QString> quickSelectDistinctValues(const std::vector<Entity>& pool,
                                               const QString& key) {
    std::vector<QString> out;
    QSet<QString> seen;
    bool allNumeric = true;
    for (const Entity& e : pool) {
        const QString v = quickSelectValue(e, key);
        if (v.isEmpty() || seen.contains(v)) continue;
        seen.insert(v);
        out.push_back(v);
        bool ok = false;
        v.toDouble(&ok);
        if (!ok) allNumeric = false;
    }
    if (allNumeric)
        std::sort(out.begin(), out.end(),
                  [](const QString& a, const QString& b) { return a.toDouble() < b.toDouble(); });
    else
        std::sort(out.begin(), out.end());
    return out;
}

} // namespace Loopwire
