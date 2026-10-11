#include "system/SystemImport.h"

#include "io/PdfText.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Edraw_Desktop {

namespace {

quint16 u16(const QByteArray& b, qsizetype at) {
    return at + 2 <= b.size() ? static_cast<quint16>(static_cast<quint8>(b[at]) | (static_cast<quint8>(b[at + 1]) << 8)) : 0;
}
quint32 u32(const QByteArray& b, qsizetype at) { return static_cast<quint32>(u16(b, at)) | (static_cast<quint32>(u16(b, at + 2)) << 16); }

// THE TEXT OF A LABEL: an HTML one (draw.io's) as plain text, a line a line.
QString plainText(const QString& value) {
    if (!value.contains(QLatin1Char('<'))) return value.trimmed();
    // The breaks marked through the HTML read, which takes a new line for a space.
    const QString mark = QStringLiteral("@@edraw-br@@");
    QString html = value;
    html.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption), mark);
    html.replace(QRegularExpression(QStringLiteral("</(div|p)>"), QRegularExpression::CaseInsensitiveOption), mark);
    return QTextDocumentFragment::fromHtml(html).toPlainText().replace(mark, QStringLiteral("\n")).trimmed();
}

QHash<QString, QString> styleOf(const QString& style) {
    QHash<QString, QString> out;
    for (const QString& part : style.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq < 0) out.insert(part.trimmed(), QStringLiteral("1"));
        else out.insert(part.left(eq).trimmed(), part.mid(eq + 1).trimmed());
    }
    return out;
}

