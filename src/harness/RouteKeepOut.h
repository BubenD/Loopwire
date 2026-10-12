#pragma once

#include "harness/Route3D.h"

#include <QString>

#include <vector>

namespace Loopwire {

// WHERE A CABLE PASSES THROUGH A KEEP-OUT: which cable, which keep-out, the first point of it inside, and whether the
// keep-out blocks it (or only warns of it).
struct KeepOutHit {
    QString segment;
    QString keepOut;
    V3 at;
    bool block = false;
};

// A keep-out's name as it is shown: its own, or "Level Z 80.0" / "Zone K2".
QString keepOutName(const KeepOut3D& k);
// WHETHER A STRETCH OF A CABLE'S MIDDLE, from `a` to `b`, passes into a keep-out - the cable's radius counted: a level
// crossed or touched, a zone entered - and where it first does.
bool crossesKeepOut(const KeepOut3D& k, const V3& a, const V3& b, double radius, V3& at);
// Every cable of the route through every keep-out, once each, at the first point it goes in.
std::vector<KeepOutHit> keepOutHits(const Route3D& r);

} // namespace Loopwire
