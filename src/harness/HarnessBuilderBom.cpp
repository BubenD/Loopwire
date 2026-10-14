#include "harness/HarnessBuilderBom.h"
#include <QSet>
#include <algorithm>

namespace Loopwire {
bool harnessTailAt(const HarnessWire& wire, int end) {
    const auto& t = end == 0 ? wire.fromTermination : wire.toTermination;
    return t.via.isEmpty() ? wire.kind == QLatin1String("coax-splice") : t.via == QLatin1String("wire");
}
bool harnessCoaxShield(const HarnessWire& wire) {
    return wire.coaxRole.isEmpty() ? wire.color.startsWith(QLatin1String("SHIELD"))
                                   : wire.coaxRole == QLatin1String("shield");
}

std::vector<HarnessBomLine> harnessMaterials(const HarnessDef& def, const PartsLibrary& lib) {
    std::vector<HarnessBomLine> lines;
    auto add = [&](const QString& id, const QString& pn, PartKind kind, const QString& description,
                   double qty, bool length, const QString& ref, const QStringList& wires) {
        const LibraryPart* part = lib.byId(id);
        HarnessBomLine line;
        line.row = part ? bomRowFromPart(*part, 0) : BomRow{};
        if (!part) { line.row.description = description; line.row.mfrPn = pn;
                     line.row.partId = id; line.row.unit = length ? QStringLiteral("m") : QStringLiteral("each"); }
        line.row.ref = ref;
        line.row.fromDrawing = true;
        line.wireIds = wires;
        line.missingPart = part ? false : (pn.isEmpty() || !id.isEmpty());
        const double factor = lengthUnitInMm(line.row.unit);
        line.missingLength = length && factor > 0 && qty <= 0;
        line.row.qty = length ? (factor > 0 ? qty / factor : 1.0) : qty;
        if (!line.missingPart && !line.missingLength) {
            for (auto& previous : lines)
                if (!previous.missingPart && !previous.missingLength &&
                    bomSameThing(previous.row, line.row) && partUnitsCompatible(previous.row.unit, line.row.unit)) {
                    double converted = line.row.qty;
                    convertPartUnit(line.row.qty, line.row.unit, previous.row.unit, converted);
                    previous.row.qty += converted;
                    previous.row.ref += QStringLiteral(", ") + ref;
                    for (const auto& wire : wires) if (!previous.wireIds.contains(wire)) previous.wireIds << wire;
                    return;
                }
        }
        Q_UNUSED(kind);
        lines.push_back(std::move(line));
    };
    for (const auto& c : def.connectors) {
        add(c.libPart, c.partNumber, PartKind::Connector, QStringLiteral("Connector %1").arg(c.designator),
            1, false, c.designator, {});
        if (!c.contactsIncluded)
            add(c.contactLibPart, c.contactPn, PartKind::ContactPin, QStringLiteral("Contacts %1").arg(c.designator),
                c.pinCount, false, c.designator, {});
    }
    QSet<QString> cables;
    for (const auto& w : def.wires) {
        const auto* part = lib.byId(w.libPart);
        const bool cable = !w.bundle.isEmpty() &&
            (harnessIsCoaxKind(w.kind) || w.kind == QLatin1String("cable") || (part && part->kind == PartKind::Cable));
        const QString key = w.bundle + QLatin1Char('/') + w.libPart + QLatin1Char('/') + w.partNumber;
        if (!cable || !cables.contains(key)) {
            QStringList ids{w.id};
            if (cable) for (const auto& member : def.wires)
                if (member.bundle == w.bundle && member.id != w.id) ids << member.id;
            add(w.libPart, w.partNumber, cable ? PartKind::Cable : PartKind::Wire,
                cable ? QStringLiteral("Cable %1").arg(w.bundle) : QStringLiteral("Wire %1").arg(w.id),
                w.cutLengthMm, true, cable ? w.bundle : w.id, ids);
            if (cable) cables.insert(key);
        }
        for (int end = 0; end < 2; ++end) {
            if (!harnessTailAt(w, end)) continue;
            const auto& t = end == 0 ? w.fromTermination : w.toTermination;
            const QString ref = QStringLiteral("%1 %2").arg(w.id, end == 0 ? QStringLiteral("From") : QStringLiteral("To"));
            add(t.wireLibPart, t.wirePn, PartKind::Wire, QStringLiteral("Tail %1").arg(ref), t.lengthMm, true, ref, {w.id});
            add(t.spliceLibPart, t.splicePn, PartKind::Splice, QStringLiteral("Splice %1").arg(ref), 1, false, ref, {w.id});
        }
    }
    return lines;
}
}
