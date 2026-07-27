#pragma once

#include "model/Entity.h"

#include <QString>
#include <vector>

namespace Edraw_Desktop {

// Quick Select (AutoCAD QSELECT): build a selection set from a property filter.
// The property list offered is derived from the sample object, so a circle offers
// Radius / Diameter / Area, a wire offers its gauge / colour / net, and so on — only
// what actually applies to that kind of object, plus the general properties every
// object has.

// One filterable property.
struct QuickSelectProp {
    QString key;          // stable id: a field name, or "meta:<metaKey>"
    QString label;        // shown in the dialog ("Radius", "Wire gauge", ...)
    bool numeric = false; // numeric properties also offer < > <= >=
};

// The properties that make sense for `e`: the general ones (layer, colour,
// lineweight, linetype) followed by the geometry and technical fields of its type.
std::vector<QuickSelectProp> quickSelectProps(const Entity& e);

// The general properties alone — used when there is no sample object to go on.
std::vector<QuickSelectProp> quickSelectGeneralProps();

// `e`'s current value for `key`, as the text the dialog shows and compares. Numbers
// are formatted plainly (no trailing zeros); an unset value is an empty string.
QString quickSelectValue(const Entity& e, const QString& key);

// How a value is compared. All is AutoCAD's "Select All" (the property is ignored,
// so the filter matches purely on object type).
enum class QsOp { Equal, NotEqual, Greater, Less, GreaterEqual, LessEqual, Contains, All };

struct QuickSelectQuery {
    bool wholeDrawing = true; // false = filter the objects already selected
    QString type;             // entity type name to restrict to ("" = any type)
    QString key;              // property to test ("" = type only)
    QsOp op = QsOp::Equal;
    QString value;
    bool exclude = false;     // select what does NOT match
    bool append = false;      // add to the current selection instead of replacing it
};

// Does `e` satisfy the query's type + property test? (`exclude` is applied by
// quickSelectMatchIds, not here.)
bool quickSelectMatches(const Entity& e, const QuickSelectQuery& q);

// The ids the query selects, in drawing order. `pool` is the whole drawing;
// `currentSelection` is used when the query applies to the current selection, and
// carried through when the query appends.
std::vector<QString> quickSelectMatchIds(const std::vector<Entity>& pool,
                                         const QuickSelectQuery& q,
                                         const std::vector<QString>& currentSelection);

// Every distinct value of `key` across `pool`, sorted (numerically for numeric
// properties). Fills the dialog's value dropdown with what the drawing contains.
std::vector<QString> quickSelectDistinctValues(const std::vector<Entity>& pool,
                                               const QString& key);

} // namespace Edraw_Desktop
