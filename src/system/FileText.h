#pragma once

#include <QString>

#include <vector>

namespace Loopwire {

// THE WORDS IN A FILE, to be searched: each piece of text with where it is - a drawing's texts, notes, table cells and
// attributes (DWX); a DXF's text strings, line by line; a PDF's text; a CSV's cells, row and column. A DWG holds its text
// compressed and is not read (nothing comes back, and `why` says so).
struct TextPiece {
    QString where;   // "Drawing", "Line 120", "PDF", "Row 3 - Description"
    QString text;
};
std::vector<TextPiece> fileText(const QString& path, QString* why = nullptr);

} // namespace Loopwire
