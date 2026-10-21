#pragma once
#include "geometry/Vec2.h"
#include "model/Entity.h"
#include <vector>
namespace Loopwire {
// Measured centre frames, independent of the visible edge spans of each fan.
struct RunGeometry {
    std::vector<Vec2> centres, normals;
    std::vector<double> along;
    std::vector<double> startOffsets, finishOffsets;
    double flushStart = 0, flushFinish = 0, stripStart = 0, stripFinish = 0;
};
RunGeometry runGeometry(const Entity& pair);
}
