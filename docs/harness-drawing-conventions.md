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
