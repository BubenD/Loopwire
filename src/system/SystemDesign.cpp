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
    // A CELL'S TEXT: written as text, or as a cell of its own with text in it.
    auto cellText = [](const QJsonValue& v) {
        return v.isObject() ? v.toObject().value(QStringLiteral("text")).toString().trimmed() : v.toString().trimmed();
    };
    // "X1-3", "X1:3", "X1.3", "X1/3", "X1 pin 3": the connector and the pin.
    static const QRegularExpression end(QStringLiteral("^([A-Za-z][A-Za-z0-9_]*?)\\s*(?:[-:./]|\\s+pin\\s*|\\s+)\\s*([A-Za-z0-9]+)$"),
                                        QRegularExpression::CaseInsensitiveOption);
    for (const Entity& e : ents) {
        // A CONNECTION LIST: a table with From and To columns - each row a connection.
        if (e.meta.value(QStringLiteral("table")).toBool() && !e.meta.value(QStringLiteral("revisionTable")).toBool()) {
            const int cols = e.meta.value(QStringLiteral("cols")).toInt();
            const int rows = e.meta.value(QStringLiteral("rows")).toInt();
            const QJsonArray cells = e.meta.value(QStringLiteral("cells")).toArray();
            int fromCol = -1, toCol = -1, nameCol = -1;
            for (int c = 0; c < cols && c < cells.size(); ++c) {
                const QString h = cellText(cells.at(c)).toLower();
                if (h == QLatin1String("from")) fromCol = c;
                else if (h == QLatin1String("to")) toCol = c;
                else if (nameCol < 0 && (h == QLatin1String("signal") || h == QLatin1String("name") || h == QLatin1String("line") ||
                                         h == QLatin1String("label") || h == QLatin1String("net") || h == QLatin1String("function")))
                    nameCol = c;
            }
            if (fromCol >= 0 && toCol >= 0) {
                info.hasConnectionList = true;
                info.hasLineNames = info.hasLineNames || nameCol >= 0;
                for (int r = 1; r < rows; ++r) {
                    const QRegularExpressionMatch a = end.match(cellText(cells.at(r * cols + fromCol)));
                    const QRegularExpressionMatch b = end.match(cellText(cells.at(r * cols + toCol)));
                    if (!a.hasMatch() || !b.hasMatch()) continue;
                    info.connections.push_back({a.captured(1), a.captured(2), b.captured(1), b.captured(2),
                                                nameCol >= 0 ? cellText(cells.at(r * cols + nameCol)) : QString()});
                }
            }
        }
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

std::vector<QString> SystemDesign::childrenOf(const QString& id) const {
    std::vector<QString> out;
    for (const SysUnit& u : units)
        if (u.parent == id && u.id != id) out.push_back(u.id);
    return out;
}

bool SystemDesign::isWithin(const QString& id, const QString& ancestor) const {
    if (ancestor.isEmpty()) return false;
    QString at = id;
    for (int guard = 0; guard < 64; ++guard) {
        const SysUnit* u = unit(at);
        if (!u || u->parent.isEmpty()) return false;
        if (u->parent == ancestor) return true;
        at = u->parent;
    }
    return false;
}

int SystemDesign::depth(const QString& id) const {
    int n = 0;
    QString at = id;
    for (int guard = 0; guard < 64; ++guard) {
        const SysUnit* u = unit(at);
        if (!u || u->parent.isEmpty()) break;
        ++n;
        at = u->parent;
    }
    return n;
}

void SystemDesign::moveUnit(const QString& id, double dx, double dy) {
    for (SysUnit& u : units)
        if (u.id == id || isWithin(u.id, id)) {
            u.x += dx;
            u.y += dy;
        }
}

QRectF SystemDesign::contentsOf(const QString& id) const {
    QRectF box;
    for (const SysUnit& u : units)
        if (isWithin(u.id, id)) box = box.isNull() ? u.rect() : box.united(u.rect());
    return box;
}

QString SystemDesign::unitAround(const QPointF& point, const QString& except) const {
    QString best;
    int deepest = -1;
    for (const SysUnit& u : units) {
        if (!except.isEmpty() && (u.id == except || isWithin(u.id, except))) continue;
        if (!u.rect().contains(point)) continue;
        const int d = depth(u.id);
        if (d > deepest) {
            deepest = d;
            best = u.id;
        }
    }
    return best;
}

const std::vector<SysRule>& SystemDesign::rules() {
    static const std::vector<SysRule> all{
        {QStringLiteral("unit-pn"), QStringLiteral("A unit has a part number"), 0},
        {QStringLiteral("port-free"), QStringLiteral("Every port has a cable plugged in"), 1},
        {QStringLiteral("port-crowded"), QStringLiteral("One cable a side of a port"), 2},
        {QStringLiteral("port-connector"), QStringLiteral("Every port has its connector's P/N"), 0},
        {QStringLiteral("cable-pn"), QStringLiteral("Every cable has a part number"), 2},
        {QStringLiteral("cable-same-pn"), QStringLiteral("Cables with one part number are alike"), 2},
        {QStringLiteral("cable-drawing"), QStringLiteral("Every cable has its drawing linked"), 1},
        {QStringLiteral("drawing-missing"), QStringLiteral("A cable's drawing is there"), 2},
        {QStringLiteral("drawing-rev"), QStringLiteral("A cable's drawing is the revision called for"), 2},
        {QStringLiteral("drawing-no"), QStringLiteral("A cable's drawing number is its part number"), 1},
        {QStringLiteral("note-link"), QStringLiteral("A note is linked to something there"), 1},
    };
    return all;
}

void SystemDesign::removeUnit(const QString& id) {
    // EVERYTHING INSIDE IT goes with it; a note about it about nothing now.
    for (const QString& child : childrenOf(id)) removeUnit(child);
    for (SysNote& n : notes)
        if (n.linkUnit == id) {
            n.linkUnit.clear();
            n.linkPort.clear();
        }
    cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const SysCable& c) { return c.aUnit == id || c.bUnit == id; }),
                 cables.end());
    units.erase(std::remove_if(units.begin(), units.end(), [&](const SysUnit& u) { return u.id == id; }), units.end());
}

