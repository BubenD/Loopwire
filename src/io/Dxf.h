#pragma once

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

bool exportFile(const Document& doc, const QString& path, QString* error = nullptr);
bool importFile(Document& doc, const QString& path, QString* error = nullptr);

} // namespace dxf
} // namespace Edraw_Desktop
