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
    Balloon,   // numbered callout balloon
    Note,      // bordered note box with text
    Revision,  // titled revision box
    // Harness
    Wire,
    Connector,
    Splice,
    NotConnected,
    NetLabel,
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
    case ToolId::Note:      // rubber-band a rectangle, like Rect
    case ToolId::Revision:
        return true;
    default:
        return false;
    }
}

// Single-click placement tools (connector / markers).
inline bool isPlacementTool(ToolId t) {
    return t == ToolId::Connector || t == ToolId::Splice ||
           t == ToolId::NotConnected || t == ToolId::NetLabel || t == ToolId::Balloon;
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
    case ToolId::Note: return QStringLiteral("Note");
    case ToolId::Revision: return QStringLiteral("Revision");
    case ToolId::Wire: return QStringLiteral("Wire");
    case ToolId::Connector: return QStringLiteral("Connector");
    case ToolId::Splice: return QStringLiteral("Splice");
    case ToolId::NotConnected: return QStringLiteral("Not connected");
    case ToolId::NetLabel: return QStringLiteral("Net label");
    case ToolId::Move: return QStringLiteral("Move");
    case ToolId::Rotate: return QStringLiteral("Rotate");
    case ToolId::Mirror: return QStringLiteral("Mirror");
    case ToolId::Scale: return QStringLiteral("Scale");
    case ToolId::Offset: return QStringLiteral("Offset");
    case ToolId::Trim: return QStringLiteral("Trim");
    case ToolId::Extend: return QStringLiteral("Extend");
    case ToolId::Fillet: return QStringLiteral("Fillet");
    case ToolId::Chamfer: return QStringLiteral("Chamfer");
    }
    return QStringLiteral("Select");
}

} // namespace Edraw_Desktop
