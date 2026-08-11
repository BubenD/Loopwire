# Edraw desktop

**Edraw desktop** is a native C++/Qt 6 standalone CAD application for 2D
wire-harness drawings — a from-scratch port of the web-based **Edraw** prototype.
The built application is named **Edraw** (`Edraw.exe`).

## Status

Active development. Working: ribbon UI (Home/Modify/Insert/Annotation/Harness/…),
custom canvas (pan/zoom/fit, grid, origin), full draw + modify tool set, snapping,
selection (click / rectangle / lasso, grips, multi-select), layers/properties
panels, DWX + DXF import/export, and the wire-harness domain (connectors, wires,
nets, splices, BOM). See `../Edraw` for the reference web app.

## Build prerequisites

- **CMake** ≥ 3.21 and **Ninja**
- **MSVC** (Visual Studio 2022 Build Tools, "Desktop development with C++")
- **Qt 6.8.x** for MSVC 2022 64-bit (installed at `C:\Qt\6.8.3\msvc2022_64`)
- *(optional)* **vcpkg** at `C:\vcpkg` for auxiliary libraries

## Building (Windows, PowerShell)

The MSVC toolchain environment must be active. Source `vcvars64.bat` first, then
configure and build with Ninja:

```powershell
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$src    = "C:\Whatson\ETS\Software\EdrawDesktop"

cmd /c "`"$vcvars`" && cmake -S `"$src`" -B `"$src\build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64"
cmd /c "`"$vcvars`" && cmake --build `"$src\build`""
```

Add `-DEWIRE_BUILD_TESTS=ON` to also build the unit tests
(`build\tests\edraw_tests.exe`).

## Running

For a standalone run, deploy the Qt runtime next to the executable once:

```powershell
& "C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe" --release "C:\Whatson\ETS\Software\EdrawDesktop\build\Release\Edraw.exe"
.\build\Release\Edraw.exe             # or pass a .dwx / .dxf path to open it
```

## Layout

```
src/geometry/  Vec2, math, intersections, bounds, snapping
src/model/     Entity, Layer, Document
src/commands/  UndoStack
src/tools/     ToolId, EntityTransforms, ModifyOps
src/harness/   Harness (wire colors/gauges, connector geometry)
src/io/        Dwx (native JSON), Dxf (import/export)
src/render/    CanvasWidget (view transform, pan/zoom/fit), EntityRenderer
src/app/       Editor (interaction brain), MainWindow (ribbon + panels)
resources/     edraw.qss theme, edraw.qrc
samples/       harness-demo.dwx, sample-edraw.dwx
```

Source code lives in the C++ namespace `Edraw_Desktop`.
