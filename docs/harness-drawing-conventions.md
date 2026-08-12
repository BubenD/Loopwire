# How a harness drawing is prepared

Read off three released sample sheets — SMP-00001, SMP-00002 and SMP-00003, all IPC/WHMA-A-620
Class 3 — and written down here because the mechanical builder is generated *against* these rules.
Where the generator does something different, it is the generator that is wrong.

## The sheet set

A harness drawing is a small set of sheets, not one drawing:

| Sheet | What is on it |
|---|---|
| 1 | The **assembly**: the harness drawn as it is built, to scale in length, with every dimension, every balloon and the process notes. Views (VIEW-A / VIEW-B, TOP SIDE / BOTTOM SIDE) for anything the plan view cannot show, and an insertion-side detail per connector. |
| 2 | The **electric schematic**: pin to pin, every conductor with its colour and gauge written on it, twists and screens drawn, unused pins struck out. |
| 3 | The **bill of materials** — on its own sheet once it is longer than a corner of sheet 1 will hold. |

Every sheet carries the same title block and the same revision table. Only sheet 1 carries the
change description; the rest say `SEE PAGE 1`.

### Title block, bottom right

`CUSTOMER LOGO` | `CUSTOMER DETAILS` | `TITLE` | `DRAWN / CHECKED / APPROVED` each with `NAME` and
`DATE` | `GENERAL TOLERANCE: According IPC-WHMA-A-620 Class 3` | `DRAWING P/N` | `PRODUCT P/N` |
`PROJECT` | `SHEET: n of m` | `SIZE: A4` | `REV`.

Plus, always: `DIMENSIONS: mm`, `DO NOT SCALE THIS DRAWING`, `THIS DRAWING IS SCHEMATIC ONLY`, and a
length-tolerance table that applies to every dimension on the sheet:

| Dimension (mm) | Tolerance (mm) |
|---|---|
| 0–300 | −0 +10 |
| 300–500 | −0 +50 |
| 1500–3000 | −0 +100 |
| >3000 | −0 +5 % |

### Revision table, top

`REV | CHANGE DESCRIPTION | DESIGNED | DRAWN | APPROVED | DATE`, first row `A01 RELEASE`.

### Electrical properties, top left of sheet 1

> 1. 100 % open & short testing.
> 2. Continuity, withstanding voltage and insulation resistance tests must be in accordance with
>    IPC/WHMA-A-620 Class 3 requirements.

## Two kinds of callout, and they are never mixed

- **Round balloon** = *what to buy*. The number in it is the BOM item number, nothing else.
- **Triangular delta** = *what to do*. The number in it indexes a `Notes:` list beside the harness.

Both are stacked in clusters where several apply at one place: `(5)(6)(13)(15)` on one heatshrink,
`(17)(1)(7)(8)(9)` at one connector, with the delta hung off the same cluster: `/1\ (17)`.

Observed delta notes, and they are worth copying verbatim:

- `1` — APPLY EPOXY ON PINS 2 MM FROM THE CONNECTOR BACK SIDE.
- `2` — WIRES GAUGE TRANSITION LOCATION.
- `3` — APPLY EPOXY OVER Y-SPLITTER, OVERLAP 2 MM ONTO HEATSHRINK ON EACH LEG.
- (SMP-00003) FILL BACKSHELL BOOT AND CABLE HEATSHRINK TRANSITION WITH DP-190 EPOXY ADHESIVE.

Note 2 is the one the builder can work out for itself: a gauge transition is wherever a conductor
ends and a different cross-section carries on, which the definition already knows.

## The bill of materials is tied to the drawing by the item number

Columns, in order: `Balloon` (or `ITEM`) | `Part Description` | `Mfr P/N` | `Mfr` / `Mfr Name` |
`Qty` | `Unit`.

Rules the three sheets keep without exception:

- Every balloon on the assembly appears in the table, and every row in the table is ballooned
  somewhere on the assembly. There is no row nobody can find and no balloon nobody can buy.
- One row per **part number**, not per place it is used: a heatshrink used in four places is one row
  whose quantity is the total.
- Units are mixed and are part of the row: `EACH` / `PCS` for things, `mm` or `METER` for anything cut
  off a reel, `ml` for adhesive. Quantities that cannot be added are never merged.
- Wire and cable are listed **per colour and gauge**, one row each, in mm — `3079 RD005 … 980 mm`,
  `3079 BK005 … 980 mm`.
- Heatshrink is listed per SIZE, and the description carries the recovery range, because that is what
  it is chosen against: `HEAT SHRINK TUBE, RNF-100-3/16, 2:1, 4.8 MM -> 2.36 MM, BLACK`.
