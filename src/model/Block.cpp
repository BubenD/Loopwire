#include "model/Block.h"

#include "harness/PartsLibrary.h"
#include "io/AppPaths.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>

namespace Edraw_Desktop {
namespace {
constexpr auto kBlockKey = "block";
constexpr auto kInstanceKey = "blockInst";
constexpr auto kOriginX = "blockOriginX";
constexpr auto kOriginY = "blockOriginY";
} // namespace

QString BlockDef::extension() { return QStringLiteral("ebk"); }

QString BlockDef::fileFilter() { return QStringLiteral("Edraw block (*.ebk);;All files (*)"); }

QString BlockDef::pathFor(const QString& name) {
    QString safe = name.trimmed();
    if (safe.isEmpty()) safe = QStringLiteral("block");
    for (QChar bad : {QLatin1Char('/'), QLatin1Char('\\'), QLatin1Char(':'), QLatin1Char('*'),
                      QLatin1Char('?'), QLatin1Char('"'), QLatin1Char('<'), QLatin1Char('>'),
                      QLatin1Char('|')})
        safe.replace(bad, QLatin1Char('-'));
    return QStringLiteral("%1/%2.%3").arg(AppPaths::blockDir(), safe, extension());
}

QStringList BlockDef::installed() {
    QDir dir(AppPaths::blockDir());
    QStringList out;
    for (const QString& f :
         dir.entryList({QStringLiteral("*.%1").arg(extension())}, QDir::Files, QDir::Name))
        out << dir.absoluteFilePath(f);
    return out;
}

QJsonObject BlockDef::toJson() const {
    QJsonObject o;
    o[QStringLiteral("kind")] = QStringLiteral("edraw-block");
    o[QStringLiteral("version")] = 1;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("description")] = description;
    o[QStringLiteral("baseX")] = basePoint.x;
    o[QStringLiteral("baseY")] = basePoint.y;
    o[QStringLiteral("source")] = source;
    o[QStringLiteral("created")] = created;
    o[QStringLiteral("entities")] = entities;
    return o;
}

void BlockDef::fromJson(const QJsonObject& root) {
    name = root.value(QStringLiteral("name")).toString();
    description = root.value(QStringLiteral("description")).toString();
    basePoint = Vec2{root.value(QStringLiteral("baseX")).toDouble(),
                     root.value(QStringLiteral("baseY")).toDouble()};
    source = root.value(QStringLiteral("source")).toString(QStringLiteral("drawn"));
    created = root.value(QStringLiteral("created")).toString();
    entities = root.value(QStringLiteral("entities")).toArray();
}

bool BlockDef::save(const QString& path) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    BlockDef stamped = *this;
    stamped.created = QDate::currentDate().toString(Qt::ISODate);
    const qint64 wrote = f.write(QJsonDocument(stamped.toJson()).toJson(QJsonDocument::Indented));
    f.close();
    return wrote > 0;
}

bool BlockDef::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("kind")).toString() != QLatin1String("edraw-block")) return false;
    fromJson(root);
    if (name.isEmpty()) name = QFileInfo(path).completeBaseName();
    return true;
}

// ---- Making one and dropping it -----------------------------------------------------------------

BlockDef blockFromEntities(const QString& name, const std::vector<Entity>& ents,
                           const Vec2& basePoint) {
    BlockDef b;
    b.name = name;
    b.basePoint = Vec2{0.0, 0.0};   // stored about its own origin, see the header
    for (const Entity& e : ents) {
        Entity moved = e;
        translateEntity(moved, Vec2{-basePoint.x, -basePoint.y});
        // A copy of a block is not itself part of the block it was copied out of.
        moved.meta.remove(QLatin1String(kBlockKey));
        moved.meta.remove(QLatin1String(kInstanceKey));
        moved.meta.remove(QLatin1String(kOriginX));
        moved.meta.remove(QLatin1String(kOriginY));
        QJsonObject o = moved.toJson();
        o.remove(QStringLiteral("id"));
        b.entities.append(o);
    }
    return b;
}

std::vector<Entity> blockInstanceEntities(const BlockDef& block, const Vec2& at,
                                          const QString& instanceKey) {
    const QString key = instanceKey.isEmpty()
                            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                            : instanceKey;
    std::vector<Entity> out;
    for (const auto v : block.entities) {
        Entity e = Entity::fromJson(v.toObject());
        e.id.clear();
        translateEntity(e, at);
        e.meta.insert(QLatin1String(kBlockKey), block.name);
        e.meta.insert(QLatin1String(kInstanceKey), key);
        e.meta.insert(QLatin1String(kOriginX), at.x);
        e.meta.insert(QLatin1String(kOriginY), at.y);
        out.push_back(e);
    }
    return out;
}

// ---- The library's drawings are blocks too -------------------------------------------------------

