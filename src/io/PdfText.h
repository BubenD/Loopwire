// Getting the text out of a PDF, in the application itself.
//
// A datasheet arrives as a PDF, and the technical data worth having - the rating, the tolerance, the
// temperature range - is text inside it. Reading it used to mean shelling out to pdftotext, which most
// machines do not have; the parts library then had to say "install this other program" to fill in a
// field. This reads the PDF directly: no external tool, nothing to install, and nothing launched.
//
// What it does: walks the file's stream objects, inflates the ones that are Flate-compressed (the
// DEFLATE decoder is here too - see PdfText.cpp), and pulls the text-showing operators out of the
// content streams, mapping bytes to characters through the font's /ToUnicode table when it has one.
//
// What it does NOT do: encrypted PDFs, and scans. A scan holds photographs of text and would need
// character recognition, which is a different kind of program; extractText says so rather than
// returning nothing as though the datasheet were blank.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

namespace Edraw_Desktop {
namespace PdfText {

// The text of a PDF, page by page in file order, pages separated by a blank line. Empty when there is
// none to be had; `why` (when given) says what happened either way, in words fit to show a user.
QString extract(const QByteArray& pdf, QString* why = nullptr);

// The same, from a file.
QString extractFromFile(const QString& path, QString* why = nullptr);

// Does this look like a PDF at all? (The header, allowing for the junk some producers put in front.)
bool looksLikePdf(const QByteArray& bytes);

// A zlib or raw-DEFLATE stream, inflated. Public because it is generally useful and because it is
// worth testing on its own. Returns an empty array on malformed input.
QByteArray inflate(const QByteArray& in, bool zlibHeader);

// ---- For reading text WHERE IT IS, not only what it says (the PDF importer) ----

// A font's codes to characters: its /ToUnicode map, or the glyph names of an /Encoding /Differences.
struct GlyphMap {
    QHash<quint32, QString> single;
    int codeBytes = 1;   // 1 for a simple font, 2 for Identity-H and friends
    bool empty() const { return single.isEmpty(); }
};
// A /ToUnicode CMap stream's contents, read.
GlyphMap readToUnicode(const QByteArray& cmap);
// The /Differences of a font's /Encoding dictionary (the dictionary's text), read.
GlyphMap readDifferences(const QByteArray& encodingDict);
// One code, as characters - empty when the map does not have it.
QString mapCode(quint32 code, const GlyphMap* map);
// A literal string's bytes with its escapes undone, and a hex string's bytes.
QByteArray unescapeLiteral(const QByteArray& raw);
QByteArray unhex(const QByteArray& raw);
// How wide a character is, in thousandths of the type size - Helvetica's widths, for a font that does
// not say.
int estimateWidth(QChar c);

} // namespace PdfText
} // namespace Edraw_Desktop
