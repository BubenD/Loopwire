#include "io/Dwx.h"

#include "model/Document.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

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
    if (!doc.bom().isEmpty()) root["bom"] = doc.bom();
    if (!doc.wizards().isEmpty()) root["wizards"] = doc.wizards();
    if (!doc.harnessPresets().isEmpty()) root["harnessPresets"] = doc.harnessPresets();
    if (!doc.bundlePresets().isEmpty()) root["bundlePresets"] = doc.bundlePresets();
    if (!doc.routing3d().isEmpty()) root["routing3d"] = doc.routing3d();
    QJsonArray layouts;
    for (const DrawingLayout& layout : doc.layouts()) layouts.append(layout.toJson());
    root["layouts"] = layouts;
    QJsonArray setups;
    for (const PageSetup& setup : doc.pageSetups()) setups.append(setup.toJson());
    root["pageSetups"] = setups;
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
    doc.bom() = obj.value("bom").toObject();
    doc.wizards() = obj.value("wizards").toObject();
    doc.harnessPresets() = obj.value("harnessPresets").toObject();
    doc.bundlePresets() = obj.value("bundlePresets").toObject();
    doc.routing3d() = obj.value("routing3d").toObject();
    doc.entities() = std::move(entities);
    std::vector<DrawingLayout> layouts;
    for (const QJsonValue& value : obj.value(QStringLiteral("layouts")).toArray())
        layouts.push_back(DrawingLayout::fromJson(value.toObject()));
    if (!layouts.empty()) doc.layouts() = std::move(layouts);
    doc.pageSetups().clear();
    for (const QJsonValue& value : obj.value(QStringLiteral("pageSetups")).toArray())
        doc.pageSetups().push_back(PageSetup::fromJson(value.toObject()));

    doc.setActiveLayer(doc.layers().empty() ? QStringLiteral("0")
                                            : doc.layers().front().name);
    doc.setDirty(false);
}

bool save(const Document& doc, const QString& path, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray payload = QJsonDocument(toJson(doc)).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
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
