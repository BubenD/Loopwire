#pragma once

#include <QByteArray>
#include <QString>

namespace Edraw_Desktop {

class Document;

namespace pdf {

bool parse(const QByteArray& bytes, Document& doc, QString* error = nullptr);
bool importFile(Document& doc, const QString& path, QString* error = nullptr);

} // namespace pdf
} // namespace Edraw_Desktop
