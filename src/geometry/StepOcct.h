#pragma once

// READING A STEP FILE THROUGH OPEN CASCADE. Only in a build that has it (EDRAW_HAVE_OCCT); nothing outside
// src/geometry includes this, so no Open CASCADE header reaches the rest of Edraw.

#include "geometry/Environment3D.h"

namespace Edraw_Desktop {

// Every solid of the file, tessellated, placed where the assembly puts it, with its name and colour. False, and
// why in `how`, when the file cannot be read.
// A SIMPLE SOLID OF A BUILT MODEL, for Open CASCADE: its shape (a BodyShape), its sizes, and where it goes - a 3x4
// matrix, each column where an axis goes (turned and scaled) and the last where it is put.
struct OcctSolid {
    int shape = 0;
    double a = 0.0, b = 0.0, c = 0.0;
    double m[3][4] = {};
};
// THE SOLID WITH THE CUTS TAKEN OUT OF IT, as triangles. False (and why) where it cannot be made.
bool occtCutSolid(const OcctSolid& solid, const std::vector<OcctSolid>& cuts, Mesh3D& out, QString* why);

bool readStepOcct(const QString& path, std::vector<EnvPart3D>& parts, QString* how, const Load3DProgress& progress = {});

} // namespace Edraw_Desktop
