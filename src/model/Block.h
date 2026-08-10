#pragma once

#include "model/Entity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

class Document;
class PartsCatalog;
struct LibraryPart;

// ---- Blocks -------------------------------------------------------------------------------------
// A named piece of drawing, defined once and dropped in wherever it is wanted: a symbol, a title
// block, a standard note, a sub-assembly.
//
// Every copy on a sheet is stamped with the block it came from and with an instance key, so the
// copies of one block are recognisable as a set. That is what lets a block be EDITED - the editor
// changes the definition, and every copy already on a sheet is rebuilt from it.
//
// Blocks are files in the block folder, saved as .ebk. Not .blk: that one is claimed by enough
// unrelated software to be a nuisance on Windows.
struct BlockDef {
    QString name;
    QString description;
    // The point the block is dropped by, in the block's own coordinates. Everything is stored
    // relative to it, so a block lands where it is clicked rather than near it.
    Vec2 basePoint{0.0, 0.0};
    QJsonArray entities;
    // Where it came from: "drawn" for one made from a selection, "library:<part id>" for a library
    // part's own drawing. A library block is not a file - it belongs to the part.
    QString source = QStringLiteral("drawn");
    QString created;

    bool isEmpty() const { return entities.isEmpty(); }
    bool fromLibrary() const { return source.startsWith(QLatin1String("library:")); }
    QString libraryPartId() const {
        return fromLibrary() ? source.mid(QStringLiteral("library:").size()) : QString();
    }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& root);
    bool save(const QString& path) const;
    bool load(const QString& path);

    static QString extension();    // "ebk"
    static QString fileFilter();
    // <app data>/block/<name>.ebk
    static QString pathFor(const QString& name);
    // The blocks in the block folder, by full path.
    static QStringList installed();
};

// Make a block out of entities - a selection, usually. Everything is moved so that `basePoint` is
// the origin, which is what makes the block land under the cursor when it is dropped.
BlockDef blockFromEntities(const QString& name, const std::vector<Entity>& ents,
                           const Vec2& basePoint);

// The entities to drop on a sheet for one copy of a block, at `at`. Each is stamped with the block's
// name, a key shared by this copy's entities, and where it was dropped, so the copy can be found
// again and rebuilt when the definition changes.
std::vector<Entity> blockInstanceEntities(const BlockDef& block, const Vec2& at,
                                          const QString& instanceKey);

// One copy of a library part's drawing, stamped as a block copy: what goes on the sheet when a part is
// inserted, or when the harness builder puts one down. The block is the part - the same name
// libraryBlocks() gives it - so editing the part's drawing rebuilds every copy already placed.
std::vector<Entity> partBlockInstance(const LibraryPart& part, bool mechanical, const Vec2& at,
                                      const QString& instanceKey = QString());

// Every block there is, in one list: the ones saved as files, and every drawing in the connected
// part libraries. A library part IS a block - a symbol drawn once and dropped in wherever it is
// wanted is exactly what a block is for - so the two are offered together rather than a part having
// to be copied into a block first. Library blocks are named "<part> (schematic)" / "(mechanical)".
std::vector<BlockDef> allBlocks(const PartsCatalog* catalog);
// The blocks that came from the library alone.
std::vector<BlockDef> libraryBlocks(const PartsCatalog& catalog);

// Which block an entity is part of, and which copy. Empty when it is not part of one.
QString entityBlockName(const Entity& e);
QString entityBlockInstance(const Entity& e);
Vec2 entityBlockOrigin(const Entity& e);

// Every copy of `name` in the document, as instance keys.
QStringList blockInstancesIn(const Document& doc, const QString& name);

// Rebuild every copy of the block in the document from this definition, keeping each copy where it
// was dropped. Returns how many copies were rebuilt - the point of editing a block.
int updateBlockInstances(Document& doc, const BlockDef& block);

} // namespace Edraw_Desktop
