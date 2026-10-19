#pragma once

#include <QString>

namespace Loopwire {

// Active interaction tool. Select is the default idle tool; the rest are
// draw/modify modes mirroring Loopwire's tool set.
enum class ToolId {
    Select,
    // Draw
    Line,
    DoubleLine,
    Polyline,
    DoublePolyline,
    Rect,
    Polygon,
    Circle,
    Arc,
    Ellipse,
    Spline,
    Text,
    MText,
    Dimension,
    Hatch,
    Wipeout,
    // Annotation
    Balloon,    // numbered callout balloon
    TextLeader, // leader with a plain text label (no circle)
    Note,       // bordered note box with text
    Revision,  // titled revision box
    Table,     // editable grid of cells
    // Harness
    Wire,
    Connector,
    Splice,
    NotConnected,
    NetLabel,
    // Where a schematic symbol meets the world: a small ring, numbered lowest-free-first (see
    // Editor::nextConnectionPinNumber()) rather than always counting up.
    ConnectionPin,
    LengthBreak,
    TwistedPair,   // two wires laid up round each other, drawn between two points
    TwistedRoute,  // ... and the same pair routed through as many knees as it needs
    TwistedHarness,// ... and a whole bundle of them laid up together, however many wires it fans out to
    TwistedHarnessRoute, // ... and that bundle routed through as many knees as IT needs
    // A bundle of wires running STRAIGHT - side by side the whole way rather than laid up round one
    // another, which is how most of a loom is drawn. The same object as the laid-up bundle and the
    // same handles on it: what differs is the lay, so one can be made the other without being drawn
    // again. Routed, because a run that does not bend is the rare one.
    WiresRun,
    // JUST THE FAN. A harness run is a fan, a body and a fan; often what a drawing wants is the one
    // fan on its own - wires breaking out of something that is drawn elsewhere, or not drawn at all.
    // The same object with the same fan settings: what it has not got is a second fan.
    Fanout,
    // AUTO ROUTE: take what is already drawn to where it has to go. Click the thing to be routed,
    // click the wall it is to arrive at, and the run is laid between them - as the same sort of
    // object that was clicked, in as few bends as it takes, arriving square to that wall.
    AutoRoute,
    // Heatshrink over a stretch of run, ROUTED like the twisted tools rather than stamped: a sleeve
    // covers a length of harness, and where that length bends the sleeve bends with it. A fixed-length
    // placement cannot say a sleeve round a corner at all.
    HeatshrinkRoute,
    // A BLOCK ARROW - the solid outlined arrow a drawing uses to say which way something goes.
    //
    // Not a leader and not a dimension: those point AT things and measure them. This one is a shape on
    // the sheet with a length and a width, drawn tail-to-tip like a line, and it is what says "assemble
    // in this direction" or "airflow this way" on a released drawing.
    VolumeArrow,
    // THE AXIS, AND WHICH WAY IT IS TURNED. A cross where the axis is, an arrow off it the way the part
    // goes, and the angle of that arrow written beside it.
    //
    // Two clicks: the axis, then the direction. The cross marks the axis whatever the arrow does, which
    // is what tells this from an ordinary arrow - the reader is being shown a centre AND a rotation
    // about it, and an arrow alone says nothing about where the middle is.
    AxisAngle,
    // Modify (reference-point driven)
    Move,
    Rotate,
    Mirror,
    Scale,
    Offset,
    // Modify (entity-pick driven)
    Trim,
    Extend,
    Fillet,
    Chamfer,
    ModelNearPoint,
    ModelFarPoint,
    ModelConnectPoint,   // where the part connects, and which way it runs from there (two clicks)
    // The face the wires leave the part by: a LINE across it, two clicks. A part with one face has one; a
    // split has one per branch, in the order they are marked. The wires are spread along it and may not run
    // off either end, which a single connection point cannot say.
    ModelOutputLine,
    // WHICH OBJECT THE APP SCALES BY. A model is scaled to fit what it is placed on, and until now that
    // meant the whole drawing - which is wrong the moment a model is a thing inside another thing. A label
    // under clear heatshrink is a label with a sleeve over it: what has to match the bundle is the LABEL,
    // and the sleeve stands outside it. Scaling the pair together fits the sleeve to the bundle and leaves
    // the label too small, which is exactly backwards. So one object in the drawing is marked as the
    // reference, and the rest is carried along by the same scale.
    ModelScaleRef,
    // The two marks that say where the middle of something is. A centre MARK is put on one round thing -
    // a circle, an arc - as the little cross at its centre. A centre LINE is drawn BETWEEN two things
    // that run parallel, down the middle of the gap: two lines, two runs of a polyline, or the two
    // physical lines of a double run. Both are picked rather than drawn: the geometry already says where
    // the middle is, and a drawing office should not be made to find it with a cursor.
    // A DETAIL VIEW: circle the thing that is too small to read, and it is drawn again bigger somewhere
    // there is room, with a letter tying the two together. Three clicks - the middle of what is wanted, how
    // much of it, and where the enlargement goes.
    DetailView,
    CenterMark,
    CenterLine,
    LacingTie, // appended so existing tool values remain unchanged
    Arrow,     // a plain arrow: drawn as a leader is, tip then tail, with no words
    // A RAY and a CONSTRUCTION LINE (AutoCAD's XLINE): a base point and a point it passes through, the line running
    // on from the base through it - one way, or both ways. Lines marked infinite (see lineReach).
    Ray,
    ConstructionLine,
    // A RECTANGLE BY THREE POINTS: two corners along one edge, at any angle, and how far the opposite edge stands off.
    Rect3Point,
    // A TRIANGLE ON A BASE: the base's two ends, a click on the side it stands on, then the angles at the two ends of
    // the base - the third is what is left of 180 degrees.
    Triangle,
    // STEXT (AutoCAD's TEXT/DTEXT): one line of words at a point - Enter places it. Text is the Mtext beside it.
    SText,
};

inline bool isPickModifyTool(ToolId t) {
    return t == ToolId::Trim || t == ToolId::Extend || t == ToolId::Fillet ||
           t == ToolId::Chamfer;
}

inline bool isDrawTool(ToolId t) {
    switch (t) {
    case ToolId::Line:
    case ToolId::DoubleLine:
    case ToolId::Polyline:
    case ToolId::DoublePolyline:
    case ToolId::Ray:
    case ToolId::ConstructionLine:
    case ToolId::Rect3Point:
    case ToolId::Triangle:
    case ToolId::Rect:
    case ToolId::Polygon:
    case ToolId::Circle:
    case ToolId::Arc:
    case ToolId::Ellipse:
    case ToolId::Spline:
    case ToolId::Text:
    case ToolId::SText:
    case ToolId::MText:
    case ToolId::Dimension:
    case ToolId::Hatch:
    case ToolId::Wipeout:
    case ToolId::Wire:
    case ToolId::Balloon:    // two clicks: arrow tip, then balloon centre
    case ToolId::TextLeader: // two clicks: arrow tip, then text location
    case ToolId::Arrow:      // two clicks: arrow tip, then its tail
    case ToolId::Note:      // rubber-band a rectangle, like Rect
    case ToolId::Revision:
    case ToolId::TwistedPair: // two clicks: the two ends the pair runs between
    case ToolId::TwistedRoute: // as many points as the route needs, like a polyline
    case ToolId::TwistedHarness: // two clicks, and however many wires the ribbon says
    case ToolId::TwistedHarnessRoute: // both: as many knees as it needs, and however many wires
    case ToolId::WiresRun:            // as many knees as the run needs, and however many wires
    case ToolId::Fanout:              // the same, and the fan is the whole of it
    case ToolId::HeatshrinkRoute: // as many knees as the stretch it covers needs
    case ToolId::VolumeArrow:     // two clicks: the tail, then the tip
    case ToolId::AxisAngle:       // two clicks: the axis, then where the sweep starts
        return true;
    default:
        return false;
    }
}

// Single-click placement tools (connector / markers). A length break is NOT one of them: it is marked
// out by a line drawn across the runs it interrupts, the way a twist or a screen is.
inline bool isPlacementTool(ToolId t) {
    return t == ToolId::Connector || t == ToolId::Splice ||
           t == ToolId::NotConnected || t == ToolId::NetLabel || t == ToolId::Table ||
           t == ToolId::ConnectionPin;
}

inline QString toolLabel(ToolId t) {
    switch (t) {
    case ToolId::Select: return QStringLiteral("Select");
    // THE LINE goes on from each end it places, a line a segment (AutoCAD's LINE); THE DOUBLE LINE is one object
    // through every point, placed as it is drawn; THE POLYLINE is AutoCAD's PLINE - one object, with arcs and a width.
    case ToolId::Line: return QStringLiteral("Line");
    case ToolId::DoubleLine: return QStringLiteral("Double line segment");
    case ToolId::Polyline: return QStringLiteral("Polyline");
    case ToolId::DoublePolyline: return QStringLiteral("Double line");
    case ToolId::Ray: return QStringLiteral("Ray");
    case ToolId::ConstructionLine: return QStringLiteral("Construction line");
    case ToolId::Rect3Point: return QStringLiteral("Rectangle by 3 points");
    case ToolId::Triangle: return QStringLiteral("Triangle");
    case ToolId::Rect: return QStringLiteral("Rectangle");
    case ToolId::Polygon: return QStringLiteral("Polygon");
    case ToolId::Circle: return QStringLiteral("Circle");
    case ToolId::Arc: return QStringLiteral("Arc");
    case ToolId::Ellipse: return QStringLiteral("Ellipse");
    case ToolId::Spline: return QStringLiteral("Spline");
    case ToolId::Text: return QStringLiteral("Multiline text");
    case ToolId::SText: return QStringLiteral("Single-line text");
    case ToolId::MText: return QStringLiteral("Multiline text box");
    case ToolId::Dimension: return QStringLiteral("Dimension");
    case ToolId::Hatch: return QStringLiteral("Hatch");
    case ToolId::Wipeout: return QStringLiteral("Wipeout");
    case ToolId::Balloon: return QStringLiteral("Balloon");
    case ToolId::TextLeader: return QStringLiteral("Leader");
    case ToolId::Arrow: return QStringLiteral("Arrow");
    case ToolId::Note: return QStringLiteral("Note");
    case ToolId::Revision: return QStringLiteral("Revision");
    case ToolId::Table: return QStringLiteral("Table");
    case ToolId::Wire: return QStringLiteral("Wire");
    case ToolId::Connector: return QStringLiteral("Connector");
    // A joint. The tool keeps its Splice id for compatibility with saved drawings and settings; what
    // it places is a joint between conductors, and that is what it is called.
    case ToolId::Splice: return QStringLiteral("Joint");
    case ToolId::NotConnected: return QStringLiteral("Not connected");
    case ToolId::ConnectionPin: return QStringLiteral("Connection pin");
    case ToolId::LengthBreak: return QStringLiteral("Length break");
    case ToolId::LacingTie: return QStringLiteral("Tie / lacing cord");
    case ToolId::TwistedPair: return QStringLiteral("Twisted pair");
    case ToolId::TwistedRoute: return QStringLiteral("Twisted route");
    case ToolId::TwistedHarness: return QStringLiteral("Twisted bundle");
    case ToolId::TwistedHarnessRoute: return QStringLiteral("Twisted bundle run");
    case ToolId::WiresRun: return QStringLiteral("Harness run");
    case ToolId::Fanout: return QStringLiteral("Fanout");
    case ToolId::AutoRoute: return QStringLiteral("Auto route");
    case ToolId::HeatshrinkRoute: return QStringLiteral("Heatshrink over the run");
    case ToolId::VolumeArrow: return QStringLiteral("Volume arrow");
    case ToolId::AxisAngle: return QStringLiteral("Axis angle");
    // The tool keeps its NetLabel id for compatibility with saved drawings; what it places
    // is a wire label, and that is what it is called.
    case ToolId::NetLabel: return QStringLiteral("Wire label");
    case ToolId::Move: return QStringLiteral("Move");
    case ToolId::Rotate: return QStringLiteral("Rotate");
    case ToolId::Mirror: return QStringLiteral("Mirror");
    case ToolId::Scale: return QStringLiteral("Scale");
    case ToolId::Offset: return QStringLiteral("Offset");
    case ToolId::Trim: return QStringLiteral("Trim");
    case ToolId::Extend: return QStringLiteral("Extend");
    case ToolId::Fillet: return QStringLiteral("Fillet");
    case ToolId::Chamfer: return QStringLiteral("Chamfer");
    case ToolId::ModelNearPoint: return QStringLiteral("Model near point");
    case ToolId::ModelFarPoint: return QStringLiteral("Model far point");
    case ToolId::ModelConnectPoint: return QStringLiteral("Model connection point");
    case ToolId::ModelOutputLine: return QStringLiteral("Model output line");
    case ToolId::ModelScaleRef: return QStringLiteral("Model scale reference");
    case ToolId::DetailView: return QStringLiteral("Detail view");
    case ToolId::CenterMark: return QStringLiteral("Centre mark");
    case ToolId::CenterLine: return QStringLiteral("Centre line");
    }
    return QStringLiteral("Select");
}

} // namespace Loopwire