// A BOX'S TEXT TO A UNIT: what it is ("[PCBA]", "[Mechanical]") and its description after it, its name, its part number
// and its revision ("rev X") - as Edraw writes them, or a name and a description from any other.
void unitFromLabel(SysUnit& u, const QString& label) {
    QStringList lines = label.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString& l : lines) l = l.trimmed();
    lines.removeAll(QString());
    static const QRegularExpression kind(QStringLiteral("^\\[(PCBA|Mechanical)\\]\\s*(.*)$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rev(QStringLiteral("\\brev\\.?\\s*([A-Za-z0-9]{1,3})\\b"), QRegularExpression::CaseInsensitiveOption);
    for (int i = 0; i < lines.size(); ++i)
        if (const QRegularExpressionMatch m = kind.match(lines[i]); m.hasMatch()) {
            u.kind = m.captured(1).compare(QLatin1String("pcba"), Qt::CaseInsensitive) == 0 ? QStringLiteral("pcba") : QStringLiteral("mech");
            u.description = m.captured(2).trimmed();
            lines.removeAt(i);
            break;
        }
    if (!lines.isEmpty()) u.name = lines.takeFirst();
    for (const QString& l : lines) {
        if (const QRegularExpressionMatch m = rev.match(l); m.hasMatch()) {
            u.rev = m.captured(1);
            const QString pn = QString(l).remove(m.capturedStart(), m.capturedLength()).trimmed();
            if (!pn.isEmpty() && u.pn.isEmpty()) u.pn = pn;
        } else if (u.pn.isEmpty() && !l.contains(QLatin1Char(' '))) {
            u.pn = l;
        } else {
            u.description += (u.description.isEmpty() ? QString() : QStringLiteral(" ")) + l;
        }
    }
}

// A LINE'S TEXT TO A CABLE: its name (W1), its part number, its revision.
void cableFromLabel(SysCable& c, const QString& label) {
    static const QRegularExpression rev(QStringLiteral("\\brev\\.?\\s*([A-Za-z0-9]{1,3})\\b"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression wname(QStringLiteral("^W\\d+[A-Z]?$"), QRegularExpression::CaseInsensitiveOption);
    QString text = QString(label).replace(QLatin1Char('\n'), QLatin1Char(' ')).trimmed();
    if (const QRegularExpressionMatch m = rev.match(text); m.hasMatch()) {
        c.rev = m.captured(1);
        text.remove(m.capturedStart(), m.capturedLength());
    }
    QStringList words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (int i = 0; i < words.size(); ++i)
        if (wname.match(words[i]).hasMatch()) {
            c.name = words.takeAt(i);
            break;
        }
    if (!words.isEmpty()) c.pn = words.takeFirst();
    if (!words.isEmpty()) c.description = words.join(QLatin1Char(' '));
}

// A PORT'S TEXT: its name, and its connector's part number in brackets after it, as Edraw writes it.
void portFromLabel(SysPort& p, const QString& label) {
    static const QRegularExpression withPn(QStringLiteral("^(.*?)\\s*\\((.+)\\)\\s*$"));
    if (const QRegularExpressionMatch m = withPn.match(label.trimmed()); m.hasMatch()) {
        p.name = m.captured(1).trimmed();
        p.connector = m.captured(2).trimmed();
    } else if (!label.trimmed().isEmpty()) {
        p.name = label.trimmed();
    }
}

void nearestSide(const SysUnit& u, const QPointF& w, QString& side, double& at) {
    const double dl = std::abs(w.x() - u.x), dr = std::abs(u.x + u.w - w.x());
    const double dt = std::abs(w.y() - u.y), db = std::abs(u.y + u.h - w.y());
    const double least = std::min({dl, dr, dt, db});
    side = least == dl ? QStringLiteral("left") : least == dr ? QStringLiteral("right") : least == dt ? QStringLiteral("top") : QStringLiteral("bottom");
    at = side == QLatin1String("left") || side == QLatin1String("right") ? (w.y() - u.y) / std::max(1e-9, u.h) : (w.x() - u.x) / std::max(1e-9, u.w);
    at = std::clamp(at, 0.02, 0.98);
}

// THE SIDE FACING A POINT away from the box: the way it lies from the box's middle, the box's shape allowed for.
void sideToward(const SysUnit& u, const QPointF& w, QString& side, double& at) {
    const double dx = (w.x() - (u.x + u.w / 2)) / std::max(1e-9, u.w), dy = (w.y() - (u.y + u.h / 2)) / std::max(1e-9, u.h);
    if (std::abs(dx) >= std::abs(dy)) {
        side = dx >= 0 ? QStringLiteral("right") : QStringLiteral("left");
        at = (w.y() - u.y) / std::max(1e-9, u.h);
    } else {
        side = dy >= 0 ? QStringLiteral("bottom") : QStringLiteral("top");
        at = (w.x() - u.x) / std::max(1e-9, u.w);
    }
    at = std::clamp(at, 0.1, 0.9);
}

// WHAT IS READ, before it is a design: boxes, small squares, lines and notes, where they are - in the diagram's units.
struct Shape {
    QString id;
    QString text;
    QRectF rect;
    bool note = false;
    QColor fill;
};
struct Line {
    QString id;
    QString text;
    QString from, to;            // the shapes it joins, where known
    std::vector<QPointF> points; // where it runs, ends first and last
};

// THE SHAPES READ, MADE A DESIGN: boxes units (the smallest round each its mother), small squares on a box's side its
// ports, lines cables - a port put where a line meets a box without one - and notes notes.
SystemDesign build(const std::vector<Shape>& shapes, const std::vector<Line>& lines) {
    SystemDesign d;
    QHash<QString, QString> unitOf;               // shape id -> unit id
    QHash<QString, QPair<QString, QString>> portOf;  // shape id -> (unit id, port id)
    std::vector<const Shape*> small;
    for (const Shape& s : shapes) {
        if (s.note) {
            const QString n = d.addNote(s.rect.x(), s.rect.y(), s.text);
            SysNote* note = d.note(n);
            note->w = std::max(40.0, s.rect.width());
            note->h = std::max(24.0, s.rect.height());
            if (s.fill.isValid() && s.fill != QColor(255, 247, 190)) note->color = s.fill;
            continue;
        }
        if (s.rect.width() <= 18.0 && s.rect.height() <= 18.0) {
            small.push_back(&s);
            continue;
        }
        const QString id = d.addUnit(s.rect.x(), s.rect.y());
        SysUnit* u = d.unit(id);
        u->w = s.rect.width();
        u->h = s.rect.height();
        unitFromLabel(*u, s.text);
        if (u->name.isEmpty()) u->name = id.toUpper();
        if (s.fill.isValid() && s.fill != QColor(232, 238, 246) && s.fill != QColor(226, 242, 230)) u->color = s.fill;
        unitOf.insert(s.id, id);
    }
    // IN ONE ANOTHER: each unit in the smallest unit wholly round it.
    for (SysUnit& u : d.units) {
        double area = 1e300;
        for (const SysUnit& o : d.units)
            if (o.id != u.id && o.rect().contains(u.rect()) && o.w * o.h < area && o.w * o.h > u.w * u.h) {
                area = o.w * o.h;
                u.parent = o.id;
            }
    }
    // PORTS: a small square on (or by) a box's side.
    for (const Shape* s : small) {
        const QPointF c = s->rect.center();
        const SysUnit* best = nullptr;
        double gap = 12.0;
        for (const SysUnit& u : d.units) {
            const QRectF r = u.rect();
            const double dx = std::max({r.left() - c.x(), 0.0, c.x() - r.right()});
            const double dy = std::max({r.top() - c.y(), 0.0, c.y() - r.bottom()});
            const double outside = std::hypot(dx, dy);
            const double toWall = r.contains(c) ? std::min({c.x() - r.left(), r.right() - c.x(), c.y() - r.top(), r.bottom() - c.y()}) : outside;
            if (toWall < gap) {
                gap = toWall;
                best = &u;
            }
        }
        if (!best) continue;
        QString side;
        double at = 0.5;
        nearestSide(*best, c, side, at);
        const QString unitId = best->id;
        const QString portId = d.addPort(unitId, side, at);
        portFromLabel(*d.unit(unitId)->port(portId), s->text);
        portOf.insert(s->id, {unitId, portId});
    }
    // CABLES: from port (or box) to port (or box) - by what they join, or by where their ends are.
    auto endAt = [&](const QString& shape, const QPointF& point, const QPointF& toward, QString& unit, QString& port) {
        if (portOf.contains(shape)) {
            unit = portOf.value(shape).first;
            port = portOf.value(shape).second;
            return true;
        }
        QString u = unitOf.value(shape);
        // Not said: the port nearest the end, or the box it ends on.
        if (u.isEmpty()) {
            double near = 14.0;
            for (const SysUnit& each : d.units)
                for (const SysPort& p : each.ports) {
                    const QPointF at = p.side == QLatin1String("left")    ? QPointF(each.x, each.y + each.h * p.at)
                                       : p.side == QLatin1String("right") ? QPointF(each.x + each.w, each.y + each.h * p.at)
                                       : p.side == QLatin1String("top")   ? QPointF(each.x + each.w * p.at, each.y)
                                                                          : QPointF(each.x + each.w * p.at, each.y + each.h);
                    const double gap = std::hypot(at.x() - point.x(), at.y() - point.y());
                    if (gap < near) {
                        near = gap;
                        unit = each.id;
                        port = p.id;
                    }
                }
            if (!port.isEmpty()) return true;
            double area = 1e300;
            for (const SysUnit& each : d.units)
                if (each.rect().adjusted(-14, -14, 14, 14).contains(point) && each.w * each.h < area) {
                    area = each.w * each.h;
                    u = each.id;
                }
        }
        if (u.isEmpty()) return false;
        // ON A BOX WITHOUT A PORT THERE: a port put on its side - where the line meets it, or toward the other end.
        const SysUnit* box = d.unit(u);
        QString side;
        double at = 0.5;
        if (box->rect().adjusted(-1, -1, 1, 1).contains(point) && !box->rect().adjusted(8, 8, -8, -8).contains(point))
            nearestSide(*box, point, side, at);
        else
            sideToward(*box, toward, side, at);
        unit = u;
        port = d.addPort(u, side, at);
        return true;
    };
    for (const Line& l : lines) {
        const QPointF a = l.points.empty() ? QPointF() : l.points.front(), b = l.points.empty() ? QPointF() : l.points.back();
        auto centreOf = [&](const QString& shape, const QPointF& otherwise) {
            if (portOf.contains(shape)) return otherwise;
            if (const SysUnit* u = d.unit(unitOf.value(shape))) return u->rect().center();
            return otherwise;
        };
        QString au, ap, bu, bp;
        if (!endAt(l.from, a, centreOf(l.to, b), au, ap) || !endAt(l.to, b, centreOf(l.from, a), bu, bp)) continue;
        const QString c = d.addCable(au, ap, bu, bp);
        if (SysCable* cable = d.cable(c)) {
            const QString name = cable->name;
            cableFromLabel(*cable, l.text);
            if (cable->name.isEmpty()) cable->name = name;
            cable->aInside = au == bu || d.isWithin(bu, au);
            cable->bInside = au == bu || d.isWithin(au, bu);
        }
    }
    return d;
}

} // namespace

std::vector<ZipItem> readZip(const QByteArray& bytes) {
    std::vector<ZipItem> out;
    // THE CENTRAL DIRECTORY, from the record at the end that says where it is.
    qsizetype end = -1;
    for (qsizetype i = bytes.size() - 22; i >= std::max<qsizetype>(0, bytes.size() - 65557); --i)
        if (u32(bytes, i) == 0x06054b50) {
            end = i;
            break;
        }
    if (end < 0) return out;
    const int count = u16(bytes, end + 10);
    qsizetype at = u32(bytes, end + 16);
    for (int k = 0; k < count && at + 46 <= bytes.size() && u32(bytes, at) == 0x02014b50; ++k) {
        const int method = u16(bytes, at + 10);
        const quint32 packed = u32(bytes, at + 20);
        const int nameLen = u16(bytes, at + 28), extraLen = u16(bytes, at + 30), commentLen = u16(bytes, at + 32);
        const quint32 local = u32(bytes, at + 42);
        const QString name = QString::fromUtf8(bytes.mid(at + 46, nameLen));
        at += 46 + nameLen + extraLen + commentLen;
        if (local + 30 > static_cast<quint32>(bytes.size()) || u32(bytes, local) != 0x04034b50) continue;
        const qsizetype data = local + 30 + u16(bytes, local + 26) + u16(bytes, local + 28);
        const QByteArray raw = bytes.mid(data, packed);
        if (method == 0) out.push_back({name, raw});
        else if (method == 8) out.push_back({name, PdfText::inflate(raw, false)});
    }
    return out;
}

// ---- draw.io ----------------------------------------------------------------------------------------

bool importSystemDrawioXml(const QByteArray& input, SystemDesign& out, QString* error) {
    QByteArray xml = input;
    // A COMPRESSED PAGE: its diagram's text base64, then raw DEFLATE, then URL-encoded - undone in turn.
    {
        QXmlStreamReader r(xml);
        bool model = false;
        QString packed;
        while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement() && r.name() == QLatin1String("mxGraphModel")) {
                model = true;
                break;
            }
            if (r.isStartElement() && r.name() == QLatin1String("diagram")) {
                packed = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                if (!packed.isEmpty() && !packed.startsWith(QLatin1Char('<'))) break;
                packed.clear();
            }
        }
        if (!model && !packed.isEmpty()) {
            const QByteArray inflated = PdfText::inflate(QByteArray::fromBase64(packed.toLatin1()), false);
            xml = QUrl::fromPercentEncoding(inflated).toUtf8();
            if (xml.isEmpty()) {
                if (error) *error = QStringLiteral("the diagram's page could not be unpacked");
                return false;
            }
        }
    }
    struct Cell {
        QString id, parent, value, style, source, target;
        bool vertex = false, edge = false;
        QRectF geo;
        std::vector<QPointF> points;
        QPointF sourcePoint, targetPoint;
        bool hasSource = false, hasTarget = false;
    };
    std::vector<Cell> cells;
    QXmlStreamReader r(xml);
    bool sawModel = false;
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) continue;
        if (r.name() == QLatin1String("mxGraphModel")) sawModel = true;
        if (r.name() == QLatin1String("mxCell") || r.name() == QLatin1String("object") || r.name() == QLatin1String("UserObject")) {
            Cell c;
            const QXmlStreamAttributes a = r.attributes();
            const bool wrapper = r.name() != QLatin1String("mxCell");
            c.id = a.value(QLatin1String("id")).toString();
            c.value = a.value(wrapper ? QLatin1String("label") : QLatin1String("value")).toString();
            if (wrapper) {
                // An object's cell inside it: its look and where it is.
                while (!r.atEnd() && !(r.isStartElement() && r.name() == QLatin1String("mxCell"))) r.readNext();
            }
            const QXmlStreamAttributes m = r.attributes();
            c.parent = m.value(QLatin1String("parent")).toString();
            c.style = m.value(QLatin1String("style")).toString();
            c.source = m.value(QLatin1String("source")).toString();
            c.target = m.value(QLatin1String("target")).toString();
            c.vertex = m.value(QLatin1String("vertex")) == QLatin1String("1");
            c.edge = m.value(QLatin1String("edge")) == QLatin1String("1");
            if (!wrapper && c.value.isEmpty()) c.value = m.value(QLatin1String("value")).toString();
            // ITS GEOMETRY, and a line's corners and loose ends.
            int depth = 0;
            while (!r.atEnd()) {
                r.readNext();
                if (r.isStartElement()) {
                    ++depth;
                    const QXmlStreamAttributes g = r.attributes();
                    if (r.name() == QLatin1String("mxGeometry"))
                        c.geo = QRectF(g.value(QLatin1String("x")).toDouble(), g.value(QLatin1String("y")).toDouble(),
                                       g.value(QLatin1String("width")).toDouble(), g.value(QLatin1String("height")).toDouble());
                    if (r.name() == QLatin1String("mxPoint")) {
                        const QPointF p(g.value(QLatin1String("x")).toDouble(), g.value(QLatin1String("y")).toDouble());
                        const QString as = g.value(QLatin1String("as")).toString();
                        if (as == QLatin1String("sourcePoint")) {
                            c.sourcePoint = p;
                            c.hasSource = true;
                        } else if (as == QLatin1String("targetPoint")) {
                            c.targetPoint = p;
                            c.hasTarget = true;
                        } else {
                            c.points.push_back(p);
                        }
                    }
                } else if (r.isEndElement()) {
                    if (depth == 0) break;
                    --depth;
                }
            }
            if (!c.id.isEmpty()) cells.push_back(c);
        }
    }
    if (r.hasError() && cells.empty()) {
        if (error) *error = QStringLiteral("not a draw.io diagram: %1").arg(r.errorString());
        return false;
    }
    if (!sawModel && cells.empty()) {
        if (error) *error = QStringLiteral("no diagram in it");
        return false;
    }
    // WHERE EACH IS ON THE PAGE: a cell inside a group or another shape placed from its corner.
    QHash<QString, const Cell*> byId;
    for (const Cell& c : cells) byId.insert(c.id, &c);
    std::function<QPointF(const QString&, int)> origin = [&](const QString& id, int guard) -> QPointF {
        const Cell* c = byId.value(id);
        if (!c || guard > 32 || !c->vertex) return {};
        return c->geo.topLeft() + origin(c->parent, guard + 1);
    };
    std::vector<Shape> shapes;
    std::vector<Line> lines;
    for (const Cell& c : cells) {
        const QHash<QString, QString> st = styleOf(c.style);
        if (c.vertex) {
            if (c.geo.width() <= 0 || c.geo.height() <= 0) continue;
            if (st.contains(QStringLiteral("group")) || st.value(QStringLiteral("shape")) == QLatin1String("group")) continue;
            Shape s;
            s.id = c.id;
            s.text = plainText(c.value);
            s.rect = QRectF(c.geo.topLeft() + origin(c.parent, 0), c.geo.size());
            s.note = st.value(QStringLiteral("shape")) == QLatin1String("note") || st.contains(QStringLiteral("text"));
            if (st.contains(QStringLiteral("edgeLabel"))) continue;   // a line's own label
            const QString fill = st.value(QStringLiteral("fillColor"));
            if (fill.startsWith(QLatin1Char('#'))) s.fill = QColor(fill);
            shapes.push_back(s);
        } else if (c.edge) {
            // A note's line to what it is about is not a cable.
            if (c.id.endsWith(QLatin1String(".link")) || st.value(QStringLiteral("dashed")) == QLatin1String("1")) continue;
            Line l;
            l.id = c.id;
            l.text = plainText(c.value);
            l.from = c.source;
            l.to = c.target;
            if (c.hasSource) l.points.push_back(c.sourcePoint);
            for (const QPointF& p : c.points) l.points.push_back(p);
            if (c.hasTarget) l.points.push_back(c.targetPoint);
            // Ends not given: the middles of what it joins.
            auto centre = [&](const QString& id) {
                const Cell* t = byId.value(id);
                return t ? QRectF(t->geo.topLeft() + origin(t->parent, 0), t->geo.size()).center() : QPointF();
            };
            if (!c.hasSource) l.points.insert(l.points.begin(), centre(c.source));
            if (!c.hasTarget) l.points.push_back(centre(c.target));
            lines.push_back(l);
        }
    }
    // A LINE'S LABEL drawn as a cell of its own on it: its text the line's.
    for (const Cell& c : cells)
        if (c.vertex && styleOf(c.style).contains(QStringLiteral("edgeLabel")))
            for (Line& l : lines)
                if (l.id == c.parent && l.text.isEmpty()) l.text = plainText(c.value);
    out = build(shapes, lines);
    return true;
}

