# Edraw desktop

Native C++20 / Qt 6 CAD application for 2D wire-harness drawings, built as `Edraw.exe`. It is a
port of the web prototype in `../Edraw`. This file is standing project memory: keep it short and
current. Put the story of a fix in the commit message, not here.

## Stack

- C++20, Qt 6.8.3 (`C:\Qt\6.8.3\msvc2022_64`), CMake 3.21+ with Ninja.
- MSVC from **Visual Studio Build Tools 2026**, at
  `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`. The 2022 Qt build links fine
  against it.
- Everything except `main.cpp` is in the `edraw_core` static library. `Edraw.exe` and
  `edraw_tests.exe` both link it.

## Build

1. **Close a running `Edraw.exe` first, without asking.** It holds the exe open, and the link then
   fails with LNK1104 after everything has compiled. Ask the window to close
   (`taskkill /IM Edraw.exe`), and force it with `/F` only if it is still there two seconds
   later.
2. In scripts launched from Git Bash, call `%SystemRoot%\System32\tasklist.exe` and `find.exe` by
   full path. On the Git Bash PATH they resolve to the POSIX tools and the guard silently does
   nothing.
3. Source `vcvars64.bat` from the 2026 Build Tools, then build:

   ```
   cmake --build build --config Release
   ```

4. Configuring from scratch needs the tests flag, which defaults OFF:

   ```
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
         -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DEWIRE_BUILD_TESTS=ON
   ```

Open CASCADE 8.0.1 is optional (`EDRAW_WITH_OCCT`, `EDRAW_OCCT_DIR`, default `C:/Software/occt-8.0.1/lean`).
It is a lean build from source (TKDESTEP, TKMesh, TKXCAF, TKFillet and what they need) with every third-party option off, so only its own `TK*.dll` ship; the
configure line is in `CMakeLists.txt`. Don't point the build at the prebuilt package: it brings FFmpeg,
FreeImage, FreeType, TBB, jemalloc and OpenVR, which the notices don't cover, and the configure step
refuses a non-`TK` DLL.

`_build_edraw.bat` in the repo is stale. It calls a 2022 vcvars and builds a different checkout.
Don't use it.

## Test

- Run `QT_QPA_PLATFORM=offscreen ./build/tests/edraw_tests.exe`. It deploys its own Qt runtime
  and starts from anywhere.
- The full run takes several minutes. Run it in the background, redirect output to a log, and
  grep the log for `FAIL|PASSED`. Don't re-run the suite to find out what broke; read the log.
- To look into a failure, run only some suites or rounds: `EDRAW_TEST_ONLY=brickharness`
  (suite names as in `test_main.cpp`, comma-separated) and `EDRAW_ROUNDS=FortyThree,FiftySix`
  (brick-harness tests whose names end in those). Finish with a full run before committing; a
  round can pass alone and fail after another one leaves state behind.
- Tests are plain functions using `check(cond, "what: plain statement")`, grouped one function per
  round (`testRoundThirtyOne()` and so on). Each new round's function is registered in
  `run_brickharness_tests()`.
- When a change alters behaviour on purpose, update the old test that asserted the old behaviour,
  and say so in the reply.
- For visual checks, a small scratch program can link `edraw_core.lib`, build a design, put it in
  a `DrawingPreview`, and `grab()` it to a PNG. Look at the PNG before calling a drawing change
  done.

### Every wizard change that affects the drawing preview

Before calling such a change done, render a harness that has branches, transition heatshrinks and a
bridge wire, and check all three:

1. **Every connection is made.** Each run, branch and bridge wire starts and ends on the part, run or
   transition it is linked to. Nothing floats free or stops short.
2. **Everything meets the face it goes into.** Every physical wire, heatshrink and fan-out cable
   touches the transition heatshrink or cable body it connects to, and enters within that face, not
   past its edges.
3. **Symmetric still works.** Run Symmetric with "about: Auto" and with a chosen part. The parts must
   sit evenly either side of the reference part's line, without overlapping, and the bricks on the
   pane must not move.

A test or a look at the PNG covers each point. Say in the reply that all three were checked, or
which one wasn't and why.

## Layout

