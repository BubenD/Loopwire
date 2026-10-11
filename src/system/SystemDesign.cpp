#include "system/SystemDesign.h"

#include "analysis/VoltageDrop.h"
#include "harness/Harness.h"
#include "io/Dwx.h"
#include "io/Dxf.h"
#include "model/Document.h"
#include "model/DrawingDetails.h"
#include "system/SystemRouting.h"
#include "system/SystemExport.h"

#include <QDate>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
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
    // THE ELECTRICAL PATHS, using the same pin/joint resolution as the electrical analysis. A joined path may
    // branch, but every wire on it must carry the same name. Anonymous entity ids are never signal names.
    auto readPaths = [&](const Document& sheet) {
        const VdGraph graph = buildVoltageDropGraph(sheet);
        info.schematic = info.schematic || !graph.segments.empty();
        QHash<QString, QString> names;
        QHash<QString, int> connectorCounts;
        QSet<QString> conflictingLabels;
        for (const Entity& e : sheet.entities()) {
            if (e.type == EntityType::Connector) ++connectorCounts[connectorSpecFromMeta(e.meta).designator];
            if (e.type == EntityType::Wire) names[e.id] = e.meta.value(QStringLiteral("wireLabel")).toString().trimmed();
        }
        for (const Entity& e : sheet.entities()) {
            if (e.type != EntityType::NetLabel) continue;
            const QString wid = e.meta.value(QStringLiteral("wireId")).toString();
            const QString label = e.meta.value(QStringLiteral("text")).toString().trimmed();
            if (!wid.isEmpty() && !label.isEmpty()) {
                if (names.value(wid).isEmpty()) names[wid] = label;
                else if (names.value(wid) != label) { names[wid] += QStringLiteral(" / ") + label; conflictingLabels.insert(wid); }
            }
        }
        QSet<QString> visited;
        for (const VdSegment& start : graph.segments) {
            if (visited.contains(start.id)) continue;
            QStringList todo{start.fromNode}, terminals, signalNames;
            bool open = false, unnamed = false, conflict = false;
            QSet<QString> nodes;
            while (!todo.isEmpty()) {
                const QString nodeId = todo.takeLast();
                if (nodes.contains(nodeId)) continue;
                nodes.insert(nodeId);
                const VdNode* node = graph.node(nodeId);
                if (node && node->kind == VdNodeKind::Pin) terminals << nodeId;
                if (node && node->kind == VdNodeKind::OpenEnd && graph.incidentTo(nodeId).size() < 2) open = true;
                for (const VdSegment* seg : graph.incidentTo(nodeId)) {
                    visited.insert(seg->id);
                    conflict = conflict || conflictingLabels.contains(seg->wireId);
                    const QString label = names.value(seg->wireId);
                    if (label.isEmpty()) unnamed = true;
                    else if (!signalNames.contains(label)) signalNames << label;
                    todo << VdGraph::other(*seg, nodeId);
                }
            }
            QStringList problems;
            int level = 0;
            if (open || terminals.size() < 2) { problems << QStringLiteral("Open or unresolved connection"); level = 2; }
            if (signalNames.size() > 1 || conflict) {
                problems << QStringLiteral("Line names differ along the electrical path"); level = 2;
            }
            if (unnamed || signalNames.isEmpty()) { problems << QStringLiteral("A wire has no line name"); level = std::max(level, 1); }
            auto end = [&](const QString& nodeId) {
                const VdNode* node = graph.node(nodeId);
                QPair<QString, QString> result;
                if (!node) return result;
                for (const Entity& e : sheet.entities()) if (e.id == node->connectorId) {
                    const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
                    result.first = spec.designator;
                    result.second = node->pin <= spec.pins.size() ? spec.pins.value(node->pin - 1) : QString::number(node->pin);
                    if (result.second.isEmpty()) result.second = connectorPinLabel(node->pin - 1, spec);
                    break;
                }
                return result;
            };
            for (const QString& terminal : terminals) {
                const auto contact = end(terminal);
                if (contact.first.isEmpty() || connectorCounts.value(contact.first) > 1) {
                    problems << QStringLiteral("Connector designator is empty or duplicated"); level = 2; break;
                }
            }
            const auto a = terminals.isEmpty() ? QPair<QString, QString>() : end(terminals.first());
            const int rows = std::max(1, static_cast<int>(terminals.size()) - 1);
            for (int i = 0; i < rows; ++i) {
                const auto b = terminals.size() > i + 1 ? end(terminals.at(i + 1)) : QPair<QString, QString>();
                info.connections.push_back({signalNames.join(QStringLiteral(" / ")), a.first, a.second, b.first, b.second,
                                            problems.join(QStringLiteral("; ")), level});
            }
        }
    };
    readPaths(doc);
    for (const DrawingLayout& layout : doc.layouts()) {
        Document sheet(layout.name); sheet.entities() = layout.paperEntities; readPaths(sheet);
    }
    info.revision = !fromTable.isEmpty() ? fromTable : !fromBoxes.isEmpty() ? fromBoxes : fromText;
    const QJsonObject details=effectiveDrawingDetails(doc);
    if(details.contains(QStringLiteral("drawingNo")))info.drawingNo=details.value(QStringLiteral("drawingNo")).toString();
    if(details.contains(QStringLiteral("revision")))info.revision=details.value(QStringLiteral("revision")).toString();
    if(details.contains(QStringLiteral("title")))info.title=details.value(QStringLiteral("title")).toString();
    for(const auto& kind:{QStringLiteral("schematic"),QStringLiteral("mechanical")})
        if(!doc.validationPassed(kind))info.fieldIssues<<QStringLiteral("%1 DRC passed note is missing or outdated; run the check and save the drawing.").arg(kind);
    for(const DrawingFieldIssue& field:checkDrawingDetails(doc))info.fieldIssues<<field.message+QStringLiteral(" [%1]").arg(field.entityId);
    return info;
}

