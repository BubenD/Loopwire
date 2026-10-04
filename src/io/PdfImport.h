#pragma once

#include <QByteArray>
#include <QString>

#include <functional>
#include <vector>

namespace Edraw_Desktop {

class Document;
struct Bounds;

namespace pdf {

// How far an import has got, 0..100, and what it is doing - for a progress bar.
using Progress = std::function<void(int percent, const QString& stage)>;

// `pages`, when given, is filled with where each page lies in the drawing - the pages are laid side by
// side, the first at the origin - for a caller that shows one page at a time.
bool parse(const QByteArray& bytes, Document& doc, QString* error = nullptr,
           const Progress& progress = nullptr, std::vector<Bounds>* pages = nullptr);
bool importFile(Document& doc, const QString& path, QString* error = nullptr,
                const Progress& progress = nullptr, std::vector<Bounds>* pages = nullptr);

} // namespace pdf
} // namespace Edraw_Desktop