void SystemDesign::removePort(const QString& unitId, const QString& port) {
    for (SysNote& n : notes)
        if (n.linkUnit == unitId && n.linkPort == port) n.linkPort.clear();
    cables.erase(std::remove_if(cables.begin(), cables.end(),
                                [&](const SysCable& c) {
                                    return (c.aUnit == unitId && c.aPort == port) || (c.bUnit == unitId && c.bPort == port);
                                }),
                 cables.end());
    if (SysUnit* u = unit(unitId))
        u->ports.erase(std::remove_if(u->ports.begin(), u->ports.end(), [&](const SysPort& p) { return p.id == port; }), u->ports.end());
}

void SystemDesign::removeCable(const QString& id) {
    for (SysNote& n : notes)
        if (n.linkCable == id) n.linkCable.clear();
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
    std::vector<SysIssue> all;
    // EACH FOUND BY ITS RULE - those switched off left out at the end.
    struct Adder {
        std::vector<SysIssue>& to;
        QString rule;
        void push_back(SysIssue i) {
            i.rule = rule;
            to.push_back(std::move(i));
        }
    };
    Adder out{all, QString()};
    auto rule = [&out](const char* id) -> Adder& {
        out.rule = QString::fromLatin1(id);
        return out;
    };
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
        if (u.pn.trimmed().isEmpty()) rule("unit-pn").push_back({0, QStringLiteral("%1 has no part number").arg(u.name), u.id, QString()});
        for (const SysPort& p : u.ports) {
            // ONE CABLE A SIDE: a port's inside and its outside each take one.
            const size_t in = cablesAt(u.id, p.id, true).size(), out_ = cablesAt(u.id, p.id, false).size();
            if (in + out_ == 0)
                rule("port-free").push_back({1, QStringLiteral("%1.%2 - nothing plugged in").arg(u.name, p.name), u.id, QString(), QString(), p.id});
            if (out_ > 1)
                rule("port-crowded").push_back({2, QStringLiteral("%1.%2 - %3 cables plugged into one port").arg(u.name, p.name).arg(out_), u.id,
                                                QString(), QString(), p.id});
            if (in > 1)
                rule("port-crowded").push_back({2, QStringLiteral("%1.%2 - %3 cables plugged into its inside").arg(u.name, p.name).arg(in),
                                                u.id, QString(), QString(), p.id});
            if (p.connector.trimmed().isEmpty())
                rule("port-connector").push_back({0, QStringLiteral("%1.%2 has no connector P/N").arg(u.name, p.name), u.id, QString(),
                                                  QString(), p.id});
        }
    }
    QHash<QString, const SysCable*> byPn;
    for (const SysCable& c : cables) {
        const QString ends = QStringLiteral("%1 (%2 - %3)").arg(c.name, portName(c.aUnit, c.aPort), portName(c.bUnit, c.bPort));
        if (c.pn.trimmed().isEmpty()) {
            rule("cable-pn").push_back({2, QStringLiteral("%1 has no part number").arg(ends), QString(), c.id});
        } else if (const SysCable* other = byPn.value(c.pn, nullptr)) {
            // The same part number on two cables is the same cable bought twice - unless they are not alike.
            if (other->rev != c.rev || other->drawing != c.drawing || other->wires != c.wires)
                rule("cable-same-pn").push_back({2, QStringLiteral("%1 and %2 have the same part number %3 but are not alike").arg(other->name, c.name, c.pn),
                               QString(), c.id});
        } else {
            byPn.insert(c.pn, &c);
        }
        switch (linkState(c)) {
        case SysLinkState::None: rule("cable-drawing").push_back({1, QStringLiteral("%1 - no drawing linked").arg(ends), QString(), c.id}); break;
        case SysLinkState::Missing:
            rule("drawing-missing").push_back({2, QStringLiteral("%1 - its drawing %2 is not there").arg(ends, QFileInfo(c.drawing).fileName()), QString(), c.id});
            break;
        case SysLinkState::Mismatch:
            rule("drawing-rev").push_back({2, QStringLiteral("%1 - rev %2 called for, the drawing is rev %3")
                                  .arg(ends, c.rev, c.drawingRev.isEmpty() ? QStringLiteral("(none written)") : c.drawingRev),
                           QString(), c.id});
            break;
        case SysLinkState::Match: break;
        }
        if (!c.drawing.isEmpty() && !c.drawingNo.isEmpty() && !c.pn.isEmpty() && c.drawingNo.compare(c.pn, Qt::CaseInsensitive) != 0)
            rule("drawing-no").push_back({1, QStringLiteral("%1 - P/N %2, its drawing is No. %3").arg(ends, c.pn, c.drawingNo), QString(), c.id});
    }
    for (const SysNote& n : notes) {
        const SysUnit* u = unit(n.linkUnit);
        const bool gone = (!n.linkUnit.isEmpty() && !u) || (!n.linkPort.isEmpty() && (!u || !u->port(n.linkPort))) ||
                          (!n.linkCable.isEmpty() && !cable(n.linkCable));
        if (gone) rule("note-link").push_back({1, QStringLiteral("A note is linked to something no longer there"), QString(), QString(), QString(), QString(), n.id});
    }
    std::vector<SysIssue> on;
    for (SysIssue& i : all)
        if (ruleOn(i.rule)) on.push_back(std::move(i));
    return on;
}

