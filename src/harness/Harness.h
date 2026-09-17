#pragma once

#include "geometry/Vec2.h"
#include "model/Entity.h" // wire annotations are read straight off the wire entity

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

// ---- Wire color codes & gauges (mirror Edraw's WIRE_COLORS / gauge table) ----

// Resolve a wire color code (e.g. "WH", "BK", "GNYE", or "#rrggbb") to hex.
QString wireColorHex(const QString& code, const QString& fallback = "#e8e8e8");
QString wireColorName(const QString& code);
QStringList wireColorCodes();

// ---- Custom wire colours ---------------------------------------------------
// Colours the user has added on top of the standard harness codes, each with its own name
// and hex. Once registered a custom code behaves exactly like a built-in one everywhere:
// wireColorHex()/wireColorName() resolve it and wireColorCodes() offers it.
void registerCustomWireColor(const QString& code, const QString& name, const QString& hex);
// The custom codes in the order they were added.
QStringList customWireColorCodes();

// ---- Cross-section notation -------------------------------------------------
// Whether a cross-section is written in AWG or in mm2, set once in the general options and
// followed everywhere a gauge is shown: the wire markings, the harness builder's gauge list
// and the bill of materials. The stored value on a wire is always the mm2 figure, so switching
// notation re-labels the drawing without touching it.
void setWireGaugeNotationAwg(bool awg);
bool wireGaugeNotationAwg();
// The AWG size a mm2 cross-section is called (nearest standard size), and back again.
QString wireGaugeAwg(const QString& mm2);
// "0.5 mm2" or "20 AWG", following the current notation. `withUnit` false drops the unit.
QString wireGaugeLabel(const QString& mm2, bool withUnit = true);
// The cross-sections offered anywhere one is chosen: every standard size from 32 AWG up to
// 4 AWG, as mm2 figures, thinnest first.
QStringList wireGaugeChoices();

// How wide a twist symbol reaches along the wires, in world units: the leads plus the
// crossings. The generator needs the same number as the renderer, so a twist is never placed
// where its own symbol would run off the straight part of the wires it braids.
double twistBandWidth(int repeats, double wireBand, double flatLen);

// Stroke weight (px) for a wire cross-section gauge (mm^2), e.g. "0.5", "2.5".
double wireGaugeWeight(const QString& gauge);

// ---- Wire annotations (colour / gauge marked along the wire) ----------------
// A wire can carry its colour and its cross-section as text along its own run, each
// switched on independently, repeated as many times as wanted. The text reads along
// the wire (so a vertical wire's text turns with it) and can only be slid ALONG the
// run — never off it. Stored on the wire as:
//   showColorName : bool   draw the colour
//   colorNameMode : "code" (2-letter, e.g. WH) | "full" (e.g. White)
//   showGaugeName : bool   draw the cross-section
//   annCount      : int    how many times to repeat them along the wire
//   annT          : [n]    each instance's position as a fraction of the wire's length
//                          (absent / short => evenly spaced)

// The colour and the cross-section are drawn as ONE line of text above the run ("RD 0.5 mm2"),
// not one above and one below: on a bundle of close-spaced wires two lines per wire collide
// with the neighbours. `textGap` on the wire sets how far clear of the line they sit.

// One annotation instance's placement on the wire.
struct WireAnnot {
    Vec2 pos;              // anchor on the wire centreline
    Vec2 dir{1.0, 0.0};    // unit direction of the wire there (text runs along it)
    Vec2 normal{0.0, 1.0}; // unit perpendicular (colour one side, gauge the other)
    double t = 0.0;        // position along the wire, 0..1
    // On a vertical run the text can sit either side; `flip` puts it on the other one (the lift is
    // measured against dir.perp(), so flipping the lift's sign is what moves it across).
    bool flip = false;
};

