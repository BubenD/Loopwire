#pragma once

#include "model/Entity.h"
#include "model/Layer.h"

#include <QJsonObject>
#include <QString>
#include <vector>

namespace Edraw_Desktop {

// View transform state: world origin offset and zoom scale (screen px per world unit).
struct ViewState {
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
};

// A single drawing document. Mirrors Edraw's per-document state created by
// createDocument(); selection/interaction transients live on the editor, not here.
class Document {
public:
    Document();
    explicit Document(QString name);

    const QString& id() const { return id_; }
    const QString& name() const { return name_; }
    void setName(QString name) { name_ = std::move(name); }

    std::vector<Entity>& entities() { return entities_; }
    const std::vector<Entity>& entities() const { return entities_; }

    std::vector<Layer>& layers() { return layers_; }
    const std::vector<Layer>& layers() const { return layers_; }

    const QString& activeLayer() const { return activeLayer_; }
    void setActiveLayer(QString name) { activeLayer_ = std::move(name); }

    const Layer* findLayer(const QString& name) const;
    Layer* findLayer(const QString& name);

    ViewState& view() { return view_; }
    const ViewState& view() const { return view_; }

    QString& unit() { return unit_; }
    const QString& unit() const { return unit_; }

    // The sheet the drawing was made on, when the file says so: a DXF states its limits, and that
    // rectangle - not the extents of every last object - is what a drawing is looked at at. A drawing
    // often carries working geometry off the sheet; fitting to all of it opens the sheet small and off
    // to one side, which is not how the drawing looks in the application it came from.
    Bounds& sheet() { return sheet_; }
    const Bounds& sheet() const { return sheet_; }

    // The harness patterns saved INTO this drawing. A pattern saved to a file is for reuse across
    // drawings; these belong to this one - generated from it, kept with it, deleted from it - so a
    // drawing that has been laid out once can be regenerated without going to find a file. Keyed by
    // the name the user gave, each value a whole builder definition.
    QJsonObject& harnessPresets() { return harnessPresets_; }
    const QJsonObject& harnessPresets() const { return harnessPresets_; }

    // The drawing's bill of materials: the rows someone typed and the column headings they chose.
    // The rows the drawing itself implies are not kept here - they are read off the entities every
    // time, so they cannot go stale (see bomFromDrawing).
    QJsonObject& bom() { return bom_; }
    const QJsonObject& bom() const { return bom_; }

    bool dirty() const { return dirty_; }
    void setDirty(bool d) { dirty_ = d; }

    // Default three-layer set matching Edraw's defaultLayers().
    static std::vector<Layer> defaultLayers();

private:
    QString id_;
    QString name_;
    std::vector<Entity> entities_;
    std::vector<Layer> layers_;
    QString activeLayer_ = "0";
    ViewState view_;
    QString unit_ = "mm";
    double doubleLineGap_ = 10.0;
    int multiLineCount_ = 3;
    int polygonSides_ = 5;
    QJsonObject bom_;
    QJsonObject harnessPresets_;
    Bounds sheet_;                 // invalid unless the file stated its limits
    bool dirty_ = false;
};

} // namespace Edraw_Desktop