QString SysIssue::key() const {
    const QJsonArray identity{rule, unit, port, cable, note, what};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                                     QCryptographicHash::Sha256).toHex());
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
    // ACCEPTED FINDINGS retain their target identity even after a part is removed.
    for (const SysIssue& accepted : bypassed) {
        taken.insert(accepted.unit); taken.insert(accepted.port); taken.insert(accepted.cable); taken.insert(accepted.note);
    }
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

bool SystemDesign::addBranch(const QString& cableId, const QString& unitId, const QString& portId) {
    SysCable* c = cable(cableId);
    const SysUnit* u = unit(unitId);
    if (!c || !u || !u->port(portId)) return false;
    if ((c->aUnit == unitId && c->aPort == portId) || (c->bUnit == unitId && c->bPort == portId)) return false;
    for (const SysBranch& b : c->branches)
        if (b.unit == unitId && b.port == portId) return false;
    SysBranch b;
    b.unit = unitId;
    b.port = portId;
    // ON THE PORT'S INSIDE where the trunk is within that unit, as for the trunk's own ends.
    b.inside = unitId == c->aUnit || unitId == c->bUnit || isWithin(c->aUnit, unitId) || isWithin(c->bUnit, unitId);
    c->branches.push_back(b);
    return true;
}

QJsonObject SystemDesign::copyPiece(const QStringList& unitIds, const QStringList& cableIds, const QStringList& noteIds) const {
    // THE UNITS, with every unit inside them; the cables wholly among them (and those asked for, if their ends come too);
    // the notes asked for.
    QSet<QString> taken;
    for (const SysUnit& u : units)
        for (const QString& id : unitIds)
            if (u.id == id || isWithin(u.id, id)) taken.insert(u.id);
    SystemDesign piece;
    for (const SysUnit& u : units)
        if (taken.contains(u.id)) {
            SysUnit copy = u;
            if (!taken.contains(copy.parent)) copy.parent.clear();
            piece.units.push_back(copy);
        }
    for (const SysCable& c : cables) {
        if (!taken.contains(c.aUnit) || !taken.contains(c.bUnit)) continue;
        Q_UNUSED(cableIds);
        SysCable copy = c;
        copy.branches.erase(std::remove_if(copy.branches.begin(), copy.branches.end(), [&](const SysBranch& b) { return !taken.contains(b.unit); }),
                            copy.branches.end());
        piece.cables.push_back(copy);
    }
    for (const SysNote& n : notes)
        if (noteIds.contains(n.id)) {
            SysNote copy = n;
            if (!taken.contains(copy.unit) && !copy.unit.isEmpty()) { copy.unit.clear(); copy.port.clear(); }
            bool cableCopied = false;
            for (const SysCable& c : piece.cables) cableCopied = cableCopied || c.id == copy.cable;
            if (!cableCopied) copy.cable.clear();
            piece.notes.push_back(copy);
        }
    QJsonObject o = piece.toJson();
    o[QStringLiteral("piece")] = true;
    return o;
}

