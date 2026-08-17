#pragma once

#include <QString>

namespace Edraw_Desktop {

// Active interaction tool. Select is the default idle tool; the rest are
// draw/modify modes mirroring Edraw's tool set.
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
    LengthBreak,
    TwistedPair,   // two wires laid up round each other, drawn between two points
    TwistedRoute,  // ... and the same pair routed through as many knees as it needs
    TwistedHarness,// ... and a whole bundle of them laid up together, however many wires it fans out to
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
    // The two marks that say where the middle of something is. A centre MARK is put on one round thing -
    // a circle, an arc - as the little cross at its centre. A centre LINE is drawn BETWEEN two things
    // that run parallel, down the middle of the gap: two lines, two runs of a polyline, or the two
    // physical lines of a double run. Both are picked rather than drawn: the geometry already says where
    // the middle is, and a drawing office should not be made to find it with a cursor.
    CenterMark,
    CenterLine,
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
    case ToolId::Rect:
    case ToolId::Polygon:
    case ToolId::Circle:
    case ToolId::Arc:
    case ToolId::Ellipse:
    case ToolId::Spline:
    case ToolId::Text:
    case ToolId::MText:
    case ToolId::Dimension:
    case ToolId::Hatch:
    case ToolId::Wipeout:
    case ToolId::Wire:
    case ToolId::Balloon:    // two clicks: arrow tip, then balloon centre
    case ToolId::TextLeader: // two clicks: arrow tip, then text location
    case ToolId::Note:      // rubber-band a rectangle, like Rect
    case ToolId::Revision:
    case ToolId::TwistedPair: // two clicks: the two ends the pair runs between
    case ToolId::TwistedRoute: // as many points as the route needs, like a polyline
    case ToolId::TwistedHarness: // two clicks, and however many wires the ribbon says
        return true;
    default:
        return false;
    }
}

// Single-click placement tools (connector / markers). A length break is NOT one of them: it is marked
// out by a line drawn across the runs it interrupts, the way a twist or a screen is.
inline bool isPlacementTool(ToolId t) {
    return t == ToolId::Connector || t == ToolId::Splice ||
           t == ToolId::NotConnected || t == ToolId::NetLabel || t == ToolId::Table;
}

inline QString toolLabel(ToolId t) {
    switch (t) {
    case ToolId::Select: return QStringLiteral("Select");
    case ToolId::Line: return QStringLiteral("Line");
    case ToolId::DoubleLine: return QStringLiteral("Double line");
    case ToolId::Polyline: return QStringLiteral("Polyline");
    case ToolId::DoublePolyline: return QStringLiteral("Double polyline");
    case ToolId::Rect: return QStringLiteral("Rectangle");
    case ToolId::Polygon: return QStringLiteral("Polygon");
    case ToolId::Circle: return QStringLiteral("Circle");
    case ToolId::Arc: return QStringLiteral("Arc");
    case ToolId::Ellipse: return QStringLiteral("Ellipse");
    case ToolId::Spline: return QStringLiteral("Spline");
    case ToolId::Text: return QStringLiteral("Text");
    case ToolId::MText: return QStringLiteral("Multiline text");
    case ToolId::Dimension: return QStringLiteral("Dimension");
    case ToolId::Hatch: return QStringLiteral("Hatch");
    case ToolId::Wipeout: return QStringLiteral("Wipeout");
    case ToolId::Balloon: return QStringLiteral("Balloon");
    case ToolId::TextLeader: return QStringLiteral("Leader");
    case ToolId::Note: return QStringLiteral("Note");
    case ToolId::Revision: return QStringLiteral("Revision");
    case ToolId::Table: return QStringLiteral("Table");
    case ToolId::Wire: return QStringLiteral("Wire");
    case ToolId::Connector: return QStringLiteral("Connector");
    // A joint. The tool keeps its Splice id for compatibility with saved drawings and settings; what
    // it places is a joint between conductors, and that is what it is called.
    case ToolId::Splice: return QStringLiteral("Joint");
    case ToolId::NotConnected: return QStringLiteral("Not connected");
    case ToolId::LengthBreak: return QStringLiteral("Length break");
    case ToolId::TwistedPair: return QStringLiteral("Twisted pair");
    case ToolId::TwistedRoute: return QStringLiteral("Twisted route");
    case ToolId::TwistedHarness: return QStringLiteral("Twisted harness");
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
    case ToolId::CenterMark: return QStringLiteral("Centre mark");
    case ToolId::CenterLine: return QStringLiteral("Centre line");
    }
    return QStringLiteral("Select");
}

} // namespace Edraw_Desktop
