#include "harness/Bom.h"

#include "model/Block.h"
#include <QSet>
#include "harness/MechanicalHarness.h"

#include "harness/Harness.h"
#include "geometry/Geometry.h"
#include "model/Document.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace Loopwire {
namespace {

// A part's identity for totalling: its library id if it came from the library, else the P/N it was
// ordered against, else what it says it is. Case-folded, so "PN-1" and "pn-1" are one part.
QString identityOf(const BomRow& r) {
    if (!r.partId.isEmpty()) return QStringLiteral("id:") + r.partId;
    if (!r.mfrPn.isEmpty()) return QStringLiteral("pn:") + r.mfrPn.toLower();
    if (!r.internalPn.isEmpty()) return QStringLiteral("ipn:") + r.internalPn.toLower();
    if (!r.description.trimmed().isEmpty()) return QStringLiteral("d:") + r.description.toLower();
    // Nothing said about what it is: the row is only itself, so a second blank row is a second
    // row rather than one more of the first.
    return QStringLiteral("row:") + r.rowId;
}

} // namespace

QString BomRow::qtyText(int decimals) const {
    // A whole figure reads as a whole number; anything else keeps the decimals asked for.
    if (std::abs(qty - std::round(qty)) < 1e-9)
        return QString::number(static_cast<long long>(std::llround(qty)));
    return QString::number(qty, 'f', std::max(0, decimals));
}

bool BomConfig::columnShown(int i) const {
    if (i < 0 || i >= bomColumnCount()) return false;
    if (columns.isEmpty()) return true; // nothing said: every column
    return i < columns.size() ? columns.at(i) : true;
}

void BomConfig::setColumnShown(int i, bool on) {
    if (i < 0 || i >= bomColumnCount()) return;
    while (columns.size() < bomColumnCount()) columns << true;
    columns[i] = on;
}

QJsonObject BomConfig::toJson() const {
    QJsonObject o;
    o[QStringLiteral("lengthUnit")] = lengthUnit;
    o[QStringLiteral("unify")] = unify;
    o[QStringLiteral("includeDrawing")] = includeDrawing;
    o[QStringLiteral("qtyDecimals")] = qtyDecimals;
    o[QStringLiteral("sparePercent")] = sparePercent;
    o[QStringLiteral("roundUpLengths")] = roundUpLengths;
    QJsonArray cols;
    for (int i = 0; i < bomColumnCount(); ++i) cols.append(columnShown(i));
    o[QStringLiteral("columns")] = cols;
    return o;
}

void BomConfig::fromJson(const QJsonObject& o) {
    const BomConfig def;
    lengthUnit = o.value(QStringLiteral("lengthUnit")).toString(def.lengthUnit);
    unify = o.value(QStringLiteral("unify")).toBool(def.unify);
    includeDrawing = o.value(QStringLiteral("includeDrawing")).toBool(def.includeDrawing);
    qtyDecimals = o.value(QStringLiteral("qtyDecimals")).toInt(def.qtyDecimals);
    sparePercent = o.value(QStringLiteral("sparePercent")).toDouble(def.sparePercent);
    roundUpLengths = o.value(QStringLiteral("roundUpLengths")).toBool(def.roundUpLengths);
    columns.clear();
    for (const auto& v : o.value(QStringLiteral("columns")).toArray()) columns << v.toBool(true);
}

std::vector<BomRow> bomApplyConfig(const std::vector<BomRow>& rows, const BomConfig& cfg) {
    std::vector<BomRow> out;
    out.reserve(rows.size());
    for (BomRow r : rows) {
        // Lengths only: a connector counted in pieces is not a length, and 2.5 pieces means
        // nothing. A row already in the working unit is simply left alone.
        if (partUnitDimension(r.unit) == QLatin1String("length")) {
            double v = r.qty;
            if (cfg.lengthUnit != r.unit && convertPartUnit(r.qty, r.unit, cfg.lengthUnit, v))
                r.unit = cfg.lengthUnit;
            if (cfg.sparePercent > 0.0) v *= 1.0 + cfg.sparePercent / 100.0;
            if (cfg.roundUpLengths) v = std::ceil(v);
            r.qty = v;
        }
        out.push_back(r);
    }
    return out;
}

