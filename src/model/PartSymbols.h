#pragma once

#include "geometry/Vec2.h"
#include "model/Entity.h"

#include <QString>
#include <vector>

namespace Loopwire {

// ---- The stock electronic parts, as drawings -------------------------------
//
// A harness drawing is not only wires: there is a resistor across a pair, a capacitor to the shell, a
// ferrite over the bundle. Each of them has to be drawn twice - once as the part itself, on the
// mechanical layout (this is what it looks like and how much room it takes), and once as its symbol, on
// the schematic (this is what it does) - so both are here, built from one description each.
//
// The mechanical ones are drawn AT SIZE, in millimetres: an 1206 chip resistor is 3.2 by 1.6 because
// that is what it is, and a mechanical layout that draws it any other size is not a layout. The
// schematic ones are drawn to the size a symbol is read at, which has nothing to do with the part.
enum class PartSymbol {
    // What the part looks like - for the mechanical layout.
    ThResistor,       // through hole, axial: body, bands and two leads
    ThCapacitor,      // through hole, radial can: body and two leads
    ThCoil,           // through hole: a wound bobbin and two leads
    SmtResistor,      // 1206 chip, seen from above: body, terminals, printed face
    SmtCapacitor,     // 1206 chip capacitor, seen from above: body and its two end bands
    SmtCoil,          // shielded chip inductor, seen from above: the drum inside its body
    FerriteRingSide,  // a ferrite ring from the side, its hole shown hidden
    FerriteRingTop,   // ... and from above
    // What the part is called - for the schematic.
    SchResistor,      // the box: IEC 60617, what a European schematic uses
    SchResistorZigzag,// the zigzag: ANSI/IEEE, what an American one uses - the same part, drawn the other way
    SchCapacitor,
    SchCoil,
    SchFerrite,       // a coil with its core: the ferrite bead / choke symbol
    Splice,
    SpliceHeatshrink,
    ThLed,
};

// What it is called on the menu, and the longer word on hovering.
QString partSymbolName(PartSymbol s);
QString partSymbolTip(PartSymbol s);
// A symbol belongs to the schematic; the rest are the parts themselves.
bool partSymbolIsSchematic(PartSymbol s);

// The drawing, built about `at` - which is the middle of the part, so it lands where the cursor is.
// Every entity is stamped with the part it belongs to, so a drawing can be asked what is on it.
std::vector<Entity> partSymbolEntities(PartSymbol s, const Vec2& at);

// What each menu offers, in the order it offers it.
std::vector<PartSymbol> mechanicalPartSymbols();
std::vector<PartSymbol> schematicPartSymbols();

// The meta key every entity of a placed part carries: the part's own name.
inline constexpr char kPartSymbolKey[] = "partSymbol";
// Which copy of a symbol this is - one key shared by every entity of one drawing of it, re-minted when the
// symbol is copied, exactly as a block instance is. Two symbols sharing a key would be one symbol to anything
// that has to tell them apart.
inline constexpr char kPartSymbolInstKey[] = "symbolInst";
// A LEAD, and which of the two it is ("1" / "2"). The entity's `a` is the TIP: the point the symbol connects
// to the net at. Stamped because a symbol dropped on a net has to cut the net between its two tips, and after
// the symbol has been turned or mirrored under the cursor nothing else can say where those tips are.
inline constexpr char kPartSymbolPinKey[] = "symbolPin";

} // namespace Loopwire
