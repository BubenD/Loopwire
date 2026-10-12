#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace Loopwire {

// THE 3D COMMANDS' ICONS, drawn rather than loaded - as every icon in Loopwire is - so they redraw in the scheme's ink and
// read at any size: a cube seen from where a view looks, a plug for a connector, a ruler for lengths, each solid as
// itself. `ink` is the line colour, `accent` what the command acts on. An unknown key draws a plain cube.
QIcon icon3D(const QString& key, const QColor& ink, const QColor& accent = QColor(232, 128, 34));

} // namespace Loopwire