double lengthUnitInMm(const QString& unit) {
    const QString u = unit.trimmed().toLower();
    if (u == QStringLiteral("mm")) return 1.0;
    if (u == QStringLiteral("cm")) return 10.0;
    if (u == QStringLiteral("m")) return 1000.0;
    if (u == QStringLiteral("km")) return 1000000.0;
    if (u == QStringLiteral("in") || u == QStringLiteral("inch") || u == QStringLiteral("\"")) return 25.4;
    if (u == QStringLiteral("ft") || u == QStringLiteral("feet") || u == QStringLiteral("foot") ||
        u == QStringLiteral("'"))
        return 304.8;
    if (u == QStringLiteral("yd") || u == QStringLiteral("yard")) return 914.4;
    return 0.0;   // "each", "off", "set": a count, not a length
}

double convertQuantity(double qty, const QString& fromUnit, const QString& toUnit) {
    const double from = lengthUnitInMm(fromUnit);
    const double to = lengthUnitInMm(toUnit);
    // Both lengths: the same quantity said another way. Otherwise there is nothing to say - a hundred
    // grommets are not any number of metres - so the figure stands and only the word changes.
    if (from <= 0.0 || to <= 0.0) return qty;
    return qty * from / to;
}

QStringList bomDefaultHeaders() {
    // "#" rather than "Line #": the column is one character wide on a sheet, and the heading
    // should not be the widest thing in it.
    return {QStringLiteral("#"),           QStringLiteral("Description"),
            QStringLiteral("Internal P/N"), QStringLiteral("Mfr P/N"),
            QStringLiteral("Mfr name"),    QStringLiteral("Qty."),
            QStringLiteral("Units")};
}

int bomColumnCount() { return bomDefaultHeaders().size(); }

QString BomTable::header(int i) const {
    const QStringList def = bomDefaultHeaders();
    if (i < 0 || i >= def.size()) return QString();
    if (i < headers.size() && !headers.at(i).isEmpty()) return headers.at(i);
    return def.at(i);
}

void BomTable::setHeader(int i, const QString& text) {
    const QStringList def = bomDefaultHeaders();
    if (i < 0 || i >= def.size()) return;
    while (headers.size() < def.size()) headers << def.at(headers.size());
    headers[i] = text;
}

QJsonObject BomTable::toJson() const {
    QJsonObject o;
    QJsonArray h;
    for (int i = 0; i < bomColumnCount(); ++i) h.append(header(i));
    o[QStringLiteral("headers")] = h;
    QJsonArray arr;
    for (const BomRow& r : rows) {
        // Only the rows a person typed are stored: the ones the DRAWING implies are read back off
        // the drawing every time, so they can never go stale.
        if (r.fromDrawing) continue;
        QJsonObject ro;
        ro[QStringLiteral("rowId")] = r.rowId;
        ro[QStringLiteral("description")] = r.description;
        ro[QStringLiteral("internalPn")] = r.internalPn;
        ro[QStringLiteral("mfrPn")] = r.mfrPn;
        ro[QStringLiteral("mfrName")] = r.mfrName;
        ro[QStringLiteral("qty")] = r.qty;
        ro[QStringLiteral("unit")] = r.unit;
        if (!r.partId.isEmpty()) ro[QStringLiteral("partId")] = r.partId;
        if (!r.ref.isEmpty()) ro[QStringLiteral("ref")] = r.ref;
        arr.append(ro);
    }
    o[QStringLiteral("rows")] = arr;
    o[QStringLiteral("config")] = config.toJson();
    return o;
}

void BomTable::fromJson(const QJsonObject& o) {
    config.fromJson(o.value(QStringLiteral("config")).toObject());
    headers.clear();
    for (const auto& v : o.value(QStringLiteral("headers")).toArray()) headers << v.toString();
    rows.clear();
    for (const auto& v : o.value(QStringLiteral("rows")).toArray()) {
        const QJsonObject ro = v.toObject();
        BomRow r;
        r.rowId = ro.value(QStringLiteral("rowId")).toString();
        r.description = ro.value(QStringLiteral("description")).toString();
        r.internalPn = ro.value(QStringLiteral("internalPn")).toString();
        r.mfrPn = ro.value(QStringLiteral("mfrPn")).toString();
        r.mfrName = ro.value(QStringLiteral("mfrName")).toString();
        r.qty = ro.value(QStringLiteral("qty")).toDouble();
        r.unit = ro.value(QStringLiteral("unit")).toString(QStringLiteral("each"));
        r.partId = ro.value(QStringLiteral("partId")).toString();
        r.ref = ro.value(QStringLiteral("ref")).toString();
        r.fromDrawing = false;
        if (r.rowId.isEmpty()) r.rowId = bomNextRowId(rows);
        rows.push_back(r);
    }
}

