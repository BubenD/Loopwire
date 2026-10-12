#pragma once

#include "model/Entity.h"
#include "model/PageSetup.h"
#include "model/Layer.h"

#include <QJsonObject>
#include <QString>
#include <vector>

namespace Loopwire {

// View transform state: world origin offset and zoom scale (screen px per world unit).
struct ViewState {
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
};

struct LayoutViewport {
    QString id;
    double x = 40.0;
    double y = 35.0;
    double width = 340.0;
    double height = 225.0;
    Vec2 modelCenter;
    double modelScale = 0.0;

    QJsonObject toJson() const;
    static LayoutViewport fromJson(const QJsonObject& obj);
};

struct DrawingLayout {
    QString name = QStringLiteral("Layout1");
    double paperWidth = 420.0;
    double paperHeight = 297.0;
    QString templateName;
    std::vector<Entity> paperEntities;
    std::vector<LayoutViewport> viewports;
    // How this sheet plots: the paper, the way round, the scale, the ink. Its own, and saved with the
    // drawing - a sheet that plots correctly today must plot the same for whoever opens the file next.
    PageSetup pageSetup;

    QJsonObject toJson() const;
    static DrawingLayout fromJson(const QJsonObject& obj);
};

// A single drawing document. Mirrors Loopwire's per-document state created by
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

    // Named page setups: a paper, a scale and a set of plot options settled once and then set on any
    // sheet. They live beside the layouts because they belong to the drawing, not to one sheet of it.
    std::vector<PageSetup>& pageSetups() { return pageSetups_; }
    const std::vector<PageSetup>& pageSetups() const { return pageSetups_; }

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

    std::vector<DrawingLayout>& layouts() { return layouts_; }
    const std::vector<DrawingLayout>& layouts() const { return layouts_; }

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

    // THE BUNDLE MAKE-UPS SAVED INTO THIS DRAWING. What went into a bundle-diameter calculation - the
    // conductors, their amounts, their sizes - kept by the name it was saved under, so the figure quoted
    // on this sheet can be got back and checked, or reused for the next bundle on the same sheet.
    //
    // WITH THE DRAWING rather than in the application settings, like harnessPresets above and for the
    // same reason: a bundle belongs to the harness it is part of. Sending somebody the drawing sends the
    // working with it, and a machine that has never seen it opens it complete.
    QJsonObject& bundlePresets() { return bundlePresets_; }
    const QJsonObject& bundlePresets() const { return bundlePresets_; }

    // The drawing's bill of materials: the rows someone typed and the column headings they chose.
    // The rows the drawing itself implies are not kept here - they are read off the entities every
    // time, so they cannot go stale (see bomFromDrawing).
    // THE HARNESS WIZARDS laid out on this drawing, each by name - saved with it, so a harness
    // described in the wizard can be opened again and changed.
    QJsonObject& wizards() { return wizards_; }
    const QJsonObject& wizards() const { return wizards_; }
    QJsonObject& bom() { return bom_; }
    const QJsonObject& bom() const { return bom_; }
    // THE HARNESS ROUTED IN 3D: the CAD model it is routed in (by its file) and the route through it - its
    // connectors, branches, clips and bundles (Route3D). With the drawing, so the routing opens with it.
    QJsonObject& routing3d() { return routing3d_; }
    const QJsonObject& routing3d() const { return routing3d_; }
    // THE SYSTEM DIAGRAM: its units, their ports and the cables between them (SystemDesign) - empty where there is none.
    QJsonObject& systemDesign() { return systemDesign_; }
    const QJsonObject& systemDesign() const { return systemDesign_; }

    QJsonObject& drawingDetails() { return drawingDetails_; }
    const QJsonObject& drawingDetails() const { return drawingDetails_; }
    QJsonObject& validationStamps() { return validationStamps_; }
    const QJsonObject& validationStamps() const { return validationStamps_; }
    QString validationFingerprint() const;
    bool validationPassed(const QString& kind) const;
    void recordValidation(const QString& kind, bool passed);
    void invalidateValidation();
    bool dirty() const { return dirty_; }
    void setDirty(bool d) { dirty_ = d; if(d)validationStamps_=QJsonObject{}; }

    // Default three-layer set matching Loopwire's defaultLayers().
    static std::vector<Layer> defaultLayers();

private:
    QString id_;
    QString name_;
    std::vector<Entity> entities_;
    std::vector<Layer> layers_;
    QString activeLayer_ = "0";
    std::vector<PageSetup> pageSetups_;
    ViewState view_;
    QString unit_ = "mm";
    double doubleLineGap_ = 10.0;
    int multiLineCount_ = 3;
    int polygonSides_ = 5;
    QJsonObject bom_;
    QJsonObject wizards_;
    QJsonObject routing3d_;
    QJsonObject systemDesign_;
    QJsonObject drawingDetails_;
    QJsonObject validationStamps_;
    QJsonObject harnessPresets_;
    QJsonObject bundlePresets_;
    std::vector<DrawingLayout> layouts_;
    Bounds sheet_;                 // invalid unless the file stated its limits
    bool dirty_ = false;
};

} // namespace Loopwire
