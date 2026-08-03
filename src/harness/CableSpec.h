// What a cable is MADE OF, described well enough to build a harness from it.
//
// A cable is not a wire. It is a jacket with a make-up inside it: so many singles, so many twisted
// pairs, a coax or two, some of them screened in foil and some in braid, each conductor a colour, and
// the whole lot possibly inside an overall screen. Every one of those facts matters to the harness that
// carries it - a twisted pair has to be drawn twisted, a screened group has to be drawn screened and
// its drain has to land somewhere - and none of them fits in a row of flat parameters.
//
// So a Cable part carries one of these, and the harness builder EXPANDS it: dropping a cable into a
// harness produces its conductors, with their colours, their cross-sections, their twisted groups and
// their screens already set, instead of a person typing eight rows and remembering which two are a pair.
#pragma once

#include "harness/PartsLibrary.h"
#include "model/Entity.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

// What one element of the make-up is. A "single" is one conductor on its own; the rest are groups that
// are laid up together and drawn as such.
enum class CableElementKind {
    Single,
    Pair,     // two conductors, normally twisted
    Triple,
    Quad,
    Coax,     // a core inside its own screen: two conductors, one of which IS the screen
};

QString cableElementKindName(CableElementKind kind);
QString cableElementKindKey(CableElementKind kind);
CableElementKind cableElementKindFromKey(const QString& key);
const std::vector<CableElementKind>& allCableElementKinds();
// How many conductors ONE element of this kind has (a coax counts its screen: it is a conductor, it
// carries current, and in a harness it lands on a pin).
int cableConductorsPerElement(CableElementKind kind);

// One entry in a cable's make-up: "2 × screened twisted pair, 24 AWG, white/blue".
struct CableElement {
    CableElementKind kind = CableElementKind::Single;
    int count = 1;                      // how many of this element the cable has
    // The cross-section, ALWAYS as the mm² figure - the same as a wire stores - so a harness drawn
    // from this cable is drawn in whatever notation the drawing is set to. `enteredAwg` only
    // remembers which way it was typed, so the builder shows it back the way it was given.
    QString gauge = QStringLiteral("0.5");
    bool enteredAwg = false;
    QStringList colours;                // one per conductor of one element; empty = unmarked
    bool twisted = false;               // pairs, triples and quads normally are
    bool foil = false;                  // a foil screen around this element
    bool braid = false;                 // a braid screen around this element
    QString note;

    int conductors() const { return cableConductorsPerElement(kind) * std::max(1, count); }
    QString summary() const;
};

// ---- Colour standards ----------------------------------------------------------------------------
//
// Which colours the conductors of a cable take is not a matter of taste: a cable is coloured to a
// standard, and the standard is what the far end expects to find. So the cable says which one it
// follows, and the colours come from it.
//
// What each one is:
//   HouseOrder   The order this application has always walked (WH, BK, RD, GN, YE, BU...), and pairs
//                as white-and-something. Not a standard - what to use when the cable follows none.
//   Din47100     DIN 47100, the multi-pair table used across European industrial and data cable: pair
//                1 white/brown, pair 2 green/yellow, pair 3 grey/pink, pair 4 blue/red, pair 5
//                black/violet, and so on. The one to reach for on a multi-pair cable.
//   Hd308S2      HD 308 S2 (EN 50525 core identification), for installation cable: two cores are
//                brown and blue; three are brown, black and grey; the earth is green-yellow.
//   MilStd681    MIL-STD-681's ten-colour code, where the colour IS the digit: 0 black, 1 brown,
//                2 red, 3 orange, 4 yellow, 5 green, 6 blue, 7 violet, 8 grey, 9 white. A conductor
//                numbered 12 is brown-red, which is why the order matters.
enum class CableColourStandard { HouseOrder, Din47100, Hd308S2, MilStd681 };

QString cableColourStandardName(CableColourStandard s);
QString cableColourStandardKey(CableColourStandard s);
CableColourStandard cableColourStandardFromKey(const QString& key);
const std::vector<CableColourStandard>& allCableColourStandards();
// What the standard is FOR, in one line - so a person choosing one is told which to choose.
QString cableColourStandardNote(CableColourStandard s);

// The whole cable.
struct CableSpec {
    double odMm = 0.0;                  // over the jacket
    QString jacket;                     // what the jacket is made of
    QString jacketColour;               // and what colour it is - which is how it is recognised
    QString conductor;                  // what the conductors are made of
    bool overallFoil = false;
    bool overallBraid = false;
    // A foil and a braid cover different amounts, and a datasheet states them separately: a foil is
    // usually 100% (it is a wrap) and a braid is 65-95% (it is woven). One figure for both would be
    // wrong for whichever screen it was not measured on.
    int foilCoverage = 0;               // %, 0 = not stated
    int braidCoverage = 0;
    QString drainGauge;                 // a drain wire's cross-section, when the cable has one
    // Which colour standard the conductors follow. A cable is coloured to a standard, and the standard
    // is what the far end expects to find - so it belongs to the cable, not to the window that made it.
    CableColourStandard colourStandard = CableColourStandard::HouseOrder;
    std::vector<CableElement> elements;

