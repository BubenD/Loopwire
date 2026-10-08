#include "system/SystemDesign.h"

#include "io/Dwx.h"
#include "io/Dxf.h"
#include "model/Document.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace Edraw_Desktop {

namespace {

QString csvCell(QString s) {
    if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))) {
        s.replace(QLatin1String("\""), QLatin1String("\"\""));
        return QLatin1Char('"') + s + QLatin1Char('"');
    }
    return s;
}

// A revision letter or number later than another: B after A, AA after Z, 3 after 2.
bool laterRevision(const QString& a, const QString& b) {
    if (b.isEmpty()) return true;
    if (a.size() != b.size()) return a.size() > b.size();
    return a.compare(b, Qt::CaseInsensitive) > 0;
}

// WHAT THE ENTITIES OF A DRAWING SAY: the title block's drawing number and title, the last revision in its revision
// table, the latest of its revision boxes, or "Rev X" written in its title block.
void readEntities(const std::vector<Entity>& ents, DrawingInfo& info, QString& fromTable, QString& fromBoxes, QString& fromText) {
    static const QRegularExpression revText(QStringLiteral("^\\s*REV(?:ISION)?\\.?\\s*[:\\-]?\\s*([A-Z0-9]{1,3})\\s*$"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cableName(QStringLiteral("^W\\d+[A-Z]?$"), QRegularExpression::CaseInsensitiveOption);
    auto designator = [&info](const QString& d) {
        const QString name = d.trimmed();
        if (name.isEmpty() || name.size() > 16 || name.contains(QLatin1Char(' '))) return;
        QStringList& list = cableName.match(name).hasMatch() ? info.cables : info.connectors;
        if (!list.contains(name)) list << name;
    };
    for (const Entity& e : ents) {
        const QString field = e.meta.value(QStringLiteral("field")).toString();
        const QString text = e.meta.value(QStringLiteral("text")).toString().trimmed();
        // ITS DESIGNATORS: a part's own, or the text a harness writes beside each part.
        designator(e.meta.value(QStringLiteral("designator")).toString());
        if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("designator")) designator(text);
        if (field == QLatin1String("drawingNo") && !text.isEmpty()) info.drawingNo = text;
        if (field == QLatin1String("title") && !text.isEmpty()) info.title = text;
        if (e.meta.value(QStringLiteral("revisionTable")).toBool()) {
            // THE REVISION TABLE: its REV column, the last row written in it.
            const int cols = e.meta.value(QStringLiteral("cols")).toInt();
            const int rows = e.meta.value(QStringLiteral("rows")).toInt();
            const QJsonArray cells = e.meta.value(QStringLiteral("cells")).toArray();
            int revCol = -1;
            for (int c = 0; c < cols && c < cells.size(); ++c)
                if (cells.at(c).toString().trimmed().compare(QLatin1String("REV"), Qt::CaseInsensitive) == 0) revCol = c;
            if (revCol >= 0)
                for (int r = 1; r < rows; ++r) {
                    const QString v = cells.at(r * cols + revCol).toString().trimmed();
                    if (!v.isEmpty()) fromTable = v;
                }
        }
        if (e.meta.value(QStringLiteral("revision")).toBool()) {
            const QString r = e.meta.value(QStringLiteral("rev")).toString().trimmed();
            if (!r.isEmpty() && laterRevision(r, fromBoxes)) fromBoxes = r;
        }
        if (const QRegularExpressionMatch m = revText.match(text); m.hasMatch() && fromText.isEmpty()) fromText = m.captured(1);
    }
}

} // namespace

