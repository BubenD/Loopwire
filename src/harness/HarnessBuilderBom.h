#pragma once
#include "harness/HarnessBuilder.h"
#include "harness/Bom.h"

namespace Loopwire {
struct HarnessBomLine {
    BomRow row;
    QStringList wireIds;
    bool missingPart = false;
    bool missingLength = false;
};
// Physical materials, independent of drawing geometry. Cable conductors do not
// multiply a cable's purchasing length. Unknown lengths remain explicitly unset.
std::vector<HarnessBomLine> harnessMaterials(const HarnessDef& def, const PartsLibrary& lib);
}