- What has no part number still gets a row and a balloon: `LABEL | N/A | N/A | 2 | EACH`.
- The order is by what the thing IS: connectors and their contacts, then backshells and boots, then
  cable and wire, then heatshrink, then braid and sleeving, then labels and markers, then adhesive.

Where a harness carries printed labels there is a second table — `LABEL TABLE: LBL# | LABEL TEXT` —
and the labels are called out on the drawing as `LBL1`, `LBL2`, … rather than by balloon.

## How the harness itself is drawn

### Branches

A branch point is drawn as a **Y**, not as a corner: the trunk arrives, and the legs leave at an
angle, with the outline continuous through the junction. A Y-splitter boot is a *part* over that
junction (`BOOT Y-SPLITTER 1-2 OUTLET`, `1-3 OUTLET`) — ballooned, and with the epoxy delta note on
it. SMP-00003 uses a 1-to-3 and a 1-to-2 boot on the same harness.

Every leg is dimensioned from the branch point to its connector, and the trunk is dimensioned overall
as well: `1000` overall, `350` to the Y, `300` per leg. Overall first, then the parts of it.

### Backshells

A backshell is drawn as a stepped body between the connector and the cable, in this order, each
narrower than the last:

```
connector | backshell (+ band adapter) | boot / heatshrink transition | cable
```

Each is its own BOM row: `BACKSHELL STRAIGHT, BAND ADAPTER, SIZE 11`, then
`HEATSHRINK BACKSHELL BOOT 24-10.4 BLACK`. The transition between the boot and the cable is filled
with epoxy, and that is a delta note rather than anything drawn.

### Heatshrink

Drawn as a plain sleeve over the bundle — a rectangle a little wider than what is under it — with its
own **length dimension** and its balloon. Sleeves are never drawn as an outline over the whole run:
they are as long as they are, where they are, and the length on the drawing is the length in the BOM.

Layers over one another are drawn concentrically, not end to end: braid over the conductors, then
heatshrink over the braid.

### Where the wires are open

This is dimensioned on all three sheets and it matters more than it looks:

> From the back of the connector (or the back of its backshell/boot) the conductors run **open** for a
> stated distance before anything covers them — `20` at P1, `10` and `20` at the fan-outs, `40` at the
> Mega-Fit housings.

So the chain along a run reads: connector face → *open length* → first heatshrink → … → cable. The
open length is a dimension of its own, and it exists because the conductors have to be able to fan out
to the contacts and be inspected. A drawing that runs the heatshrink up to the connector face is
wrong, and so is one that leaves the distance to the builder.

The same applies where a jacketed cable is opened up: the jacket stops, the conductors run open, and
only then does the fan-out into the connector begin.

### Gauge transitions

Where a conductor is spliced to a different cross-section (`#16 RED` to `#24 RED`), the location is
marked with the delta note `WIRES GAUGE TRANSITION LOCATION` and the splice is covered — the
schematic sheet shows the two gauges meeting at a dot, and the assembly sheet shows the heatshrink
over it.

## What this means for the generator

Implemented against the above:

- `mechBom()` — one line per part number, quantities totalled in each part's own unit, ordered by what
  the thing is. The line's `item` **is** the balloon number.
- Balloons on the generated drawing carry that item number, so the sheet and the table are one thing.
- `MechNode::openLengthMm` — the open conductors between the connector (or the back of its bodies) and
  the first covering, drawn and dimensioned.
- `mechDeltaNotes()` — the process notes the definition can work out for itself, drawn as numbered
  triangles with a `NOTES:` block, starting with the gauge-transition note.
- A Y-splitter boot at a branch point is drawn as a Y over the junction rather than as a box across it.
- Where the conductors are open they are drawn as CONDUCTORS: the bundle body stops short of the
  connector by the open length and each wire is a line from the bundle to the contacts, which is also
  the check a builder makes on the sheet — that the count going in matches the count in the run.
- Balloons are on by default and the conductors are ballooned on the run they go through, one per size.
  A harness of nothing but wire would otherwise have a table and no callouts at all.
- The open length starts at 20 mm on every connector — a starting figure in the same sense as a run's
  starting length, because zero would mean heatshrink hard against the contacts, which is never right.

## How test1.dwx builds its mechanical view

`test1.dwx` carries both sheets of one harness: the schematic below the origin (wires, screens,
twists, connectors on the `Wires` / `Connectors` layers) and a mechanical view above it, drawn by
hand on layer `0`. The harness is the same one either way — read the schematic back with
`mechFromDrawing()` and you get the trunk, the breakout and the two branches that the mechanical view
draws. So the mechanical view is a worked example of the drawing the builder has to produce, measured
in the same units as the model. What it does, feature by feature:

