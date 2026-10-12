#pragma once

#include "geometry/Geometry.h"
#include "model/Entity.h"

#include <QJsonArray>
#include <QString>

#include <vector>

namespace Loopwire {

struct LibraryPart;

// ---- A part drawn from its datasheet PDF ---------------------------------------------------------
//
// A datasheet already holds the two things a library part needs most and nobody wants to draw twice: the
// part's outline drawing, as vector paths, and its pinout, as a table of pin and signal. The PDF importer
// reads both - the paths as lines, the table as text placed where the page sets it - and these take the
// part's own out of the page.

// One row of a pinout: the pin as the datasheet names it ("1", "B") and what it carries ("GND", "CAN_H").
struct PdfPin {
    QString pin;
    QString name;
};

// THE PINOUT, read from placed text. A pin table is a column of pin numbers counting up from one (or
// letters from A), each with its signal beside it on the right; a table split over two columns or two
// pages is read on where the first stops. Only the text whose anchor lies inside `within` is read, when it
// is given - for a page that has a pin table and a parts list of numbered rows side by side. Empty when no
// run of at least two pins is found.
std::vector<PdfPin> pinoutFromText(const std::vector<Entity>& ents, const Bounds* within = nullptr);

// The objects lying wholly inside a box - the view of the part, without the title block, the dimensions
// table and the other views around it. Text only when asked for: a datasheet view's text is its
// dimensions, which describe the drawing rather than being part of it.
std::vector<Entity> entitiesInBox(const std::vector<Entity>& ents, const Bounds& box, bool withText);

// The objects as a part's mechanical drawing: centred on the origin, scaled so the whole is `mmWide`
// across, and in the layer's colour like every other part drawing (a datasheet draws in black, which on
// a dark sheet is nothing). Empty when there is nothing with a width to scale.
QJsonArray partDrawingFromPdf(const std::vector<Entity>& picked, double mmWide);

// A PDF's true size in millimetres for a length in its points (1/72 inch).
double pdfPointsToMm(double points);

// The pinout a part keeps, and setting it. Kept in the part's params as "pinout"; setting a pinout with
// more pins than the part has raises its pin count to match, since a symbol built from it has to have
// somewhere to put each one.
std::vector<PdfPin> partPinout(const LibraryPart& part);
void setPartPinout(LibraryPart& part, const std::vector<PdfPin>& pins);

// What a pin is labelled with on a symbol: its pin and its signal, "1 GND"; the pin alone with no signal.
QString pinLabelFor(const PdfPin& pin);

} // namespace Loopwire
