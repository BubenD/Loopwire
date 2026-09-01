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

The Qt runtime is deployed beside `build\Edraw.exe` by every build (windeployqt runs after the link), so
the executable is always startable — adding a Qt module cannot leave a binary that compiles and then
refuses to run. To deploy by hand, or to a folder of your own:

```powershell
& "C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe" --release `
    --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler `
    "C:\Whatson\ETS\Software\EdrawDesktop\build\Edraw.exe"
.\build\Edraw.exe                     # or pass a .dwx / .dxf path to open it
```

Keep the three `--no-*` flags on any deployment you make by hand. Without them windeployqt copies four
graphics libraries Edraw never calls — Mesa's software rasteriser and three Microsoft DirectX binaries —
each of which would then have to be licensed and credited. See [Licensing](#licensing) below.

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
resources/notices/  the notices generator and its hand-written header
licenses/      LGPL v3 and GPL v3, verbatim — shipped with the program
```

Source code lives in the C++ namespace `Edraw_Desktop`.

## Licensing

Edraw is proprietary. It links **Qt dynamically** and ships Qt's DLLs beside `Edraw.exe`, which makes the
shipped application a combined work under the **LGPL v3**. Dynamic linking is what keeps Edraw's own
source closed — LGPL §4(d)(1) is satisfied by using a shared-library mechanism — and four obligations come
with it. All four are met by the build; none of them survives being ignored.

| Obligation | Where it is met |
|---|---|
| State that Qt is used, its version, and that the LGPL covers it | The About dialog, and `THIRD-PARTY-NOTICES.txt` |
| Supply the LGPL **and** GPL texts | `licenses/`, copied beside the exe and compiled into it |
| Make Qt's source obtainable | The designated address in `THIRD-PARTY-NOTICES.txt` (GPL v3 §6(d)) |
| Carry the notices of the components inside Qt | The generated schedule in `THIRD-PARTY-NOTICES.txt` |

**What is actually LGPL here, and what is not.** The only LGPL-licensed software in Edraw is **Qt** — the
six `Qt6*.dll` files deployed beside `Edraw.exe`. Nothing else, and none of Edraw's own code, is LGPL.

**No part of Edraw is GPL-licensed, and no GPL software ships with it.** The GPL text is included because
the LGPL requires it: LGPL v3 is not a standalone licence, it "incorporates the terms and conditions of
version 3 of the GNU General Public License, supplemented by the additional permissions listed below", and
§4(b) requires a combined work be accompanied by "a copy of the GNU GPL and this license document". That is
why `licenses/GPL-3.0.txt` is 35 KB while `licenses/LGPL-3.0.txt` is only 7.6 KB — the LGPL is a delta, and
nearly every operative term it relies on lives in the GPL. Ship only the LGPL and you have supplied half a
licence. Section 1 of `THIRD-PARTY-NOTICES.txt` says all of this to the reader, so nobody concludes from the
presence of the GPL text that there is GPL code in the product.

**If ETS holds a commercial Qt licence, none of this is required.** Qt is offered as "Qt Commercial OR
LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only" and a licensee is under exactly one of them. No commercial
licence was found on the build machine (no `qtaccount.ini`, no `.qtlicense`), so the open-source terms are
the operative ones and LGPL-3.0 is the only option among them that permits Edraw to stay proprietary. Qt's
binaries are identical either way — the licence follows the agreement, not the file — so if a commercial
agreement exists, say so and this paperwork can be removed.

`THIRD-PARTY-NOTICES.txt` is **generated from the SBOM that Qt installs**, so it describes the Qt that is
actually linked rather than the Qt somebody remembers. It is committed, so an ordinary build needs no
Python. After changing the Qt version:

```powershell
python resources\notices\gen_notices.py             # regenerate, then commit the result
python resources\notices\gen_notices.py --check     # non-zero if the committed file is stale
python resources\notices\gen_notices.py --audit-gpl # non-zero if anything linked/shipped is GPL-only
```

`cmake --build build --target edraw_notices` and `--target edraw_notices_check` do the same through the
build. `tests/test_licenses.cpp` fails if the notice goes missing from the binary, if a licence text is
absent, or if the notices name a different Qt version from the one linked — the point being that a
compliance failure should look like a broken build rather than like nothing at all.

### The GPL audit runs on every build

**Edraw must never link GPL-only code.** The LGPL permits a proprietary combined work; the GPL does not —
it would require Edraw's own source to be published. The distance between those two is one word added to a
`find_package` line, by someone who wanted a graph and had no reason to think they were making a licensing
decision.

So it is checked rather than remembered. `add_dependencies(Edraw edraw_gpl_audit)` means **every build runs
the audit, and a GPL-only dependency fails the build.** It costs a fraction of a second — five JSON files
read, no network, no compilation — and it answers one question: is anything Edraw links or ships available
*only* under the GPL?

It checks two things:

1. **Every Qt module the build asks for**, parsed out of `CMakeLists.txt`. Qt Charts, Qt Data
   Visualization, Qt Quick 3D and the Qt Virtual Keyboard have **no LGPL option at all** — they are
   GPL-or-commercial. Naming one fails the build with the reason.
2. **Every component inside the Qt repositories that ship**, from the SBOM. A dual licence with a non-GPL
   option is fine (FreeType is `FTL OR GPL-2.0-only`, taken under the FTL). GPL-only is not. Anything
   GPL-only that is *not* distributed — the build tools under the Qt GPL Exception, Qt Test's Linux perf
   headers — is listed on every build rather than hidden, because those are harmless only while they
   really are not shipped.

A module whose Qt repository is missing from `SHIPPED_QT_REPOS` also fails, because its bundled components
would go uncredited in the notices.

Two limits worth knowing: Python is optional for building Edraw, so a machine without it gets a CMake
warning instead of the check; and a Qt too old to publish an SBOM warns rather than fails, since being
unable to audit is not the same as a violation. Neither replaces reading the licence when adding a
dependency that is not a Qt module at all.

### Other rules to keep in mind when changing the build

- **Do not pack Edraw into a single executable, and do not make it verify its own DLLs.** The LGPL
  entitles a user to substitute their own Qt build; loose, replaceable DLLs beside the exe are what make
  that possible.
- **Keep the three `--no-*` windeployqt flags.** They are the reason Edraw ships no Mesa and no Microsoft
  DirectX binaries. `--no-system-d3d-compiler` drops only `d3dcompiler_47.dll`; `dxcompiler.dll` and
  `dxil.dll` need `--no-system-dxc-compiler` as well.
- **Check a Qt module's licence before adding it.** Qt Charts, Qt Data Visualization, Qt Quick 3D and Qt
  Virtual Keyboard are offered as GPL-or-commercial only, with no LGPL option; linking one would oblige
  ETS either to publish Edraw's source or to buy a commercial Qt licence. When adding any module, add its
  Qt repository to `SHIPPED_QT_REPOS` in the generator so its components get credited.

Qt's build tools (`moc`, `rcc`, `uic`, `qmake`, `windeployqt`) are GPL-3.0 with the Qt GPL Exception 1.0,
which permits building proprietary software with them. That holds because they are used and not shipped.

Edraw's own DEFLATE decoder in `src/io/PdfText.cpp` follows the approach of zlib's reference `puff.c`; the
attribution the zlib licence asks for is in that file and in the notices. Manufacturer names in the parts
library are used for identification only — see section 4 of `THIRD-PARTY-NOTICES.txt`.

### The source-code obligation, and the duty it leaves behind

GPL v3 §6 (reached through LGPL §4) offers several ways to make Qt's source obtainable. Edraw uses
**§6(d)** — access from a designated place — which expressly permits that place to be a server operated
by a third party, provided clear directions to it accompany the binary. `THIRD-PARTY-NOTICES.txt` names
`download.qt.io` as that place, so the obligation is discharged without ETS hosting anything.

Edraw deliberately makes **no three-year written offer**. That is §6(b), an alternative intended for
object code shipped on physical media, and it is a standing commitment there is no reason to take on.

What §6(d) does leave is this: *"you remain obligated to ensure that it is available for as long as
needed to satisfy these requirements"* — whoever hosts it. So **keep an archived copy of
`qt-everywhere-src-6.8.3.tar.xz` somewhere ETS controls.** Nothing in the build can enforce that; if
Qt's archive is ever reorganised and a customer asks, that copy is the only thing standing between ETS
and a breach.
