#pragma once

// READING A STEP FILE THROUGH OPEN CASCADE. Only in a build that has it (EDRAW_HAVE_OCCT); nothing outside
// src/geometry includes this, so no Open CASCADE header reaches the rest of Edraw.

#include "geometry/Environment3D.h"

namespace Edraw_Desktop {

// Every solid of the file, tessellated, placed where the assembly puts it, with its name and colour. False, and
// why in `how`, when the file cannot be read.
bool readStepOcct(const QString& path, std::vector<EnvPart3D>& parts, QString* how);

} // namespace Edraw_Desktop