**Every section is an outline of two lines at its true diameter, and is closed off across its width
wherever it stops.** The trunk is a double run Ø10 from x=170 to x=410. At x=170 there is a bar
across Ø13.1, at x=141.8 another across Ø8.6, at x=410 one across Ø10, at x=439 one across Ø7.4.
Ten of the drawing's forty-one objects are those bars. Without them the outline reads as two loose
lines running off the end of the object.

**A boot or backshell is a taper, not a box.** From the connector end: a bar at the neck (Ø8.6), a
straight neck to x=147.6, a cone out to Ø13.1 by x=151.5, then a straight body to x=170.4 where it is
barred. Its wide end lies over the cable, its neck faces the connector — the conductors are squeezed
together going into the shell, so the shape narrows towards it. The trunk's own Ø10 outline runs on
underneath.

**Open conductors are drawn as conductors, each at its own diameter.** Between the neck and the
connector body every wire is a double run of Ø1.5 with a rounded knee: out of the neck at an angle,
then straight into the face. Eleven of the objects in the view are those wires. The knee radius is 5
and it is set per corner (`cornerRadii: [0, 5, 0]`), so the middle vertex is rounded and the ends are
not. They are trimmed against the boot's cone where they meet it, which is why the trim caps matter.

**A breakout is a cone between two bars.** The trunk ends at x=410 barred at Ø10; two lines run out
to x=439 where a bar closes Ø7.4; the two branches leave from x=440.5, each a double run, each with
its start trimmed to the cone it comes out of.

**Connectors are plain rectangles** sized to the part — 12 × 22.9 for the 8-way, 8.7 × 14.7 for the
small ones — with the wires running to the face.

**Dimensions are linear, and there is one for every length the harness is built to**: textHeight 5,
arrowSize 3.3, extOffset 2, two decimals, standing 20 to 60 mm clear of the geometry. Eleven of them:
the open-wire length at each of the three connectors, the boot length at each, each run, the spread
cone, and one overall length from the first connector to the last.

**Nothing else.** No diameter labels, no balloons, no notes on this view — the callouts belong to the
released sheet, and the view itself is geometry and dimensions.

### The funnel, in the generator

A breakout is drawn as the funnel it is rather than as mitred corners: the run that arrives stops
short of the junction by three times its own width (bounded, and never more than a third of the run),
is closed off across its width there, and two lines close in to a narrower bar across the throat. The
throat is as wide as the runs leaving it laid side by side, and never wider than what arrives - where
they would be wider they share it in proportion. Each branch starts on its own stretch of that bar,
in the order they fan out, so a branch heading one way takes the near side and one heading the other
takes the far side. Mitring the outlines into one another drew the branches as a sharp V from a single
point with their inner edges crossing behind it, which is not a shape a harness has.

And no two dimension figures are written across each other. Each dimension already stands off its
geometry in a known direction, so where a figure would land on one already placed it is stood further
off along that same direction until it is clear - decided once every dimension is made, because only
then can it be seen which of them collide.

## Describing a harness: the steps

A harness is built from one end to the others, and the window that asks for one asks in that order -
the numbering is the build sheet's:

| # | The step | What it asks for |
|---|----------|------------------|
| 1 | the end you start from | open leads, a connector (with or without a cap), or a terminal; its part, its ways, its body |
| 2 | backshell / wires without jacket | the backshell and the boot behind it, each with a part, a length and an outside diameter, and how far the conductors run bare |
| 3 | transition heatshrink | over the step from the cable to the conductors coming out of it, and how far the jacket is cut back |
| 4 | the main run | loose conductors (how many, what cross-section) or a cable (which one, over the jacket), and how long |
| 5 | over the main run | braid, heatshrink, either or both, each from a point along the run for a length |
| 6 | the split | how many branches, and whether the split is nothing, heatshrink, or a Y-split boot |
| 7 | each branch | which conductors go that way, how long it is, which way it leaves the split |
| 8 | over each branch | braid, heatshrink |
| 9 | transition heatshrink | at the far end of the branch |
| 10 | backshell / wires without jacket | dressing that end |
| 11 | the end it finishes at | as step 1 |

Steps 7 to 11 repeat for every branch, under that branch's own heading, so where you are in the build
is where you are in the list. Asking for another branch in step 6 brings its five steps with it. The
drawing is built beside the questions - the same generator, the same renderer, so what is shown is what
will be placed - and every figure asked for is a figure of the part, not of the picture: a length, a
diameter, a cross-section, a count.

