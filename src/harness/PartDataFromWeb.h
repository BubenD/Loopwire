#pragma once

#include "harness/PartsLibrary.h"

#include <QString>
#include <QStringList>
#include <QUrl>

#include <vector>

namespace Edraw_Desktop {

// Reading a part's technical data off a manufacturer's page.
//
// What this is: a careful reader of the TEXT of a page. It knows what a part of each kind is described by -
// a wire by its gauge, its insulation, its voltage and its diameter; heatshrink by its ratio and its
// recovered size; a connector by its way count and its pitch - and it looks for those things written the way
// a catalogue writes them. What it finds it reports, with the words it found them in.
//
// What this is NOT: a manufacturer integration, and not a promise. A page that writes "22AWG" in an image,
// or hides its table behind a script, or calls the pitch "grid", yields nothing - and a page that says "for
// use with 22 AWG wire" when the part itself is 20 AWG yields the wrong thing. So nothing is ever written
// into a part behind the user's back: every find is shown with its evidence and has to be ticked. A part
// number and a datasheet are worth more than a guess, and the guess is always shown as one.
struct WebFieldFind {
    QString key;        // the parameter key it fills ("gauge", "ratio", "pins", ...)
    QString label;      // what that parameter is called, for the list
    QString value;      // what was read
    QString evidence;   // the sentence it was read from, so it can be judged
    bool exists = false; // the part already has something in that field
};

// The page's text, tags and scripts taken out - what a reader would see.
QString htmlToText(const QString& html);
// What the page calls itself: its <title>, tidied. Empty if it has none.
QString pageTitle(const QString& html);
// Every link on the page that looks like a datasheet (a PDF, or a link that says datasheet), made absolute.
QStringList datasheetLinks(const QString& html, const QUrl& base);

// The technical fields that can be read out of this page for a part of that kind, in the order they are
// worth showing. `part` is used only to say which of them the part already has filled in.
std::vector<WebFieldFind> partFieldsFromPage(const QString& html, const LibraryPart& part);

// Write the finds that were ticked into the part. Returns how many fields it changed.
int applyWebFinds(LibraryPart& part, const std::vector<WebFieldFind>& finds);

} // namespace Edraw_Desktop
