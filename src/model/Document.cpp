#include "model/Document.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include "io/Dwx.h"
#include <QUuid>
#include <algorithm>

namespace Edraw_Desktop {

QString Document::validationFingerprint() const {
    QJsonObject content=dwx::toJson(*this);
    content.remove(QStringLiteral("validationStamps"));
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(content).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
bool Document::validationPassed(const QString& kind) const {
    const QString stamp=validationStamps_.value(kind).toString();
    return !stamp.isEmpty() && stamp==validationFingerprint();
}
void Document::recordValidation(const QString& kind,bool passed) {
    if(passed)validationStamps_.insert(kind,validationFingerprint());
    else validationStamps_.remove(kind);
    dirty_=true;
}
void Document::invalidateValidation() {
    if(validationStamps_.isEmpty())return;
    const QString fingerprint=validationFingerprint();
    for(const QString& key:validationStamps_.keys())
        if(validationStamps_.value(key).toString()!=fingerprint){validationStamps_.remove(key);dirty_=true;}
}


QJsonObject LayoutViewport::toJson() const {
    return {{QStringLiteral("id"), id},
            {QStringLiteral("x"), x},
            {QStringLiteral("y"), y},
            {QStringLiteral("width"), width},
            {QStringLiteral("height"), height},
            {QStringLiteral("modelX"), modelCenter.x},
            {QStringLiteral("modelY"), modelCenter.y},
            {QStringLiteral("modelScale"), modelScale}};
}

LayoutViewport LayoutViewport::fromJson(const QJsonObject& obj) {
    LayoutViewport viewport;
    viewport.id = obj.value(QStringLiteral("id")).toString();
    viewport.x = obj.value(QStringLiteral("x")).toDouble(40.0);
    viewport.y = obj.value(QStringLiteral("y")).toDouble(35.0);
    viewport.width = std::max(10.0, obj.value(QStringLiteral("width")).toDouble(340.0));
    viewport.height = std::max(10.0, obj.value(QStringLiteral("height")).toDouble(225.0));
    viewport.modelCenter = {obj.value(QStringLiteral("modelX")).toDouble(),
                            obj.value(QStringLiteral("modelY")).toDouble()};
    viewport.modelScale = std::max(0.0, obj.value(QStringLiteral("modelScale")).toDouble());
    return viewport;
}

QJsonObject DrawingLayout::toJson() const {
    QJsonArray entities;
    for (const Entity& entity : paperEntities) entities.append(entity.toJson());
    QJsonArray viewportArray;
    for (const LayoutViewport& viewport : viewports) viewportArray.append(viewport.toJson());
    return {{QStringLiteral("name"), name},
            {QStringLiteral("paperWidth"), paperWidth},
            {QStringLiteral("paperHeight"), paperHeight},
            {QStringLiteral("templateName"), templateName},
            {QStringLiteral("paperEntities"), entities},
            {QStringLiteral("viewports"), viewportArray},
            {QStringLiteral("pageSetup"), pageSetup.toJson()}};
}

DrawingLayout DrawingLayout::fromJson(const QJsonObject& obj) {
    DrawingLayout layout;
    layout.name = obj.value(QStringLiteral("name")).toString(QStringLiteral("Layout1"));
    layout.paperWidth = std::max(10.0, obj.value(QStringLiteral("paperWidth")).toDouble(420.0));
    layout.paperHeight = std::max(10.0, obj.value(QStringLiteral("paperHeight")).toDouble(297.0));
    layout.templateName = obj.value(QStringLiteral("templateName")).toString();
    for (const QJsonValue& value : obj.value(QStringLiteral("paperEntities")).toArray())
        layout.paperEntities.push_back(Entity::fromJson(value.toObject()));
    for (const QJsonValue& value : obj.value(QStringLiteral("viewports")).toArray())
        layout.viewports.push_back(LayoutViewport::fromJson(value.toObject()));
    if (obj.contains(QStringLiteral("pageSetup"))) {
        layout.pageSetup = PageSetup::fromJson(obj.value(QStringLiteral("pageSetup")).toObject());
    } else {
        // A drawing written before page setups existed: the sheet's own size, full bleed, at 1:1 - which
        // is how it was being plotted, so nothing about it changes by being written down.
        layout.pageSetup.paper = paperSizeFor(layout.paperWidth, layout.paperHeight, true);
        layout.pageSetup.landscape = layout.paperWidth >= layout.paperHeight;
    }
    return layout;
}

std::vector<Layer> Document::defaultLayers() {
    return {
        {"0", "#ffffff", true, false, 0.25},
        {"Construction", "#00f0ff", true, false, 0.25},
        {"Dimensions", "#f5c66d", true, false, 0.25},
    };
}

Document::Document() : Document(QStringLiteral("Drawing1")) {}

Document::Document(QString name)
    : id_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      name_(std::move(name)),
      layers_(defaultLayers()) {
    // Two sheets to begin with, the way a drawing office starts: one to issue and one to work something
    // out on. Either can be thrown away with the x on its tab, and more added with the + beside them.
    for (int i = 1; i <= 2; ++i) {
        DrawingLayout layout;
        layout.name = QStringLiteral("Layout%1").arg(i);
        LayoutViewport viewport;
        viewport.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        layout.viewports.push_back(viewport);
        layouts_.push_back(std::move(layout));
    }
}

const Layer* Document::findLayer(const QString& name) const {
    for (const auto& l : layers_)
        if (l.name == name) return &l;
    return nullptr;
}

Layer* Document::findLayer(const QString& name) {
    for (auto& l : layers_)
        if (l.name == name) return &l;
    return nullptr;
}

} // namespace Edraw_Desktop