## Where a part attaches: the connection line

A part's mechanical drawing is drawn once and placed a hundred times, and the generator has to know
which way round to put it down. Centring it on the joint is a guess that only works when the part is
symmetrical about its own face - a connector whose shell is 18 mm one side of the face and nothing the
other lands half inside the harness.

So a part can say where the harness attaches to it: select that line while the drawing is being made
and mark it (Harness ▸ connection line, or the `connline` command). It is drawn ORANGE from then on -
on the sheet, in the library preview, wherever the part is shown - and travels with the drawing into
the library.

A part with one is set down by it: the centre of the marked line on the joint, the line itself across
the wires, and the body on the far side from them - which is the opposite way round at the two ends of
a run, because a connector faces away from the harness it is on the end of. A part without one is
centred on the joint as before, so nothing that was drawn before this changes.

Marking it: the library's **Edit** button opens the part on a canvas of its own - not the sheet, because
this is not "insert a copy to use", it is "look at the part and say where the harness attaches to it".
Click the line; it turns orange. Clicking another MOVES the mark rather than adding a second, because a
part has one place the harness meets it. Keep writes it onto the part and the library preview shows it
orange from then on.

On the sheet the marked line is an ordinary line until the part is SELECTED, and orange while it is. It
is a note about the part, not a feature of the drawing: orange at all times would put a coloured line
through every connector on a finished sheet, and nowhere is it more wanted than when the connector it
belongs to has just been picked.

A part goes onto the sheet as a BLOCK - inserted from the library or put down by the builder, either
way. A part is a block by definition: drawn once, dropped in wherever it is wanted. As one it stays one
thing - picked in a click, moved in a piece, and rebuilt everywhere on the sheet when the part's drawing
is edited. As loose lines it was none of that the moment it landed.

### What the guided build draws, and from what

The canvas beside the questions opens **empty**. A harness does not exist until something has been said
about it, and a drawing of the defaults looks like an answer when it is not one; the first field changed
or step moved on from starts it.

A **branch is two legs**: a short one turning it off the trunk, then a straight one parallel to the trunk
again — a quarter of the branch and three quarters of it, bounded so a short branch is not all corner.
The fan is symmetric about the trunk and includes the straight-through when the count is odd, so two
branches make a Y and three make a Y with a straight through the middle, which is the shape a harness of
three has.

**Drawn from the library, or drawn from its figures.** A connector, a backshell, a Y- or T-split and a
terminal are drawn from their own mechanical drawing: what those look like is the point of them, their
shape is particular, and it is what a person recognises on the sheet. Heatshrink, braid, a boot, a
sleeve, a conduit, a tie, a grommet are drawn by the generator from their numbers — how long, how wide,
where it starts — all of them the same way. A dozen sleeves each drawn in whatever style whoever captured
it happened to use is harder to read, not easier. Only the mechanical drawing is ever used for this; a
part chosen for a connector that has none says so in the list, and comes out as a plain body sized on
what arrives at it.

**To scale, both ways.** Widths and lengths at the same scale, so a width taken off the drawing is the
width of the thing: a bar across the outline measures the diameter itself. The builder proper can
exaggerate the bodies to make a formboard sheet readable, and says so on the drawing when it does; a
harness being described for the first time is drawn as it is.

## The default mechanical models

A part in the library is a thing you buy, and its drawing is its own. The other half of a harness
drawing is the pieces that are a KIND of thing rather than a part number: a length of wire, a cable,
heatshrink over a transition, the wires between a connector and the cable they come out of, a ring
terminal, a ferrule, a boot, a Y-split, the body of a cable. Whatever house style an office has for
those, it has one, and every drawing it releases uses it.

So they are drawn once — **Harness mechanical ▸ Default models** — and stored as a set (one file,
`mech-models.json`, in the application's data folder, which is how a house style moves from machine to
machine). Each is drawn on **a canvas of its own** — the Edit button — which is the same canvas the application
draws on, with the same tools (line, polyline, rectangle, circle, arc, plus snapping, the grid, zoom and
undo), over a scratch drawing that holds nothing else. The sheet is a drawing of a harness; a model is a
piece of the vocabulary those drawings are written in, and mixing the two means finding somewhere clear
to work and remembering to clear up afterwards. The two ends that must not stretch are marked on the
canvas as dashed lines while you draw, so a boot's taper or a wire's flush end is drawn UP TO the mark
rather than measured afterwards; what is kept is moved so it starts at x = 0 and is centred across, and
the marks are left behind. Capturing a selection off the sheet still works, for a model that already
exists there.