std::vector<SysConnection> SystemDesign::checkConnections(const std::function<DrawingInfo(const QString&)>& read) const {
    std::vector<SysConnection> out;
    // WHAT EACH PIN OF EACH PORT CARRIES, by the cables plugged into it: the line's name, which cable says so.
    struct Said {
        QString name, cable;
        size_t row;
    };
    QHash<QString, std::vector<Said>> onPin;   // "unit|port|pin"
    for (const SysCable& c : cables) {
        if (c.drawing.isEmpty()) continue;
        const DrawingInfo info = read ? read(c.drawing) : readDrawingInfo(c.drawing);
        if (!info.found) continue;
        const SysUnit* ua = unit(c.aUnit);
        const SysUnit* ub = unit(c.bUnit);
        const SysPort* pa = ua ? ua->port(c.aPort) : nullptr;
        const SysPort* pb = ub ? ub->port(c.bPort) : nullptr;
        if (!pa || !pb) continue;
        if (!info.hasConnectionList) {
            out.push_back({c.name, QStringLiteral("%1.%2").arg(ua->name, pa->name), QStringLiteral("%1.%2").arg(ub->name, pb->name), QString(),
                           QStringLiteral("no connection list in its drawing (a table with From and To columns)"), 1});
            continue;
        }
        for (const DrawingInfo::Connection& k : info.connections) {
            // EACH END TO THE PORT OF THE CABLE IT NAMES - by the port's name.
            auto endOf = [&](const QString& conn, const SysUnit*& u, const SysPort*& p) {
                if (conn.compare(pa->name, Qt::CaseInsensitive) == 0) {
                    u = ua;
                    p = pa;
                } else if (conn.compare(pb->name, Qt::CaseInsensitive) == 0) {
                    u = ub;
                    p = pb;
                }
            };
            const SysUnit* fu = nullptr;
            const SysPort* fp = nullptr;
            const SysUnit* tu = nullptr;
            const SysPort* tp = nullptr;
            endOf(k.fromConn, fu, fp);
            endOf(k.toConn, tu, tp);
            SysConnection row;
            row.cable = c.name;
            row.from = fu ? QStringLiteral("%1.%2 pin %3").arg(fu->name, fp->name, k.fromPin) : QStringLiteral("%1 pin %2").arg(k.fromConn, k.fromPin);
            row.to = tu ? QStringLiteral("%1.%2 pin %3").arg(tu->name, tp->name, k.toPin) : QStringLiteral("%1 pin %2").arg(k.toConn, k.toPin);
            row.name = k.name;
            if (!fu || !tu) {
                row.status = QStringLiteral("%1 is at neither end of %2 (%3.%4, %5.%6)")
                                 .arg(!fu ? k.fromConn : k.toConn, c.name, ua->name, pa->name, ub->name, pb->name);
                row.level = 2;
            } else if (info.hasLineNames && k.name.isEmpty()) {
                row.status = QStringLiteral("the line has no name");
                row.level = 0;
            }
            out.push_back(row);
            if (fu) onPin[QStringLiteral("%1|%2|%3").arg(fu->id, fp->id, k.fromPin.toUpper())].push_back({k.name, c.name, out.size() - 1});
            if (tu) onPin[QStringLiteral("%1|%2|%3").arg(tu->id, tp->id, k.toPin.toUpper())].push_back({k.name, c.name, out.size() - 1});
        }
    }
    // ONE PIN, TWO CABLES (one each side of the port): the line named alike by both.
    for (auto it = onPin.cbegin(); it != onPin.cend(); ++it) {
        const std::vector<Said>& said = it.value();
        for (size_t i = 0; i < said.size(); ++i)
            for (size_t j = i + 1; j < said.size(); ++j) {
                if (said[i].cable == said[j].cable || said[i].name.isEmpty() || said[j].name.isEmpty()) continue;
                if (said[i].name.compare(said[j].name, Qt::CaseInsensitive) == 0) continue;
                const QStringList key = it.key().split(QLatin1Char('|'));
                const SysUnit* u = unit(key.value(0));
                const SysPort* p = u ? u->port(key.value(1)) : nullptr;
                for (const size_t r : {said[i].row, said[j].row}) {
                    out[r].status = QStringLiteral("named %1 by %2 and %3 by %4 on %5.%6 pin %7")
                                        .arg(said[i].name, said[i].cable, said[j].name, said[j].cable, u ? u->name : QString(),
                                             p ? p->name : QString(), key.value(2));
                    out[r].level = 2;
                }
            }
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
    if (!rulesOff.isEmpty()) o[QStringLiteral("rulesOff")] = QJsonArray::fromStringList(rulesOff);
    QJsonArray us;
    for (const SysUnit& u : units) {
        QJsonObject j{{QStringLiteral("id"), u.id}, {QStringLiteral("name"), u.name}, {QStringLiteral("x"), u.x},
                      {QStringLiteral("y"), u.y},   {QStringLiteral("w"), u.w},       {QStringLiteral("h"), u.h}};
        if (!u.pn.isEmpty()) j[QStringLiteral("pn")] = u.pn;
        if (!u.rev.isEmpty()) j[QStringLiteral("rev")] = u.rev;
        if (!u.description.isEmpty()) j[QStringLiteral("description")] = u.description;
        if (u.color.isValid()) j[QStringLiteral("color")] = u.color.name();
        if (u.kind != QLatin1String("mech")) j[QStringLiteral("kind")] = u.kind;
        if (u.opacity < 1.0) j[QStringLiteral("opacity")] = u.opacity;
        j[QStringLiteral("parent")] = u.parent;
        if (!u.hidden.isEmpty()) j[QStringLiteral("hide")] = QJsonArray::fromStringList(u.hidden);
        QJsonArray ps;
        for (const SysPort& p : u.ports) {
            QJsonObject q{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("side"), p.side},
                          {QStringLiteral("at"), p.at}};
            if (!p.connector.isEmpty()) q[QStringLiteral("connector")] = p.connector;
            if (!p.contactPn.isEmpty()) q[QStringLiteral("contactPn")] = p.contactPn;
            if (p.labelDx != 0.0 || p.labelDy != 0.0) {
                q[QStringLiteral("ldx")] = p.labelDx;
                q[QStringLiteral("ldy")] = p.labelDy;
            }
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
        if (c.hidden != QStringList{QStringLiteral("desc")}) j[QStringLiteral("hide")] = QJsonArray::fromStringList(c.hidden);
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
        for (const SysNote& n : notes) {
            QJsonObject j{{QStringLiteral("id"), n.id}, {QStringLiteral("text"), n.text}, {QStringLiteral("x"), n.x},
                          {QStringLiteral("y"), n.y},   {QStringLiteral("w"), n.w},       {QStringLiteral("h"), n.h}};
            if (n.color.isValid()) j[QStringLiteral("color")] = n.color.name();
            if (!n.linkUnit.isEmpty()) j[QStringLiteral("linkUnit")] = n.linkUnit;
            if (!n.linkPort.isEmpty()) j[QStringLiteral("linkPort")] = n.linkPort;
            if (!n.linkCable.isEmpty()) j[QStringLiteral("linkCable")] = n.linkCable;
            ns.append(j);
        }
        o[QStringLiteral("notes")] = ns;
    }
    return o;
}

SystemDesign SystemDesign::fromJson(const QJsonObject& o) {
    SystemDesign d;
    d.name = o.value(QStringLiteral("name")).toString();
    d.pnPrefix = o.value(QStringLiteral("pnPrefix")).toString(QStringLiteral("CBL-"));
    d.pnDigits = std::clamp(o.value(QStringLiteral("pnDigits")).toInt(3), 1, 9);
    for (const QJsonValue& r : o.value(QStringLiteral("rulesOff")).toArray()) d.rulesOff << r.toString();
    for (const QJsonValue& v : o.value(QStringLiteral("notes")).toArray()) {
        const QJsonObject j = v.toObject();
        SysNote n;
        n.id = j.value(QStringLiteral("id")).toString();
        n.text = j.value(QStringLiteral("text")).toString();
        n.x = j.value(QStringLiteral("x")).toDouble();
        n.y = j.value(QStringLiteral("y")).toDouble();
        n.w = j.value(QStringLiteral("w")).toDouble(180.0);
        n.h = j.value(QStringLiteral("h")).toDouble(70.0);
        if (j.contains(QStringLiteral("color"))) n.color = QColor(j.value(QStringLiteral("color")).toString());
        n.linkUnit = j.value(QStringLiteral("linkUnit")).toString();
        n.linkPort = j.value(QStringLiteral("linkPort")).toString();
        n.linkCable = j.value(QStringLiteral("linkCable")).toString();
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
        u.opacity = std::clamp(j.value(QStringLiteral("opacity")).toDouble(1.0), 0.05, 1.0);
        u.parent = j.value(QStringLiteral("parent")).toString();
        // Saved before units went in units: none is in another yet - found below by where it lies.
        if (!j.contains(QStringLiteral("parent"))) u.parent = QStringLiteral("?");
        for (const QJsonValue& h : j.value(QStringLiteral("hide")).toArray()) u.hidden << h.toString();
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
            // An inside P/N of a design saved when a port had two: its connector's, where it had none for its outside.
            if (p.connector.isEmpty()) p.connector = q.value(QStringLiteral("insidePn")).toString();
            p.contactPn = q.value(QStringLiteral("contactPn")).toString();
            p.labelDx = q.value(QStringLiteral("ldx")).toDouble(0.0);
            p.labelDy = q.value(QStringLiteral("ldy")).toDouble(0.0);
            p.pins = q.value(QStringLiteral("pins")).toInt(0);
            if (!p.id.isEmpty()) u.ports.push_back(p);
        }
        if (!u.id.isEmpty()) d.units.push_back(u);
    }
    for (SysUnit& u : d.units) {
        if (u.parent != QLatin1String("?")) continue;
        u.parent.clear();
        double area = 1e300;
        for (const SysUnit& o2 : d.units)
            if (o2.id != u.id && o2.rect().contains(u.rect()) && o2.w * o2.h < area && o2.w * o2.h > u.w * u.h) {
                area = o2.w * o2.h;
                u.parent = o2.id;
            }
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
        if (j.contains(QStringLiteral("hide"))) {
            c.hidden.clear();
            for (const QJsonValue& h : j.value(QStringLiteral("hide")).toArray()) c.hidden << h.toString();
        }
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