DrawingInfo readDrawingInfo(const QString& path) {
    DrawingInfo info;
    if (path.isEmpty() || !QFileInfo::exists(path)) return info;
    const QString suffix = QFileInfo(path).suffix().toLower();
    Document doc(QFileInfo(path).completeBaseName());
    QString error;
    bool ok = false;
    if (suffix == QLatin1String("dwx")) ok = dwx::load(doc, path, &error);
    else if (suffix == QLatin1String("dxf")) ok = dxf::importFile(doc, path, &error);
    else {
        // A DWG is there, but not read here: nothing to say about it.
        info.found = true;
        return info;
    }
    if (!ok) return info;
    info.found = true;
    QString fromTable, fromBoxes, fromText;
    readEntities(doc.entities(), info, fromTable, fromBoxes, fromText);
    for (const DrawingLayout& l : doc.layouts()) readEntities(l.paperEntities, info, fromTable, fromBoxes, fromText);
    info.revision = !fromTable.isEmpty() ? fromTable : !fromBoxes.isEmpty() ? fromBoxes : fromText;
    return info;
}

const SysPort* SysUnit::port(const QString& id) const {
    for (const SysPort& p : ports)
        if (p.id == id) return &p;
    return nullptr;
}

SysPort* SysUnit::port(const QString& id) {
    for (SysPort& p : ports)
        if (p.id == id) return &p;
    return nullptr;
}

const SysUnit* SystemDesign::unit(const QString& id) const {
    for (const SysUnit& u : units)
        if (u.id == id) return &u;
    return nullptr;
}

SysUnit* SystemDesign::unit(const QString& id) {
    for (SysUnit& u : units)
        if (u.id == id) return &u;
    return nullptr;
}

const SysCable* SystemDesign::cable(const QString& id) const {
    for (const SysCable& c : cables)
        if (c.id == id) return &c;
    return nullptr;
}

SysCable* SystemDesign::cable(const QString& id) {
    for (SysCable& c : cables)
        if (c.id == id) return &c;
    return nullptr;
}

QString SystemDesign::freshId(const QString& prefix) const {
    QSet<QString> taken;
    for (const SysUnit& u : units) {
        taken.insert(u.id);
        for (const SysPort& p : u.ports) taken.insert(p.id);
    }
    for (const SysCable& c : cables) taken.insert(c.id);
    for (const SysNote& n : notes) taken.insert(n.id);
    for (int n = 1;; ++n) {
        const QString id = prefix + QString::number(n);
        if (!taken.contains(id)) return id;
    }
}

QString SystemDesign::addUnit(double x, double y, const QString& name) {
    SysUnit u;
    u.id = freshId(QStringLiteral("u"));
    u.x = x;
    u.y = y;
    if (name.isEmpty()) {
        QSet<QString> names;
        for (const SysUnit& o : units) names.insert(o.name);
        int n = 1;
        while (names.contains(QStringLiteral("U%1").arg(n))) ++n;
        u.name = QStringLiteral("U%1").arg(n);
    } else {
        u.name = name;
    }
    units.push_back(u);
    return u.id;
}

QString SystemDesign::addPort(const QString& unitId, const QString& side, double at, const QString& name) {
    SysUnit* u = unit(unitId);
    if (!u) return QString();
    SysPort p;
    p.id = freshId(QStringLiteral("p"));
    p.side = side;
    p.at = std::clamp(at, 0.05, 0.95);
    if (name.isEmpty()) {
        QSet<QString> names;
        for (const SysPort& o : u->ports) names.insert(o.name);
        int n = 1;
        while (names.contains(QStringLiteral("P%1").arg(n))) ++n;
        p.name = QStringLiteral("P%1").arg(n);
    } else {
        p.name = name;
    }
    u->ports.push_back(p);
    return p.id;
}

