# Loopwire: what the application is and how it works

Loopwire is a native Windows CAD application for **wire-harness engineering**. It is written in C++20 on
Qt 6 and ships as `Loopwire.exe`. It is a port of the earlier web prototype, rebuilt as a fast desktop
program that works offline.

A harness engineer normally uses a general CAD tool, a spreadsheet for the bill of materials, a second tool
for voltage-drop sums and a third for the system diagram. Loopwire puts these in one program and one file,
and keeps them consistent with each other: the drawing, its parts, its bill, its design checks and its 3D
routing all come from the same data.

## 1. The big picture

```
            System design  (units, ports, cables, linked drawings)
                   |
                   v
   Schematic harness  <---->  Mechanical harness  <---->  3D routing
   (what connects to what)    (how it is built)           (where it runs)
          \                        |                          /
           +---------- one drawing document (.dwx) ----------+
                              |
           parts library · BOM · DRC · analysis · output (PDF, DXF, DWX)
```

Work moves from the outside in. The *system design* says which units exist and which cable joins them. A
*schematic harness* says which pin of which connector goes to which pin of which other connector. The
*mechanical harness* turns that into a physical drawing: bundles, branches, backshells, heatshrink and fan-outs.
The *3D view* shows that harness routed through a real CAD environment and gives back real lengths.

## 2. The drawing editor (the base layer)

Underneath the harness features is a general 2D CAD editor:

- A custom canvas with pan, zoom, fit, grid, origin and snapping (endpoint, midpoint, centre, intersection,
  and snapping to the paper and to windows on a sheet).
- A full set of draw tools and modify tools (move, rotate, scale, mirror, offset, trim, fillet, array and
  so on), with selection by click, rectangle or lasso, grips, and quick-select.
- Layers, properties (colour, weight), blocks, pictures, tables, text with a font library, and callouts
  (leaders, balloons, centre marks, detail views).
- Model space and **sheets** (paper space) with a page-setup manager, drawing templates (frame, zones, title
  block, revision table, company band and logo) and per-drawing details: DRAWING P/N, REVISION and TITLE.
- Unlimited undo and redo, crash recovery, and several drawings open at once (tabbed, cascaded or split).

The interface is a **ribbon**: Home, Modify, Insert, Annotation, Template, View, Schematic build, Harness
mechanical, System, and 3D view. There are dockable panels (layers, properties, parts list, blocks list, BOM,
calculator) and dark and light colour schemes.

## 3. The parts library

A harness is made of purchased parts, so the library is central. It lives in a `.plb` file
(`loopwirelib.plb`) and holds:

- Wires and cables (gauge, colour, insulation, temperature rating, cores, shields), connectors with pins and
  contacts, ferrules, crimp terminals, terminal blocks, splices, heatshrink and boots, backshells, sleeves,
  conduits, ties, clips, labels and electronic in-line components (resistor, capacitor, coil, ferrite).
- Per part: part number, manufacturer, the unit it is bought in (each, metre, mm), parameters specific to
  its kind, a **2D symbol** with a marked connection line, a **3D model** (`model3d`) and a **datasheet**
  stored beside the library.
- **Part from PDF** and **part data from the web**: a datasheet is read and the part's figures are filled in
  for you to review.
- Mechanical parts come from the office's standard drawings (the "default mechanical models") or are typed
  on the spot. Either way the part is checked by the same rules; the library stores *what a part is*, the
  drawing stores *where it is placed*.

## 4. Schematic harness builder

A four-step guided builder that produces a schematic drawing of a harness:

1. **Connectors**: connector, pin count, pitch, side and placement, taken from the library or typed, with
   contacts and contact P/N, and Y-split legs.
