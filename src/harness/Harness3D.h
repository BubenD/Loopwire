#pragma once

#include "geometry/Mesh3D.h"
#include "model/Entity.h"

#include <QColor>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace Edraw_Desktop {

// THE HARNESS IN 3D: its runs as tubes - a cable's body, the transition sleeves on it, each wire where the wires fan
// out - and each end part as its 3D model, fitted to where and how big it is drawn, or as a box where it has none.
struct Harness3DItem {
    Mesh3D mesh;
    QColor color;
    QString what;          // a part's name or P/N, or the run's
    bool model = false;    // a part shown by its own 3D model
    bool cube = false;     // a part shown as a box, having none
    QString brick;         // the wizard brick it is (on the sheet: the brick it was placed from)
};

struct Harness3DScene {
    std::vector<Harness3DItem> items;
    int parts = 0, modelled = 0, runs = 0;
    QStringList notes;     // what could not be read, and why
};

// HOW A PART WITH NO 3D MODEL IS SHOWN: a box as big as it is drawn, or a solid made from its 2D drawing - its outline
// turned round the way it goes into the harness (a round connector), or given depth across (a square one).
enum class PartLook { Box, Revolved, Extruded };

// From the objects of a harness drawing - placed on the sheet (each part by its copy mark) or straight out of the
// wizard (each by its brick). `modelOf` gives the 3D model file attached to a library part, or nothing.
Harness3DScene buildHarness3D(const std::vector<Entity>& ents, const std::function<QString(const QString& libPart)>& modelOf,
                              PartLook look = PartLook::Box);

// A model file read once and kept while it is unchanged on disk.
const Mesh3D& cachedMesh3D(const QString& path, QString* how = nullptr);

} // namespace Edraw_Desktop