QStringList SystemDesign::pastePiece(const QJsonObject& o, const QPointF& offset) {
    const SystemDesign piece = SystemDesign::fromJson(o);
    QHash<QString, QString> unitOf, portOf, cableOf;
    QStringList added;
    QSet<QString> unitNames, cableNames;
    for (const SysUnit& u : units) unitNames.insert(u.name);
    for (const SysCable& c : cables) cableNames.insert(c.name);
    // A NAME THAT IS FREE: the one copied where it is, the next of its kind (U3, W4) where it is taken.
    auto freeName = [](const QString& name, QSet<QString>& names, const QString& kind) {
        QString out = name;
        if (out.isEmpty() || names.contains(out)) {
            int n = 1;
            while (names.contains(kind + QString::number(n))) ++n;
            out = kind + QString::number(n);
        }
        names.insert(out);
        return out;
    };
    for (const SysUnit& u : piece.units) {
        SysUnit copy = u;
        copy.id = freshId(QStringLiteral("u"));
        unitOf.insert(u.id, copy.id);
        copy.name = freeName(u.name, unitNames, QStringLiteral("U"));
        copy.x += offset.x();
        copy.y += offset.y();
        units.push_back(copy);
        // Its ports' ids fresh too, one by one so none clashes with another just given.
        for (SysPort& p : units.back().ports) {
            const QString old = p.id;
            p.id = QStringLiteral("~");
            p.id = freshId(QStringLiteral("p"));
            portOf.insert(u.id + QLatin1Char('/') + old, p.id);
        }
        added << copy.id;
    }
    for (SysUnit& u : units)
        if (unitOf.values().contains(u.id) && !u.parent.isEmpty()) u.parent = unitOf.value(u.parent);
    for (const SysCable& c : piece.cables) {
        SysCable copy = c;
        copy.id = freshId(QStringLiteral("c"));
        cableOf.insert(c.id, copy.id);
        copy.name = freeName(c.name, cableNames, QStringLiteral("W"));
        copy.aPort = portOf.value(c.aUnit + QLatin1Char('/') + c.aPort);
        copy.bPort = portOf.value(c.bUnit + QLatin1Char('/') + c.bPort);
        copy.aUnit = unitOf.value(c.aUnit);
        copy.bUnit = unitOf.value(c.bUnit);
        for (SysBranch& b : copy.branches) {
            b.port = portOf.value(b.unit + QLatin1Char('/') + b.port);
            b.unit = unitOf.value(b.unit);
        }
        for (QPointF& w : copy.waypoints) w += offset;
        if (copy.aUnit.isEmpty() || copy.bUnit.isEmpty()) continue;
        cables.push_back(copy);
        added << copy.id;
    }
    for (const SysNote& n : piece.notes) {
        SysNote copy = n;
        copy.id = freshId(QStringLiteral("n"));
        copy.x += offset.x();
        copy.y += offset.y();
        if (!copy.unit.isEmpty()) {
            copy.port = portOf.value(copy.unit + QLatin1Char('/') + copy.port);
            copy.unit = unitOf.value(copy.unit);
        }
        copy.cable = cableOf.value(copy.cable);
        notes.push_back(copy);
        added << copy.id;
    }
    return added;
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

void SystemDesign::removeUnit(const QString& id, bool removeContents) {
    const SysUnit* removed = unit(id);
    if (!removed) return;
    const QString parent = removed->parent;
    if (removeContents) {
        for (const QString& child : childrenOf(id)) removeUnit(child);
    } else {
        // Positions are in canvas coordinates: only the ownership changes.
        for (SysUnit& child : units)
            if (child.parent == id) child.parent = parent;
    }
    cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const SysCable& c) { return c.aUnit == id || c.bUnit == id; }),
                 cables.end());
    for (SysCable& c : cables)
        c.branches.erase(std::remove_if(c.branches.begin(), c.branches.end(), [&](const SysBranch& b) { return b.unit == id; }), c.branches.end());
    units.erase(std::remove_if(units.begin(), units.end(), [&](const SysUnit& u) { return u.id == id; }), units.end());
}

void SystemDesign::removePort(const QString& unitId, const QString& port) {
    cables.erase(std::remove_if(cables.begin(), cables.end(),
                                [&](const SysCable& c) {
                                    return (c.aUnit == unitId && c.aPort == port) || (c.bUnit == unitId && c.bPort == port);
                                }),
                 cables.end());
    // A BRANCH ENDING THERE: that branch only, the harness kept.
    for (SysCable& c : cables)
        c.branches.erase(std::remove_if(c.branches.begin(), c.branches.end(),
                                        [&](const SysBranch& b) { return b.unit == unitId && b.port == port; }),
                         c.branches.end());
    if (SysUnit* u = unit(unitId))
        u->ports.erase(std::remove_if(u->ports.begin(), u->ports.end(), [&](const SysPort& p) { return p.id == port; }), u->ports.end());
}

void SystemDesign::removeCable(const QString& id) {
    cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const SysCable& c) { return c.id == id; }), cables.end());
}