std::vector<BomRow> bomUnify(const std::vector<BomRow>& rows) {
    std::vector<BomRow> out;
    for (const BomRow& r : rows) {
        const QString key = identityOf(r);
        bool merged = false;
        for (BomRow& o : out) {
            if (identityOf(o) != key) continue;
            // The same part: add it in, in the unit the first row of that part used. Units that
            // cannot be added (metres to pieces) leave the row alone - it becomes its own line, so
            // the sheet says what it is rather than a total that means nothing.
            double asFirst = 0.0;
            if (!convertPartUnit(r.qty, r.unit, o.unit, asFirst)) continue;
            o.qty += asFirst;
            // Two references become one list, so the row still says where it is used.
            if (!r.ref.isEmpty()) {
                const QStringList have = o.ref.split(QStringLiteral(", "), Qt::SkipEmptyParts);
                if (!have.contains(r.ref))
                    o.ref = o.ref.isEmpty() ? r.ref : o.ref + QStringLiteral(", ") + r.ref;
            }
            // Anything the first row did not know, the second may.
            if (o.internalPn.isEmpty()) o.internalPn = r.internalPn;
            if (o.mfrPn.isEmpty()) o.mfrPn = r.mfrPn;
            if (o.mfrName.isEmpty()) o.mfrName = r.mfrName;
            merged = true;
            break;
        }
        if (!merged) out.push_back(r);
    }
    return out;
}

BomRow bomRowFromPart(const LibraryPart& part, double qty) {
    BomRow r;
    r.description = part.description.isEmpty() ? partKindName(part.kind) : part.description;
    r.internalPn = part.internalPn;
    r.mfrPn = part.mfrPn;
    r.mfrName = part.mfrName;
    r.qty = qty;
    r.unit = part.unit.isEmpty() ? defaultUnitForKind(part.kind) : part.unit;
    r.partId = part.id;
    return r;
}

bool bomSameThing(const BomRow& a, const BomRow& b) {
    const QString key = identityOf(a);
    // Nothing said about what it is: the row is only itself, and "itself" is not an answer to
    // "is this part already on the bill?" - so a blank line is never the same thing as anything,
    // another blank line included.
    if (key.startsWith(QLatin1String("row:"))) return false;
    return key == identityOf(b);
}

std::vector<int> bomAlreadyListed(const std::vector<BomRow>& rows, const std::vector<BomRow>& adding) {
    std::vector<int> found;
    found.reserve(adding.size());
    for (const BomRow& a : adding) {
        int at = -1;
        for (size_t i = 0; i < rows.size(); ++i)
            if (bomSameThing(a, rows[i])) { at = static_cast<int>(i); break; }
        found.push_back(at);
    }
    return found;
}

std::vector<BomRow> bomRowsFromMechHarness(const MechHarnessDef& def, const PartsCatalog* catalog) {
    std::vector<BomRow> out;
    for (const MechBomLine& line : mechBom(def, catalog)) {
        // The library part behind the line, when there is one and it is connected: it carries the
        // internal P/N and the manufacturer's name, which the harness's own table has no column for.
        const LibraryPart* part =
            catalog && !line.libPart.isEmpty() ? catalog->byId(line.libPart) : nullptr;
        BomRow r = part ? bomRowFromPart(*part, line.qty) : BomRow{};
        if (!part) {
            r.description = line.description;
            r.mfrPn = line.mfrPn;
            r.mfrName = line.mfrName;
        }
        // The part it IS, whether or not the library that holds it happens to be connected: a bill
        // that forgets which part a line was about cannot be totalled against the same part later.
        r.partId = line.libPart;
        r.qty = line.qty;
        r.unit = line.unit;
        // Where it is used - "P1", "the run to P2" - so a line can be found on the drawing.
        r.ref = line.where.join(QStringLiteral(", "));
        r.fromDrawing = false;   // put there by a window, and so removable by hand
        out.push_back(std::move(r));
    }
    return out;
}