**Length is not part of a model.** A wire is drawn at whatever length suits and the harness says how
long the wire is, so each model carries the two ends that must NOT stretch — the flush end of a wire,
the crimp of a ferrule, the taper of a boot — and everything between them makes up the difference. A
wire drawn 60 long with 4 mm ends, asked for at 500, keeps those two ends and puts 492 of wire between
them. Asked for shorter than its own two ends, they shrink in proportion rather than crossing over one
another. A model can also say what diameter it was drawn at, and is then scaled across to whatever it
goes over.

The ones that are **not** stretched — a ring terminal, a ferrule, a Y-split — are placed at their own
size, because a bigger terminal is a different terminal.

Two of them are drawn **per conductor**: the open wires, and the wires between a connector and its
cable. Draw one conductor and say how many the drawing shows; the harness says how many there really
are, and they are spaced across rather than the picture being squashed to fit.

The generator uses a model wherever there is one for the kind of thing it is drawing, and draws its own
shape where there is not — so an office that has captured nothing sees exactly what it saw before.

## Model space and the sheets

The strip along the bottom of the canvas is where the drawing is: **Model**, then a tab per layout.
Model space is the harness, at size — a 500 mm wire is 500 mm there, and that is why model space has no
plot scale. A layout is a sheet of paper the harness is plotted on: A3 by default, with a **window** cut
in it that looks at the model. The scale in the right-hand corner of the strip is the layout's — one of
paper to so many of the model, or *Fit* to size the drawing to its window — and it is kept on the
window, in the drawing, so a sheet plots the same next time it is opened.

**A sheet is drawn on with the same tools as the model.** That is had without a second set of tools by
giving each layout a document of its own — its paper space. On a layout the editor is pointed at that
document, so the tools, the selection, the grips, the snaps, the dimensions and the undo stack all work
exactly as they do on the harness, because as far as the editor knows it *is* a drawing. The canvas
keeps the drawing itself in its other hand (`setDrawingDocument`) for the layouts and for what the
windows look at. What is on the sheet is written back into the layout on the way out of it and before
the drawing is saved, so it goes in the file with everything else.

Two things follow from that, and both are deliberate:

- **A window is taken hold of by its frame** — its edge, or one of its corner handles. Its inside is
  paper like the rest of the sheet, so a line can be drawn across a window, something can be selected
  through one and a note can be moved over one. Were the whole rectangle a handle, a window would be a
  hole in the sheet that nothing could be put on.

  Dragging an **edge carries the window** about the sheet, and nothing clamps it to the paper: a window
  is often parked half off the edge while a sheet is being laid out. Dragging a **corner resizes the
  window only** — what it looks at stays where it is on the paper, so the window crops rather than the
  drawing inside it changing size. A window with no scale of its own fits the drawing to its rectangle,
  so that fit is pinned as a scale of its own the moment the rectangle is taken hold of; otherwise
  resizing would rescale what is inside.

  **Double-clicking a window steps inside it**, and inside, the harness is what is being drawn on: the
  editor is pointed at the drawing and handed that window's own coordinates, so a 100 mm line drawn
  through a window is 100 mm in the model however small the window shows it. The selection and the grips
  are the model's too, drawn through the window and kept inside its frame. A middle-drag or the wheel
  moves and zooms what the window looks at, as in model space. Esc, or a press on the paper, steps back
  out. Stepping in and out is announced with `viewportEntered()`, because that is what decides which
  document the editor is working in.

  A window is added with **+ window** beside the scale, with **Add a window onto the drawing** in the
  right-click menu on a sheet, or by typing `LAYOUTWINDOW`; the same menu steps into a picked window and
  takes the picked ones off the sheet.

- **A selection zone on the paper picks windows as well as geometry.** A window is not an entity, so
  nothing in the editor's own loop would ever find one: the editor announces every zone it closes
  (`regionSelected`, with the polygon and which kind it was) and the canvas takes the windows the zone
  caught. The blue window zone takes a window only if the whole of it is inside; the green crossing zone
  takes every window it so much as touches; Shift lets them go again. Several windows can be picked at
  once, and dragging the frame of one that is already picked carries all of them — a frame that is not
  picked picks just itself, as it always did. Inside a window the zone belongs to the model being worked
  in, and picks nothing on the paper.

Moving what a window looks at is a **middle**-drag, and letting the middle button go lets the drawing
go — the release is keyed to the button that was doing it, not to the left button alone.

Two things follow from the editor working in the model while the canvas is showing paper:

