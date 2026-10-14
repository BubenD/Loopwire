#include "system/SystemExport.h"
#include "system/SystemImport.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QRectF>

#include <algorithm>
#include <array>
#include <cmath>

#include <limits>

namespace Loopwire {

namespace {

constexpr double kLead = 22.0;

QString esc(const QString& s) { return s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("&#xa;")); }

QString num(double v) { return QString::number(v, 'f', 4); }

QString colour(const QColor& c, const QColor& otherwise) { return (c.isValid() ? c : otherwise).name().toUpper(); }

QString unitLabel(const SysUnit& u) {
    QString kind = u.kind == QLatin1String("pcba") ? QStringLiteral("[PCBA]") : u.kind==QLatin1String("module") ? QStringLiteral("[System module]")
                 : u.kind == QLatin1String("device") ? QStringLiteral("[Device]") : QStringLiteral("[Mechanical]");
    if (u.shows(QStringLiteral("desc")) && !u.description.isEmpty()) kind += QLatin1Char(' ') + u.description;
    QStringList lines{kind};
    if (u.shows(QStringLiteral("name"))) lines << u.name;
    QStringList sub;
    if (!u.pn.isEmpty() && u.shows(QStringLiteral("pn"))) sub << u.pn;
    if (!u.rev.isEmpty() && u.shows(QStringLiteral("rev"))) sub << QStringLiteral("rev ") + u.rev;
    if (!sub.isEmpty()) lines << sub.join(QStringLiteral("  "));
    return lines.join(QLatin1Char('\n'));
}

QString cableLabel(const SysCable& c) {
    QStringList parts;
    if (c.shows(QStringLiteral("name")) && !c.name.isEmpty()) parts << c.name;
    if (c.shows(QStringLiteral("pn")) && !c.pn.isEmpty()) parts << c.pn;
    if (c.shows(QStringLiteral("rev")) && !c.rev.isEmpty()) parts << QStringLiteral("rev ") + c.rev;
    if (c.shows(QStringLiteral("desc")) && !c.description.isEmpty()) parts << c.description;
    return parts.join(QStringLiteral("  "));
}

QString portLabel(const SysPort& p) {
    QString label = p.name;
    if (!p.connector.isEmpty()) label += QStringLiteral(" (") + p.connector + QLatin1Char(')');
    return label;
}

QRectF bounds(const SystemDesign& d) {
    QRectF box;
    for (const SysUnit& u : d.units) {
        box = box.united(u.rect());
        for (const SysPort& p : u.ports) box = box.united(sysPortLabelRect(u, p));
    }
    for (const SysNote& n : d.notes) box = box.united(QRectF(n.x, n.y, n.w, n.h));
    for (const SysCable& c : d.cables)
        for (const QPointF& p : sysCablePath(d, c)) box = box.united(QRectF(p, QSizeF(1, 1)));
    if (box.isEmpty()) box = QRectF(0, 0, 800, 600);
    return box.adjusted(-40, -40, 40, 40);
}

bool writeFile(const QString& path, const QByteArray& bytes, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        if (error) *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    return true;
}

// ---- A ZIP, its entries stored as they are (no compression) - as an OPC package (a .vsdx) may be ----

quint32 crc32(const QByteArray& data) {
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (const char ch : data) c = table[(c ^ static_cast<quint8>(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct ZipEntry {
    QString name;
    QByteArray data;
};

QByteArray zipStored(const std::vector<ZipEntry>& entries) {
    QByteArray out, central;
    auto u16 = [](QByteArray& b, quint16 v) {
        b.append(static_cast<char>(v & 0xFF));
        b.append(static_cast<char>(v >> 8));
    };
    auto u32 = [&](QByteArray& b, quint32 v) {
        u16(b, static_cast<quint16>(v & 0xFFFF));
        u16(b, static_cast<quint16>(v >> 16));
    };
    // The time written: 1 January 2026, 00:00 (DOS form) - the same every time, so the same diagram gives the same file.
    const quint16 time = 0, date = static_cast<quint16>(((2026 - 1980) << 9) | (1 << 5) | 1);
    for (const ZipEntry& e : entries) {
        const QByteArray name = e.name.toUtf8();
        const quint32 crc = crc32(e.data), size = static_cast<quint32>(e.data.size()), at = static_cast<quint32>(out.size());
        u32(out, 0x04034b50);
        u16(out, 20);
        u16(out, 0x0800);   // the name in UTF-8
        u16(out, 0);        // stored
        u16(out, time);
        u16(out, date);
        u32(out, crc);
        u32(out, size);
        u32(out, size);
        u16(out, static_cast<quint16>(name.size()));
        u16(out, 0);
        out += name;
        out += e.data;
        u32(central, 0x02014b50);
        u16(central, 20);
        u16(central, 20);
        u16(central, 0x0800);
        u16(central, 0);
        u16(central, time);
        u16(central, date);
        u32(central, crc);
        u32(central, size);
        u32(central, size);
        u16(central, static_cast<quint16>(name.size()));
        u16(central, 0);
        u16(central, 0);
        u16(central, 0);
        u16(central, 0);
        u32(central, 0);
        u32(central, at);
        central += name;
    }
    const quint32 centralAt = static_cast<quint32>(out.size());
    out += central;
    u32(out, 0x06054b50);
    u16(out, 0);
    u16(out, 0);
    u16(out, static_cast<quint16>(entries.size()));
    u16(out, static_cast<quint16>(entries.size()));
    u32(out, static_cast<quint32>(central.size()));
    u32(out, centralAt);
    u16(out, 0);
    return out;
}

} // namespace

QByteArray writeZip(const std::vector<ZipItem>& items) {
    std::vector<ZipEntry> entries;
    entries.reserve(items.size());
    for (const ZipItem& item : items) entries.push_back({item.name, item.data});
    return zipStored(entries);
}

// ---- Where things are -----------------------------------------------------------------------------

QPointF sysPortPoint(const SysUnit& u, const SysPort& p) {
    if (p.side == QLatin1String("left")) return {u.x, u.y + u.h * p.at};
    if (p.side == QLatin1String("top")) return {u.x + u.w * p.at, u.y};
    if (p.side == QLatin1String("bottom")) return {u.x + u.w * p.at, u.y + u.h};
    return {u.x + u.w, u.y + u.h * p.at};
}

QPointF sysPortOut(const SysPort& p) {
    if (p.side == QLatin1String("left")) return {-1, 0};
    if (p.side == QLatin1String("top")) return {0, -1};
    if (p.side == QLatin1String("bottom")) return {0, 1};
    return {1, 0};
}

QRectF sysPortLabelRect(const SysUnit& u, const SysPort& p) {
    const QPointF at = sysPortPoint(u, p) + sysPortOut(p) * 28.0 + QPointF(p.labelDx, p.labelDy);
    return QRectF(at.x() - 35.0, at.y() - 24.0, 70.0, 18.0);
}

bool sysNoteTarget(const SystemDesign& d, const SysNote& n, QPointF& target) {
    if (!n.cable.isEmpty()) {
        const SysCable* c = d.cable(n.cable);
        if (!c) return false;
        const auto path = sysCablePath(d, *c);
        if (path.size() < 2) return false;
        target = (path[path.size() / 2 - 1] + path[path.size() / 2]) / 2.0;
        return true;
    }
    const SysUnit* u = d.unit(n.unit);
    if (!u) return false;
    if (n.port.isEmpty()) {
        const QPointF centre = u->rect().center();
        QPointF direction = QRectF(n.x, n.y, n.w, n.h).center() - centre;
        if (direction.manhattanLength() < 1e-9) direction = QPointF(0, 1);
        const double ratio = std::max(std::abs(direction.x()) / std::max(.01, u->w/2),
                                      std::abs(direction.y()) / std::max(.01, u->h/2));
        target = centre + direction / ratio;
        return true;
    }
    const SysPort* p = u->port(n.port);
    if (!p) return false;
    target = sysPortPoint(*u, *p);
    return true;
}

std::vector<QPointF> sysCableWaypoints(const SystemDesign& d, const SysCable& c, double end) {
    std::vector<QPointF> w = c.waypoints;
    if (w.empty()) return w;
    auto keepAhead = [&](const QString& unitId, const QString& portId, bool inside, bool first) {
        const SysUnit* u = d.unit(unitId);
        const SysPort* p = u ? u->port(portId) : nullptr;
        if (!p) return;
        const QPointF dir = sysPortOut(*p) * (inside ? -1.0 : 1.0);
        const QPointF lead = sysPortPoint(*u, *p) + dir * (end + kLead);
        const QPointF bend = first ? w.front() : w.back();
        const double behind = (bend.x() - lead.x()) * dir.x() + (bend.y() - lead.y()) * dir.y();
        if (behind >= 0.0) return;
        // The bend, and those level with it across the port's way (the dragged leg), brought up to the lead together.
        const double level = bend.x() * dir.x() + bend.y() * dir.y();
        for (QPointF& q : w)
            if (std::abs(q.x() * dir.x() + q.y() * dir.y() - level) < 0.01) q -= dir * behind;
    };
    keepAhead(c.aUnit, c.aPort, c.aInside, true);
    keepAhead(c.bUnit, c.bPort, c.bInside, false);
    return w;
}

std::vector<QPointF> sysBranchPath(const SystemDesign& d, const SysCable& c, const SysBranch& b, double end) {
    const std::vector<QPointF> trunk = sysCablePath(d, c, end);
    const SysUnit* u = d.unit(b.unit);
    const SysPort* p = u ? u->port(b.port) : nullptr;
    // A LOOSE END: no port - it ends where it lies, straight off the trunk's nearest point and round one corner.
    const bool loose = b.unit.isEmpty();
    if (trunk.size() < 2 || (!p && !loose)) return {};
    QPointF dir = p ? sysPortOut(*p) * (b.inside ? -1.0 : 1.0) : QPointF(1, 0);
    const QPointF at = p ? sysPortPoint(*u, *p) + dir * end : b.at, lead = p ? at + dir * kLead : b.at;
    // THE SPLIT: the point of the trunk nearest the branch's lead - its port leads at either end left out - so the branch
    // is short and leaves the trunk clear of its label.
    QPointF split = (trunk[trunk.size() / 2 - 1] + trunk[trunk.size() / 2]) / 2.0;
    double nearest = std::numeric_limits<double>::infinity();
    const size_t first = trunk.size() > 3 ? 1 : 0, last = trunk.size() > 3 ? trunk.size() - 2 : trunk.size() - 1;
    for (size_t i = first; i < last; ++i) {
        const QPointF a = trunk[i], e = trunk[i + 1] - a;
        const double ll = e.x() * e.x() + e.y() * e.y();
        const double t = ll < 1e-12 ? 0.0 : std::clamp(((lead.x() - a.x()) * e.x() + (lead.y() - a.y()) * e.y()) / ll, 0.0, 1.0);
        const QPointF q = a + e * t;
        const double gap = std::abs(q.x() - lead.x()) + std::abs(q.y() - lead.y());
        if (gap < nearest) { nearest = gap; split = q; }
    }
    if (loose) {
        const QPointF off = at - split;
        dir = std::abs(off.x()) >= std::abs(off.y()) ? QPointF(off.x() >= 0 ? 1 : -1, 0) : QPointF(0, off.y() >= 0 ? 1 : -1);
    }
    // OUT OF THE PORT along its way, then across to the split: one corner (none where it lies straight ahead).
    const QPointF corner = std::abs(dir.x()) > 0.5 ? QPointF(lead.x(), split.y()) : QPointF(split.x(), lead.y());
    std::vector<QPointF> path{split};
    for (const QPointF& q : {corner, lead, at})
        if (std::abs(q.x() - path.back().x()) + std::abs(q.y() - path.back().y()) > 0.01) path.push_back(q);
    if (path.size() < 2) path.push_back(at);
    return path;
}

std::vector<QPointF> sysCableBasePath(const SystemDesign& d, const SysCable& c, double end) {
    const SysUnit* ua = d.unit(c.aUnit);
    const SysUnit* ub = d.unit(c.bUnit);
    const SysPort* pa = ua ? ua->port(c.aPort) : nullptr;
    const SysPort* pb = ub ? ub->port(c.bPort) : nullptr;
    // A LOOSE END (no unit) ends where it lies; any other end not on its port and the cable is not drawn.
    const bool looseA = c.aUnit.isEmpty(), looseB = c.bUnit.isEmpty();
    if ((!pa && !looseA) || (!pb && !looseB)) return {};
    // SQUARE: straight out of each port - out of its outside, or in from its inside - then across and along to meet. A
    // loose end leaves towards the other end, the way it is further.
    const QPointF a = pa ? sysPortPoint(*ua, *pa) + sysPortOut(*pa) * (c.aInside ? -1.0 : 1.0) * end : c.aAt;
    const QPointF b = pb ? sysPortPoint(*ub, *pb) + sysPortOut(*pb) * (c.bInside ? -1.0 : 1.0) * end : c.bAt;
    auto toward = [](const QPointF& from, const QPointF& to) {
        const QPointF off = to - from;
        return std::abs(off.x()) >= std::abs(off.y()) ? QPointF(off.x() >= 0 ? 1 : -1, 0) : QPointF(0, off.y() >= 0 ? 1 : -1);
    };
    const QPointF da = pa ? sysPortOut(*pa) * (c.aInside ? -1.0 : 1.0) : toward(a, b);
    const QPointF db = pb ? sysPortOut(*pb) * (c.bInside ? -1.0 : 1.0) : toward(b, a);
    const QPointF a1 = a + da * kLead, b1 = b + db * kLead;
    std::vector<QPointF> path{a, a1};
    if(!c.waypoints.empty()){
        QPointF current=a1;std::vector<QPointF> goals=sysCableWaypoints(d,c,end);goals.push_back(b1);
        for(const QPointF& goal:goals){if(std::abs(current.x()-goal.x())>.001&&std::abs(current.y()-goal.y())>.001)path.push_back({goal.x(),current.y()});path.push_back(goal);current=goal;}
        path.push_back(b);return path;
    }
    if (std::abs(da.x()) > 0.5 && std::abs(db.x()) > 0.5) {
        const double mx = (a1.x() + b1.x()) / 2.0;
        path.push_back({mx, a1.y()});
        path.push_back({mx, b1.y()});
    } else if (std::abs(da.y()) > 0.5 && std::abs(db.y()) > 0.5) {
        const double my = (a1.y() + b1.y()) / 2.0;
        path.push_back({a1.x(), my});
        path.push_back({b1.x(), my});
    } else if (std::abs(da.x()) > 0.5) {
        path.push_back({b1.x(), a1.y()});
    } else {
        path.push_back({a1.x(), b1.y()});
    }
    path.push_back(b1);
    path.push_back(b);
    return path;
}

// ---- A file of its own ----------------------------------------------------------------------------

bool saveSystemFile(const SystemDesign& d, const QString& path, QString* error) {
    QJsonObject o{{QStringLiteral("format"), QStringLiteral("edraw-system-design")}, {QStringLiteral("version"), 1},
                  {QStringLiteral("design"), d.toJson()}};
    return writeFile(path, QJsonDocument(o).toJson(QJsonDocument::Indented), error);
}

bool loadSystemFile(const QString& path, SystemDesign& d, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QStringLiteral("format")).toString() != QLatin1String("edraw-system-design")) {
        if (error) *error = QStringLiteral("%1 is not a system design").arg(path);
        return false;
    }
    d = SystemDesign::fromJson(o.value(QStringLiteral("design")).toObject());
    return true;
}

// ---- draw.io --------------------------------------------------------------------------------------

QString systemDrawioXml(const SystemDesign& d) {
    const QRectF box = bounds(d);
    QString x;
    x += QStringLiteral("<mxfile host=\"Loopwire\" type=\"device\">\n<diagram id=\"system\" name=\"%1\">\n")
             .arg(esc(d.name.isEmpty() ? QStringLiteral("System") : d.name));
    x += QStringLiteral("<mxGraphModel dx=\"1000\" dy=\"800\" grid=\"1\" gridSize=\"10\" guides=\"1\" tooltips=\"1\" connect=\"1\" "
                        "arrows=\"1\" fold=\"1\" page=\"1\" pageScale=\"1\" pageWidth=\"%1\" pageHeight=\"%2\" math=\"0\" shadow=\"0\">\n"
                        "<root>\n<mxCell id=\"0\"/>\n<mxCell id=\"1\" parent=\"0\"/>\n")
             .arg(std::ceil(box.right())).arg(std::ceil(box.bottom()));
    auto vertex = [&](const QString& id, const QString& value, const QString& style, double vx, double vy, double vw, double vh) {
        x += QStringLiteral("<mxCell id=\"%1\" value=\"%2\" style=\"%3\" vertex=\"1\" parent=\"1\">"
                            "<mxGeometry x=\"%4\" y=\"%5\" width=\"%6\" height=\"%7\" as=\"geometry\"/></mxCell>\n")
                 .arg(esc(id), esc(value), esc(style), num(vx), num(vy), num(vw), num(vh));
    };
    // THE UNITS, the biggest first - a unit inside another over it.
    std::vector<const SysUnit*> order;
    for (const SysUnit& u : d.units) order.push_back(&u);
    std::stable_sort(order.begin(), order.end(), [&d](const SysUnit* a, const SysUnit* b) { return d.depth(a->id) < d.depth(b->id); });
    for (const SysUnit* u : order) {
        const bool pcba = u->kind == QLatin1String("pcba");
        vertex(u->id, unitLabel(*u),
               QStringLiteral("rounded=1;arcSize=4;whiteSpace=wrap;html=0;verticalAlign=top;spacingTop=6;fontStyle=1;fillColor=%1;"
                              "fillOpacity=%2;strokeColor=#5A626E;%3")
                   .arg(colour(u->color, pcba ? QColor(226, 242, 230) : u->kind == QLatin1String("device") ? QColor(246, 236, 214) : QColor(232, 238, 246)))
                   .arg(std::lround(u->opacity * 100.0)).arg(u->kind==QLatin1String("module")?QStringLiteral("dashed=1;"):QString()),
               u->x, u->y, u->w, u->h);
    }
    // THE PORTS: a small square on the side, named; the cables go from and to them.
    for (const SysUnit& u : d.units)
        for (const SysPort& p : u.ports) {
            const QPointF at = sysPortPoint(u, p);
            const QPointF out = sysPortOut(p);
            const QString side = QStringLiteral("verticalLabelPosition=top;verticalAlign=bottom;align=center;spacingTop=%1;spacingLeft=%2;")
                                     .arg(num(6.0 - out.y() * 28.0 - p.labelDy)).arg(num(out.x() * 28.0 + p.labelDx));
            vertex(QStringLiteral("%1.%2").arg(u.id, p.id), portLabel(p),
                   QStringLiteral("rounded=0;fillColor=#FFFFFF;strokeColor=#3C424C;fontSize=9;%1").arg(side), at.x() - 4, at.y() - 4, 8, 8);
        }
    // THE CABLES: from port to port, through the corners they are drawn with, labelled.
    for (const SysCable& c : d.cables) {
        const std::vector<QPointF> path = sysCablePath(d, c);
        if (path.size() < 2) continue;
        x += QStringLiteral("<mxCell id=\"%1\" value=\"%2\" style=\"edgeStyle=none;rounded=0;endArrow=none;startArrow=none;html=0;jumpStyle=arc;jumpSize=10;"
                            "strokeWidth=2;strokeColor=%3;labelBackgroundColor=#FFFFFF;fontSize=10;\" edge=\"1\" parent=\"1\" "
                            "%4%5><mxGeometry relative=\"1\" as=\"geometry\">")
                 .arg(esc(c.id), esc(cableLabel(c)), colour(c.color, QColor(60, 66, 76)),
                      // A LOOSE END on no port: no source (or target) - the line ends at a point of its own.
                      c.aUnit.isEmpty() ? QString() : QStringLiteral("source=\"%1.%2\" ").arg(esc(c.aUnit), esc(c.aPort)),
                      c.bUnit.isEmpty() ? QString() : QStringLiteral("target=\"%1.%2\"").arg(esc(c.bUnit), esc(c.bPort)));
        if (c.aUnit.isEmpty()) x += QStringLiteral("<mxPoint x=\"%1\" y=\"%2\" as=\"sourcePoint\"/>").arg(num(path.front().x()), num(path.front().y()));
        if (c.bUnit.isEmpty()) x += QStringLiteral("<mxPoint x=\"%1\" y=\"%2\" as=\"targetPoint\"/>").arg(num(path.back().x()), num(path.back().y()));
        x += QStringLiteral("<Array as=\"points\">");
        for (size_t i = 0; i < path.size(); ++i) x += QStringLiteral("<mxPoint x=\"%1\" y=\"%2\"/>").arg(num(path[i].x()), num(path[i].y()));
        x += QStringLiteral("</Array></mxGeometry></mxCell>\n");
        for (size_t k = 0; k < c.branches.size(); ++k) {
            const std::vector<QPointF> limb = sysBranchPath(d, c, c.branches[k]);
            if (limb.size() < 2) continue;
            x += QStringLiteral("<mxCell id=\"%1-b%2\" style=\"edgeStyle=none;rounded=0;endArrow=none;startArrow=none;jumpStyle=arc;jumpSize=10;"
                                "strokeWidth=2;strokeColor=%3;\" edge=\"1\" parent=\"1\" %4><mxGeometry relative=\"1\" as=\"geometry\">"
                                "<mxPoint x=\"%5\" y=\"%6\" as=\"sourcePoint\"/>%7<Array as=\"points\">")
                     .arg(esc(c.id)).arg(k + 1).arg(colour(c.color, QColor(60, 66, 76)),
                          c.branches[k].unit.isEmpty() ? QString()
                                                       : QStringLiteral("target=\"%1.%2\"").arg(esc(c.branches[k].unit), esc(c.branches[k].port)),
                          num(limb.front().x()), num(limb.front().y()),
                          c.branches[k].unit.isEmpty() ? QStringLiteral("<mxPoint x=\"%1\" y=\"%2\" as=\"targetPoint\"/>").arg(num(limb.back().x()), num(limb.back().y()))
                                                       : QString());
            for (size_t i = 1; i < limb.size(); ++i) x += QStringLiteral("<mxPoint x=\"%1\" y=\"%2\"/>").arg(num(limb[i].x()), num(limb[i].y()));
            x += QStringLiteral("</Array></mxGeometry></mxCell>\n");
        }
    }
    // THE NOTES.
    for (const SysNote& n : d.notes)
        vertex(n.id, n.text,
               QStringLiteral("shape=note;size=12;whiteSpace=wrap;html=0;align=left;verticalAlign=top;spacingLeft=6;spacingTop=4;"
                              "fillColor=%1;strokeColor=#C8AA3C;").arg(colour(n.color, QColor(255, 247, 190))),
               n.x, n.y, n.w, n.h);
    for (const SysNote& n : d.notes) {
        QPointF target;
        if (!n.showLink || !sysNoteTarget(d, n, target)) continue;
        const QString targetId = !n.cable.isEmpty() ? n.cable : !n.port.isEmpty() ? n.unit + QLatin1Char('.') + n.port : n.unit;
        x += QStringLiteral("<mxCell id=\"%1-leader\" style=\"dashed=1;endArrow=none;strokeColor=#7D828C;\" edge=\"1\" parent=\"1\" source=\"%1\" target=\"%2\"><mxGeometry relative=\"1\" as=\"geometry\"/></mxCell>\n").arg(esc(n.id), esc(targetId));
    }
    x += QStringLiteral("</root>\n</mxGraphModel>\n</diagram>\n</mxfile>\n");
    return x;
}

bool exportSystemDrawio(const SystemDesign& d, const QString& path, QString* error) {
    return writeFile(path, systemDrawioXml(d).toUtf8(), error);
}

// ---- Visio ----------------------------------------------------------------------------------------

bool exportSystemVsdx(const SystemDesign& d, const QString& path, QString* error) {
    const QRectF box = bounds(d);
    // A HUNDRED OF THE DIAGRAM'S UNITS TO THE INCH; Visio's up is the diagram's down.
    const double perInch = 100.0;
    const double pageW = box.width() / perInch, pageH = box.height() / perInch;
    auto vx = [&](double v) { return (v - box.left()) / perInch; };
    auto vy = [&](double v) { return (box.bottom() - v) / perInch; };
    const QString ns = QStringLiteral("http://schemas.microsoft.com/office/visio/2012/main");
    const QString rns = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    QString shapes;
    int id = 0;
    auto cell = [](const QString& n, const QString& v) { return QStringLiteral("<Cell N=\"%1\" V=\"%2\"/>").arg(n, v); };
    // A BOX: its middle, size, fill and line, a rectangle round it, and its text.
    auto rect = [&](double x0, double y0, double w, double h, const QString& fill, const QString& line, const QString& text,
                    double fontPt, bool top) {
        const double W = w / perInch, H = h / perInch;
        shapes += QStringLiteral("<Shape ID=\"%1\" Type=\"Shape\" LineStyle=\"0\" FillStyle=\"0\" TextStyle=\"0\">").arg(++id);
        shapes += cell(QStringLiteral("PinX"), num(vx(x0) + W / 2)) + cell(QStringLiteral("PinY"), num(vy(y0) - H / 2)) +
                  cell(QStringLiteral("Width"), num(W)) + cell(QStringLiteral("Height"), num(H)) +
                  cell(QStringLiteral("LocPinX"), num(W / 2)) + cell(QStringLiteral("LocPinY"), num(H / 2)) +
                  cell(QStringLiteral("FillForegnd"), fill) + cell(QStringLiteral("FillPattern"), QStringLiteral("1")) +
                  cell(QStringLiteral("LineColor"), line) + cell(QStringLiteral("LineWeight"), QStringLiteral("0.01")) +
                  cell(QStringLiteral("VerticalAlign"), top ? QStringLiteral("0") : QStringLiteral("1"));
        shapes += QStringLiteral("<Section N=\"Character\"><Row IX=\"0\">%1</Row></Section>")
                      .arg(cell(QStringLiteral("Size"), num(fontPt / 72.0)));
        shapes += QStringLiteral("<Section N=\"Geometry\" IX=\"0\">") + cell(QStringLiteral("NoFill"), QStringLiteral("0")) +
                  cell(QStringLiteral("NoLine"), QStringLiteral("0"));
        const double pts[5][2] = {{0, 0}, {W, 0}, {W, H}, {0, H}, {0, 0}};
        for (int i = 0; i < 5; ++i)
            shapes += QStringLiteral("<Row T=\"%1\" IX=\"%2\">%3%4</Row>")
                          .arg(i == 0 ? QStringLiteral("MoveTo") : QStringLiteral("LineTo"))
                          .arg(i + 1)
                          .arg(cell(QStringLiteral("X"), num(pts[i][0])), cell(QStringLiteral("Y"), num(pts[i][1])));
        shapes += QStringLiteral("</Section>");
        if (!text.isEmpty()) shapes += QStringLiteral("<Text>%1</Text>").arg(text.toHtmlEscaped());
        shapes += QStringLiteral("</Shape>");
    };
    std::vector<const SysUnit*> order;
    for (const SysUnit& u : d.units) order.push_back(&u);
    std::stable_sort(order.begin(), order.end(), [&d](const SysUnit* a, const SysUnit* b) { return d.depth(a->id) < d.depth(b->id); });
    for (const SysUnit* u : order) {
        rect(u->x, u->y, u->w, u->h, colour(u->color, u->kind == QLatin1String("pcba") ? QColor(226, 242, 230)
                                                     : u->kind == QLatin1String("device") ? QColor(246, 236, 214) : QColor(232, 238, 246)),
             QStringLiteral("#5A626E"), unitLabel(*u), 9.0, true);
        const QString marker=QStringLiteral("<Section N=\"Geometry\" IX=\"0\">");
        const qsizetype at=shapes.lastIndexOf(marker);
        if(at>=0)shapes.insert(at,cell(QStringLiteral("FillForegndTrans"),num(1-u->opacity))+
                               (u->kind==QLatin1String("module")?cell(QStringLiteral("LinePattern"),QStringLiteral("2")):QString()));
    }
    for (const SysUnit& u : d.units)
        for (const SysPort& p : u.ports) {
            const QPointF at = sysPortPoint(u, p);
            rect(at.x() - 4, at.y() - 4, 8, 8, QStringLiteral("#FFFFFF"), QStringLiteral("#3C424C"), QString(), 7.0, false);
            // Its name just inside the unit.
            const QRectF label = sysPortLabelRect(u, p);
            rect(label.x(), label.y(), label.width(), label.height(), QStringLiteral("#FFFFFF"), QStringLiteral("#FFFFFF"), portLabel(p), 7.0, false);
            shapes.replace(shapes.lastIndexOf(QStringLiteral("<Cell N=\"NoFill\" V=\"0\"/><Cell N=\"NoLine\" V=\"0\"/>")),
                           QStringLiteral("<Cell N=\"NoFill\" V=\"0\"/><Cell N=\"NoLine\" V=\"0\"/>").size(),
                           QStringLiteral("<Cell N=\"NoFill\" V=\"1\"/><Cell N=\"NoLine\" V=\"1\"/>"));
        }
    // THE CABLES: each a line through its corners, in a box round it, its label in the middle.
    for (const SysCable& c : d.cables) {
        const std::vector<QPointF> line = sysCablePath(d, c);
        if (line.size() < 2) continue;
        QRectF around;
        for (const QPointF& p : line) around = around.united(QRectF(p, QSizeF(0.01, 0.01)));
        const double W = std::max(0.01, around.width() / perInch), H = std::max(0.01, around.height() / perInch);
        shapes += QStringLiteral("<Shape ID=\"%1\" Type=\"Shape\" LineStyle=\"0\" FillStyle=\"0\" TextStyle=\"0\">").arg(++id);
        shapes += cell(QStringLiteral("PinX"), num(vx(around.left()) + W / 2)) + cell(QStringLiteral("PinY"), num(vy(around.top()) - H / 2)) +
                  cell(QStringLiteral("Width"), num(W)) + cell(QStringLiteral("Height"), num(H)) +
                  cell(QStringLiteral("LocPinX"), num(W / 2)) + cell(QStringLiteral("LocPinY"), num(H / 2)) +
                  cell(QStringLiteral("LineColor"), colour(c.color, QColor(60, 66, 76))) +
                  cell(QStringLiteral("LineWeight"), QStringLiteral("0.02"));
        shapes += QStringLiteral("<Section N=\"Character\"><Row IX=\"0\">%1</Row></Section>").arg(cell(QStringLiteral("Size"), num(8.0 / 72.0)));
        shapes += QStringLiteral("<Section N=\"Geometry\" IX=\"0\">") + cell(QStringLiteral("NoFill"), QStringLiteral("1")) +
                  cell(QStringLiteral("NoLine"), QStringLiteral("0"));
        for (size_t i = 0; i < line.size(); ++i)
            shapes += QStringLiteral("<Row T=\"%1\" IX=\"%2\">%3%4</Row>")
                          .arg(i == 0 ? QStringLiteral("MoveTo") : QStringLiteral("LineTo"))
                          .arg(i + 1)
                          .arg(cell(QStringLiteral("X"), num((line[i].x() - around.left()) / perInch)),
                               cell(QStringLiteral("Y"), num((around.bottom() - line[i].y()) / perInch)));
        shapes += QStringLiteral("</Section><Text>%1</Text></Shape>").arg(cableLabel(c).toHtmlEscaped());
    }
    for (const SysNote& n : d.notes) {
        QPointF target;
        if (n.showLink && sysNoteTarget(d, n, target)) {
            const QPointF edge(std::clamp(target.x(), n.x, n.x + n.w), std::clamp(target.y(), n.y, n.y + n.h));
            const double left = std::min(edge.x(), target.x()), top = std::min(edge.y(), target.y());
            const double W = std::max(0.01, std::abs(edge.x() - target.x()) / perInch);
            const double H = std::max(0.01, std::abs(edge.y() - target.y()) / perInch);
            shapes += QStringLiteral("<Shape ID=\"%1\" Type=\"Shape\" LineStyle=\"0\" FillStyle=\"0\" TextStyle=\"0\">").arg(++id);
            shapes += cell(QStringLiteral("PinX"), num(vx(left) + W / 2)) + cell(QStringLiteral("PinY"), num(vy(top) - H / 2)) +
                      cell(QStringLiteral("Width"), num(W)) + cell(QStringLiteral("Height"), num(H)) +
                      cell(QStringLiteral("LocPinX"), num(W / 2)) + cell(QStringLiteral("LocPinY"), num(H / 2)) +
                      cell(QStringLiteral("LineColor"), QStringLiteral("#7D828C")) + cell(QStringLiteral("LinePattern"), QStringLiteral("2"));
            shapes += QStringLiteral("<Section N=\"Geometry\" IX=\"0\">") + cell(QStringLiteral("NoFill"), QStringLiteral("1"));
            int point = 0;
            for (const QPointF& at : {edge, target})
                shapes += QStringLiteral("<Row T=\"%1\" IX=\"%2\">%3%4</Row>")
                              .arg(point == 0 ? QStringLiteral("MoveTo") : QStringLiteral("LineTo")).arg(++point)
                              .arg(cell(QStringLiteral("X"), num((at.x() - left) / perInch)), cell(QStringLiteral("Y"), num(H - (at.y() - top) / perInch)));
            shapes += QStringLiteral("</Section></Shape>");
        }
        rect(n.x, n.y, n.w, n.h, colour(n.color, QColor(255, 247, 190)), QStringLiteral("#C8AA3C"), n.text, 9.0, true);
    }

    const QString xmlHead = QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\n");
    std::vector<ZipEntry> parts;
    parts.push_back({QStringLiteral("[Content_Types].xml"),
                     (xmlHead + QStringLiteral(
                                    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                                    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                                    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                                    "<Override PartName=\"/visio/document.xml\" ContentType=\"application/vnd.ms-visio.drawing.main+xml\"/>"
                                    "<Override PartName=\"/visio/pages/pages.xml\" ContentType=\"application/vnd.ms-visio.pages+xml\"/>"
                                    "<Override PartName=\"/visio/pages/page1.xml\" ContentType=\"application/vnd.ms-visio.page+xml\"/>"
                                    "<Override PartName=\"/visio/windows.xml\" ContentType=\"application/vnd.ms-visio.windows+xml\"/>"
                                    "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
                                    "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
                                    "</Types>"))
                         .toUtf8()});
    parts.push_back({QStringLiteral("_rels/.rels"),
                     (xmlHead + QStringLiteral(
                                    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                    "<Relationship Id=\"rId1\" Type=\"http://schemas.microsoft.com/visio/2010/relationships/document\" Target=\"visio/document.xml\"/>"
                                    "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
                                    "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
                                    "</Relationships>"))
                         .toUtf8()});
    parts.push_back({QStringLiteral("docProps/core.xml"),
                     (xmlHead + QStringLiteral("<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                                               "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>%1</dc:title><dc:creator>Loopwire</dc:creator>"
                                               "</cp:coreProperties>")
                                    .arg(esc(d.name.isEmpty() ? QStringLiteral("System") : d.name)))
                         .toUtf8()});
    parts.push_back({QStringLiteral("docProps/app.xml"),
                     (xmlHead + QStringLiteral("<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
                                               "<Application>Microsoft Visio</Application><AppVersion>15.0000</AppVersion></Properties>"))
                         .toUtf8()});
    // THE DOCUMENT: one style, "No Style", that every shape is drawn in.
    parts.push_back(
        {QStringLiteral("visio/document.xml"),
         (xmlHead + QStringLiteral("<VisioDocument xmlns=\"%1\" xmlns:r=\"%2\" xml:space=\"preserve\">"
                                   "<DocumentSettings TopPage=\"0\" DefaultTextStyle=\"0\" DefaultLineStyle=\"0\" DefaultFillStyle=\"0\" DefaultGuideStyle=\"0\">"
                                   "<GlueSettings>9</GlueSettings><SnapSettings>65847</SnapSettings><SnapExtensions>34</SnapExtensions>"
                                   "<SnapAngles/><DynamicGridEnabled>1</DynamicGridEnabled><ProtectStyles>0</ProtectStyles>"
                                   "<ProtectShapes>0</ProtectShapes><ProtectMasters>0</ProtectMasters><ProtectBkgnds>0</ProtectBkgnds>"
                                   "</DocumentSettings>"
                                   "<Colors><ColorEntry IX=\"0\" RGB=\"#000000\"/><ColorEntry IX=\"1\" RGB=\"#FFFFFF\"/></Colors>"
                                   "<FaceNames><FaceName NameU=\"Calibri\" UnicodeRanges=\"-536859905 -1073732485 9 0\" CharSets=\"536871423 0\" "
                                   "Panos=\"2 15 5 2 2 2 4 3 2 4\" Flags=\"325\"/></FaceNames>"
                                   "<StyleSheets><StyleSheet ID=\"0\" NameU=\"No Style\" Name=\"No Style\">"
                                   "<Cell N=\"EnableLineProps\" V=\"1\"/><Cell N=\"EnableFillProps\" V=\"1\"/><Cell N=\"EnableTextProps\" V=\"1\"/>"
                                   "<Cell N=\"HideForApply\" V=\"0\"/><Cell N=\"LineWeight\" V=\"0.01041666666666667\"/>"
                                   "<Cell N=\"LineColor\" V=\"0\"/><Cell N=\"LinePattern\" V=\"1\"/><Cell N=\"Rounding\" V=\"0\"/>"
                                   "<Cell N=\"EndArrowSize\" V=\"2\"/><Cell N=\"BeginArrow\" V=\"0\"/><Cell N=\"EndArrow\" V=\"0\"/>"
                                   "<Cell N=\"LineCap\" V=\"0\"/><Cell N=\"BeginArrowSize\" V=\"2\"/><Cell N=\"LineColorTrans\" V=\"0\"/>"
                                   "<Cell N=\"CompoundType\" V=\"0\"/><Cell N=\"FillForegnd\" V=\"1\"/><Cell N=\"FillBkgnd\" V=\"0\"/>"
                                   "<Cell N=\"FillPattern\" V=\"1\"/><Cell N=\"ShdwForegnd\" V=\"0\"/><Cell N=\"ShdwPattern\" V=\"0\"/>"
                                   "<Cell N=\"FillForegndTrans\" V=\"0\"/><Cell N=\"FillBkgndTrans\" V=\"0\"/>"
                                   "<Cell N=\"LeftMargin\" V=\"0.05\"/><Cell N=\"RightMargin\" V=\"0.05\"/><Cell N=\"TopMargin\" V=\"0.05\"/>"
                                   "<Cell N=\"BottomMargin\" V=\"0.05\"/><Cell N=\"VerticalAlign\" V=\"1\"/><Cell N=\"TextBkgnd\" V=\"0\"/>"
                                   "<Section N=\"Character\"><Row IX=\"0\"><Cell N=\"Font\" V=\"Calibri\"/><Cell N=\"Color\" V=\"0\"/>"
                                   "<Cell N=\"Size\" V=\"0.1666666666666667\"/></Row></Section>"
                                   "<Section N=\"Paragraph\"><Row IX=\"0\"><Cell N=\"HorzAlign\" V=\"1\"/></Row></Section>"
                                   "</StyleSheet></StyleSheets>"
                                   "<DocumentSheet NameU=\"TheDoc\" Name=\"TheDoc\" LineStyle=\"0\" FillStyle=\"0\" TextStyle=\"0\"/>"
                                   "</VisioDocument>")
                                  .arg(ns, rns))
             .toUtf8()});
    parts.push_back({QStringLiteral("visio/_rels/document.xml.rels"),
                     (xmlHead + QStringLiteral(
                                    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                    "<Relationship Id=\"rId1\" Type=\"http://schemas.microsoft.com/visio/2010/relationships/pages\" Target=\"pages/pages.xml\"/>"
                                    "<Relationship Id=\"rId2\" Type=\"http://schemas.microsoft.com/visio/2010/relationships/windows\" Target=\"windows.xml\"/>"
                                    "</Relationships>"))
                         .toUtf8()});
    parts.push_back({QStringLiteral("visio/windows.xml"),
                     (xmlHead + QStringLiteral("<Windows ClientWidth=\"1600\" ClientHeight=\"900\" xmlns=\"%1\" xmlns:r=\"%2\" xml:space=\"preserve\">"
                                               "<Window ID=\"0\" WindowType=\"Drawing\" WindowState=\"1073741824\" ClientWidth=\"1600\" "
                                               "ClientHeight=\"900\" Page=\"0\" ViewScale=\"-1\" ViewCenterX=\"%3\" ViewCenterY=\"%4\"/></Windows>")
                                    .arg(ns, rns, num(pageW / 2), num(pageH / 2)))
                         .toUtf8()});
    parts.push_back({QStringLiteral("visio/pages/pages.xml"),
                     (xmlHead + QStringLiteral("<Pages xmlns=\"%1\" xmlns:r=\"%2\" xml:space=\"preserve\">"
                                               "<Page ID=\"0\" NameU=\"Page-1\" Name=\"%3\" ViewScale=\"-1\" ViewCenterX=\"%4\" ViewCenterY=\"%5\">"
                                               "<PageSheet LineStyle=\"0\" FillStyle=\"0\" TextStyle=\"0\">"
                                               "<Cell N=\"PageWidth\" V=\"%6\"/><Cell N=\"PageHeight\" V=\"%7\"/>"
                                               "<Cell N=\"PageScale\" V=\"1\" U=\"IN_F\"/><Cell N=\"DrawingScale\" V=\"1\" U=\"IN_F\"/>"
                                               "<Cell N=\"DrawingSizeType\" V=\"0\"/><Cell N=\"DrawingScaleType\" V=\"0\"/>"
                                               "</PageSheet><Rel r:id=\"rId1\"/></Page></Pages>")
                                    .arg(ns, rns, esc(d.name.isEmpty() ? QStringLiteral("System") : d.name), num(pageW / 2), num(pageH / 2),
                                         num(pageW), num(pageH)))
                         .toUtf8()});
    parts.push_back({QStringLiteral("visio/pages/_rels/pages.xml.rels"),
                     (xmlHead + QStringLiteral(
                                    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                    "<Relationship Id=\"rId1\" Type=\"http://schemas.microsoft.com/visio/2010/relationships/page\" Target=\"page1.xml\"/>"
                                    "</Relationships>"))
                         .toUtf8()});
    parts.push_back({QStringLiteral("visio/pages/page1.xml"),
                     (xmlHead + QStringLiteral("<PageContents xmlns=\"%1\" xmlns:r=\"%2\" xml:space=\"preserve\"><Shapes>%3</Shapes></PageContents>")
                                    .arg(ns, rns, shapes))
                         .toUtf8()});
    return writeFile(path, zipStored(parts), error);
}

} // namespace Loopwire