std::vector<const SysCable*> SystemDesign::cablesAt(const QString& unitId, const QString& port) const {
    std::vector<const SysCable*> out;
    for (const SysCable& c : cables) {
        bool at = (c.aUnit == unitId && c.aPort == port) || (c.bUnit == unitId && c.bPort == port);
        for (const SysBranch& b : c.branches) at = at || (b.unit == unitId && b.port == port);
        if (at) out.push_back(&c);
    }
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
    for (const SysCable& c : cables) {
        bool at = (c.aUnit == unitId && c.aPort == port && c.aInside == inside) || (c.bUnit == unitId && c.bPort == port && c.bInside == inside);
        for (const SysBranch& b : c.branches) at = at || (b.unit == unitId && b.port == port && b.inside == inside);
        if (at) out.push_back(&c);
    }
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
        c.drawingTitle=info.title;
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
            if (in + out_ == 0) out.push_back({1, QStringLiteral("%1.%2 - nothing plugged in").arg(u.name, p.name), u.id, QString(), {}, p.id});
            if (out_ > 1)
                out.push_back({2, QStringLiteral("%1.%2 - %3 cables plugged into one port").arg(u.name, p.name).arg(out_), u.id, QString(), {}, p.id});
            if (in > 1)
                out.push_back({2, QStringLiteral("%1.%2 - %3 cables plugged into its inside").arg(u.name, p.name).arg(in), u.id, QString(), {}, p.id});
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
    for (SysIssue& finding : out) {
        finding.rule = finding.what.contains(QLatin1String("plugged")) ? QStringLiteral("PORT-CONNECTION")
                     : finding.what.contains(QLatin1String("drawing")) || finding.what.contains(QLatin1String("rev ")) ? QStringLiteral("DRAWING-LINK")
                     : QStringLiteral("PART-NUMBER");
    }
    for(const SysCable& cable:cables){const auto path=sysCablePath(*this,cable);
        for(const QRectF& box:systemRouteObstacles(*this,cable,0))if(systemPathCrossesBox(path,box)){
            out.push_back({1,QStringLiteral("%1: route overlaps a unit; move its route or separate the units").arg(cable.name),{},cable.id,QStringLiteral("ROUTING"),{}});break;}
    }
    QSet<QString> unitNames, cableNames;
    for (const SysUnit& u : units) {
        const QString key = u.name.trimmed().toCaseFolded();
        if (key.isEmpty() || unitNames.contains(key))
            out.push_back({2, QStringLiteral("Unit name is empty or duplicated: %1").arg(u.name), u.id, {}, QStringLiteral("DESIGNATOR"), {}});
        unitNames.insert(key);
        QSet<QString> portNames;
        for (const SysPort& p : u.ports) {
            if (p.connector.trimmed().isEmpty())
                out.push_back({1, QStringLiteral("%1.%2 has no connector part number").arg(u.name, p.name), u.id, {}, QStringLiteral("PART-NUMBER"), p.id});
            if (p.contactPn.trimmed().isEmpty())
                out.push_back({1, QStringLiteral("%1.%2 has no contact part number").arg(u.name, p.name), u.id, {}, QStringLiteral("PART-NUMBER"), p.id});
            const QString pk = p.name.trimmed().toCaseFolded();
            if (pk.isEmpty() || portNames.contains(pk))
                out.push_back({2, QStringLiteral("%1: port designator is empty or duplicated: %2").arg(u.name, p.name), u.id, {},
                               QStringLiteral("DESIGNATOR"), p.id});
            portNames.insert(pk);
        }
    }
    for (const SysCable& c : cables) {
        const QString key = c.name.trimmed().toCaseFolded();
        if (key.isEmpty() || cableNames.contains(key))
            out.push_back({2, QStringLiteral("Cable name is empty or duplicated: %1").arg(c.name), {}, c.id, QStringLiteral("DESIGNATOR"), {}});
        cableNames.insert(key);
    }
    for (const SysNote& n : notes) {
        const SysUnit* u = unit(n.unit);
        if ((!n.unit.isEmpty() && !u) || (!n.port.isEmpty() && (!u || !u->port(n.port))) || (!n.cable.isEmpty() && !cable(n.cable)))
            out.push_back({1, QStringLiteral("Note %1 links to an item that was removed").arg(n.id), {}, {}, QStringLiteral("NOTE-LINK"), {}, n.id});
    }
    // GRANULAR DISABLES FROM THE EARLIER RULE TABLE still apply to their corresponding findings.
    // New traffic-light categories remain independent: switching off one old port rule must not
    // switch off every other port rule in the current category.
    const auto oldRule = [](const SysIssue& finding) {
        const QString& what = finding.what;
        if (finding.rule == QLatin1String("NOTE-LINK")) return QStringLiteral("note-link");
        if (what.contains(QLatin1String("nothing plugged in"))) return QStringLiteral("port-free");
        if (what.contains(QLatin1String("cables plugged into"))) return QStringLiteral("port-crowded");
        if (what.contains(QLatin1String("no connector part number"))) return QStringLiteral("port-connector");
        if (what.contains(QLatin1String("no part number")))
            return finding.cable.isEmpty() ? QStringLiteral("unit-pn") : QStringLiteral("cable-pn");
        if (what.contains(QLatin1String("same part number"))) return QStringLiteral("cable-same-pn");
        if (what.contains(QLatin1String("no drawing linked"))) return QStringLiteral("cable-drawing");
        if (finding.rule == QLatin1String("DRAWING-LINK")) {
            if (what.contains(QLatin1String("not there"))) return QStringLiteral("drawing-missing");
            if (what.contains(QLatin1String(" - rev "))) return QStringLiteral("drawing-rev");
            if (what.contains(QLatin1String("its drawing is No."))) return QStringLiteral("drawing-no");
        }
        return QString();
    };
    out.erase(std::remove_if(out.begin(), out.end(), [&](const SysIssue& finding) {
        const QString legacy = oldRule(finding);
        return !legacy.isEmpty() && disabledRules.contains(legacy);
    }), out.end());
    return out;
}

