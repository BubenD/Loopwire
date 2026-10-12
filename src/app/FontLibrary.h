#pragma once

#include <QStringList>

class QFontComboBox;

namespace Loopwire {

// Central font library shared by every text editor in the app (the on-canvas
// text editor, the Text dialog and the Connector build fonts). Bundled font
// files (a "fonts" folder next to the executable) are registered once with the
// application, and every picker is restricted to the same curated family list.
class FontLibrary {
public:
    // The library families: bundled fonts + curated standard families that are
    // installed on this system (sorted, no duplicates). Never empty — falls
    // back to the full system list if nothing matches.
    static QStringList families();
    // Point a font picker at the library (keeps its current selection).
    static void applyTo(QFontComboBox* box);
};

} // namespace Loopwire