QString SystemDesign::addCable(const QString& aUnit, const QString& aPort, const QString& bUnit, const QString& bPort) {
    const SysUnit* ua = unit(aUnit);
    const SysUnit* ub = unit(bUnit);
    if (!ua || !ub || !ua->port(aPort) || !ub->port(bPort) || (aUnit == bUnit && aPort == bPort)) return QString();
    SysCable c;
    c.id = freshId(QStringLiteral("c"));
    QSet<QString> names;
    for (const SysCable& o : cables) names.insert(o.name);
    int n = 1;
    while (names.contains(QStringLiteral("W%1").arg(n))) ++n;
    c.name = QStringLiteral("W%1").arg(n);
    c.aUnit = aUnit;
    c.aPort = aPort;
    c.bUnit = bUnit;
    c.bPort = bPort;
    cables.push_back(c);
    return c.id;
}

void SystemDesign::removeUnit(const QString& id) {
    cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const SysCable& c) { return c.aUnit == id || c.bUnit == id; }),
                 cables.end());
    units.erase(std::remove_if(units.begin(), units.end(), [&](const SysUnit& u) { return u.id == id; }), units.end());
}

void SystemDesign::removePort(const QString& unitId, const QString& port) {
    cables.erase(std::remove_if(cables.begin(), cables.end(),
                                [&](const SysCable& c) {
                                    return (c.aUnit == unitId && c.aPort == port) || (c.bUnit == unitId && c.bPort == port);
                                }),
                 cables.end());
    if (SysUnit* u = unit(unitId))
        u->ports.erase(std::remove_if(u->ports.begin(), u->ports.end(), [&](const SysPort& p) { return p.id == port; }), u->ports.end());
}

void SystemDesign::removeCable(const QString& id) {
    cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const SysCable& c) { return c.id == id; }), cables.end());
}

std::vector<const SysCable*> SystemDesign::cablesAt(const QString& unitId, const QString& port) const {
    std::vector<const SysCable*> out;
    for (const SysCable& c : cables)
        if ((c.aUnit == unitId && c.aPort == port) || (c.bUnit == unitId && c.bPort == port)) out.push_back(&c);
    return out;
}

const SysNote* SystemDesign::note(const QString& id) const {
    for (const SysNote& n : notes)
        if (n.id == id) return &n;
    return nullptr;
}

SysNote* SystemDesign::note(const QString& id) {
    for (SysNote& n : notes)
        if (n.id == id) return &n;
    return nullptr;
}

QString SystemDesign::addNote(double x, double y, const QString& text) {
    SysNote n;
    n.id = freshId(QStringLiteral("n"));
    n.x = x;
    n.y = y;
    n.text = text.isEmpty() ? QStringLiteral("Note") : text;
    notes.push_back(n);
    return n.id;
}

void SystemDesign::removeNote(const QString& id) {
    notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const SysNote& n) { return n.id == id; }), notes.end());
}

std::vector<const SysCable*> SystemDesign::cablesAt(const QString& unitId, const QString& port, bool inside) const {
    std::vector<const SysCable*> out;
    for (const SysCable& c : cables)
        if ((c.aUnit == unitId && c.aPort == port && c.aInside == inside) || (c.bUnit == unitId && c.bPort == port && c.bInside == inside))
            out.push_back(&c);
    return out;
}

int SystemDesign::numberCables() {
    // THE NEXT FREE NUMBER after those the prefix has been given already.
    int highest = 0;
    for (const SysCable& c : cables)
        if (c.pn.startsWith(pnPrefix)) highest = std::max(highest, c.pn.mid(pnPrefix.size()).toInt());
    int given = 0;
    for (SysCable& c : cables) {
        if (!c.pn.trimmed().isEmpty()) continue;
        c.pn = pnPrefix + QStringLiteral("%1").arg(++highest, std::clamp(pnDigits, 1, 9), 10, QLatin1Char('0'));
        ++given;
    }
    return given;
}

SysLinkState SystemDesign::linkState(const SysCable& c) {
    if (c.drawing.isEmpty()) return SysLinkState::None;
    if (!QFileInfo::exists(c.drawing)) return SysLinkState::Missing;
    if (!c.rev.isEmpty() && c.drawingRev.compare(c.rev, Qt::CaseInsensitive) != 0) return SysLinkState::Mismatch;
    return SysLinkState::Match;
}

