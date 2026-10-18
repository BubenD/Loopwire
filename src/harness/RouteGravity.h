#pragma once

#include "harness/Route3D.h"

#include <QString>

#include <vector>

namespace Loopwire {

// A CABLE HUNG UNDER ITS OWN WEIGHT: each bundle of a route, held where it is held - at its two nodes, straight out of
// a connector for its lead, and wherever it lies on something (a face right under it, the way gravity pulls) - and
// between those, where it is in the air, pulled down by its weight against the tension it is put in with: it sags, a
// cable put in with less tension more, and one put in slack hangs by its own length; and it rests on whatever solid it
// falls onto. A cable laid along a floor stays as it is laid; a stretch laid through the air, or up a wall with nothing
// holding it, falls. What it shows is where the cable will fall to.

// HOW TAUT A CABLE IS PUT IN: the choices of the fall menu, each with its pull in newtons. Nought: slack - it hangs by
// its own length.
struct CableTension {
    QString name;
    double newtons = 0.0;
};
const std::vector<CableTension>& cableTensions();
// The name of a pull - one of the choices, or so many newtons.
QString cableTensionName(double newtons);
std::vector<CableTension> cableTensionsFor(const Route3D& route, const RouteSegment3D& segment);
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
    // THE STRETCHES IN THE AIR - nothing under them - by their first and last points (held, or lying on something).
    std::vector<std::pair<int, int>> spans;
    // The way gravity pulled.
    V3 down{0.0, 0.0, -1.0};
};

// The tension a bundle is put in with: its own, or the route's.
double tensionOf(const Route3D& r, const RouteSegment3D& s);
// Each bundle hung, gravity pulling along `down`; the route's solids (where it has them) catch it.
CableSag3D hangCable(const Route3D& r, const RouteSegment3D& s, const V3& down, double density = kCableDensity);
std::vector<CableSag3D> hangRoute(const Route3D& r, const V3& down, double density = kCableDensity);
// How far a point must fall to be shown falling: a millimetre, or a quarter of the bundle's diameter.
double fallShown(double diameter);
// HOW STIFF A CABLE IS PULLED, by its tension: how many times it is smoothed round between its steps as it is pulled -
// none slack, more the tauter.
int stiffnessOf(double tension);

} // namespace Loopwire