std::function<int(const QString&, PartKind, const QString&)> bomItemLookup(const Document& doc,
                                                                            const PartsLibrary& lib) {
    BomTable table;
    table.fromJson(doc.bom());
    std::vector<BomRow> rows = table.rows;
    if (table.config.includeDrawing) {
        const std::vector<BomRow> drawn = bomFromDrawing(doc, lib);
        rows.insert(rows.end(), drawn.begin(), drawn.end());
    }
    if (table.config.unify) rows = bomUnify(rows);

    // The parts the harness will ask about arrive as a library id and a description; the bill's own
    // lines may have been typed in against a manufacturer's P/N and nothing else. So the identities
    // are tried as ALTERNATIVES rather than as the single winner bomSameThing picks: the question here
    // is "which line is this part on", and a line that names the same P/N is that line whether or not
    // it also remembers which library the part came out of.
    struct Line {
        QString partId, mfrPn, internalPn, description;
    };
    std::vector<Line> lines;
    lines.reserve(rows.size());
    for (const BomRow& r : rows)
        lines.push_back({r.partId, r.mfrPn.toLower(), r.internalPn.toLower(), r.description.toLower()});

    // The P/Ns of every part the library holds, by id, copied so the lookup owns everything it needs.
    // Captured rather than holding the library by reference: callers hand over PartsCatalog::merged(),
    // which is a temporary, and a lookup outliving it would read freed memory.
    QHash<QString, QPair<QString, QString>> pnOf;
    for (const LibraryPart& lp : lib.parts)
        pnOf.insert(lp.id, {lp.mfrPn.toLower(), lp.internalPn.toLower()});

    return [lines, pnOf](const QString& libPart, PartKind, const QString& description) {
        const QPair<QString, QString> pn = pnOf.value(libPart);
        const QString mfrPn = pn.first;
        const QString internalPn = pn.second;
        const QString desc = description.toLower();
        for (size_t i = 0; i < lines.size(); ++i) {
            const Line& l = lines[i];
            const bool same = (!libPart.isEmpty() && l.partId == libPart) ||
                              (!mfrPn.isEmpty() && l.mfrPn == mfrPn) ||
                              (!internalPn.isEmpty() && l.internalPn == internalPn) ||
                              (!desc.isEmpty() && l.description == desc);
            if (same) return static_cast<int>(i) + 1;
        }
        return 0;
    };
}

QString bomNextRowId(const std::vector<BomRow>& rows) {
    for (int i = static_cast<int>(rows.size()) + 1;; ++i) {
        const QString id = QStringLiteral("r%1").arg(i);
        bool taken = false;
        for (const BomRow& r : rows)
            if (r.rowId == id) { taken = true; break; }
        if (!taken) return id;
    }
}