int SystemDesign::refreshDrawings(const std::function<DrawingInfo(const QString&)>& read) {
    int n = 0;
    for (SysCable& c : cables) {
        if (c.drawing.isEmpty()) continue;
        const DrawingInfo info = read ? read(c.drawing) : readDrawingInfo(c.drawing);
        c.drawingNo = info.drawingNo;
        c.drawingRev = info.revision;
        n += info.found ? 1 : 0;
    }
    return n;
}

std::vector<DesignatorChange> SystemDesign::designatorChanges(const QString& cableId, const DrawingInfo& info) const {
    std::vector<DesignatorChange> out;
    const SysCable* c = cable(cableId);
    if (!c) return out;
    // THE CABLE: the drawing names one, and not this.
    if (info.cables.size() == 1 && info.cables.first().compare(c->name, Qt::CaseInsensitive) != 0)
        out.push_back({true, QString(), QString(), c->name, info.cables.first()});
    // ITS ENDS: the drawing names two connectors - each port at an end given one, where it is not one of them.
    const SysUnit* ua = unit(c->aUnit);
    const SysUnit* ub = unit(c->bUnit);
    const SysPort* pa = ua ? ua->port(c->aPort) : nullptr;
    const SysPort* pb = ub ? ub->port(c->bPort) : nullptr;
    if (!pa || !pb || info.connectors.size() != 2) return out;
    QStringList left = info.connectors;
    const bool aAlike = left.contains(pa->name, Qt::CaseInsensitive);
    const bool bAlike = left.contains(pb->name, Qt::CaseInsensitive);
    if (aAlike) left.removeAll(pa->name);
    if (bAlike) left.removeAll(pb->name);
    if (!aAlike && !left.isEmpty()) out.push_back({false, ua->id, pa->id, pa->name, left.takeFirst()});
    if (!bAlike && !left.isEmpty()) out.push_back({false, ub->id, pb->id, pb->name, left.takeFirst()});
    return out;
}

void SystemDesign::applyDesignatorChanges(const QString& cableId, const std::vector<DesignatorChange>& changes) {
    for (const DesignatorChange& ch : changes) {
        if (ch.cable) {
            if (SysCable* c = cable(cableId)) c->name = ch.to;
        } else if (SysUnit* u = unit(ch.unit)) {
            if (SysPort* p = u->port(ch.port)) p->name = ch.to;
        }
    }
}

