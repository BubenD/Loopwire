#pragma once

#include "system/SystemDesign.h"

#include <QPointF>
#include <QString>

#include <vector>

namespace Edraw_Desktop {

// THE SYSTEM DIAGRAM OUT OF EDRAW: saved as a file of its own (and read back), and drawn for draw.io (diagrams.net)
// and for Visio - units, ports, cables (with their bends and labels) and notes where they are on the diagram.

// WHERE THINGS ARE ON THE DIAGRAM, as the System design tab draws them: a port's point on its unit's side, the way
// out of that side, and a cable's square path - out of each port's outside, or in from its inside, `end` beyond it.
QPointF sysPortPoint(const SysUnit& u, const SysPort& p);
QPointF sysPortOut(const SysPort& p);
std::vector<QPointF> sysCablePath(const SystemDesign& d, const SysCable& c, double end = 8.0);

// A FILE OF ITS OWN (.wetsys - Edraw's own extension, Whatson ETS system; .edsys read too): the design as JSON, marked
// as one. Read back: false where it is not one.
bool saveSystemFile(const SystemDesign& d, const QString& path, QString* error = nullptr);
bool loadSystemFile(const QString& path, SystemDesign& d, QString* error = nullptr);

// FOR DRAW.IO: the diagram as a .drawio file (mxGraph XML, not compressed) - and the text of it.
QString systemDrawioXml(const SystemDesign& d);
bool exportSystemDrawio(const SystemDesign& d, const QString& path, QString* error = nullptr);

// FOR VISIO: the diagram as a .vsdx package (Visio 2013 and on) - a page of shapes, a hundred of the diagram's units to
// the inch.
bool exportSystemVsdx(const SystemDesign& d, const QString& path, QString* error = nullptr);

} // namespace Edraw_Desktop
