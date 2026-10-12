#pragma once

#include <QString>

namespace Loopwire {

// Where the application keeps the files it owns. One place that decides, so a folder is never
// spelled twice and a file written by one window is found by another.
//
//   <app data>/library/     part libraries        (.plb)
//   <app data>/template/    drawing templates     (.etp)
//   <app data>/schbuilder/  harness builder presets (.bcf)
//   <app data>/block/       blocks                (.ebk)
//   <app data>/datasheet/   the datasheets parts point at
//
// A file left in an older place is MOVED into its folder the first time the folder is asked for -
// moved, not copied, so there is never a second stale copy of a library to wonder about.
namespace AppPaths {

// %APPDATA%/Whatson/Loopwire on Windows. Everything below lives under this.
QString root();

// Each of the three, created on first use. The returned path always exists.
QString libraryDir();
QString templateDir();
QString builderDir();
QString blockDir();
QString datasheetDir();

// Take `from` over as `to` when `to` does not exist yet and `from` does. Returns true when
// something was moved, so the caller can say so. A move rather than a copy: two libraries with the
// same parts in them is how the wrong one ends up being edited.
bool adopt(const QString& from, const QString& to);

} // namespace AppPaths
} // namespace Loopwire