- **A snapped point is put on the screen the way the editor's own world is** — through the window inside
  one, as paper outside. Mapped as paper while working inside a window, the crosshair (and the flying
  input field, which anchors to it) jumped off to wherever that model point would fall on the sheet the
  moment a snap caught. Both now ask `crosshairScreenPos()`, which is that rule in one place.
- **The pick and snap tolerances are sized by the scale in use** (`Editor::setInputScale`, fed by
  `CanvasWidget::syncEditorScale`). A pick is so many pixels of screen, turned into world by dividing by
  how big the world is being shown; the editor took that from its document's own view, which inside a
  window is model space's zoom and has nothing to do with how the window shows the model. So the catch
  radius came out wrong by whatever the two zooms differed by, and grew with every step of zoom inside the
  window: at a 4:1 window with model space left zoomed out it reached over a thousand pixels and snapped
  to whatever it found. It is now a steady ten pixels at every window zoom, measured.

- **The overlay's ink is darkened on a white sheet** (`Editor::paperInk`). Every colour in the overlay —
  the line being drawn, the ghosts, the guides, the grips, the selection — was chosen to read on the dark
  canvas of model space, where pale blue on near-black is exactly right. Laid on white paper the same ink
  all but disappears, which is why a line drawn inside a window could not be seen before it was placed.
  Same hue, taken down to where it reads on white and made more solid; on the dark canvas the colours are
  returned untouched, so model space looks as it always did.

**Opening a drawing while a sheet is showing opens that drawing's sheet** — the same one by number if it
goes that far, otherwise its first — because the work in hand is laying out sheets, and dropping to model
space interrupts it. A drawing with no sheets shows its model. The view a drawing was saved with is kept:
the space is applied without refitting.
- **What is drawn on the sheet is drawn last, over the windows.** A window is filled with the view
  through it, so anything under one would be painted out — and a note, a leader or a border drawn on a
  sheet is meant to be read across the view it points at.

The layer list is the drawing's, not the sheet's: a sheet resolves colour, lineweight and linetype
through the same layers, so the layer box on the ribbon means what it says while a sheet is being drawn
on. The harness features — connectors, pin tables, the BOM, blocks, the generator and the wizard — stay
on the model, because a harness lives in the model and not on a sheet of paper.

## Output: what comes out, in what order

**Publish** (the E menu, Ctrl+P, or `PUBLISH` / `OUTPUT` / `PLOT`) is where a drawing goes out. A drawing
is a model and a set of sheets, so putting it out is choosing among them: the list is Model followed by
every layout, each with a tick, and it can be **arranged** — the order in the list is the order of the
pages, because a pack that opens on the wrong sheet is the wrong pack. Standing on a sheet, the model
starts unticked; standing on the model, it starts ticked. The destination is a **PDF file** or a
**printer**, and that choice changes nothing else: the same pages, laid out the same way, painted onto
whichever device. A PDF can also be written as **one file per sheet**, named after each.

**Each page is the size of the thing on it.** A 420 × 297 sheet is a 420 × 297 page, so a scale read off
the print with a rule is right; *Fit each page to the paper* is offered for when the printer has what it
has. Model space has no size of its own, so its page is its extents with a margin — which is what Export
PDF has always done with it.

**A sheet page is the sheet, not a picture of the model.** The paper space geometry goes on at 1:1, each
window shows the model at the scale that window was given, and the window frames are not printed — a
window is a hole to see the drawing through, not a rectangle in the drawing. The arithmetic that puts a
window on the page is the same code the canvas uses (`CanvasWidget::layoutViewportRect` and
`layoutViewportView`, both static for this reason): two copies of it would be two chances to differ, and
what comes off the printer must be what was laid out. What is on the sheet is painted over the windows,
as on the screen.

Sheets are flushed out of paper space before anything is written, so a line drawn a moment ago is on the
page. The two **Export PDF** entries above it in the menu stay as they are — one quick page of whatever is
showing, and one of a dragged window.

## Page setup: what a sheet plots on

Right-click a layout's tab: **Duplicate**, **Rename**, **Plot**, **Page setup**, **Delete**. That
is the menu for what is done TO a sheet, as against what is drawn on it, and the tab is the thing being
talked about.

It is taken through an **event filter** on the strip rather than through the context-menu signal, because a
`QTabBar` *ignores* a right-button press: the press then travels up the parents — the strip, and the canvas
the strip is laid over — and the canvas answers it with the drawing's own menu. Which is exactly what
happened at first: right-clicking a layout tab offered Copy, Cut, Quick Select. The press is now caught on
the tab bar and on the strip, and stops there. The menu is `popup()`ed rather than `exec()`ed, so the
handler returns at once and the menu owns itself. Duplicating copies the paper geometry, the windows and the page setup, gives the windows
fresh ids (two windows with one id are one window to anything that looks them up) and names the copy after
its original. Model space's menu has only the plotting entries: there is one model, it has no name to
change and no paper of its own.

