#pragma once

#include <QJsonObject>
#include <QString>

namespace Loopwire {

// A drawing layer. Mirrors Loopwire's layer record:
// { name, color, visible, locked, weight }.
struct Layer {
    QString name = "0";
    QString color = "#ffffff";
    bool visible = true;
    bool locked = false;
    // The default lineweight, and it is a HAIRLINE. The renderer uses this as a pen width in screen
    // pixels, so 2.0 - what this used to be - drew every line two pixels wide: the heaviest weight on
    // the standard list, applied to everything by default. 0.25 is what a drawing office means by a
    // normal line and what CAD defaults to, and it comes out as the single-pixel line a frame, a table
    // and a piece of geometry should be.
    double weight = 0.25;

    QJsonObject toJson() const {
        return QJsonObject{
            {"name", name},
            {"color", color},
            {"visible", visible},
            {"locked", locked},
            {"weight", weight},
        };
    }

    static Layer fromJson(const QJsonObject& o) {
        Layer l;
        l.name = o.value("name").toString("0");
        l.color = o.value("color").toString("#ffffff");
        l.visible = o.value("visible").toBool(true);
        l.locked = o.value("locked").toBool(false);
        l.weight = o.value("weight").toDouble(0.25);
        return l;
    }
};

} // namespace Loopwire