std::vector<SysConnection> SystemDesign::checkConnections(const std::function<DrawingInfo(const QString&)>& read) const {
    std::vector<SysConnection> rows;
    QHash<QString, DrawingInfo> cache;
    auto endpoint = [this](const QString& uid, const QString& pid, const QString& contact) {
        const SysUnit* u = unit(uid);
        const SysPort* p = u ? u->port(pid) : nullptr;
        return QStringLiteral("%1.%2:%3").arg(u ? u->name : uid, p ? p->name : pid, contact);
    };
    for (const SysCable& c : cables) {
        if (c.drawing.isEmpty()) {
            rows.push_back({c.id, {}, endpoint(c.aUnit, c.aPort, {}), endpoint(c.bUnit, c.bPort, {}), QStringLiteral("No drawing linked"), 1});
            continue;
        }
        if (!cache.contains(c.drawing)) cache[c.drawing] = read ? read(c.drawing) : readDrawingInfo(c.drawing);
        const DrawingInfo& info = cache[c.drawing];
        if (!info.found || !info.schematic || info.connections.empty()) {
            rows.push_back({c.id, {}, endpoint(c.aUnit, c.aPort, {}), endpoint(c.bUnit, c.bPort, {}),
                            !info.found ? QStringLiteral("Drawing missing or unreadable") : QStringLiteral("No readable schematic wire connections"),
                            !info.found ? 2 : 1});
            continue;
        }
        const SysUnit* a = unit(c.aUnit), *b = unit(c.bUnit);
        const SysPort* pa = a ? a->port(c.aPort) : nullptr, *pb = b ? b->port(c.bPort) : nullptr;
        if (!pa || !pb) continue;
        for (const DrawingInfo::Connection& line : info.connections) {
            SysConnection r;
            r.cable = c.id; r.line = line.name; r.level = line.level; r.status = line.issue;
            const bool forward = line.fromConnector == pa->name && line.toConnector == pb->name;
            const bool reverse = line.fromConnector == pb->name && line.toConnector == pa->name;
            if (!forward && !reverse) {
                r.from = line.fromConnector + QLatin1Char(':') + line.fromContact;
                r.to = line.toConnector + QLatin1Char(':') + line.toContact;
                if (!r.status.isEmpty()) r.status += QStringLiteral("; ");
                r.status += QStringLiteral("Schematic ports do not match %1.%2 / %3.%4").arg(a->name, pa->name, b->name, pb->name);
                r.level = 2;
            } else {
                r.aUnit = c.aUnit; r.aPort = c.aPort; r.bUnit = c.bUnit; r.bPort = c.bPort;
                r.aContact = forward ? line.fromContact : line.toContact;
                r.bContact = forward ? line.toContact : line.fromContact;
                r.from = endpoint(r.aUnit, r.aPort, r.aContact); r.to = endpoint(r.bUnit, r.bPort, r.bContact);
            }
            if (r.status.isEmpty()) r.status = QStringLiteral("OK");
            rows.push_back(r);
        }
    }
    // A SHARED CONTACT on a system port must keep its signal name on every drawing that uses it.
    QHash<QString, QStringList> byContact;
    for (const SysConnection& r : rows) {
        if (r.aUnit.isEmpty() || r.line.isEmpty() || r.level == 2) continue;
        for (const QString& key : {r.aUnit + QLatin1Char('/') + r.aPort + QLatin1Char('/') + r.aContact,
                                   r.bUnit + QLatin1Char('/') + r.bPort + QLatin1Char('/') + r.bContact})
            if (!byContact[key].contains(r.line)) byContact[key] << r.line;
    }
    for (SysConnection& r : rows) {
        if (r.aUnit.isEmpty()) continue;
        for (const QString& key : {r.aUnit + QLatin1Char('/') + r.aPort + QLatin1Char('/') + r.aContact,
                                   r.bUnit + QLatin1Char('/') + r.bPort + QLatin1Char('/') + r.bContact}) {
            if (byContact.value(key).size() < 2) continue;
            if (r.status == QLatin1String("OK")) r.status.clear();
            if (!r.status.isEmpty()) r.status += QStringLiteral("; ");
            r.status += QStringLiteral("Line names differ at shared contact: %1").arg(byContact.value(key).join(QStringLiteral(" / ")));
            r.level = 2;
        }
    }
    return rows;
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

namespace {

QJsonObject sheetToJson(const SysSheet& s) {
    QJsonObject o{{QStringLiteral("name"), s.name}, {QStringLiteral("content"), s.content},
                  {QStringLiteral("w"), s.widthMm}, {QStringLiteral("h"), s.heightMm},
                  {QStringLiteral("x"), s.x}, {QStringLiteral("y"), s.y},
                  {QStringLiteral("scale"), s.scale}, {QStringLiteral("fields"), s.fields},
                  {QStringLiteral("layers"), s.layers}};
    if (!s.sources.isEmpty()) o[QStringLiteral("sources")] = s.sources;
    return o;
}

SysSheet sheetFromJson(const QJsonObject& s) {
    SysSheet out;
    out.name = s.value(QStringLiteral("name")).toString();
    out.content = s.value(QStringLiteral("content")).toArray();
    out.widthMm = s.value(QStringLiteral("w")).toDouble(420.0);
    out.heightMm = s.value(QStringLiteral("h")).toDouble(297.0);
    out.x = s.value(QStringLiteral("x")).toDouble();
    out.y = s.value(QStringLiteral("y")).toDouble();
    out.scale = std::max(0.05, s.value(QStringLiteral("scale")).toDouble(2.0));
    out.fields = s.value(QStringLiteral("fields")).toObject();
    out.layers = s.value(QStringLiteral("layers")).toArray();
    out.sources = s.value(QStringLiteral("sources")).toObject();
    return out;
}

// A REVISION'S COLUMNS, by the headings a template's revision table may have.
const char* const kRevisionKeys[] = {"rev", "note", "eco", "drawn", "checked", "approved", "date"};

QString* revisionField(SysRevision& r, int i) {
    QString* all[] = {&r.rev, &r.note, &r.eco, &r.drawn, &r.checked, &r.approved, &r.date};
    return i >= 0 && i < 7 ? all[i] : nullptr;
}

// WHICH OF A REVISION'S COLUMNS A HEADING IS: REV, CHANGE NOTE (or DESCRIPTION), ECO, DRAWN (or BY), CHECKED, APPROVED,
// DATE; -1 for none of them.
int revisionColumn(const QString& heading) {
    const QString h = heading.trimmed().toUpper();
    if (h == QLatin1String("REV") || h == QLatin1String("REVISION")) return 0;
    if (h.contains(QLatin1String("NOTE")) || h.startsWith(QLatin1String("DESC")) || h == QLatin1String("CHANGE")) return 1;
    if (h.contains(QLatin1String("ECO")) || h.contains(QLatin1String("ECN"))) return 2;
    if (h.startsWith(QLatin1String("DRAWN")) || h == QLatin1String("BY")) return 3;
    if (h.startsWith(QLatin1String("CHECK"))) return 4;
    if (h.startsWith(QLatin1String("APPR"))) return 5;
    if (h == QLatin1String("DATE")) return 6;
    return -1;
}

}  // namespace

const std::vector<std::pair<QString, QString>>& sysFieldSources() {
    static const std::vector<std::pair<QString, QString>> sources{
        {QString(), QStringLiteral("Typed")},
        {QStringLiteral("project"), QStringLiteral("Project name")},
        {QStringLiteral("drawingNo"), QStringLiteral("Drawing P/N (sheet 1)")},
        {QStringLiteral("title"), QStringLiteral("Title (sheet 1)")},
        {QStringLiteral("revision"), QStringLiteral("Latest revision")},
        {QStringLiteral("revisionDate"), QStringLiteral("Latest revision's date")},
        {QStringLiteral("drawn"), QStringLiteral("Latest revision's drawn by")},
        {QStringLiteral("approved"), QStringLiteral("Latest revision's approved by")},
        {QStringLiteral("today"), QStringLiteral("Today's date")},
        {QStringLiteral("sheet"), QStringLiteral("Sheet number (1 of 3)")},
        {QStringLiteral("units"), QStringLiteral("Number of units")},
        {QStringLiteral("cables"), QStringLiteral("Number of harnesses")}};
    return sources;
}

QJsonObject SystemDesign::sheetTexts(const SysSheet& s) const {
    QJsonObject out = s.fields;
    const SysRevision* latest = revisions.empty() ? nullptr : &revisions.back();
    int index = 0;
    for (int i = 0; i < sheetCount(); ++i)
        if (sheetAt(i) == &s) index = i;
    for (auto it = s.sources.begin(); it != s.sources.end(); ++it) {
        const QString source = it.value().toString();
        QString text;
        if (source == QLatin1String("project")) text = name;
        else if (source == QLatin1String("drawingNo") || source == QLatin1String("title")) text = sheet.fields.value(source).toString();
        else if (source == QLatin1String("revision")) text = latest ? latest->rev : QString();
        else if (source == QLatin1String("revisionDate")) text = latest ? latest->date : QString();
        else if (source == QLatin1String("drawn")) text = latest ? latest->drawn : QString();
        else if (source == QLatin1String("approved")) text = latest ? latest->approved : QString();
        else if (source == QLatin1String("today")) text = QDate::currentDate().toString(Qt::ISODate);
        else if (source == QLatin1String("sheet")) text = QStringLiteral("%1 of %2").arg(index + 1).arg(sheetCount());
        else if (source == QLatin1String("units")) text = QString::number(units.size());
        else if (source == QLatin1String("cables")) text = QString::number(cables.size());
        else continue;
        out[it.key()] = text;
    }
    return out;
}

QJsonArray SystemDesign::sheetContent(const SysSheet& s) const {
    const QJsonObject texts = sheetTexts(s);
    QJsonArray out;
    for (const QJsonValue v : s.content) {
        // AN ENTITY'S OWN KEYS ARE ITS META, side by side with its geometry.
        QJsonObject meta = v.toObject();
        const QString field = meta.value(QStringLiteral("field")).toString();
        if (!field.isEmpty() && texts.contains(field)) meta[QStringLiteral("text")] = texts.value(field).toString();
        if (meta.value(QStringLiteral("revisionTable")).toBool() && !revisions.empty()) {
            // THE REVISIONS IN ITS TABLE, a row each under the headings - the newest where it has fewer rows than there are.
            const int cols = meta.value(QStringLiteral("cols")).toInt();
            const int rows = meta.value(QStringLiteral("rows")).toInt();
            QJsonArray cells = meta.value(QStringLiteral("cells")).toArray();
            if (cols > 0 && rows > 1 && cells.size() >= cols * rows) {
                const int room = rows - 1;
                const int first = std::max(0, static_cast<int>(revisions.size()) - room);
                for (int r = 0; r < room; ++r) {
                    const int which = first + r;
                    for (int c = 0; c < cols; ++c) {
                        QString text;
                        if (which < static_cast<int>(revisions.size())) {
                            SysRevision rev = revisions[static_cast<size_t>(which)];
                            if (QString* f = revisionField(rev, revisionColumn(cells.at(c).toString()))) text = *f;
                        }
                        cells[(r + 1) * cols + c] = text;
                    }
                }
                meta[QStringLiteral("cells")] = cells;
            }
        }
        out.append(meta);
    }
    return out;
}

QJsonObject SystemDesign::toJson() const {
    QJsonObject o;
    if (!name.isEmpty()) o[QStringLiteral("name")] = name;
    o[QStringLiteral("pnPrefix")] = pnPrefix;
    if (!disabledRules.isEmpty()) o[QStringLiteral("disabledRules")] = QJsonArray::fromStringList(disabledRules);
    if (!ruleLevels.isEmpty()) o[QStringLiteral("ruleLevels")] = ruleLevels;
    if (background != QColor(48, 52, 58)) o[QStringLiteral("background")] = background.name();
    if (designatorColor.isValid()) o[QStringLiteral("designatorColor")] = designatorColor.name();
    if (!sheet.empty()) o[QStringLiteral("sheet")] = sheetToJson(sheet);
    if (!moreSheets.empty()) {
        QJsonArray more;
        for (const SysSheet& s : moreSheets) more.append(sheetToJson(s));
        o[QStringLiteral("moreSheets")] = more;
    }
    if (!revisions.empty()) {
        QJsonArray list;
        for (SysRevision r : revisions) {
            QJsonObject j;
            for (int i = 0; i < 7; ++i)
                if (const QString* f = revisionField(r, i); !f->isEmpty()) j[QLatin1String(kRevisionKeys[i])] = *f;
            list.append(j);
        }
        o[QStringLiteral("revisions")] = list;
    }
    if (!bypassed.empty()) {
        QJsonArray accepted;
        for (const SysIssue& i : bypassed)
            accepted.append(QJsonObject{{QStringLiteral("rule"), i.rule}, {QStringLiteral("what"), i.what}, {QStringLiteral("level"), i.level},
                                        {QStringLiteral("unit"), i.unit}, {QStringLiteral("port"), i.port}, {QStringLiteral("cable"), i.cable},
                                        {QStringLiteral("note"), i.note}});
        o[QStringLiteral("bypassed")] = accepted;
    }
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
        if (u.opacity < 1.0) j[QStringLiteral("opacity")] = u.opacity;
        if (!u.picture.isEmpty()) j[QStringLiteral("picture")] = QString::fromLatin1(u.picture.toBase64());
        if (!u.pictureShown) j[QStringLiteral("pictureHidden")] = true;
        if (u.pictureScale < 1.0) j[QStringLiteral("pictureScale")] = u.pictureScale;
        if (!u.pictureAlign.isEmpty()) j[QStringLiteral("pictureAlign")] = u.pictureAlign;
        j[QStringLiteral("parent")] = u.parent;
        if (!u.hidden.isEmpty()) j[QStringLiteral("hide")] = QJsonArray::fromStringList(u.hidden);
        QJsonArray ps;
        for (const SysPort& p : u.ports) {
            QJsonObject q{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("side"), p.side},
                          {QStringLiteral("at"), p.at}};
            if (!p.connector.isEmpty()) q[QStringLiteral("connector")] = p.connector;
            if (!p.insidePn.isEmpty()) q[QStringLiteral("insidePn")] = p.insidePn;
            if (p.pins > 0) q[QStringLiteral("pins")] = p.pins;
            if (!p.contactPn.isEmpty()) q[QStringLiteral("contactPn")] = p.contactPn;
            if (!p.description.isEmpty()) q[QStringLiteral("description")] = p.description;
            if (p.labelDx != 0.0) q[QStringLiteral("labelDx")] = p.labelDx;
            if (p.labelDy != 0.0) q[QStringLiteral("labelDy")] = p.labelDy;
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
        if (!c.branches.empty()) {
            QJsonArray branches;
            for (const SysBranch& b : c.branches) {
                QJsonObject end{{QStringLiteral("unit"), b.unit}, {QStringLiteral("port"), b.port}};
                if (b.inside) end[QStringLiteral("in")] = true;
                branches.append(end);
            }
            j[QStringLiteral("branches")] = branches;
        }
        if (c.wires > 0) j[QStringLiteral("wires")] = c.wires;
        if (c.color.isValid()) j[QStringLiteral("color")] = c.color.name();
        if(!c.waypoints.empty()){QJsonArray points;for(const QPointF& point:c.waypoints)points.append(QJsonArray{point.x(),point.y()});j[QStringLiteral("waypoints")]=points;}
        if (!c.drawing.isEmpty()) {
            j[QStringLiteral("drawing")] = c.drawing;
            j[QStringLiteral("drawingNo")] = c.drawingNo;
            j[QStringLiteral("drawingRev")] = c.drawingRev;
            if(!c.drawingTitle.isEmpty())j[QStringLiteral("drawingTitle")]=c.drawingTitle;
            if(!c.expectedTitle.isEmpty())j[QStringLiteral("expectedTitle")]=c.expectedTitle;
        }
        cs.append(j);
    }
    o[QStringLiteral("cables")] = cs;
    if (!notes.empty()) {
        QJsonArray ns;
        for (const SysNote& n : notes) {
            QJsonObject j{{QStringLiteral("id"), n.id}, {QStringLiteral("text"), n.text}, {QStringLiteral("x"), n.x},
                          {QStringLiteral("y"), n.y}, {QStringLiteral("w"), n.w}, {QStringLiteral("h"), n.h}};
            if (n.color.isValid()) j[QStringLiteral("color")] = n.color.name();
            if (!n.unit.isEmpty()) j[QStringLiteral("unit")] = n.unit;
            if (!n.port.isEmpty()) j[QStringLiteral("port")] = n.port;
            if (!n.cable.isEmpty()) j[QStringLiteral("cable")] = n.cable;
            if (!n.showLink) j[QStringLiteral("hideLink")] = true;
            ns.append(j);
        }
        o[QStringLiteral("notes")] = ns;
    }
    return o;
}

