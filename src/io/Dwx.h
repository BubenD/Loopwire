#pragma once

#include <QJsonObject>
#include <QString>

namespace Loopwire {

class Document;

// DWX is Loopwire's native offline drawing format: a JSON document holding
// format/version/unit metadata, layers, and vector entities. Ewire reads and
// writes the identical schema so files interchange with Loopwire.
namespace dwx {

QJsonObject toJson(const Document& doc);
void fromJson(Document& doc, const QJsonObject& obj);

bool save(const Document& doc, const QString& path, QString* error = nullptr);
bool load(Document& doc, const QString& path, QString* error = nullptr);

} // namespace dwx
} // namespace Loopwire