bool wireShowsColorName(const Entity& wire);
bool wireShowsGaugeName(const Entity& wire);
// How many annotation instances the wire is set to show (>= 1; 0 if none are on).
int wireAnnotCount(const Entity& wire);
// The text of each part for this wire ("" when switched off / not set).
QString wireAnnotColorText(const Entity& wire);
QString wireAnnotGaugeText(const Entity& wire);
// Both parts as the single line they are drawn as ("RD  0.5 mm2"); either half alone when
// only one is switched on, empty when neither is.
QString wireAnnotText(const Entity& wire);
// Extra clearance between the run and the text above it, in world units ("textGap").
double wireTextGap(const Entity& wire);
// Where the wire's own label sits along it, 0..1 ("labelT"; mid-span when unset). Stored so the
// label can be dragged along the run the way a marking can.
double wireLabelFraction(const Entity& wire);
void setWireLabelFraction(Entity& wire, double t);
// Placement of every annotation instance, in order. Empty when neither part is shown
// or the wire is degenerate.
std::vector<WireAnnot> wireAnnots(const Entity& wire);

// The three markings a wire carries are independent: each is switched on by itself, is drawn as
// many times as IT is set to, and each instance sits where it was dragged to. `part` names one of
// them: "color", "gauge" or "label".
QStringList wireAnnotParts();          // the three, bottom row first
QString wireAnnotPartText(const Entity& wire, const QString& part);
int wireAnnotCountFor(const Entity& wire, const QString& part);
std::vector<double> wireAnnotFractionsFor(const Entity& wire, const QString& part, int count);
void setWireAnnotFractionsFor(Entity& wire, const QString& part, const std::vector<double>& ts);
std::vector<WireAnnot> wireAnnotsFor(const Entity& wire, const QString& part);
// How far above the wire that marking is drawn - an absolute height, not an offset from the one
// below it: two markings given the same gap sit at the same height. Its own value if it has one,
// else the wire's general text gap plus that marking's default place in the stack.
double wireAnnotGapFor(const Entity& wire, const QString& part);
// The default height of every marking above its wire. They share it: a harness whose pins are 15
// apart has no room to stack them, so they are spread along the wire instead (see
// wireAnnotDefaultOffset). What a generated harness writes, and what a wire with nothing set uses.
double wireAnnotDefaultGap(const QString& part);
// How far ALONG the wire a marking sits from the middle of its group, in world units. The three
// read side by side in the order label, colour, cross-section, with kAnnotGroupGap of clear space
// between them - measured from each one's own text and height, so none is ever drawn over another.
double wireAnnotDefaultOffset(const Entity& wire, const QString& part);
// The clear space between two markings of a group, in world units.
inline constexpr double kAnnotGroupGap = 10.0;
// A vertical run has two sides to put the text on and two ways to read it, and neither follows
// from the geometry - so the drawing says. "left" / "right" is which side of the run the text sits
// on, seen looking along it upwards; "up" / "down" is which way the text reads.
QString wireAnnotVerticalSide(const Entity& wire);  // "left" (default) | "right"
QString wireAnnotVerticalText(const Entity& wire);  // "up" (default) | "down"

// The text height of one marking. Its own value if it has one, else the wire's marking size.
double wireAnnotSizeFor(const Entity& wire, const QString& part);
void setWireAnnotSizeFor(Entity& wire, const QString& part, double height);
void setWireAnnotGapFor(Entity& wire, const QString& part, double gap);
// Where fraction `t` along the wire lands, with the local direction. False if the wire
// has no length.
bool wirePointAtFraction(const Entity& wire, double t, Vec2& pos, Vec2& dir);
// Fraction along the wire nearest world point `p` (0..1). 0 for a degenerate wire.
double wireFractionNearest(const Entity& wire, const Vec2& p);

// The same two questions for ANY run a mark can be attached to - a line, a double line (its
// centreline), a wire, a polyline, a spline. What lets an annotation ride the thing it annotates:
// where along the run it sits, and which way the run points there.
std::vector<Vec2> runCenterline(const Entity& e);