SystemDesign SystemDesign::fromJson(const QJsonObject& o) {
    SystemDesign d;
    d.name = o.value(QStringLiteral("name")).toString();
    // ACCEPT THE EARLIER SYSTEM FILE KEYS while keeping the current settings authoritative.
    const QJsonArray disabled = o.contains(QStringLiteral("disabledRules"))
                                   ? o.value(QStringLiteral("disabledRules")).toArray()
                                   : o.value(QStringLiteral("rulesOff")).toArray();
    for (const QJsonValue& rule : disabled) d.disabledRules << rule.toString();
    const QJsonObject levels = o.value(QStringLiteral("ruleLevels")).toObject();
    for (auto it = levels.begin(); it != levels.end(); ++it) if (it.value().isDouble()) d.ruleLevels[it.key()] = std::clamp(it.value().toInt(2), 0, 2);
    const QColor background(o.value(QStringLiteral("background")).toString());
    if (background.isValid()) d.background = background;
    d.designatorColor = QColor(o.value(QStringLiteral("designatorColor")).toString());
    if (const QJsonObject s = o.value(QStringLiteral("sheet")).toObject(); !s.value(QStringLiteral("name")).toString().isEmpty())
        d.sheet = sheetFromJson(s);
    // MORE SHEETS only after a first one.
    if (!d.sheet.empty())
        for (const QJsonValue v : o.value(QStringLiteral("moreSheets")).toArray())
            if (SysSheet s = sheetFromJson(v.toObject()); !s.empty()) d.moreSheets.push_back(s);
    for (const QJsonValue v : o.value(QStringLiteral("revisions")).toArray()) {
        const QJsonObject j = v.toObject();
        SysRevision r;
        for (int i = 0; i < 7; ++i) *revisionField(r, i) = j.value(QLatin1String(kRevisionKeys[i])).toString();
        d.revisions.push_back(r);
    }
    for (const QJsonValue& v : o.value(QStringLiteral("bypassed")).toArray()) {
        const QJsonObject j = v.toObject();
        SysIssue i;
        i.rule = j.value(QStringLiteral("rule")).toString(); i.what = j.value(QStringLiteral("what")).toString();
        i.level = std::clamp(j.value(QStringLiteral("level")).toInt(1), 0, 2);
        i.unit = j.value(QStringLiteral("unit")).toString(); i.port = j.value(QStringLiteral("port")).toString();
        i.cable = j.value(QStringLiteral("cable")).toString(); i.note = j.value(QStringLiteral("note")).toString();
        if (!i.rule.isEmpty() && !i.what.isEmpty()) d.bypassed.push_back(i);
    }
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
        n.h = std::max(30.0, j.value(QStringLiteral("h")).toDouble(70.0));
        n.w = std::max(60.0, n.w);
        n.color = QColor(j.value(QStringLiteral("color")).toString());
        n.unit = j.value(QStringLiteral("unit")).toString(j.value(QStringLiteral("linkUnit")).toString());
        n.port = j.value(QStringLiteral("port")).toString(j.value(QStringLiteral("linkPort")).toString());
        n.cable = j.value(QStringLiteral("cable")).toString(j.value(QStringLiteral("linkCable")).toString());
        n.showLink = !j.value(QStringLiteral("hideLink")).toBool(false);
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
        u.opacity = std::clamp(j.value(QStringLiteral("opacity")).toDouble(1.0),0.0,1.0);
        u.picture = QByteArray::fromBase64(j.value(QStringLiteral("picture")).toString().toLatin1());
        u.pictureShown = !j.value(QStringLiteral("pictureHidden")).toBool(false);
        u.pictureScale = std::clamp(j.value(QStringLiteral("pictureScale")).toDouble(1.0), 0.1, 1.0);
        u.pictureAlign = j.value(QStringLiteral("pictureAlign")).toString();
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
            p.insidePn = q.value(QStringLiteral("insidePn")).toString();
            p.pins = q.value(QStringLiteral("pins")).toInt(0);
            if (p.connector.isEmpty()) p.connector = p.insidePn;
            p.contactPn = q.value(QStringLiteral("contactPn")).toString();
            p.description = q.value(QStringLiteral("description")).toString();
            p.labelDx = q.value(QStringLiteral("labelDx")).toDouble(q.value(QStringLiteral("ldx")).toDouble());
            p.labelDy = q.value(QStringLiteral("labelDy")).toDouble(q.value(QStringLiteral("ldy")).toDouble());
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
        for (const QJsonValue& value : j.value(QStringLiteral("branches")).toArray()) {
            const QJsonObject end = value.toObject();
            SysBranch b;
            b.unit = end.value(QStringLiteral("unit")).toString();
            b.port = end.value(QStringLiteral("port")).toString();
            b.inside = end.value(QStringLiteral("in")).toBool(false);
            if (!b.unit.isEmpty() && !b.port.isEmpty()) c.branches.push_back(b);
        }
        if (j.contains(QStringLiteral("color"))) c.color = QColor(j.value(QStringLiteral("color")).toString());
        for(const QJsonValue& value:j.value(QStringLiteral("waypoints")).toArray()){const QJsonArray point=value.toArray();if(point.size()==2)c.waypoints.push_back({point[0].toDouble(),point[1].toDouble()});}
        c.drawing = j.value(QStringLiteral("drawing")).toString();
        c.drawingNo = j.value(QStringLiteral("drawingNo")).toString();
        c.drawingRev = j.value(QStringLiteral("drawingRev")).toString();
        c.drawingTitle=j.value(QStringLiteral("drawingTitle")).toString();c.expectedTitle=j.value(QStringLiteral("expectedTitle")).toString();
        // A cable to a unit or port that is not there is dropped, not kept dangling.
        const SysUnit* a = d.unit(c.aUnit);
        const SysUnit* b = d.unit(c.bUnit);
        if (!c.id.isEmpty() && a && b && a->port(c.aPort) && b->port(c.bPort)) d.cables.push_back(c);
    }
    return d;
}

} // namespace Edraw_Desktop