bool importSystemDrawio(const QString& path, SystemDesign& out, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    if (!importSystemDrawioXml(f.readAll(), out, error)) return false;
    if (out.name.isEmpty()) out.name = QFileInfo(path).completeBaseName();
    return true;
}

// ---- Visio ------------------------------------------------------------------------------------------

bool importSystemVsdx(const QString& path, SystemDesign& out, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    const std::vector<ZipItem> parts = readZip(f.readAll());
    QByteArray page, pages;
    for (const ZipItem& p : parts) {
        if (p.name.compare(QLatin1String("visio/pages/pages.xml"), Qt::CaseInsensitive) == 0) pages = p.data;
        if (p.name.startsWith(QLatin1String("visio/pages/page"), Qt::CaseInsensitive) && p.name.endsWith(QLatin1String(".xml")) &&
            !p.name.endsWith(QLatin1String("pages.xml")) && (page.isEmpty() || p.name.contains(QLatin1String("page1."))))
            page = p.data;
    }
    if (page.isEmpty()) {
        if (error) *error = QStringLiteral("%1 is not a Visio drawing (.vsdx) - no page in it").arg(QFileInfo(path).fileName());
        return false;
    }
    // THE PAGE'S HEIGHT: Visio's up is the diagram's down.
    double pageH = 0.0;
    {
        QXmlStreamReader r(pages);
        while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement() && r.name() == QLatin1String("Cell") && r.attributes().value(QLatin1String("N")) == QLatin1String("PageHeight")) {
                pageH = r.attributes().value(QLatin1String("V")).toDouble();
                break;
            }
        }
    }
    struct VShape {
        QString id;
        QHash<QString, double> cell;
        QHash<QString, QString> text;   // cells' text values (colours)
        QString words;
        bool oneD = false;
        bool noFill = false;
        std::vector<QPointF> geometry;   // its own Geometry rows' points, in inches from its corner
    };
    std::vector<VShape> found;
    struct Connect {
        QString from, fromCell, to;
    };
    std::vector<Connect> connects;
    QXmlStreamReader r(page);
    std::vector<size_t> open;   // the shapes being read, by place (groups nest)
    int geometryDepth = -1;
    QString rowType;
    QPointF rowPoint;
    bool inText = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QXmlStreamAttributes a = r.attributes();
            if (r.name() == QLatin1String("Shape")) {
                found.push_back(VShape{});
                found.back().id = a.value(QLatin1String("ID")).toString();
                found.back().oneD = a.value(QLatin1String("Type")) == QLatin1String("Edge");
                open.push_back(found.size() - 1);
            } else if (r.name() == QLatin1String("Cell") && !open.empty()) {
                const QString n = a.value(QLatin1String("N")).toString();
                const QString v = a.value(QLatin1String("V")).toString();
                if (geometryDepth >= 0) {
                    if (n == QLatin1String("X")) rowPoint.setX(v.toDouble());
                    if (n == QLatin1String("Y")) rowPoint.setY(v.toDouble());
                    if (n == QLatin1String("NoFill") && v == QLatin1String("1")) found[open.back()].noFill = true;
                } else {
                    found[open.back()].cell.insert(n, v.toDouble());
                    found[open.back()].text.insert(n, v);
                    if (n == QLatin1String("BeginX")) found[open.back()].oneD = true;
                }
            } else if (r.name() == QLatin1String("Section") && a.value(QLatin1String("N")) == QLatin1String("Geometry")) {
                geometryDepth = 0;
            } else if (r.name() == QLatin1String("Row") && geometryDepth >= 0) {
                rowType = a.value(QLatin1String("T")).toString();
                rowPoint = {};
            } else if (r.name() == QLatin1String("Text") && !open.empty()) {
                inText = true;
            } else if (r.name() == QLatin1String("Connect")) {
                connects.push_back({a.value(QLatin1String("FromSheet")).toString(), a.value(QLatin1String("FromCell")).toString(),
                                    a.value(QLatin1String("ToSheet")).toString()});
            }
        } else if (r.isCharacters() && inText && !open.empty()) {
            found[open.back()].words += r.text();
        } else if (r.isEndElement()) {
            if (r.name() == QLatin1String("Shape") && !open.empty()) open.pop_back();
            else if (r.name() == QLatin1String("Text")) inText = false;
            else if (r.name() == QLatin1String("Section") && geometryDepth >= 0) geometryDepth = -1;
            else if (r.name() == QLatin1String("Row") && geometryDepth >= 0 && !open.empty() &&
                     (rowType == QLatin1String("MoveTo") || rowType == QLatin1String("LineTo")))
                found[open.back()].geometry.push_back(rowPoint);
        }
    }
    if (pageH <= 0.0)
        for (const VShape& s : found) pageH = std::max(pageH, s.cell.value(QStringLiteral("PinY")) + s.cell.value(QStringLiteral("Height")) / 2.0);
    // IN THE DIAGRAM'S UNITS: a hundred to the inch, down the page.
    const double perInch = 100.0;
    auto toDiagram = [&](double x, double y) { return QPointF(x * perInch, (pageH - y) * perInch); };
    std::vector<Shape> shapes;
    std::vector<Line> lines;
    QHash<QString, QString> beginOf, endOf;
    for (const Connect& c : connects) {
        if (c.fromCell.startsWith(QLatin1String("Begin"))) beginOf.insert(c.from, c.to);
        else if (c.fromCell.startsWith(QLatin1String("End"))) endOf.insert(c.from, c.to);
    }
    for (const VShape& s : found) {
        const double pinX = s.cell.value(QStringLiteral("PinX")), pinY = s.cell.value(QStringLiteral("PinY"));
        const double w = s.cell.value(QStringLiteral("Width")), h = s.cell.value(QStringLiteral("Height"));
        const double locX = s.cell.contains(QStringLiteral("LocPinX")) ? s.cell.value(QStringLiteral("LocPinX")) : w / 2;
        const double locY = s.cell.contains(QStringLiteral("LocPinY")) ? s.cell.value(QStringLiteral("LocPinY")) : h / 2;
        if (s.oneD || beginOf.contains(s.id) || endOf.contains(s.id)) {
            Line l;
            l.id = s.id;
            l.text = s.words.trimmed();
            l.from = beginOf.value(s.id);
            l.to = endOf.value(s.id);
            l.points = {toDiagram(s.cell.value(QStringLiteral("BeginX")), s.cell.value(QStringLiteral("BeginY"))),
                        toDiagram(s.cell.value(QStringLiteral("EndX")), s.cell.value(QStringLiteral("EndY")))};
            lines.push_back(l);
            continue;
        }
        if (s.noFill && s.geometry.size() >= 2) {
            // A LINE DRAWN AS A SHAPE (as Edraw writes a cable): its corners, from the shape's corner.
            Line l;
            l.id = s.id;
            l.text = s.words.trimmed();
            for (const QPointF& g : s.geometry) l.points.push_back(toDiagram(pinX - locX + g.x(), pinY - locY + g.y()));
            lines.push_back(l);
            continue;
        }
        if (w <= 0 || h <= 0) continue;
        Shape sh;
        sh.id = s.id;
        sh.text = s.words.trimmed();
        const QPointF topLeft = toDiagram(pinX - locX, pinY - locY + h);
        sh.rect = QRectF(topLeft, QSizeF(w * perInch, h * perInch));
        const QString fill = s.text.value(QStringLiteral("FillForegnd"));
        if (fill.startsWith(QLatin1Char('#'))) sh.fill = QColor(fill);
        sh.note = fill.compare(QLatin1String("#FFF7BE"), Qt::CaseInsensitive) == 0;
        // A port's name drawn beside it as text with no box: not a shape of its own.
        if (s.text.value(QStringLiteral("LineColor")).compare(QLatin1String("#FFFFFF"), Qt::CaseInsensitive) == 0 &&
            fill.compare(QLatin1String("#FFFFFF"), Qt::CaseInsensitive) == 0 && w * perInch <= 70)
            continue;
        shapes.push_back(sh);
    }
    // A PORT'S NAME, written beside its square (as Edraw writes it): given to the square nearest it.
    for (const VShape& s : found) {
        const double w = s.cell.value(QStringLiteral("Width")), h = s.cell.value(QStringLiteral("Height"));
        if (!(s.text.value(QStringLiteral("LineColor")).compare(QLatin1String("#FFFFFF"), Qt::CaseInsensitive) == 0 && w * perInch <= 70)) continue;
        const QPointF c = toDiagram(s.cell.value(QStringLiteral("PinX")), s.cell.value(QStringLiteral("PinY")));
        Shape* best = nullptr;
        double near = 60.0;
        for (Shape& sh : shapes)
            if (sh.rect.width() <= 18 && sh.rect.height() <= 18) {
                const double gap = std::hypot(sh.rect.center().x() - c.x(), sh.rect.center().y() - c.y());
                if (gap < near && sh.text.isEmpty()) {
                    near = gap;
                    best = &sh;
                }
            }
        if (best) best->text = s.words.trimmed();
        (void)h;
    }
    out = build(shapes, lines);
    out.name = QFileInfo(path).completeBaseName();
    return true;
}

bool importSystemDiagram(const QString& path, SystemDesign& out, QString* error) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("vsdx")) return importSystemVsdx(path, out, error);
    if (suffix == QLatin1String("drawio") || suffix == QLatin1String("xml") || suffix == QLatin1String("dio")) return importSystemDrawio(path, out, error);
    if (error) *error = QStringLiteral("%1: not a draw.io (.drawio) or Visio (.vsdx) diagram").arg(QFileInfo(path).fileName());
    return false;
}

} // namespace Edraw_Desktop
