#pragma once

#include "tools/ToolId.h"

#include <QColor>
#include <QIcon>

namespace Edraw_Desktop {

// The ribbon's monochrome tool glyphs, drawn in code so the icon grid needs no external assets.
// Shared by MainWindow and by every ribbon tab file, which is why it lives here rather than in one
// translation unit's anonymous namespace.
QIcon makeToolIcon(ToolId id, const QColor& ink = QColor("#d6dae1"));

} // namespace Edraw_Desktop
