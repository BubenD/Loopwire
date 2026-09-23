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
#include <QHash>
#include <cmath>
#include <functional>

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

void stampEntityGroup(Entity& e, const QString& key) {
    e.meta.insert(QLatin1String(kInstanceKey), key);
}

void clearEntityBlockStamp(Entity& e) {
    // The instance stamp is what makes a set of primitives one object, and the name is what sends a
    // double-click looking for a block definition. A TEMPLATE group is deliberately left alone: the
    // sheet frame moves as one because it is the sheet's frame, not because somebody grouped it.
    e.meta.remove(QLatin1String(kBlockKey));
    e.meta.remove(QLatin1String(kInstanceKey));
    e.meta.remove(QLatin1String(kOriginX));
    e.meta.remove(QLatin1String(kOriginY));
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
    // THE FACES THE WIRES ENTER BY go down with its body, where it was put down.
    if (mechanical && !part.outputs.empty()) {
        QJsonArray faces;
        for (const MechOutputFace& f : part.outputs)
            for (const Vec2& v : {f.a + at, f.b + at}) {
                faces.append(v.x);
                faces.append(v.y);
            }
        for (Entity& e : out) e.meta.insert(QLatin1String(kEntryFacesKey), faces);
    }
    // And WHICH drawing of it: the same part put down as its body and as its symbol is one part to buy.
    for (Entity& e : out)
        e.meta.insert(QLatin1String(kLibPartViewKey),
                      mechanical ? QStringLiteral("mechanical") : QStringLiteral("schematic"));
    return out;
}

int recoverEntryFaces(std::vector<Entity>& ents, const PartsCatalog& catalog) {
    // Each copy's entities, in the order they were put down - the order of the library's drawing.
    QHash<QString, std::vector<size_t>> copies;
    QStringList order;
    for (size_t i = 0; i < ents.size(); ++i) {
        const Entity& e = ents[i];
        if (!e.meta.contains(QStringLiteral("libPart")) ||
            e.meta.contains(QLatin1String(kEntryFacesKey)))
            continue;
        if (e.meta.value(QLatin1String(kLibPartViewKey)).toString() != QLatin1String("mechanical"))
            continue;
        const QString key = e.meta.value(QLatin1String(kInstanceKey)).toString();
        if (key.isEmpty()) continue;
        if (!copies.contains(key)) order << key;
        copies[key].push_back(i);
    }
    int stamped = 0;
    for (const QString& key : order) {
        const std::vector<size_t>& idx = copies[key];
        const LibraryPart* part =
            catalog.byId(ents[idx.front()].meta.value(QStringLiteral("libPart")).toString());
        if (!part || part->outputs.empty() || part->mechanical.size() != int(idx.size())) continue;
        // The same points, in the library and on the sheet.
        std::vector<Vec2> from, to;
        bool same = true;
        for (size_t j = 0; j < idx.size() && same; ++j) {
            const Entity lib = Entity::fromJson(part->mechanical[int(j)].toObject());
            const Entity& put = ents[idx[j]];
            if (lib.type != put.type || lib.points.size() != put.points.size()) {
                same = false;
                break;
            }
            from.push_back(lib.a);
            to.push_back(put.a);
            from.push_back(lib.b);
            to.push_back(put.b);
            for (size_t m = 0; m < lib.points.size(); ++m) {
                from.push_back(lib.points[m]);
                to.push_back(put.points[m]);
            }
        }
        if (!same || from.size() < 2) continue;
        size_t far = 0;
        for (size_t i = 1; i < from.size(); ++i)
            if (distance(from[i], from[0]) > distance(from[far], from[0])) far = i;
        if (distance(from[far], from[0]) < 1e-9) continue;
        // HOW IT WAS PLACED: turned and scaled about its first point and moved, and perhaps mirrored
        // first. Whichever of the two puts every point where it is on the sheet is the one.
        std::function<Vec2(const Vec2&)> placed;
        for (const bool mirror : {false, true}) {
            auto m = [mirror](const Vec2& p) { return mirror ? Vec2{p.x, -p.y} : p; };
            const Vec2 v = m(from[far]) - m(from[0]);
            const Vec2 w = to[far] - to[0];
            const double s = w.length() / v.length();
            const double turn = std::atan2(w.y, w.x) - std::atan2(v.y, v.x);
            const double c = std::cos(turn) * s, sn = std::sin(turn) * s;
            const Vec2 p0 = m(from[0]), q0 = to[0];
            auto t = [=](const Vec2& p) {
                const Vec2 d = m(p) - p0;
                return q0 + Vec2{d.x * c - d.y * sn, d.x * sn + d.y * c};
            };
            double err = 0.0;
            for (size_t i = 0; i < from.size(); ++i) err = std::max(err, distance(t(from[i]), to[i]));
            if (err < 1e-3 * std::max(1.0, w.length())) {
                placed = t;
                break;
            }
        }
        if (!placed) continue;
        QJsonArray faces;
        for (const MechOutputFace& f : part->outputs)
            for (const Vec2& v : {placed(f.a), placed(f.b)}) {
                faces.append(v.x);
                faces.append(v.y);
            }
        for (size_t i : idx) ents[i].meta.insert(QLatin1String(kEntryFacesKey), faces);
        ++stamped;
    }
    return stamped;
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
