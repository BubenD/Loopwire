#pragma once

#include "model/PartSymbols.h"
#include "tools/ToolId.h"

#include <QColor>
#include <QIcon>

namespace Edraw_Desktop {

// The ribbon's monochrome tool glyphs, drawn in code so the icon grid needs no external assets.
// Shared by MainWindow and by every ribbon tab file, which is why it lives here rather than in one
// translation unit's anonymous namespace.
// The ink a tool icon is drawn in. The SAME blue the fall-menu method icons use (kGeoBlue, #5a9bf0):
// Circle, Arc, Ellipse, Spline and Dimension all wear their chosen method's icon, which is drawn in that
// blue, so with the plain tools in a pale grey the Draw group came out half blue and half grey - the four
// with a fall-menu coloured and the eight without them washed out, as though those were disabled.
QIcon makeToolIcon(ToolId id, const QColor& ink = QColor("#5a9bf0"));

// The icon for one of the stock electronic parts, drawn from the part's OWN drawing scaled into the
// icon: the menu entry then cannot show anything other than what the entry places.
QIcon makePartSymbolIcon(PartSymbol s, const QColor& ink = QColor("#d6dae1"));

} // namespace Edraw_Desktop
