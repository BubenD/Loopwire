#include "model/EntityRefs.h"

#include "model/PartSymbols.h"

#include <QJsonArray>
#include <QUuid>

namespace Edraw_Desktop {

const QStringList& entityRefKeys() {
    // One id each. "host" is the run a break mark or a marking is attached to; "wireId" the wire a label
    // slides along; "overallShield" the shield a wire belongs to; "chain" the batch a balloon is one of;
    // "bundle" the bundle a run is part of; "legWireId" the wire a shield's leg comes off.
    static const QStringList keys = {QStringLiteral("host"),          QStringLiteral("wireId"),
                                     QStringLiteral("overallShield"), QStringLiteral("chain"),
                                     QStringLiteral("bundle"),        QStringLiteral("legWireId"),
                                     QStringLiteral("shieldGroup"),   QStringLiteral("connectorId")};
    return keys;
}

const QStringList& entityRefListKeys() {
    // Lists of ids: the wires a shield catches, the wires a twist band lays up, the members of a group.
    static const QStringList keys = {QStringLiteral("wires"), QStringLiteral("members"),
                                     QStringLiteral("list")};
    return keys;
}

void remapEntityReferences(Entity& copy, const QHash<QString, QString>& oldToNew) {
    for (const QString& key : entityRefKeys()) {
        if (!copy.meta.contains(key)) continue;
        const QString was = copy.meta.value(key).toString();
        if (was.isEmpty()) continue;
        const auto found = oldToNew.constFind(was);
        if (found != oldToNew.constEnd()) copy.meta.insert(key, *found);
        else copy.meta.remove(key);   // it pointed at something that was not copied
    }
    for (const QString& key : entityRefListKeys()) {
        if (!copy.meta.contains(key)) continue;
        const QJsonArray was = copy.meta.value(key).toArray();
        QJsonArray now;
        for (const QJsonValue& v : was) {
            const QString id = v.toString();
            if (id.isEmpty()) continue;
            const auto found = oldToNew.constFind(id);
            if (found != oldToNew.constEnd()) now.append(*found);
        }
        // A shield that caught four wires and was copied with two of them keeps the two. With none, the key
        // goes: an empty list is what "it catches nothing" is written as everywhere else.
        if (now.isEmpty()) copy.meta.remove(key);
        else copy.meta.insert(key, now);
    }
    // A block instance key says WHICH copy of a block this is. Two instances sharing one key are one
    // instance as far as anything that counts or rebuilds them is concerned - so a copy gets a new one.
    if (copy.meta.contains(QStringLiteral("blockInst")))
        copy.meta.insert(QStringLiteral("blockInst"),
                         QUuid::createUuid().toString(QUuid::WithoutBraces));
    // The same for a stock symbol: a copied resistor is another resistor, and the two have to be told apart
    // by whatever cuts a net under one of them.
    if (copy.meta.contains(QLatin1String(kPartSymbolInstKey)))
        copy.meta.insert(QLatin1String(kPartSymbolInstKey),
                         QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void remapEntityReferences(std::vector<Entity>& copies, const QHash<QString, QString>& oldToNew) {
    // The block instance key is per COPY SET, not per entity: every entity of one placed block shares it, and
    // a copy of that block is one new instance rather than one per line. So it is decided here and handed to
    // each entity that carried the same one.
    QHash<QString, QString> instances;
    QHash<QString, QString> symbols;
    for (Entity& copy : copies) {
        const QString wasInstance = copy.meta.value(QStringLiteral("blockInst")).toString();
        const QString wasSymbol = copy.meta.value(QLatin1String(kPartSymbolInstKey)).toString();
        remapEntityReferences(copy, oldToNew);
        if (!wasInstance.isEmpty()) {
            auto found = instances.constFind(wasInstance);
            if (found == instances.constEnd())
                found = instances.insert(wasInstance,
                                         copy.meta.value(QStringLiteral("blockInst")).toString());
            copy.meta.insert(QStringLiteral("blockInst"), *found);
        }
        if (!wasSymbol.isEmpty()) {
            auto found = symbols.constFind(wasSymbol);
            if (found == symbols.constEnd())
                found = symbols.insert(wasSymbol,
                                       copy.meta.value(QLatin1String(kPartSymbolInstKey)).toString());
            copy.meta.insert(QLatin1String(kPartSymbolInstKey), *found);
        }
    }
}

} // namespace Edraw_Desktop
