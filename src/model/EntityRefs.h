#pragma once

#include "model/Entity.h"

#include <QHash>
#include <QString>

#include <vector>

namespace Edraw_Desktop {

// Entities point at one another by id, and a copy has to point at the copies.
//
// A shield is drawn ACROSS the wires it names. A twist band clips the wires it names. A wire label sits on
// the wire it names, a break mark on the run it is attached to, a balloon in the chain it belongs to. All of
// those are ids in meta - and a copy made by duplicating the entity keeps the ids of the ORIGINALS. What is
// drawn then is a shield whose ellipse spans the wires on the other side of the sheet: a long line straight
// across the drawing, from the copy to the original, which is exactly what a right-drag copy of a harness
// produced.
//
// So every copy set is re-mapped: a reference to something that was copied too becomes a reference to its
// copy, and a reference to something that was left behind is DROPPED. Dropped rather than kept, because a
// shield with no wires draws nothing - which is honest and obvious - while a shield reaching across the sheet
// to somebody else's wires is neither.
//
// `oldToNew` maps the id an entity had to the id its copy has. Anything not in the map was not copied.
void remapEntityReferences(std::vector<Entity>& copies, const QHash<QString, QString>& oldToNew);

// The same, for one entity: used where copies are made one at a time.
void remapEntityReferences(Entity& copy, const QHash<QString, QString>& oldToNew);

// Every meta key that holds an entity id, so the list is written once and the same everywhere. Single ids
// and lists of them are named separately because they are read differently.
const QStringList& entityRefKeys();
const QStringList& entityRefListKeys();

} // namespace Edraw_Desktop
