#include "model/Block.h"

#include "harness/PartsLibrary.h"
#include "io/AppPaths.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <limits>
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

Bounds blockLocalBounds(const QJsonArray& entities) {
    Bounds b;
    for (const QJsonValue& v : entities) {
        const Bounds eb = Entity::fromJson(v.toObject()).bounds();
        if (eb.valid) {
            b.expand(eb.min);
            b.expand(eb.max);
        }
    }
    return b;
}

namespace {
// A box with no width or no height still has to have four distinct corners, or the reference's
// frame cannot be read back off them.
Bounds paddedBox(Bounds b) {
    if (!b.valid) {
        b.expand({0.0, 0.0});
    }
    if (b.max.x - b.min.x < 1e-6) { b.min.x -= 0.5; b.max.x += 0.5; }
    if (b.max.y - b.min.y < 1e-6) { b.min.y -= 0.5; b.max.y += 0.5; }
    return b;
}

void setBoxMeta(Entity& e, const Bounds& b) {
    e.meta.insert(QStringLiteral("bx0"), b.min.x);
    e.meta.insert(QStringLiteral("by0"), b.min.y);
    e.meta.insert(QStringLiteral("bx1"), b.max.x);
    e.meta.insert(QStringLiteral("by1"), b.max.y);
}
} // namespace

Entity blockRefEntity(const BlockDef& block, const Vec2& at) {
    Entity e;
    e.type = EntityType::BlockRef;
    const Bounds b = paddedBox(blockLocalBounds(block.entities));
    e.points = {at + Vec2{b.min.x, b.min.y}, at + Vec2{b.max.x, b.min.y}, at + Vec2{b.max.x, b.max.y},
                at + Vec2{b.min.x, b.max.y}};
    e.meta.insert(QLatin1String(kBlockRefKey), block.name);
    setBoxMeta(e, b);
    return e;
}

bool blockRefFrame(const Entity& ref, Vec2& origin, Vec2& ux, Vec2& uy) {
    if (ref.type != EntityType::BlockRef || ref.points.size() < 4) return false;
    const double x0 = ref.meta.value(QStringLiteral("bx0")).toDouble();
    const double y0 = ref.meta.value(QStringLiteral("by0")).toDouble();
    const double w = ref.meta.value(QStringLiteral("bx1")).toDouble() - x0;
    const double h = ref.meta.value(QStringLiteral("by1")).toDouble() - y0;
    if (w <= 1e-12 || h <= 1e-12) return false;
    ux = (ref.points[1] - ref.points[0]) * (1.0 / w);
    uy = (ref.points[3] - ref.points[0]) * (1.0 / h);
    origin = ref.points[0] - ux * x0 - uy * y0;
    return true;
}

void refitBlockRef(Entity& ref, const Bounds& local) {
    Vec2 o, ux, uy;
    if (!blockRefFrame(ref, o, ux, uy)) return;
    const Bounds b = paddedBox(local);
    auto at = [&](double x, double y) { return o + ux * x + uy * y; };
    ref.points = {at(b.min.x, b.min.y), at(b.max.x, b.min.y), at(b.max.x, b.max.y), at(b.min.x, b.max.y)};
    setBoxMeta(ref, b);
}

namespace {
struct KeptBlock {
    QDateTime stamp;
    std::shared_ptr<const BlockRefContent> content;
};
QHash<QString, KeptBlock>& keptBlocks() {
    static QHash<QString, KeptBlock> kept;
    return kept;
}
} // namespace

void BlockRefContent::buildGrid() {
    box = {};
    for (const Bounds& b : bounds)
        if (b.valid) box.expand(b);
    cells.clear();
    if (!box.valid) { cols = rows = 0; return; }
    // About eight to a cell, square-ish, and never a silly number of them.
    const double w = std::max(box.width(), 1e-6), h = std::max(box.height(), 1e-6);
    const double per = std::sqrt(std::max<double>(1.0, double(entities.size()) / 8.0) / (w * h));
    cols = std::clamp(int(std::ceil(w * per)), 1, 512);
    rows = std::clamp(int(std::ceil(h * per)), 1, 512);
    cells.assign(size_t(cols) * size_t(rows), {});
    const double cw = w / cols, ch = h / rows;
    for (size_t i = 0; i < bounds.size(); ++i) {
        const Bounds& b = bounds[i];
        if (!b.valid) continue;
        const int x0 = std::clamp(int((b.min.x - box.min.x) / cw), 0, cols - 1);
        const int x1 = std::clamp(int((b.max.x - box.min.x) / cw), 0, cols - 1);
        const int y0 = std::clamp(int((b.min.y - box.min.y) / ch), 0, rows - 1);
        const int y1 = std::clamp(int((b.max.y - box.min.y) / ch), 0, rows - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) cells[size_t(y) * cols + x].push_back(int(i));
    }
}