std::vector<BomRow> bomFromDrawing(const Document& doc, const PartsLibrary& lib) {
    std::vector<BomRow> out;
    // A part placed from the library is one line however many entities its drawing is made of, so each
    // block INSTANCE is counted once. And where a connector was placed with one of its contacts, that
    // contact goes on the line below it, in the quantity the connector needs - which is how a connector is
    // ordered: never by itself.
    // One part is drawn several WAYS - its body on the assembly, its symbol on the schematic, its fanout at
    // the connector - and all of them are the same thing to buy. So the views are counted separately and the
    // part is counted as the BIGGEST of them: four connectors drawn mechanically and four drawn on the
    // schematic are four connectors, and if the schematic shows six then six is the truth and the assembly
    // sheet is two short. Adding the views up instead said eight, which is a bill nobody could order from.
    struct Placed {
        QString instance;
        QString partId;
        QString view;
        QString designator;
        QString contactId;
    };
    std::vector<Placed> placedInOrder;
    QSet<QString> countedInstances;
    for (const Entity& e : doc.entities()) {
        if (e.meta.value("wizardPhysicalBom").toBool()) continue;
        const QString instance = e.meta.value(QStringLiteral("blockInst")).toString();
        const QString placedId = e.meta.value(QStringLiteral("libPart")).toString();
        if (instance.isEmpty() || placedId.isEmpty()) continue;
        if (countedInstances.contains(instance)) continue;
        countedInstances.insert(instance);
        if (!lib.byId(placedId)) continue;
        Placed p;
        p.instance = instance;
        p.partId = placedId;
        // A copy placed before views were written down counts as the mechanical one: whatever it is, every
        // such copy is the same view as every other, so the count comes out as it always did.
        p.view = e.meta.value(QLatin1String(kLibPartViewKey)).toString();
        if (p.view.isEmpty()) p.view = QStringLiteral("mechanical");
        p.designator = e.meta.value(QStringLiteral("designator")).toString();
        p.contactId = e.meta.value(QLatin1String(kContactPartKey)).toString();
        placedInOrder.push_back(std::move(p));
    }
    // How many of each view there are of each part, and which view is the biggest. Ties go to the view seen
    // first, so a part drawn once each way reads as the one it was put down as first.
    QHash<QString, QHash<QString, int>> views;
    QHash<QString, QString> firstView;
    for (const Placed& p : placedInOrder) {
        ++views[p.partId][p.view];
        if (!firstView.contains(p.partId)) firstView.insert(p.partId, p.view);
    }
    // THE SCHEMATIC CARRIES THE COUNT, whenever there is one. A bill of materials is what is BUILT, and
    // what is built is what the circuit says: four connectors on the schematic are four connectors, whether
    // the assembly sheet happens to show two of them or six. It used to count whichever view had the most
    // copies, which is a rule that gives a different answer depending on which sheet somebody was working on
    // last - and on a drawing where a connector is shown twice mechanically (a front view and a section) it
    // ordered two.
    //
    // With no schematic copy at all, whatever view there is carries it: a mechanical-only drawing is still a
    // drawing, and counting nothing would be worse than counting the one view it has.
    QHash<QString, QString> countThisView;
    for (auto part = views.cbegin(); part != views.cend(); ++part) {
        if (part.value().value(QStringLiteral("schematic"), 0) > 0) {
            countThisView.insert(part.key(), QStringLiteral("schematic"));
            continue;
        }
        QString best = firstView.value(part.key());
        int most = part.value().value(best, 0);
        for (auto v = part.value().cbegin(); v != part.value().cend(); ++v)
            if (v.value() > most) {
                most = v.value();
                best = v.key();
            }
        countThisView.insert(part.key(), best);
    }
    for (const Placed& p : placedInOrder) {
        // Only the view that carries the count: the others are the same parts, drawn again.
        if (countThisView.value(p.partId) != p.view) continue;
        const LibraryPart* placed = lib.byId(p.partId);
        if (!placed) continue;
        BomRow r = bomRowFromPart(*placed, 1.0);
        r.ref = p.designator;
        r.fromDrawing = true;
        out.push_back(r);
        // The contact it was placed with, immediately under it.
        if (p.contactId.isEmpty()) continue;
        if (const LibraryPart* contact = lib.byId(p.contactId)) {
            BomRow c = bomRowFromPart(*contact, static_cast<double>(placed->wayCount()));
            c.ref = r.ref;   // the same connector: that is what these contacts are for
            c.fromDrawing = true;
            out.push_back(c);
        }
    }
    for (const Entity& e : doc.entities()) {
        for (const auto& entry : e.meta.value("wizardMaterials").toArray()) {
            const auto material = entry.toObject();
            const QString id = material.value("partId").toString();
            const LibraryPart* part = lib.byId(id);
            BomRow row = part ? bomRowFromPart(*part, 0) : BomRow{};
            if (!part) { row.description = material.value("description").toString(); row.mfrPn = material.value("pn").toString();
                         row.partId = id; row.unit = material.value("length").toBool() ? QStringLiteral("mm") : QStringLiteral("each"); }
            const double quantity = material.value("quantity").toDouble();
            row.qty = quantity;
            if (material.value("length").toBool() && !convertPartUnit(quantity, QStringLiteral("mm"), row.unit, row.qty)) row.qty = 1;
            row.ref = material.value("ref").toString(); row.fromDrawing = true;
            out.push_back(row);
        }
        if (e.meta.value("wizardPhysicalBom").toBool()) continue;
        // A part stamped on the entity (by the harness builder, or by inserting a library part)
        // is what the row is about; the entity only says how much of it there is.
        const QString partId = e.meta.value(QStringLiteral("libPart")).toString();
        const LibraryPart* part = partId.isEmpty() ? nullptr : lib.byId(partId);
        if (e.type == EntityType::Connector) {
            const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
            BomRow r = part ? bomRowFromPart(*part, 1.0) : BomRow{};
            if (!part) {
                r.description = spec.partNumber.isEmpty()
                                    ? QStringLiteral("Connector, %1 way").arg(spec.pinCount)
                                    : spec.partNumber;
                r.mfrPn = spec.partNumber;
                r.qty = 1.0;
                r.unit = QStringLiteral("each");
            }
            r.ref = spec.designator;
            r.fromDrawing = true;
            out.push_back(r);
            // A contact chosen when it was placed goes on the line below, one per way.
            const QString contactId = e.meta.value(QLatin1String(kContactPartKey)).toString();
            if (const LibraryPart* contact = contactId.isEmpty() ? nullptr : lib.byId(contactId)) {
                const int ways = part ? part->wayCount() : std::max(1, spec.pinCount);
                BomRow c = bomRowFromPart(*contact, static_cast<double>(ways));
                c.ref = spec.designator;
                c.fromDrawing = true;
                out.push_back(c);
            }
            continue;
        }
        if (e.type != EntityType::Wire || e.points.size() < 2) continue;
        // A wire's quantity is its length. The drawing is in millimetres, so that is the unit -
        // unless the part it names is issued in something else, in which case it is converted.
        double len = 0.0;
        for (size_t i = 1; i < e.points.size(); ++i) len += distance(e.points[i - 1], e.points[i]);
        BomRow r = part ? bomRowFromPart(*part, 0.0) : BomRow{};
        if (!part) {
            const QString colour = e.meta.value(QStringLiteral("wireColor")).toString();
            const QString gauge = e.meta.value(QStringLiteral("wireGauge")).toString();
            r.description = QStringLiteral("Wire %1 %2")
                                .arg(gauge.isEmpty() ? QString() : wireGaugeLabel(gauge),
                                     colour.isEmpty() ? QString() : wireColorName(colour))
                                .trimmed();
            r.unit = QStringLiteral("mm");
        }
        double qty = len;
        if (!convertPartUnit(len, QStringLiteral("mm"), r.unit, qty)) {
            // The part is not issued by length (a cable sold each, say): one of it per run.
            qty = 1.0;
        }
        r.qty = qty;
        r.ref = e.meta.value(QStringLiteral("wireLabel")).toString();
        r.fromDrawing = true;
        out.push_back(r);
    }
    return out;
}

