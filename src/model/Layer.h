#pragma once

#include <QJsonObject>
#include <QString>

namespace Edraw_Desktop {

// A drawing layer. Mirrors Edraw's layer record:
// { name, color, visible, locked, weight }.
struct Layer {
    QString name = "0";
    QString color = "#ffffff";
    bool visible = true;
    bool locked = false;
    double weight = 2.0;

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
        l.weight = o.value("weight").toDouble(2.0);
        return l;
    }
};

} // namespace Edraw_Desktop
