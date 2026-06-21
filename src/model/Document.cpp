#include "model/Document.h"

#include <QUuid>

namespace Edraw_Desktop {

std::vector<Layer> Document::defaultLayers() {
    return {
        {"0", "#ffffff", true, false, 2.0},
        {"Construction", "#00f0ff", true, false, 2.0},
        {"Dimensions", "#f5c66d", true, false, 2.0},
    };
}

Document::Document() : Document(QStringLiteral("Drawing1")) {}

Document::Document(QString name)
    : id_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      name_(std::move(name)),
      layers_(defaultLayers()) {}

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
