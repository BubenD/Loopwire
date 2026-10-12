#pragma once

#include "harness/PartsLibrary.h"

#include <QString>
#include <QStringList>
#include <vector>

namespace Loopwire {

// ---- Datasheets ---------------------------------------------------------------------------------
// A part's datasheet, kept with the application rather than left wherever it was downloaded to.
//
// The file is copied into the datasheet folder and named after its own CONTENT, so the same
// datasheet attached to six parts is one file referenced six times. That is what makes removing one
// safe: the file goes only when nothing else points at it, and when something does, the answer says
// how many parts and which.

namespace DatasheetStore {

QString dir();

// Copy a file in. Returns the name it is stored under (not a path) - that is what a part records.
// Empty on failure, with `error` set. Attaching a file that is already stored returns the existing
// name and copies nothing.
QString attach(const QString& sourcePath, QString* error = nullptr);

QString pathOf(const QString& storedName);
bool exists(const QString& storedName);
// What it was called before it was filed, for showing to a person.
QString originalName(const QString& storedName);

// Every part in the catalog that points at this datasheet.
std::vector<const LibraryPart*> tiedParts(const PartsCatalog& catalog, const QString& storedName);
// Their titles, for saying so.
QStringList tiedPartTitles(const PartsCatalog& catalog, const QString& storedName);

// What happened when a datasheet was taken off a part.
struct DetachResult {
    QString storedName;
    bool wasAttached = false;   // the part had one at all
    bool fileDeleted = false;   // and nothing else pointed at it, so it is gone
    QStringList stillTiedTo;    // the parts that still do - the file was kept for them
};

// Take a file off `partId` and delete it if no other part uses it. The catalog is searched across
// every connected library, because a datasheet shared between two libraries is still shared.
// `storedName` empty means the first file the part has, which is what "the datasheet" means when only
// one can be named.
DetachResult detach(PartsCatalog& catalog, const QString& partId,
                    const QString& storedName = QString());

// Datasheets in the folder that no part points at any more - what a tidy-up would remove.
QStringList orphans(const PartsCatalog& catalog);

} // namespace DatasheetStore

// ---- Reading one --------------------------------------------------------------------------------

// The text of a datasheet, for pulling figures out of. Plain text, CSV and HTML are read directly;
// a PDF needs `pdftotext` on the PATH, and when it is not there this returns nothing and says so in
// `how` rather than pretending. Never guesses at content it could not read.
QString datasheetText(const QString& path, QString* how = nullptr);

// One figure found in a datasheet, and where it was found - the evidence matters, because a figure
// pulled out of a document by pattern is a suggestion and not a fact.
struct PulledField {
    QString key;        // the part field it goes in
    QString label;      // what that field is called
    QString value;
    QString unit;
    QString evidence;   // the line it came from, so it can be judged before it is accepted
};

// Read the applicable technical fields for `kind` out of the text. Only fields that KIND has are
// looked for, and only ones that are actually found are returned - nothing is invented, and nothing
// already filled in is touched by this function (the caller decides what to overwrite).
//
// `sizeHint` is the size THIS part is - "1/4", "3/32", "12.7". A family datasheet gives its figures in
// a table of sizes: one row per size, one column per figure, and nothing in the row itself says what
// the figures are. Given the size, the row that belongs to this part is read and its columns matched to
// fields by the headings above them. Without it, only the figures written as "label: value" are found -
// which on a table-style datasheet is next to none of them.
std::vector<PulledField> pullTechnicalFields(const QString& text, PartKind kind,
                                             const QString& sizeHint = QString());

// The size in a part's number or description: "DR-25-1/4" -> "1/4", "RNF-100-3/32" -> "3/32",
// "ATUM 19/6" -> "19". Empty when there is nothing that reads as a size.
QString sizeTokenOf(const LibraryPart& part);

// A size as a number, so "1/4" and "0.25" compare equal and "1 1/2" is one and a half. NaN when the
// text is not a size at all.
double sizeAsNumber(const QString& text);

} // namespace Loopwire