std::vector<SysIssue> SystemDesign::check() const {
    std::vector<SysIssue> out;
    auto unitName = [this](const QString& id) {
        const SysUnit* u = unit(id);
        return u ? u->name : id;
    };
    auto portName = [this](const QString& u, const QString& p) {
        const SysUnit* unitP = unit(u);
        const SysPort* port = unitP ? unitP->port(p) : nullptr;
        return QStringLiteral("%1.%2").arg(unitP ? unitP->name : u, port ? port->name : p);
    };
    for (const SysUnit& u : units) {
        if (u.pn.trimmed().isEmpty()) out.push_back({0, QStringLiteral("%1 has no part number").arg(u.name), u.id, QString()});
        for (const SysPort& p : u.ports) {
            // ONE CABLE A SIDE: a port's inside and its outside each take one.
            const size_t in = cablesAt(u.id, p.id, true).size(), out_ = cablesAt(u.id, p.id, false).size();
            if (in + out_ == 0) out.push_back({1, QStringLiteral("%1.%2 - nothing plugged in").arg(u.name, p.name), u.id, QString()});
            if (out_ > 1)
                out.push_back({2, QStringLiteral("%1.%2 - %3 cables plugged into one port").arg(u.name, p.name).arg(out_), u.id, QString()});
            if (in > 1)
                out.push_back({2, QStringLiteral("%1.%2 - %3 cables plugged into its inside").arg(u.name, p.name).arg(in), u.id, QString()});
        }
    }
    QHash<QString, const SysCable*> byPn;
    for (const SysCable& c : cables) {
        const QString ends = QStringLiteral("%1 (%2 - %3)").arg(c.name, portName(c.aUnit, c.aPort), portName(c.bUnit, c.bPort));
        if (c.pn.trimmed().isEmpty()) {
            out.push_back({2, QStringLiteral("%1 has no part number").arg(ends), QString(), c.id});
        } else if (const SysCable* other = byPn.value(c.pn, nullptr)) {
            // The same part number on two cables is the same cable bought twice - unless they are not alike.
            if (other->rev != c.rev || other->drawing != c.drawing || other->wires != c.wires)
                out.push_back({2, QStringLiteral("%1 and %2 have the same part number %3 but are not alike").arg(other->name, c.name, c.pn),
                               QString(), c.id});
        } else {
            byPn.insert(c.pn, &c);
        }
        switch (linkState(c)) {
        case SysLinkState::None: out.push_back({1, QStringLiteral("%1 - no drawing linked").arg(ends), QString(), c.id}); break;
        case SysLinkState::Missing:
            out.push_back({2, QStringLiteral("%1 - its drawing %2 is not there").arg(ends, QFileInfo(c.drawing).fileName()), QString(), c.id});
            break;
        case SysLinkState::Mismatch:
            out.push_back({2, QStringLiteral("%1 - rev %2 called for, the drawing is rev %3")
                                  .arg(ends, c.rev, c.drawingRev.isEmpty() ? QStringLiteral("(none written)") : c.drawingRev),
                           QString(), c.id});
            break;
        case SysLinkState::Match: break;
        }
        if (!c.drawing.isEmpty() && !c.drawingNo.isEmpty() && !c.pn.isEmpty() && c.drawingNo.compare(c.pn, Qt::CaseInsensitive) != 0)
            out.push_back({1, QStringLiteral("%1 - P/N %2, its drawing is No. %3").arg(ends, c.pn, c.drawingNo), QString(), c.id});
    }
    return out;
}

