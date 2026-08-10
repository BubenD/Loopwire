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