namespace {
QColor namedColour(const QString& text) {
    const QString said = text.trimmed();
    if (said.isEmpty() || said.compare(QLatin1String("clear"), Qt::CaseInsensitive) == 0 ||
        said.compare(QLatin1String("transparent"), Qt::CaseInsensitive) == 0) return {};
    const QString hex = leadColorHex(said);
    if (!hex.isEmpty()) return QColor(hex);
    const QString first = said.section(QLatin1Char('/'), 0, 0).trimmed();
    if (first.compare(QLatin1String("black"), Qt::CaseInsensitive) == 0) return QColor(28, 28, 30);
    return QColor(first.startsWith(QLatin1Char('#')) ? first : first.toLower().remove(QLatin1Char(' ')));
}
} // namespace

QColor partTextColour(const LibraryPart& part) { return namedColour(part.value(QStringLiteral("textColour"))); }

QColor partOuterColour(const LibraryPart& part) {
    for (const char* key : {"jacketColour", "colour", "color"}) {
        const QString said = part.value(QLatin1String(key)).trimmed();
        if (said.isEmpty()) continue;
        if (said.compare(QLatin1String("clear"), Qt::CaseInsensitive) == 0 ||
            said.compare(QLatin1String("transparent"), Qt::CaseInsensitive) == 0) return {};
        const QString hex = leadColorHex(said);
        if (!hex.isEmpty()) return QColor(hex);
        const QString first = said.section(QLatin1Char('/'), 0, 0).trimmed();
        if (first.compare(QLatin1String("black"), Qt::CaseInsensitive) == 0) return QColor(28, 28, 30);
        const QColor named(first.startsWith(QLatin1Char('#')) ? first : first.toLower().remove(QLatin1Char(' ')));
        if (named.isValid()) return named;
    }
    return {};
}

std::vector<BomRow> bomShownRows(const Document& doc, const PartsLibrary& lib) {
    BomTable table;
    table.fromJson(doc.bom());
    std::vector<BomRow> rows = table.rows;
    if (table.config.includeDrawing) {
        const std::vector<BomRow> drawn = bomFromDrawing(doc, lib);
        rows.insert(rows.end(), drawn.begin(), drawn.end());
    }
    if (table.config.unify) rows = bomUnify(rows);
    return rows;
}