QString SystemDesign::cableListCsv() const {
    QStringList lines{QStringLiteral("Cable,P/N,Rev,From unit,From port,To unit,To port,Length (mm),Wires,Drawing,Drawing No.,Drawing rev,Status")};
    for (const SysCable& c : cables) {
        const SysUnit* a = unit(c.aUnit);
        const SysUnit* b = unit(c.bUnit);
        const SysPort* pa = a ? a->port(c.aPort) : nullptr;
        const SysPort* pb = b ? b->port(c.bPort) : nullptr;
        const SysLinkState st = linkState(c);
        const QString status = st == SysLinkState::None      ? QStringLiteral("not linked")
                               : st == SysLinkState::Missing ? QStringLiteral("drawing missing")
                               : st == SysLinkState::Mismatch ? QStringLiteral("revision differs")
                                                              : QStringLiteral("ok");
        QStringList row{c.name, c.pn, c.rev, a ? a->name : QString(), pa ? pa->name : QString(), b ? b->name : QString(),
                        pb ? pb->name : QString(), c.lengthMm > 0.0 ? QString::number(c.lengthMm, 'f', 0) : QString(),
                        c.wires > 0 ? QString::number(c.wires) : QString(), c.drawing, c.drawingNo, c.drawingRev, status};
        for (QString& s : row) s = csvCell(s);
        lines << row.join(QLatin1Char(','));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QJsonObject SystemDesign::toJson() const {
    QJsonObject o;
    if (!name.isEmpty()) o[QStringLiteral("name")] = name;
    o[QStringLiteral("pnPrefix")] = pnPrefix;
    if (pnDigits != 3) o[QStringLiteral("pnDigits")] = pnDigits;
    QJsonArray us;
    for (const SysUnit& u : units) {
        QJsonObject j{{QStringLiteral("id"), u.id}, {QStringLiteral("name"), u.name}, {QStringLiteral("x"), u.x},
                      {QStringLiteral("y"), u.y},   {QStringLiteral("w"), u.w},       {QStringLiteral("h"), u.h}};
        if (!u.pn.isEmpty()) j[QStringLiteral("pn")] = u.pn;
        if (!u.rev.isEmpty()) j[QStringLiteral("rev")] = u.rev;
        if (!u.description.isEmpty()) j[QStringLiteral("description")] = u.description;
        if (u.color.isValid()) j[QStringLiteral("color")] = u.color.name();
        if (u.kind != QLatin1String("mech")) j[QStringLiteral("kind")] = u.kind;
        QJsonArray ps;
        for (const SysPort& p : u.ports) {
            QJsonObject q{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("side"), p.side},
                          {QStringLiteral("at"), p.at}};
            if (!p.connector.isEmpty()) q[QStringLiteral("connector")] = p.connector;
            if (!p.insidePn.isEmpty()) q[QStringLiteral("insidePn")] = p.insidePn;
            if (p.pins > 0) q[QStringLiteral("pins")] = p.pins;
            ps.append(q);
        }
        j[QStringLiteral("ports")] = ps;
        us.append(j);
    }
    o[QStringLiteral("units")] = us;
    QJsonArray cs;
    for (const SysCable& c : cables) {
        QJsonObject j{{QStringLiteral("id"), c.id},       {QStringLiteral("name"), c.name},   {QStringLiteral("aUnit"), c.aUnit},
                      {QStringLiteral("aPort"), c.aPort}, {QStringLiteral("bUnit"), c.bUnit}, {QStringLiteral("bPort"), c.bPort}};
        if (!c.pn.isEmpty()) j[QStringLiteral("pn")] = c.pn;
        if (!c.rev.isEmpty()) j[QStringLiteral("rev")] = c.rev;
        if (!c.description.isEmpty()) j[QStringLiteral("description")] = c.description;
        if (c.lengthMm > 0.0) j[QStringLiteral("length")] = c.lengthMm;
        if (c.aInside) j[QStringLiteral("aIn")] = true;
        if (c.bInside) j[QStringLiteral("bIn")] = true;
        if (c.wires > 0) j[QStringLiteral("wires")] = c.wires;
        if (c.color.isValid()) j[QStringLiteral("color")] = c.color.name();
        if (!c.drawing.isEmpty()) {
            j[QStringLiteral("drawing")] = c.drawing;
            j[QStringLiteral("drawingNo")] = c.drawingNo;
            j[QStringLiteral("drawingRev")] = c.drawingRev;
        }
        cs.append(j);
    }
    o[QStringLiteral("cables")] = cs;
    if (!notes.empty()) {
        QJsonArray ns;
        for (const SysNote& n : notes)
            ns.append(QJsonObject{{QStringLiteral("id"), n.id}, {QStringLiteral("text"), n.text}, {QStringLiteral("x"), n.x},
                                  {QStringLiteral("y"), n.y},   {QStringLiteral("w"), n.w},       {QStringLiteral("h"), n.h}});
        o[QStringLiteral("notes")] = ns;
    }
    return o;
}

SystemDesign SystemDesign::fromJson(const QJsonObject& o) {
    SystemDesign d;
    d.name = o.value(QStringLiteral("name")).toString();
    d.pnPrefix = o.value(QStringLiteral("pnPrefix")).toString(QStringLiteral("CBL-"));
    d.pnDigits = std::clamp(o.value(QStringLiteral("pnDigits")).toInt(3), 1, 9);
    for (const QJsonValue& v : o.value(QStringLiteral("notes")).toArray()) {
        const QJsonObject j = v.toObject();
        SysNote n;
        n.id = j.value(QStringLiteral("id")).toString();
        n.text = j.value(QStringLiteral("text")).toString();
        n.x = j.value(QStringLiteral("x")).toDouble();
        n.y = j.value(QStringLiteral("y")).toDouble();
        n.w = j.value(QStringLiteral("w")).toDouble(180.0);
        n.h = j.value(QStringLiteral("h")).toDouble(70.0);
        if (!n.id.isEmpty()) d.notes.push_back(n);
    }
    for (const QJsonValue& v : o.value(QStringLiteral("units")).toArray()) {
        const QJsonObject j = v.toObject();
        SysUnit u;
        u.id = j.value(QStringLiteral("id")).toString();
        u.name = j.value(QStringLiteral("name")).toString();
        u.pn = j.value(QStringLiteral("pn")).toString();
        u.rev = j.value(QStringLiteral("rev")).toString();
        u.description = j.value(QStringLiteral("description")).toString();
        u.kind = j.value(QStringLiteral("kind")).toString(QStringLiteral("mech"));
        u.x = j.value(QStringLiteral("x")).toDouble();
        u.y = j.value(QStringLiteral("y")).toDouble();
        u.w = j.value(QStringLiteral("w")).toDouble(160.0);
        u.h = j.value(QStringLiteral("h")).toDouble(100.0);
        if (j.contains(QStringLiteral("color"))) u.color = QColor(j.value(QStringLiteral("color")).toString());
        for (const QJsonValue& pv : j.value(QStringLiteral("ports")).toArray()) {
            const QJsonObject q = pv.toObject();
            SysPort p;
            p.id = q.value(QStringLiteral("id")).toString();
            p.name = q.value(QStringLiteral("name")).toString();
            p.side = q.value(QStringLiteral("side")).toString(QStringLiteral("right"));
            p.at = q.value(QStringLiteral("at")).toDouble(0.5);
            p.connector = q.value(QStringLiteral("connector")).toString();
            p.insidePn = q.value(QStringLiteral("insidePn")).toString();
            p.pins = q.value(QStringLiteral("pins")).toInt(0);
            if (!p.id.isEmpty()) u.ports.push_back(p);
        }
        if (!u.id.isEmpty()) d.units.push_back(u);
    }
    for (const QJsonValue& v : o.value(QStringLiteral("cables")).toArray()) {
        const QJsonObject j = v.toObject();
        SysCable c;
        c.id = j.value(QStringLiteral("id")).toString();
        c.name = j.value(QStringLiteral("name")).toString();
        c.pn = j.value(QStringLiteral("pn")).toString();
        c.rev = j.value(QStringLiteral("rev")).toString();
        c.description = j.value(QStringLiteral("description")).toString();
        c.aUnit = j.value(QStringLiteral("aUnit")).toString();
        c.aPort = j.value(QStringLiteral("aPort")).toString();
        c.bUnit = j.value(QStringLiteral("bUnit")).toString();
        c.bPort = j.value(QStringLiteral("bPort")).toString();
        c.lengthMm = j.value(QStringLiteral("length")).toDouble(0.0);
        c.wires = j.value(QStringLiteral("wires")).toInt(0);
        c.aInside = j.value(QStringLiteral("aIn")).toBool(false);
        c.bInside = j.value(QStringLiteral("bIn")).toBool(false);
        if (j.contains(QStringLiteral("color"))) c.color = QColor(j.value(QStringLiteral("color")).toString());
        c.drawing = j.value(QStringLiteral("drawing")).toString();
        c.drawingNo = j.value(QStringLiteral("drawingNo")).toString();
        c.drawingRev = j.value(QStringLiteral("drawingRev")).toString();
        // A cable to a unit or port that is not there is dropped, not kept dangling.
        const SysUnit* a = d.unit(c.aUnit);
        const SysUnit* b = d.unit(c.bUnit);
        if (!c.id.isEmpty() && a && b && a->port(c.aPort) && b->port(c.bPort)) d.cables.push_back(c);
    }
    return d;
}

} // namespace Edraw_Desktop