    int conductorCount() const;         // every conductor inside, drain included
    bool screened() const;              // any screen at all, at any level
    // One line saying what it is: "4 × twisted pair 24 AWG (foil), 1 × coax, overall braid".
    QString summary() const;

    QJsonObject toJson() const;
    static CableSpec fromJson(const QJsonObject& o);
    // A part keeps this as ONE compact JSON string among its parameters, so it is saved, loaded,
    // copied and duplicated by the machinery that already does that for every other parameter.
    QString toCompact() const;
    static CableSpec fromCompact(const QString& text);
    bool isEmpty() const { return elements.empty(); }
};

// ---- What the harness gets ----------------------------------------------------------------------

// One conductor of a cable, as a harness needs it. This is the bridge between the two: the library
// says what the cable is, this says what to draw, and the builder turns each of these into a row.
struct CableConductor {
    QString colour;         // the harness colour code (WH, BK, RD...), or empty
    QString gauge;          // the mm² figure
    QString label;          // "P1-A", "coax core", "drain" - what to write on the row
    QString kind;           // "wire" | "coax" - what the harness builder calls this row
    QString bundle;         // every conductor of one cable shares this: they are one cable
    QString twistGroup;     // the conductors of one twisted element share this
    QString shieldGroup;    // the conductors of one screened element share this
    bool isScreen = false;  // this conductor IS a screen (a coax braid, or a drain)
    int element = 0;        // which element of the make-up it came from, from 1
};

// Every conductor of the cable, in the order they should be listed. `bundleName` names the bundle the
// rows are gathered under - the cable's own designation is what a drawing wants to see.
std::vector<CableConductor> cableConductors(const CableSpec& spec, const QString& bundleName);

// Whether the cable has a screen around the WHOLE of it. Such a screen is not one of the conductors
// and is not written on to them: it is asked for on the connectors the cable arrives at, which is
// where a screen is terminated. See the note in cableConductors.
bool cableWantsOverallScreen(const CableSpec& spec);

// The colours one element takes under a standard. `index` counts the elements of that kind seen so
// far, which is what walks a multi-pair table from pair 1 to pair 2 to pair 3.
QStringList cableColoursFor(CableColourStandard standard, CableElementKind kind, int index);

// The colours a cable's conductors take when the make-up does not say. Kept for what already calls it;
// the same as cableColoursFor(HouseOrder, ...).
QStringList defaultCableColours(CableElementKind kind, int index);

// Colour every element of the cable to its standard, in order - what the builder does when a standard
// is chosen, and when an element is added. A conductor whose colour was set BY HAND is left alone
// unless `force`, because a hand-set colour is a decision and a standard is a default.
void applyCableColourStandard(CableSpec& spec, bool force = false);

// Where a part keeps its make-up, and where it keeps the shorthand a person types: two different
// things - one is the structure, the other is the description of it. The make-up used to be kept under
// "structure" itself, so a part written then is read from there and moved on the way past.
inline constexpr auto kCableMakeupKey = "makeup";
inline constexpr auto kCableStructureKey = "structure";

// The make-up a part carries, from wherever it is kept.
CableSpec cableSpecOf(const LibraryPart& part);
// And written back into it, with the shorthand kept in step.
void setCableSpecOn(LibraryPart& part, const CableSpec& spec);

// ---- The make-up written as one line -------------------------------------------------------------
//
// A cable is spoken about in a shorthand - "3x24AWG + FTP 2x2x28AWG + coax, foil, braid, black" - and
// that is how it arrives: in an e-mail, on a drawing, in a purchase order. Typing it out and pressing a
// button beats filling in a table row by row, and the same shorthand written back out is what the
// part's Structure field says it is.
//
// The reading is deliberately forgiving: AWG or mm², × or x, "2x2x28AWG" or "2 pair 28AWG", the screens
// and the jacket colour anywhere among the terms. What it could NOT make sense of comes back in
// `notes` rather than being quietly dropped - a term nobody noticed being ignored is how a cable ends
// up built wrong.
CableSpec cableSpecFromText(const QString& text, QStringList* notes = nullptr);

// And the other way: the shorthand for a make-up, so the field and the table always agree.
QString cableSpecToText(const CableSpec& spec);

// ---- The picture ---------------------------------------------------------------------------------

// A cross-section of the cable, as entities: the jacket, the screens, and every conductor inside it,
// laid out the way a cable actually lays up (a centre and rings around it). Good enough to be the
// part's mechanical drawing, and it says more about the make-up than any table can.
std::vector<Entity> cableCrossSection(const CableSpec& spec, double diameterMm);

// The cable as a SCHEMATIC: a line per conductor, with the signs that say what is done to them - a
// twist symbol at both ends of every twisted group, a screen at both ends of every screened one, a coax
// drawn as a coax, and the overall screen around the lot. This is what goes on a wiring diagram, where
// a cross-section says nothing: what matters there is which conductor is which and what is screened.
//
// `lengthMm` is how long the run is drawn; the height follows from how many conductors there are.
std::vector<Entity> cableSchematic(const CableSpec& spec, double lengthMm = 90.0);

} // namespace Edraw_Desktop
