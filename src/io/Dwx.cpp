#include "io/Dwx.h"

#include "model/Document.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace Edraw_Desktop::dwx {

QJsonObject toJson(const Document& doc) {
    QJsonObject root;
    root["format"] = QStringLiteral("DWX");
    root["version"] = 1;
    root["app"] = QStringLiteral("Ewire");
    root["unit"] = doc.unit();

    QJsonArray layers;
    for (const auto& l : doc.layers()) layers.append(l.toJson());
    root["layers"] = layers;

    QJsonArray entities;
    for (const auto& e : doc.entities()) entities.append(e.toJson());
    root["entities"] = entities;
    return root;
}

void fromJson(Document& doc, const QJsonObject& obj) {
    doc.unit() = obj.value("unit").toString("mm");

    std::vector<Layer> layers;
    for (const auto& v : obj.value("layers").toArray())
        layers.push_back(Layer::fromJson(v.toObject()));
    if (layers.empty()) layers = Document::defaultLayers();
    doc.layers() = std::move(layers);

    std::vector<Entity> entities;
    for (const auto& v : obj.value("entities").toArray())
        entities.push_back(Entity::fromJson(v.toObject()));
    doc.entities() = std::move(entities);

    doc.setActiveLayer(doc.layers().empty() ? QStringLiteral("0")
                                            : doc.layers().front().name);
    doc.setDirty(false);
}

bool save(const Document& doc, const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QJsonDocument json(toJson(doc));
    file.write(json.toJson(QJsonDocument::Indented));
    return true;
}

bool load(Document& doc, const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !json.isObject()) {
        if (error) *error = perr.errorString();
        return false;
    }
    fromJson(doc, json.object());
    return true;
}

} // namespace Edraw_Desktop::dwx