2. **Shields**: overall shields per connector, and where each drain goes (the connector body, or any
   connector's pin), directly or through a hook-up wire, drawn with a splice where one is used.
3. **Wires**: the wire table (from pin, to pin, gauge, colour, kind: loose wire, twisted pair, cable core,
   coax), with the drawing always shown beneath it.
4. **Layout**: placement and ordering.

A pin map is always at hand, with optimisers that reorder pins to reduce wire crossings. Each step shows a tick
or a count of what is wrong in it, checked after every change. A builder definition can be saved, loaded,
undone and redone, and is the same data the harness engine draws from, so the preview is exactly what
Generate produces.

## 5. Mechanical harness: the brick wizard

The mechanical drawing is a **tree**: a trunk that branches at breakouts, ending in connectors or free
wire ends. Its thickness is never drawn by hand; it is worked out from the conductors that pass through.

The **brick wizard** describes a harness as **bricks** on a coarse grid:

- An **End** brick is a part the harness lands on (connector, ferrule, crimp, Y-split).
- A **Special** brick is what lies between: a transition, splice, fan-out, backshell, heatshrink boot or sleeve.
- **Runs** join bricks and carry wires, cables, twisted pairs, labels and layers; each run, branch and bridge
  wire is drawn to touch the parts it is linked to.
- Fields marked **A** are Auto: while A is on, the field shows the real figure the drawing came to; turning it
  off starts from that figure.
- **Symmetric** layout places parts evenly either side of a reference part.

From the same design it also works out the extras of the bill: transition, layer and fan-end counts and lengths.

## 6. Design rule checks (DRC) and validation

- **Schematic checks** verify connectivity and consistency of the drawing.
- **Harness (mechanical) DRC** has logical rules (what is linked, named, chosen, long enough, on the bill) and
  compatibility rules (stack, entry size, gauge, ways, contacts, joint, data and logic). Each finding has a
  rule id and a bypass key. Bricks show compatibility dots on the pane.
- Rules can be switched off or **bypassed** one by one, with a bypass table kept with the drawing.
- Passing a check saves a **fingerprinted validation stamp** in the file and shows a green badge at the
  canvas top right. Any drawing edit, including undo and redo, clears it; selection does not. Linked project
  drawings must have current passes. Changing rules or bypasses means checking again.

## 7. Bill of materials

The BOM is read from the drawing: a row per part to buy, with the item number tying each row to its balloon on
the drawing. Quantities carry their unit (metres of wire, each of connectors). Options include totalling rows
that name the same part, including what the drawing calls for by itself, a spare percentage on lengths,
rounding lengths up, the display unit and decimals. It is exported alongside the drawing.

## 8. Engineering analysis

- **Voltage drop**: the harness is treated as a resistive network of pins, splices and open ends joined by
  wires. A supply and a load are picked, and the drop is worked out around the whole loop (feed and return),
  broken down into copper, contacts and splices, using worst-case or nominal figures. Strand lay length is
  accounted for.
- **Thermal**: current through resistance heats the bundle, heat raises resistance, and the loop is iterated
  to a steady temperature: conductors in a bundle, in an enclosure or conduit, to the ambient air.
  Insulation ratings are compared with the result.
- Neither guesses. A figure the drawing does not state is reported as a **gap**, not assumed.
- A scientific calculator is built in.

## 9. 3D view and routing

- A software z-buffer renderer with orbit camera, section cut, wireframe and see-through, plus the same
  picture rendered on the graphics card off screen where OpenGL exists.
- A harness drawing is shown in 3D: tubes for runs and each part's own model (STL, OBJ or STEP) fitted to it,
  or a box.
- **Environment**: a STEP CAD model the harness is routed in, read solid by solid (through Open CASCADE when
  built with it). Surfaces can be picked, keep-outs defined, and gravity and cable tension applied.
- **Route**: connectors, branches, clips and bundles are laid over faces by hand, giving a centreline with
  bend-radius arcs and true lengths. Those lengths are given back to the wizard's design, tied to its End
  bricks by id.
- The environment file and the route are saved with the drawing.

## 10. System design

A diagram view of a whole product: **units** (boxes with P/N and revision), their **ports**, and the **cables**
between them, each with name, P/N, revision, length, wire count and a **linked harness drawing**. It checks the
design, checks that linked drawings are at the revision the diagram expects (and carry current validation
passes), finds drawings on disk with a preview, imports and exports (CSV cable list, Visio, PDF, PNG) and
routes the cables on the diagram.

## 11. Files and interchange

| Format | Use |
|---|---|
| **DWX** | Native format: a JSON document holding the drawing, sheets, layers, blocks, harness data, validation stamps, system design and 3D routing. Sections that are empty are not written. |
| **DXF** | Import and export; DWG through an optional ODA File Converter. |
| **PDF** | Output of sheets; import of a PDF page as drawing or as a part. |
| Libraries `.plb` | Parts libraries. |
| STL, OBJ, STEP | 3D part models and environments. |
| CSV, Visio, PNG | System cable lists, diagrams. |

Saved designs keep their meaning between versions: new fields default so old files open as they were.

## 12. How the code is organised

| Path | Role |
|---|---|
| `src/geometry/` | Vectors, bounds, intersections, snapping, and the 3D mesh, STEP and environment code. |
| `src/model/` | Document, entities, layers, blocks, page setup, templates, drawing details. |
| `src/commands/` | Undo stack. |
| `src/tools/` | Tool ids, entity transforms, modify operations, quick select. |
| `src/render/` | Canvas, 2D entity renderer, 3D software and GPU renderers. |
| `src/io/` | DWX, DXF, PDF, ODA conversion, GIF writing, application paths. |
| `src/harness/` | Harness domain: brick design, schematic builder, mechanical harness, DRC, BOM, parts library, datasheets, 3D harness and routing. |
| `src/analysis/` | Voltage-drop and thermal engines. |
| `src/system/` | System design model, import, export, P/N matching, routing. |
| `src/app/` | Main window, ribbon tabs, the editor's interaction logic and all dialogs. |
| `tests/` | `loopwire_tests`, plain-function test rounds. |

Everything except `main.cpp` is in the static library `loopwire_core`, which `Loopwire.exe` and
`loopwire_tests.exe` both link. Open CASCADE (lean build) is optional.

See also: [harness-drawing-conventions.md](harness-drawing-conventions.md) for how a harness drawing is
prepared, [mechanical-module-engine.md](mechanical-module-engine.md) for the specification-versus-placement
design, and the [README](../README.md) for building, running and licensing.
