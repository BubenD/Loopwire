#pragma once

#include "harness/BrickHarness.h"
#include "harness/Route3D.h"

#include <QStringList>

#include <vector>

namespace Edraw_Desktop {

// THE 3D ROUTE AND THE HARNESS WIZARD'S DESIGN, side by side: each node of the route put for a part of the harness
// (an End brick - a connector, a splice), and each cable's length read off the route between the parts it joins.

// The shortest way along the route's bundles from one node to another, in mm; less than zero where none joins them.
double routeDistance(const Route3D& route, const QString& from, const QString& to);
// The node the part `brick` is put at, or empty.
QString routeNodeOfPart(const Route3D& route, const QString& brick);

// ONE LENGTH THE ROUTE WOULD CHANGE: a cable's own (part empty), or one branch of it to a part where it goes to
// several at one end (0 there: locked to the cable's length with the others).
struct RouteLengthChange {
    QString special;
    QString part;
    QString what;   // said for a person: "W1 (X1 - X2)", "W1 to X3"
    double was = 0.0;
    double now = 0.0;
};

struct RouteLengths {
    std::vector<RouteLengthChange> changes;
    // The cables it could not measure, and why.
    QStringList notes;
    // How many cables it measured.
    int measured = 0;
};

// WHAT THE ROUTE SAYS EACH CABLE'S LENGTH IS: between the nodes its parts are put at, the shortest way along the
// bundles, to the whole millimetre; where a cable goes to several parts at one end, the longest is its length and each
// shorter one that branch's own. A branch off a cable's body, or a cable with a part not put in 3D, is left as it is
// and said why.
RouteLengths routeLengthsFor(const BrickDesign& design, const Route3D& route);
void applyRouteLengths(BrickDesign& design, const RouteLengths& lengths);

} // namespace Edraw_Desktop
