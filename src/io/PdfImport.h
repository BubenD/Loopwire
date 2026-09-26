#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

namespace Edraw_Desktop {

class Document;

namespace pdf {

// How far an import has got, 0..100, and what it is doing - for a progress bar.
using Progress = std::function<void(int percent, const QString& stage)>;

bool parse(const QByteArray& bytes, Document& doc, QString* error = nullptr,
           const Progress& progress = nullptr);
bool importFile(Document& doc, const QString& path, QString* error = nullptr,
                const Progress& progress = nullptr);

} // namespace pdf
} // namespace Edraw_Desktop