QHash<QString, std::vector<PartLink>> partLinksOf(const Document& doc, const PartsLibrary& lib) {
    QHash<QString, std::vector<PartLink>> out;
    // THE BILL AS THE DRAWING SHOWS IT - the line numbers a balloon counts down.
    const std::vector<BomRow> rows = bomShownRows(doc, lib);
    auto partOfRow = [&](const BomRow& r) -> const LibraryPart* {
        if (!r.partId.isEmpty())
            if (const LibraryPart* p = lib.byId(r.partId)) return p;
        for (const LibraryPart& p : lib.parts)
            if ((!r.mfrPn.isEmpty() && p.mfrPn.compare(r.mfrPn, Qt::CaseInsensitive) == 0) ||
                (!r.internalPn.isEmpty() && p.internalPn.compare(r.internalPn, Qt::CaseInsensitive) == 0))
                return &p;
        return nullptr;
    };
    auto lineOf = [&](const LibraryPart& part) {
        for (size_t i = 0; i < rows.size(); ++i)
            if (partOfRow(rows[i]) == &part) return static_cast<int>(i) + 1;
        return 0;
    };
    // AN OBJECT WITH ITS OWN PART first; a balloon pointing at it comes after.
    // (A sleeve over the run with heatshrink on its ends has the heatshrink's as well: libPartEnds.)
    for (const Entity& e : doc.entities())
        for (const char* key : {"libPart", "libPartEnds"}) {
            const QString id = e.meta.value(QLatin1String(key)).toString();
            if (id.isEmpty()) continue;
            if (const LibraryPart* part = lib.byId(id)) out[e.id].push_back({lineOf(*part), part->description, part, false});
        }
    // WHAT A LEADER'S TIP IS ON: the object whose route passes nearest it, within half its width and a little more.
    auto halfWidth = [](const Entity& e) {
        for (const char* key : {"shrinkWidth", "gap", "twistBodyWidth", "twistWireWidth"}) {
            const double w = e.meta.value(QLatin1String(key)).toDouble(0.0);
            if (w > 0.0) return w / 2.0;
        }
        return 0.0;
    };
    auto pointedAt = [&](const Vec2& tip) -> QString {
        QString best;
        double bestGap = 1e300;
        for (const Entity& e : doc.entities()) {
            if (e.meta.value(QStringLiteral("balloon")).toBool() || e.type == EntityType::Text ||
                e.type == EntityType::Dimension)
                continue;
            std::vector<Vec2> path = e.points.size() >= 2 ? e.points : std::vector<Vec2>{e.a, e.b};
            if (e.type == EntityType::Circle) path = {e.center};
            double gap = 1e300;
            if (path.size() == 1) gap = distance(tip, path.front()) - e.radius;
            for (size_t i = 1; i < path.size(); ++i)
                gap = std::min(gap, closestPointOnSegment(tip, path[i - 1], path[i]).distance);
            gap = std::max(0.0, gap - halfWidth(e));
            if (gap < bestGap) { bestGap = gap; best = e.id; }
        }
        return bestGap <= 1.5 ? best : QString();
    };
    for (const Entity& b : doc.entities()) {
        if (!b.meta.value(QStringLiteral("balloon")).toBool()) continue;
        bool ok = false;
        const int item = b.meta.value(QStringLiteral("text")).toString().trimmed().toInt(&ok);
        if (!ok || item < 1 || item > static_cast<int>(rows.size())) continue;
        const BomRow& row = rows[static_cast<size_t>(item - 1)];
        const LibraryPart* part = partOfRow(row);
        for (const QJsonValue& leader : b.meta.value(QStringLiteral("leaders")).toArray()) {
            const QJsonArray at = leader.toArray();
            if (at.size() < 2) continue;
            const QString id = pointedAt({at[0].toDouble(), at[1].toDouble()});
            if (!id.isEmpty()) out[id].push_back({item, row.description, part, true});
        }
    }
    return out;
}

QHash<QString, QColor> partColoursFromBom(const Document& doc, const PartsLibrary& lib) {
    QHash<QString, QColor> out;
    // Its own part, then the parts its balloons number - the last with a colour is the one.
    const QHash<QString, std::vector<PartLink>> links = partLinksOf(doc, lib);
    for (auto it = links.cbegin(); it != links.cend(); ++it)
        for (const PartLink& l : it.value()) {
            if (!l.part) continue;
            const QColor c = partOuterColour(*l.part);
            if (c.isValid()) out.insert(it.key(), c);
            const QColor ink = partTextColour(*l.part);
            if (ink.isValid()) out.insert(it.key() + QStringLiteral("|text"), ink);
        }
    return out;
}

} // namespace Loopwire
