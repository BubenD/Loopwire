#pragma once

#include "model/PartSymbols.h"
#include "tools/ToolId.h"

#include <QColor>
#include <QIcon>

namespace Loopwire {

// The ribbon's monochrome tool glyphs, drawn in code so the icon grid needs no external assets.
// Shared by MainWindow and by every ribbon tab file, which is why it lives here rather than in one
// translation unit's anonymous namespace.
// The ink a tool icon is drawn in: light, for the dark ribbon it normally sits on. The theme pass overrides
// it per scheme. A tool with a fall-menu wears its chosen METHOD's icon instead, which is drawn in the
// geometry blue - that difference is deliberate and is how you can see a button has methods behind it.
QIcon makeCadActionIcon(const QString& key, const QColor& ink = QColor("#d6dae1"));

QIcon makeToolIcon(ToolId id, const QColor& ink = QColor("#d6dae1"), int variant = 0);

// The icon for one of the stock electronic parts, drawn from the part's OWN drawing scaled into the
// icon: the menu entry then cannot show anything other than what the entry places.
QIcon makePartSymbolIcon(PartSymbol s, const QColor& ink = QColor("#d6dae1"));

// THE WARM ICONS: while one of these is alive, every icon drawn is drawn with an orange accent instead of the blue -
// and remembered as one, so it is drawn orange again whenever the scheme redraws the ribbon. Held round the building
// of the Modify and Annotation tabs and the Special group.
struct WarmRibbonIcons {
    WarmRibbonIcons();
    ~WarmRibbonIcons();
    WarmRibbonIcons(const WarmRibbonIcons&) = delete;
    WarmRibbonIcons& operator=(const WarmRibbonIcons&) = delete;
};

} // namespace Loopwire
