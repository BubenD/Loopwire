#pragma once
#include "geometry/Vec2.h"
#include <QJsonObject>
#include <QString>
#include <vector>
namespace Edraw_Desktop {
class Document;
struct DrawingFieldIssue {
    QString key, field, message, entityId, layout;
    Vec2 at;
};
QString drawingFieldKey(const QString& key);
QString drawingFieldLabel(const QString& key);
QString drawingFieldTextType(const QString& key);
QJsonObject effectiveDrawingDetails(const Document& doc);
std::vector<DrawingFieldIssue> checkDrawingDetails(const Document& doc);
int updateDrawingFields(Document& doc, const QJsonObject& details);
}
