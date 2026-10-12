#pragma once

#include "system/SystemDesign.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace Loopwire {

// A SYSTEM DIAGRAM BROUGHT IN FROM ANOTHER PROGRAM: a draw.io (diagrams.net) file or a Visio drawing read as units,
// their ports, the cables between them and notes - each where it is on the page.
//
//  - Boxes become units (named by their text: the first line its name, a "rev X" its revision, "[PCBA]" or
//    "[Mechanical]" what it is), a box wholly inside another a unit in it.
//  - Small squares on a box's side become its ports.
//  - Lines from one box (or port) to another become cables - a port put where a line meets a box without one.
//  - Note shapes, and text standing on its own, become notes.
// What Loopwire exported reads back as it was.

// FROM DRAW.IO: a .drawio (or .xml) file, its first page - plain or compressed. False, and why, where it cannot be read.
bool importSystemDrawio(const QString& path, SystemDesign& out, QString* error = nullptr);
bool importSystemDrawioXml(const QByteArray& xml, SystemDesign& out, QString* error = nullptr);
// FROM VISIO: a .vsdx drawing, its first page.
bool importSystemVsdx(const QString& path, SystemDesign& out, QString* error = nullptr);
// By the file's extension.
bool importSystemDiagram(const QString& path, SystemDesign& out, QString* error = nullptr);

// A ZIP read: each file in it by its name, inflated where it was compressed (stored and deflated entries).
struct ZipItem {
    QString name;
    QByteArray data;
};
std::vector<ZipItem> readZip(const QByteArray& bytes);

} // namespace Loopwire