// A BREAK MARK's two strokes, each as a polyline in world units (a straight stroke is two points; an
// S is sampled). Everything that has to agree about where the mark is - what it draws, what a click
// picks, what a zone catches, what stretch of run it hides - asks this, so none of them can drift.
std::vector<std::vector<Vec2>> breakStrokes(const Entity& mark);
// How far the strokes lean out of square, as a fraction of the mark's reach. Straight strokes lean;
// an S is upright and sweeps instead.
double breakLeanFactor(const Entity& mark);

// ---- A twisted pair, drawn ---------------------------------------------------
// Two wires laid up round each other: a straight lead in, a run of crossings, a straight lead out.
// What a mechanical drawing shows where a pair is twisted, and what the ends are for - a twisted pair
// is spliced, crimped and dressed like any other wire, and it is the straight ends that get worked on.
inline constexpr double kTwistedPairWidth = 6.0;   // the cable, across: both wires and the parting
inline constexpr double kTwistedPairPitch = 18.0; // the length of one whole crossing
inline constexpr double kTwistedPairLead = 6.0;   // the straight run at each end
inline constexpr double kTwistedPairPart = 0.085; // half the parting between wires, in widths
inline constexpr int kTwistedMaxWires = 12;       // as many as one bundle is drawn with
// A WIRE OF A STRAIGHT RUN, and how far apart they are packed. A laid-up bundle is a cable of a
// certain thickness that the wires share out between them - so more wires means thinner ones. A run of
// wires lying side by side is the other way about: the wire is the size a wire is, and six of them are
// wider than three. These are the figures the drawings are drawn at - a wire 2 across, on a 3 pitch.
inline constexpr double kWiresRunWire = 2.0;
inline constexpr double kWiresRunPitch = 3.0;
// How much of the end a straight run gives to the STRAIGHT bit before the fan. The wires are landed on
// something square to the run, so they leave the fan and go into it straight rather than arriving
// still spreading - the drawings show about two parts straight to one part fan.
inline constexpr double kWiresRunTailShare = 0.6;
// And what a straight run's ends are, unless the drawing says otherwise: long enough for that straight
// bit to read as one. The laid-up bundle's own figure is the length of its lead-in and is shorter.
inline constexpr double kWiresRunEnd = 15.0;
// The four lines the pair is drawn with: each wire's two edges, each running the whole length. What a
// click picks, what a zone catches and how far it reaches all come from these, so none of them can
// drift from what is drawn.
std::vector<std::vector<Vec2>> twistedPairStrands(const Entity& pair);
// Which end(s) the bundle spreads its wires out at: "left", "right" or "both". Anything else -
// including an older drawing with no value - reads as "both", which is what those drawings were made
// with. Left and right mean the sheet ends, not the click order: a run drawn right-to-left still fans
// on the left when asked to.
QString twistedPairSpread(const Entity& pair);

// The bundle as it is DRAWN: one stretch per half crossing, with that stretch's wires ordered BACK TO
// FRONT. It reads as laid up only if a wire nearer the front hides the ones behind it - drawn as lines
// that simply cross, it reads as a chain of eyes instead.
struct TwistedPairStretch {
    struct Wire {
        std::vector<Vec2> outline;      // closed: filled with the background to hide what is behind
        std::vector<Vec2> edgeA, edgeB; // and then its own two edges
    };
    std::vector<Wire> wires;            // back to front
};
std::vector<TwistedPairStretch> twistedPairWeave(const Entity& pair);
bool runPointAtFraction(const Entity& e, double t, Vec2& pos, Vec2& dir);
double runFractionNearest(const Entity& e, const Vec2& p);
// Read / write the stored per-instance fractions (padded to `count`, evenly spaced).
std::vector<double> wireAnnotFractions(const Entity& wire, int count);
void setWireAnnotFractions(Entity& wire, const std::vector<double>& ts);