| Path | What |
|---|---|
| `src/app/` | Dialogs and the main window. The brick wizard is `BrickWizardDialog.*`. |
| `src/harness/` | Harness domain: `BrickHarness.*` (brick design → drawing), `Harness.cpp` (twisted-pair / cable rendering and its cache key), `Bom.*`, `PartsLibrary.*` |
| `src/harness/BrickDrc.*` | The harness DRC: `brickCompatibility()` (the brick dots) and `brickDrc()`, the logical and compatibility rules, each finding with a rule id and a bypass key. The DRC window is `src/app/MechanicalValidationDialog.*`; its rule switches and bypasses live in `Editor` (`drc*`), apart from the schematic's. |
| 3D preview | `src/geometry/Mesh3D.*` (shapes; STL, OBJ and a STEP B-rep reader - planes, cylinders, cones), `src/render/Render3D.*` (software z-buffer renderer, orbit camera, section, see-through), `src/render/GlRender3D.*` (the same picture on the graphics card, off screen; software where there is no OpenGL, always under the tests' offscreen platform), `src/harness/Harness3D.*` (a harness drawing in 3D: tubes for runs, each part's model fitted to it or a box), `src/app/View3DPane.*` (the "3D view" dock, View tab; the environment row). A part's model is its library param `model3d`, a file kept in the datasheet store. |
| 3D environment | `src/geometry/Environment3D.*` (the CAD model a harness is routed in, solid by solid; `RayIndex3D` picking), `src/geometry/StepOcct.*` (STEP read whole through Open CASCADE; only with `EDRAW_HAVE_OCCT`, and with `tests/test_occt_fixture.cpp` the only place OCCT headers are included), `src/harness/Route3D.*` (the harness routed by hand: connectors, branches, clips, bundles laid over faces; centreline with bend-radius arcs and lengths), `src/harness/RouteLink.*` (route nodes put for the wizard's End bricks by id; each cable's length along the route, given back to the design). The environment file and the route are saved with the drawing as `routing3d` (`Document::routing3d`), written only when not empty. |
| `src/model/` | `Document`, `Entity` (`EntityType` enum), layers, blocks |
| `src/render/` | Canvas and entity renderer |
| `src/geometry/` | `Vec2`, `Bounds`, offsets, snapping |
| `src/io/` | DWX / DXF / PDF import and export |
| `tests/` | `edraw_tests`; the wizard's tests are in `test_brickharness.cpp` |

### The brick wizard (Mechanical harness wizard)

- `BrickDesign` holds the bricks and links. `buildBrickHarness()` turns it into entities, plus a
  `BrickBuild` with faces, part positions, the actual figures where a field is on Auto, and the
  bill extras (transition, layer and fan-end counts and lengths).
- The run loop draws each branch of each special. A `TwistedPair` entity carries `twist*` meta;
  `Harness.cpp` reads that meta to draw it. **A new meta key that changes the shape must also be
  added to the cache key in `Harness.cpp`,** or the old shape is reused.
- Every new `BrickRunSpec` or `Brick` field needs `toJson` / `fromJson` entries, with a default
  that keeps old designs unchanged.
- Fields with an "A" (Auto) button store 0 or a negative value for Auto. While Auto is on, the
  field shows the drawing's actual figure from `BrickActual`.
- Wizard state that isn't saved (holds, caches) is commented "Not saved" in `BrickHarness.h`.

## Code style

- **Source files are CRLF.** Keep them CRLF after every edit.
  - In this Git Bash, `sed -i` rewrites a file with LF endings. After any `sed -i`, run
    `perl -pi -e 's/\r?\n/\r\n/' <file>`.
  - Check that `git diff --stat` stays small. A whole-file diff means the line endings changed.
  - `grep` and `cat -A` here don't show the CR. Use `od -c` to check.
- Match the surrounding comments. A comment block opens with an upper-case lead phrase saying
  what it is (`// A BRIDGE WIRE ON THIS END: ...`), then plain sentences about why. Don't comment
  what the code plainly says.
- Qt idioms: `QStringLiteral`, `QLatin1String` for comparisons, and `QSignalBlocker` when loading
  widgets from the design. Give every widget a test needs an `objectName` (`brick...`).
- New UI goes next to its kind. Transition settings go in the transition frames; per-part
  settings go in the part panel.
- MSVC shadowing warnings (C4456/C4458) already exist in these files. Don't add new ones where it's
  easy to avoid.

## Editing large files

`BrickHarness.cpp`, `BrickWizardDialog.cpp` and `Editor.cpp` are thousands of lines long. Find the
place with `grep -n`, then read just that range.

For multi-hunk changes, write OLD/NEW patch blocks to a file with the Write tool and apply them
with a small perl script that requires each OLD block to match exactly once. Bash heredocs whose
body contains an apostrophe fail in this environment.

## Git

- Work on the current feature branch, not `main`.
- After each finished round: build, run the full suite, then commit and push.
- End commit messages with the `Co-Authored-By` line from the session's instructions.
- The house style for a commit subject is one long line listing what changed, in plain words
  (`Brick wizard: ...; ...; ...`).
- **Never commit `tests/test*.dwx`.** They are the user's own drawings.
- Don't commit build output or scratch probes.

## Ask before

- Deleting or rewriting the user's parts library (`edrawlib.plb`). Back it up first, as with
  `edrawlib.plb.backup-before-heatshrink-walls`.
- Changing what a saved design or drawing means, for example JSON keys or entity meta that old
  files rely on.
