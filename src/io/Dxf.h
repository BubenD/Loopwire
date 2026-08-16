#pragma once

#include <QByteArray>
#include <QString>

namespace Edraw_Desktop {

class Document;

// DXF import/export. Export mirrors Edraw's toDxf() (AC1009 / R12-style:
// LINE/CIRCLE/ARC/TEXT + POLYLINE for rects/polylines/ellipses). Import mirrors
// parseDxf(), reading LINE/LWPOLYLINE/POLYLINE/CIRCLE/ARC/TEXT and the LAYER
// table. Polylines and rectangles are decomposed to segments on export, as in
// the original.
namespace dxf {

QString toDxf(const Document& doc);
bool parse(Document& doc, const QString& text, QString* error = nullptr);
// A DXF as it comes off the disk, text or BINARY. AutoCAD writes a binary DXF whenever it is asked for one -
// the same drawing, the same group codes, written as bytes rather than as lines - and it is what "Save as
// DXF" gives by default in some versions. Read as text it is gibberish, and a drawing that plainly exists
// would not open.
bool parseBytes(Document& doc, const QByteArray& bytes, QString* error = nullptr);
// Is this the binary form? (Its 22-byte sentinel.)
bool looksBinary(const QByteArray& bytes);

bool exportFile(const Document& doc, const QString& path, QString* error = nullptr);
bool importFile(Document& doc, const QString& path, QString* error = nullptr);

} // namespace dxf
} // namespace Edraw_Desktop
