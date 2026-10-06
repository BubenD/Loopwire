#pragma once

#include "harness/Route3D.h"

#include <QString>

#include <vector>

namespace Edraw_Desktop {

// A CABLE HUNG UNDER ITS OWN WEIGHT: each bundle of a route, held where it is held - at its two nodes, and straight out
// of a connector for its lead - pulled by the tension it is put in with and by gravity, and resting on whatever solid
// of the model it falls onto. Where a stretch of it hangs in the air it sags; a cable put in with less tension sags
// more, and one put in slack hangs by its own length. What it shows is where the cable will fall to.

// HOW TAUT A CABLE IS PUT IN: the choices of the fall menu, each with its pull in newtons. Nought: slack - it hangs by
// its own length.
struct CableTension {
    QString name;
    double newtons = 0.0;
};
const std::vector<CableTension>& cableTensions();
// The name of a pull - one of the choices, or so many newtons.
QString cableTensionName(double newtons);
// A cable's weight: a bundle of wires is about this dense, in g/cm3.
constexpr double kCableDensity = 1.8;

// ONE BUNDLE AS IT HANGS: its points as laid, the same points as they hang, and how far each has fallen (down the way
// gravity pulls); the tension it hangs by - the one it is put in with, or (`byLength`, put in slack) the pull its own
// length leaves it. Put in with a tension, it is as long as that tension makes it: `hungLength` against `laidLength`.
struct CableSag3D {
    QString segment;
    std::vector<V3> laid;
    std::vector<V3> hung;
    std::vector<double> drop;
    double maxDrop = 0.0;
    V3 maxAt;
    double tension = 0.0;
    bool byLength = false;
    double laidLength = 0.0;
    double hungLength = 0.0;
    // WHERE IT FALLS: the lowest point of each stretch that falls further than a cable's sag is worth showing.
    std::vector<int> falls;
};

// The tension a bundle is put in with: its own, or the route's.
double tensionOf(const Route3D& r, const RouteSegment3D& s);
// Each bundle hung, gravity pulling along `down`; the route's solids (where it has them) catch it.
CableSag3D hangCable(const Route3D& r, const RouteSegment3D& s, const V3& down, double density = kCableDensity);
std::vector<CableSag3D> hangRoute(const Route3D& r, const V3& down, double density = kCableDensity);
// How far a point must fall to be shown falling: a millimetre, or a quarter of the bundle's diameter.
double fallShown(double diameter);

} // namespace Edraw_Desktop
