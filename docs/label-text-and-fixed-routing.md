# Label text and finished cable routing

Select a label placed from Special, then edit **Label → Text** in Properties.
Enter applies the text; Shift+Enter starts a new line. The text wraps within
the opaque label and fits its font to the available space. Each edit starts
from the normal font size, so shorter text can recover its size. The clear
heatshrink around a label does not provide additional printing space.

Place an annotation text leader with its arrow tip on the opaque label to
move its text to the callout. The physical label is left without duplicate
text. Undo restores the printing and removes the new leader together.

For a mechanical harness design, open the 3D view and load the environment.
Use **Route from harness** with **Fixed design lengths** checked to attach
the designed harness as a route. Its linked connectors can be moved onto
the model; its slack is reshaped while its cable lengths stay fixed. A move
that requires more cable is refused. A design initially too short for the
connector positions must have its connectors placed closer before routing.
The length readout is display-only; cable and branch lengths are edited in the routing table.
Uncheck **Fixed design lengths** to unlock existing rows. Recheck it to restore the original
designed lengths; a change that cannot reach the connectors is refused. **Lengths to harness**
does not overwrite their specified design lengths. Fixed lengths and design
links are saved with the drawing. Existing unconstrained routes retain their
previous behavior.

Freestyle mechanical drawings can also use **Route from harness**. Mechanical
wire runs and cable bundles are linked to the library parts at their ends;
a wizard design is not required. Auto body widths and the outer covering
(including heatshrink) are retained in the 3D view and attached route.

After attachment, **Return to initial position** restores the harness route
to its first attached position. The initial position is saved with the
drawing. Undo restores the moved position; the environment is kept in place.


The routed harness retains its labels, including their captions, placement along the cable,
and clear sleeves. Select a cable, choose a **Label**, and select **Flexible** or **Stiff**.
The **Stiffness** slider controls an approximate local resistance to bending: 0 follows the
cable; 100 straightens the labelled span. This also affects the gravity preview. Label
length stays defined in millimetres; its diameter follows the supporting wire, cable or
heatshrink. **Body D** sets the outer covering diameter independently; Auto follows the bundle.

**5 N** is available in the global, selected-cable and routing-table tension menus.
Use **Measure > Bend radius**, then click a bend to annotate its actual centreline radius.
A straight stretch has no finite radius. Radius annotations are saved with the drawing.

In the parts library, **View / edit 3D** opens the existing model in its own editable window,
including attached mesh models. **Auto create 3D** creates an editable preview using the
pinout's closed outline as the cross-section, carried along the mechanical profile, and adds
the pinout's contacts. Keyed and concave outlines are retained. Review or modify the result,
then **Save to part** to apply it. Closing the preview does not replace the library model.
