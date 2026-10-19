#pragma once
#include "harness/Harness3D.h"
#include "harness/Route3D.h"
namespace Loopwire {
void appendStockParts3D(Harness3DScene& scene, const std::vector<Entity>& drawing);
struct RoutedFitting3D { Harness3DItem item; QString segment; };
std::vector<RoutedFitting3D> routedStockAndTies3D(const Harness3DScene& scene, const Route3D& route, const Route3D& original);
void appendLacingTies3D(Harness3DScene& scene, const std::vector<Entity>& drawing);
}