std::vector<Entity> partBlockInstance(const LibraryPart& part, bool mechanical, const Vec2& at,
                                      const QString& instanceKey) {
    const QJsonArray drawing = mechanical ? part.mechanical : partSchematicDrawing(part);
    if (drawing.isEmpty()) return {};
    // The same block libraryBlocks() offers, built here rather than searched for: a part's drawing IS
    // the block, and naming it the same way is what lets every copy already on a sheet be rebuilt when
    // the part is edited.
    BlockDef b;
    b.name = QStringLiteral("%1 (%2)")
                 .arg(part.title(), mechanical ? QStringLiteral("mechanical")
                                               : QStringLiteral("schematic"));
    b.description = part.description.isEmpty() ? partKindName(part.kind) : part.description;
    b.source = QStringLiteral("library:%1").arg(part.id);
    b.entities = drawing;
    std::vector<Entity> out = blockInstanceEntities(b, at, instanceKey);
    // Which PART this is, on every entity of it. Without that a part placed from the library is a set of
    // lines that nothing can trace back to a part number - the bill of materials could not count it, and
    // counting what is on the drawing is most of what a bill of materials is for.
    for (Entity& e : out) e.meta.insert(QStringLiteral("libPart"), part.id);
    // And WHICH drawing of it: the same part put down as its body and as its symbol is one part to buy.
    for (Entity& e : out)
        e.meta.insert(QLatin1String(kLibPartViewKey),
                      mechanical ? QStringLiteral("mechanical") : QStringLiteral("schematic"));
    return out;
}

std::vector<Entity> partDrawingBlockInstance(const LibraryPart& part, const QJsonArray& drawing,
                                             const QString& what, const Vec2& at, bool mirrorY,
                                             const QString& instanceKey) {
    if (drawing.isEmpty()) return {};
    BlockDef b;
    b.name = QStringLiteral("%1 (%2)").arg(part.title(), what);
    b.description = part.description.isEmpty() ? partKindName(part.kind) : part.description;
    b.source = QStringLiteral("library:%1").arg(part.id);
    b.entities = drawing;
    std::vector<Entity> out = blockInstanceEntities(b, at, instanceKey);
    if (mirrorY) {
        // About the X axis through where it was placed: the fanout drawn for one side of the connector, put
        // down for the other. Mirroring is what turns a front view into a back view - the wires leave the
        // other way, and everything else about the drawing is the same drawing.
        for (Entity& e : out) mirrorEntity(e, at, Vec2{at.x + 1.0, at.y});
        b.name = QStringLiteral("%1 (%2, mirrored)").arg(part.title(), what);
        for (Entity& e : out) e.meta.insert(QLatin1String(kBlockKey), b.name);
    }
    for (Entity& e : out) e.meta.insert(QStringLiteral("libPart"), part.id);
    for (Entity& e : out) e.meta.insert(QLatin1String(kLibPartViewKey), what);
    return out;
}

std::vector<BlockDef> libraryBlocks(const PartsCatalog& catalog) {
    std::vector<BlockDef> out;
    const PartsLibrary merged = catalog.merged();
    for (const LibraryPart& p : merged.parts) {
        const QString title = p.title();
        // A part can have two drawings of itself, and they are two different blocks.
        struct Side { const QJsonArray* drawing; const char* what; };
        const QJsonArray schematic = partSchematicDrawing(p);
        const Side sides[] = {{&schematic, "schematic"}, {&p.mechanical, "mechanical"}};
        for (const Side& side : sides) {
            if (side.drawing->isEmpty()) continue;
            BlockDef b;
            b.name = QStringLiteral("%1 (%2)").arg(title, QString::fromLatin1(side.what));
            b.description = p.description.isEmpty() ? partKindName(p.kind) : p.description;
            b.source = QStringLiteral("library:%1").arg(p.id);
            b.entities = *side.drawing;
            out.push_back(b);
        }
    }
    return out;
}

std::vector<BlockDef> allBlocks(const PartsCatalog* catalog) {
    std::vector<BlockDef> out;
    for (const QString& path : BlockDef::installed()) {
        BlockDef b;
        if (b.load(path)) out.push_back(b);
    }
    if (catalog)
        for (BlockDef& b : libraryBlocks(*catalog)) out.push_back(b);
    return out;
}

QString entityBlockName(const Entity& e) {
    return e.meta.value(QLatin1String(kBlockKey)).toString();
}

QString entityBlockInstance(const Entity& e) {
    return e.meta.value(QLatin1String(kInstanceKey)).toString();
}

Vec2 entityBlockOrigin(const Entity& e) {
    return Vec2{e.meta.value(QLatin1String(kOriginX)).toDouble(),
                e.meta.value(QLatin1String(kOriginY)).toDouble()};
}

QStringList blockInstancesIn(const Document& doc, const QString& name) {
    QStringList keys;
    for (const Entity& e : doc.entities()) {
        if (entityBlockName(e) != name) continue;
        const QString key = entityBlockInstance(e);
        if (!key.isEmpty() && !keys.contains(key)) keys << key;
    }
    return keys;
}

int updateBlockInstances(Document& doc, const BlockDef& block) {
    if (block.name.isEmpty()) return 0;
    // Where each copy was dropped, so it can be put back in the same place.
    struct Instance { QString key; Vec2 at; };
    std::vector<Instance> found;
    for (const Entity& e : doc.entities()) {
        if (entityBlockName(e) != block.name) continue;
        const QString key = entityBlockInstance(e);
        if (key.isEmpty()) continue;
        bool seen = false;
        for (const Instance& i : found) seen = seen || i.key == key;
        if (!seen) found.push_back({key, entityBlockOrigin(e)});
    }
    if (found.empty()) return 0;

    // Out with the old copies...
    std::vector<Entity> kept;
    for (const Entity& e : doc.entities()) {
        if (entityBlockName(e) == block.name && !entityBlockInstance(e).isEmpty()) continue;
        kept.push_back(e);
    }
    // ...and in with the new, each where its own copy was.
    for (const Instance& i : found)
        for (Entity& e : blockInstanceEntities(block, i.at, i.key)) kept.push_back(e);
    doc.entities() = kept;
    doc.setDirty(true);
    return static_cast<int>(found.size());
}

} // namespace Edraw_Desktop