// Move the wire END at `endIdx` (0 or the last index) by `delta` — because the connector
// pin it is attached to moved — KEEPING the right angle at the knee beside it: the part of
// the move ALONG that run is absorbed by the run's length, the part ACROSS it shifts the
// knee, which simply lengthens or shortens the next run. A diagonal run has no right angle
// to preserve and is left alone, as is a wire with no knee. Returns the index of the knee
// that was adjusted, or -1 if none was.
int dragWireEndKeepingRightAngle(Entity& wire, int endIdx, const Vec2& delta);

// ---- Connector ------------------------------------------------------------

// A connector's editable specification, stored in the entity's meta.
struct ConnectorSpec {
    QString designator;   // e.g. "X1"
    QString partNumber;   // e.g. "MOLEX-12345"
    int pinCount = 4;
    double pitch = 15.0;      // spacing between pins (world units)
    double pinLength = 40.0;  // pin stub length
    double rectWidth = 40.0;
    double rectHeight = 84.0; // custom body height (dynamic mode auto-computes)
    double textSize = 10.0;   // general size (== max of the per-text sizes)
    bool mirrorX = false;     // false: pins exit right; true: exit left (mirror across Y)
    bool mirrorY = false;     // true: pin order flipped top<->bottom (mirror across X)
    QStringList pins;         // per-pin labels (size == pinCount)
    QStringList pinGauge;     // per-pin wire cross-section (AWG or mm² string), size==pinCount
    QStringList pinAmp;       // per-pin ampere rating (string), size==pinCount
    // Web-parity build options (Schematic build "Connector build" ribbon):
    QString annotation = QStringLiteral("numbers"); // "numbers" | "letters"
    QString pinOrder = QStringLiteral("top");       // "top" (1 at top) | "bottom"
    QString rectMode = QStringLiteral("dynamic");   // "dynamic" (auto H, W=40) | "custom"
    bool associative = true;                        // re-editable as a group
    QString pinFont = QStringLiteral("Arial");
    QString desFont = QStringLiteral("Arial");
    QString pnFont = QStringLiteral("Arial");
    double pinSize = 15.0;
    double desSize = 10.0;
    double pnSize = 10.0;
    // Movable-text offsets (world units, relative to the connector). Dragged via grips;
    // pin offset applies to every pin label, the others to their single text.
    double pinTextDX = 0.0, pinTextDY = 0.0;
    double desTextDX = 0.0, desTextDY = 0.0;
    double pnTextDX = 0.0, pnTextDY = 0.0;
    // Whether each marking is DRAWN. The designator and the part number are wanted on the schematic and
    // usually not on the mechanical view of the same connector - and a sheet crowded at one end is tidied
    // by turning one off rather than by dragging it somewhere it does not belong. The text itself is kept
    // either way: this hides it, it does not delete it.
    bool showDesignator = true;
    bool showPartNumber = true;
};

ConnectorSpec defaultConnectorSpec(int pinCount = 4);
ConnectorSpec connectorSpecFromMeta(const QJsonObject& meta);
void writeConnectorSpec(QJsonObject& meta, const ConnectorSpec& spec);

// Auto pin label for pin `index` from the annotation ("numbers"/"letters") and order
// ("top"/"bottom"): e.g. numbers top -> "1","2",…; letters -> "A","B",…
QString connectorPinLabel(int index, const ConnectorSpec& spec);
// Refill spec.pins with auto labels for the current annotation/order/pinCount.
void regenPinLabels(ConnectorSpec& spec);

// Computed geometry for rendering / hit-testing / bounds. `origin` is the body
// top corner on the non-exit side (matches Edraw's connectorEntitiesAt origin).
struct ConnectorGeometry {
    Vec2 rectMin;             // bottom-left
    Vec2 rectMax;             // top-right
    std::vector<Vec2> pinTip; // pin exit point on the body edge
    std::vector<Vec2> pinEnd; // far end of pin stub
    std::vector<Vec2> pinLabelPos;
    Vec2 designatorPos;
    Vec2 partPos;
    double effectiveHeight = 0.0;
    double effectiveWidth = 0.0;
};

ConnectorGeometry connectorGeometry(const Vec2& origin, const ConnectorSpec& spec);

} // namespace Edraw_Desktop