**Page setup** opens the **Page Setup Manager**. Two kinds of setup are on its list: the layout's own,
written `*Layout1*` the way a plot dialog writes it, and **named** setups, which live on the drawing beside
the layouts so a paper, a scale and a set of plot options can be settled once and set on every sheet of a
pack. *Modify* edits whichever is picked, *New* makes a named one from it, *Set current* copies a named
one onto the sheet, and *Import* reads the setups out of another drawing — both its named ones and each of
its layouts' own, since most offices keep their paper and scale on the sheets. A layout always plots by its
own setup: a named setup that is later renamed or deleted must not be able to change how a sheet plots,
which is why *Set current* copies rather than references.

A setup holds the device (a printer by name, or none — meaning a PDF), the paper, across or upright,
upside-down, the scale or *fit to paper*, the offset or *centre*, and how the ink comes out: black or the
drawing's own colours, with or without object lineweights, sheet over the windows.

**What goes on the page** is one of four. *The whole sheet*, edge to edge. *Everything drawn on it* — the
bounds of the paper geometry and the windows, and no more of the paper than that needs. *What is on screen
now*, taken from the canvas's own view of that sheet at the moment it prints (and, when the canvas is
standing somewhere else, the whole sheet rather than nothing). Or *a window of it*, which is **picked on the
sheet**: press *Pick window*, the page setup closes, the two corners are picked on the drawing, and the
dialog opens again with the window in it and everything else the user had set still there. *Type it
instead* gives the corners as figures, for a window that has to be an exact number.

The picking goes through the **editor**, not through a rubber band of its own, and that is the whole point:
every running object snap applies, the snap list on the status bar governs it, and ortho, polar and a typed
coordinate work as they do for any other point — because a plot window is lined up with the drawing, and
lining up means snapping to it. The windows on the sheet stand aside while a rectangle is being picked: a
corner is often *on* a window's frame, and taking hold of the window would eat the click that was asked
for. Esc gives up and the dialog comes back as it was.

Both dialogs have to close for the pick — a modal dialog blocks the canvas however well hidden it is — so
the request travels up: the setup dialog closes with `PickWindowResult`, the manager passes it on with
which setup was being edited and what the user had set, and `MainWindow` picks the window and opens the
manager again straight back into that setup. Whatever
is chosen, the plot is clipped to the printable area, so nothing lands in the margin.

Beside the choices is a **picture of the page**: the paper, the margin as a dashed rectangle, and what will
actually land on it as a blue one. Full bleed, fit-to-paper and an offset stop being words — a sheet
hanging over the printable area visibly hangs over it, and says so in red, before it is printed rather than
after. It is **saved with the drawing**, because a sheet that plots correctly today must plot the same
for whoever opens the file next; a drawing written before page setups existed gets its own sheet size, full
bleed, at 1:1 — which is how it was being plotted all along.

**Full bleed is the one worth understanding.** A printer will not put ink within a few millimetres of the
paper's edge, so every plain size keeps a margin: a plain A3 is 420 × 297 of paper but only 410 × 287 of
ink, and a 420 × 297 sheet does not fit on it. A full-bleed size has no margin — the printable area *is*
the page — so the sheet comes out at true size, which is what a plotter and a PDF can do and what a drawing
that will be measured needs. Every size is offered both ways (ISO A, B and C, ANSI, ARCH, Letter and
Legal), and the dialog shows the printable area in millimetres so the difference is a figure rather than a
guess. Nothing is printed in the margin by anything: a sheet plotted at 1:1 on paper too small for it is
clipped at the printable edge, exactly as it would come out of the machine, rather than being quietly
shrunk. *Fit to paper* is how a sheet is made to go on paper that will not hold it.

### The words on the printing dialogs

The fields are named the way a plot is talked about in a drawing office and written in a spec: *plot area*
(layout, extents, display, window), *plot scale and offset*, *fit to paper*, *plot lineweights*, *plot
paper space last*, *monochrome*, *landscape* and *portrait*, *borderless — full bleed to the paper edge*.
The manager is the *page setup manager*, with *set current*, *new*, *edit* and *import from drawing*, and
what it reports about a setup is a spec sheet: device, paper size, printable area, plot area, plot scale,
ink. The arrangement is our own — a print setup is a short list of decisions and reads better as one, with
the page drawn beside it — but the vocabulary is the trade's, so nothing has to be translated when it is
discussed with whoever runs the plotter.

