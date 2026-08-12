#include "harness/Bom.h"
#include <QSet>
#include "harness/MechanicalHarness.h"

#include "harness/Harness.h"
#include "model/Document.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {
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
    QSet<QString> countedInstances;
    for (const Entity& e : doc.entities()) {
        const QString instance = e.meta.value(QStringLiteral("blockInst")).toString();
        const QString placedId = e.meta.value(QStringLiteral("libPart")).toString();
        if (instance.isEmpty() || placedId.isEmpty()) continue;
        if (countedInstances.contains(instance)) continue;
        countedInstances.insert(instance);
        const LibraryPart* placed = lib.byId(placedId);
        if (!placed) continue;
        BomRow r = bomRowFromPart(*placed, 1.0);
        r.ref = e.meta.value(QStringLiteral("designator")).toString();
        r.fromDrawing = true;
        out.push_back(r);
        // The contact it was placed with, immediately under it.
        const QString contactId = e.meta.value(QLatin1String(kContactPartKey)).toString();
        if (contactId.isEmpty()) continue;
        if (const LibraryPart* contact = lib.byId(contactId)) {
            BomRow c = bomRowFromPart(*contact, static_cast<double>(placed->wayCount()));
            c.ref = r.ref;   // the same connector: that is what these contacts are for
            c.fromDrawing = true;
            out.push_back(c);
        }
    }
    for (const Entity& e : doc.entities()) {
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

} // namespace Edraw_Desktop
