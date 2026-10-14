# Canvas latency measurements

Measured on Windows on 2026-10-14, in the Release build. The benchmark uses real
Qt mouse events and paint events, with 20 warm-up moves and 120 measured moves
per case. It covers line and rectangle previews on blank, 2,000-object and
20,000-object drawings, with object snapping on and off. Fixtures mix lines and
circles. The full-window case additionally loads the application stylesheet,
ribbon, panels and live coordinate readout.

These are software timings, **not physical mouse-to-photon measurements**.
`input-paint` ends when the canvas paint handler completes. `input-flush` ends
after Qt processes the frame, including the rest of the window. Neither includes
the monitor's scan-out or measures Windows compositor presentation.

## Results

| Windows case | Before, median / p95 | Final repeated runs, median / p95 |
| --- | --- | --- |
| 20,000 objects, line, snapping | 10.97 / 18.05 ms | 3.59–4.54 / 5.97–6.64 ms |
| 20,000 objects, rectangle, snapping | 11.73 / 16.46 ms | 3.87–4.33 / 5.95–6.30 ms |
| Full application, blank drawing, line | 7.08 / 13.74 ms | 3.43–4.08 / 5.04–7.98 ms |

The final native validation measured 3.68 ms median and 5.04 ms p95 for the full
application, with zero coordinate-induced status-bar layout requests.

The standalone canvas matrix uses the default Qt widget style so its before/after
runs are comparable. Full-window measurements use Loopwire's stylesheet. Desktop
scheduling causes occasional outliers; the CSV files also retain maximum times.
Results are machine dependent and do not establish an absolute hardware minimum.

## Causes and changes

- `effectiveDrawingDetails()` walked all model and paper entities on every
  preview paint. The canvas now caches resolved title-block fields, refreshing
  on scene rebuilds, owner changes and declared metadata changes.
- Remembered circle-centre membership cost a linear list search for every
  circle on every frame. A set built once per overlay makes membership constant
  time. Existing remembered marks are preserved.
- Nearest snapping evaluated angles for distant rings. A conservative box now
  rejects them before layer lookup and trigonometry. Nearby objects retain the
  existing snap logic. Hidden harness runs are rejected before building their
  expensive snap geometry.
- The coordinate `QLabel` invalidated status-bar and main-window layouts on
  every mouse move. Tracing found 140 requests for each in 140 moves. Giving the
  readout a fixed width **and height**, updated when its font/style changes,
  eliminates these requests while keeping the coordinates live. Fixing width
  alone did not eliminate them.
- The native OS crosshair moved independently of the QWidget preview. During
  an active draft, the crosshair now paints in the same backing-store frame as
  the rubber band, at the same snapped position, including fractional positions.
  The native crosshair returns when the draft ends. This prevents separate cursor
  and preview positions; it does not make physical presentation instantaneous.
- The canvas declares its opaque paint coverage to Qt, avoiding unnecessary
  background painting. The earlier 8 ms repaint timer remains removed.

An experiment hiding the crosshair overlay did not improve full-window timings,
so the existing overlay remains in use.

## Reproduce

Build with `EWIRE_BUILD_TESTS=ON`, then run from the repository root:

```powershell
cmake --build build --config Release --parallel 4
$env:LOOPWIRE_TEST_ONLY = 'latency'
$env:LOOPWIRE_TEST_PLATFORM = 'windows'
$env:LOOPWIRE_LATENCY_CSV = 'build/latency-native.csv'
& .\build\tests\Release\loopwire_tests.exe

$env:LOOPWIRE_LATENCY_MAINWINDOW = '1'
$env:LOOPWIRE_LATENCY_CSV = 'build/latency-app.csv'
& .\build\tests\Release\loopwire_tests.exe
```

The native test shows temporary windows. Omit `LOOPWIRE_TEST_PLATFORM` to use the
default offscreen platform. Latency runs are opt-in and are not part of the
default correctness suite. They assert next-pass paints, scene-cache reuse,
crosshair/preview alignment, cursor restoration, metadata-cache refresh and
absence of coordinate-induced status-bar layouts. Absolute timing thresholds
are not hard-coded because workstation load and display configurations vary.

Regression suites:

```powershell
Remove-Item Env:LOOPWIRE_TEST_PLATFORM,Env:LOOPWIRE_LATENCY_MAINWINDOW -ErrorAction SilentlyContinue
$env:LOOPWIRE_TEST_ONLY = 'geometry,editor,mechmodelcanvas,drawingspace'
& .\build\tests\Release\loopwire_tests.exe
```

This session's raw CSVs and logs are in `build/latency-baseline-*`,
`build/latency-iteration*`, `build/latency-final-*` and
`build/latency-regression-tests.log`.