The one place plain words are kept is the line under each field, which explains what the term does: "the
sheet as laid out, edge to edge", "the view on screen at the moment of plotting". A name should be the
trade's; an explanation should be anybody's.

### Snapping to the paper and to the windows on it

A sheet's own edge and the frame of every window on it are **snap targets**, though neither is an entity:
corners catch as endpoints, the middle of each edge as a midpoint, and anywhere along an edge as nearest,
each only if that snap is switched on. Lining a plot window, a border or a note up against the paper's
corner or against a view already on the sheet is the commonest thing anyone does on a layout, and nothing in
the snap search over the drawing's entities would ever find either of them.

They are ranked **by kind before distance**: a corner beats the middle of an edge, and either beats a point
somewhere along one. Ranked by distance alone, a click 1.5 mm along the bottom edge and 1.3 mm up from it
caught the edge rather than the corner it was plainly aiming at. The drawing still wins a tie against the
paper, since geometry is what is usually being lined up with. In model space the list is empty — there is no
sheet there — and inside a window the work is in the model, where the paper's edge means nothing.

Two consequences worth knowing: geometry drawn a millimetre or two off a window's frame will land **on** the
frame, which is the point of the feature; and while a rectangle is being picked the windows stand aside, so
a corner on a frame is picked rather than taking hold of the window.

### The heat-shrink shelf

`addStandardHeatshrink()` puts the families a harness shop keeps on the shelf into a library, in every size
they are made in — 67 parts: **DR-25** (2:1, fluid-resistant elastomer, the aerospace default), **RNF-100**
(2:1, thin wall, flame retardant), **RT-375** (2:1, clear, so a joint can be inspected through it), **ATUM**
(4:1, adhesive lined, sized in millimetres as expanded/recovered), and **TMS-SCE** identification sleeve in
white and yellow, filed as a *marker* rather than as tubing because it goes on to be printed and that is
what it is on a drawing and on a bill of materials.

It is seeded into a new library, **put into an existing one the first time that library is opened**, and
offered any time by **Add heatshrink range** in the parts library. It adds only what is missing: a second
DR-25 3/8 would be a second line on every bill of materials that used it.

Once, and once only — `ensureStandardHeatshrink()` records `standardHeatshrink` **in the library file**, so
a size somebody deliberately deleted does not come back the next time the library is opened, and a library
that has had the shelf since it was seeded is left alone. A library written before the flag existed reads as
not done, which is what brings the shelf to a library that was made before the shelf was.

The sizes and the designations are the manufacturers' own. The **dimensions are nominal** — what a bundle it
fits is worked out from the size designation and the family's shrink ratio, which is what a size means —
wall thickness is left **blank** rather than guessed at, and every part says in its notes that the figures
are nominal and the datasheet is the authority. A starter library is a starting point; a build held to a
tolerance is checked against the current datasheet.

## A connector and its contacts

A connector is never ordered by itself: it is ordered with its contacts, and a drawing that names the
connector without them is a drawing nobody can build from. So the **contacts are tied to the connector in
the library** — `contactIds()`, kept as one parameter so they travel through the file, the copy and the
merge — and there is usually more than one to choose from: a pin and a socket, two gauges, sealed and plain.

**Which one is fitted is chosen when the connector is placed.** The fall menu on *Insert* lists the tied
contacts and *With no contact*; the button itself takes the first tied one, because a connector placed
without its contacts is a connector nobody can build. What is chosen is stamped on every entity of the
placed block (`contactPart`), so a copy carries its contact with it and whichever entity the bill of
materials reads, it finds the same answer.

**The bill of materials puts the contact on the line below the connector**, in the quantity the connector
needs — one per way, from the part's own way count. Placing a part from the library now reaches the BOM at
all: `partBlockInstance` stamps `libPart` on the entities it makes, and each block *instance* is counted
once however many lines its drawing is made of. Two of the same connector are two pairs of lines, each with
the contact chosen for it.

**A connector's schematic symbol can be built from its data** — *From data* on the schematic side. It reads
the way count (and the pitch, if the data gives one) and produces a real **connector object**, not loose
lines: so what is placed can be wired, edited with the connector tools and counted like any other
connector. Nothing else about the symbol is invented from the data.

**The designator and the part number each have an eye** in the properties panel: whether that marking is
*drawn*. The same connector wants its designator on a schematic and often not on the mechanical view beside
it, and a crowded corner is tidied by turning one off rather than by dragging it somewhere it does not
belong. The text is kept either way — the eye hides it, it does not delete it — and what is hidden stays
hidden when the drawing is saved and opened again.