double BlockRefContent::nearest(const Vec2& p, const std::function<double(const Entity&)>& dist) const {
    double near = std::numeric_limits<double>::infinity();
    if (cols <= 0 || rows <= 0) return near;
    const double cw = std::max(box.width(), 1e-6) / cols, ch = std::max(box.height(), 1e-6) / rows;
    const double step = std::min(cw, ch);
    // How far outside the grid the point is: nothing in it is nearer than that.
    const double outX = std::max({box.min.x - p.x, 0.0, p.x - box.max.x});
    const double outY = std::max({box.min.y - p.y, 0.0, p.y - box.max.y});
    const double outside = std::hypot(outX, outY);
    const int cx = std::clamp(int((p.x - box.min.x) / cw), 0, cols - 1);
    const int cy = std::clamp(int((p.y - box.min.y) / ch), 0, rows - 1);
    std::vector<char> looked(entities.size(), 0);
    const int most = std::max(cols, rows);
    for (int r = 0; r <= most; ++r) {
        // Everything not yet looked at is at least this far off.
        if (r > 0 && near <= outside + (r - 1) * step) break;
        for (int y = cy - r; y <= cy + r; ++y) {
            if (y < 0 || y >= rows) continue;
            for (int x = cx - r; x <= cx + r; ++x) {
                if (x < 0 || x >= cols) continue;
                if (std::max(std::abs(x - cx), std::abs(y - cy)) != r) continue;   // the ring only
                for (int i : cells[size_t(y) * cols + x]) {
                    if (looked[size_t(i)]) continue;
                    looked[size_t(i)] = 1;
                    near = std::min(near, dist(entities[size_t(i)]));
                }
            }
        }
    }
    return near;
}

std::shared_ptr<const BlockRefContent> blockRefContent(const QString& name) {
    if (name.isEmpty()) return nullptr;
    const QString path = BlockDef::pathFor(name);
    const QFileInfo fi(path);
    if (!fi.exists()) return nullptr;
    auto& kept = keptBlocks();
    const auto it = kept.constFind(name);
    if (it != kept.constEnd() && it->stamp == fi.lastModified()) return it->content;
    BlockDef def;
    if (!def.load(path)) return nullptr;
    auto ents = std::make_shared<BlockRefContent>();
    ents->entities.reserve(static_cast<size_t>(def.entities.size()));
    ents->bounds.reserve(static_cast<size_t>(def.entities.size()));
    for (const QJsonValue& v : def.entities) {
        ents->entities.push_back(Entity::fromJson(v.toObject()));
        ents->bounds.push_back(ents->entities.back().bounds());
    }
    ents->buildGrid();
    kept.insert(name, KeptBlock{fi.lastModified(), ents});
    return ents;
}

std::vector<Entity> explodeBlockRef(const Entity& ref) {
    Vec2 o, ux, uy;
    if (!blockRefFrame(ref, o, ux, uy)) return {};
    const auto content = blockRefContent(ref.meta.value(QLatin1String(kBlockRefKey)).toString());
    if (!content) return {};
    // The frame as the moves that make it: mirrored or not, then scaled, turned and put in place.
    const bool mirrored = ux.x * uy.y - ux.y * uy.x < 0.0;
    const double k = ux.length();
    const double angle = std::atan2(ux.y, ux.x);
    std::vector<Entity> out;
    out.reserve(content->entities.size());
    for (Entity e : content->entities) {
        if (mirrored) mirrorEntity(e, Vec2{0.0, 0.0}, Vec2{1.0, 0.0});
        if (std::abs(k - 1.0) > 1e-12) scaleEntity(e, Vec2{0.0, 0.0}, k);
        if (std::abs(angle) > 1e-12) rotateEntity(e, Vec2{0.0, 0.0}, angle);
        translateEntity(e, o);
        if (e.layer.isEmpty() || e.layer == QLatin1String("0")) e.layer = ref.layer;
        out.push_back(std::move(e));
    }
    return out;
}

void forgetBlockRefContent(const QString& name) {
    if (name.isEmpty()) keptBlocks().clear();
    else keptBlocks().remove(name);
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
                                      const QString& instanceKey, bool withWires) {
    // A wired connector placed by hand comes with its wires when it is set to show them; the harness builder
    // asks without, since it draws the harness's own wires.
    // A part drawn to proportion goes down at its size - scaled by its entry face (see partSized).
    const LibraryPart sizedPart = mechanical ? partSized(part) : part;
    const QJsonArray bare = mechanical ? sizedPart.mechanical : partSchematicDrawing(part);
    const QJsonArray drawing = withWires ? withWiredWires(sizedPart, mechanical, bare) : bare;
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
    if (mechanical && !sizedPart.outputs.empty()) {
        QJsonArray faces;
        for (const MechOutputFace& f : sizedPart.outputs)
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
    // READ ONCE, kept until the file changes. An imported sheet is a block of tens of thousands of
    // lines, and the blocks panel, the Blocks window and naming the next import all ask for the whole
    // list - every sheet ever imported was parsed again each time, and it grew with every import.
    struct Read {
        QDateTime stamp;
        qint64 size = -1;
        BlockDef block;
    };
    static QHash<QString, Read> read;
    std::vector<BlockDef> out;
    for (const QString& path : BlockDef::installed()) {
        const QFileInfo fi(path);
        auto it = read.find(path);
        if (it == read.end() || it->stamp != fi.lastModified() || it->size != fi.size()) {
            BlockDef b;
            if (!b.load(path)) {
                read.remove(path);
                continue;
            }
            it = read.insert(path, Read{fi.lastModified(), fi.size(), b});
        }
        out.push_back(it->block);
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
