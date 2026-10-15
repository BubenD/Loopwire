# Schematic build wizzard

Open the wizzard from Schematic Build. The compact connection table shows six
columns instead of fourteen. **More fields** exposes the original controls.
The preview menu chooses the side or bottom position, including with More fields
enabled. Drag the splitter edges and section grips to resize the editors and
preview. Each column scrolls when its sections need more room. **Checks** expands the validation list.

Window geometry, table column widths and splitter positions are saved for the
next opening. **Fit columns** fits the current columns to the editor;
**Reset layout** restores the default compact layout.

## Coax with spliced leads

1. Add the connectors, then use **Add coax** to create signal and shield
   conductors together. Expand the assembly row to edit the pin assignments.
2. Select a conductor row or click its pin in **Pin map**. The detail panel
   offers a conductor selector for the signal and shield of that assembly.
3. Select the cable from the parts list and enter its manufacturing cut length.
   Cable part and cut length apply to the whole assembly.
4. For each conductor's From and To end, choose **Direct to pin** or
   **Through splice + hook-up wire**. The latter has independent wire and
   splice library selections, optional manual P/Ns, wire gauge, colour and
   tail cut length. Signal and shield can use different parts and terminations.
5. Open **Live BOM** to check cable, hook-up wires, splices, connectors and
   separate contacts. Missing parts and cut lengths are marked. Click a wire
   material row to highlight its connections in the preview and pin map.

Physical purchasing quantities use the entered cut lengths, not distances in
the schematic. A paired coax assembly counts as one cable. Generated drawings
carry the same physical materials for their drawing BOM. Existing patterns
keep their previous BOM behavior until generated with the updated wizzard.

Use **Color** for two-colour wire definitions; `WH-BL` means White/Blue.
Actual-colour drawing mode displays the stripe. Choosing colours does not
change a coax conductor's signal/shield role.

**Duplicate** copies an assembly with open endpoints for reassignment.
**Swap ends** also swaps its end-specific termination parts. Ctrl-click
connection rows and use **Apply to selected** to copy material, gauge, colour
and marking choices. Undo/Redo includes these edits.

Pin map shows each wire's cross section in the selected mm2/AWG notation and its
label when **Print on wire: Label** is enabled. The drawing remains visible while
the map is open. The three small circular optimisation switches turn green when
enabled; **Apply optimisation** applies all enabled criteria together.

Wire details show Construction, Part description from part list, Part P/N,
Cross section and Color. Selecting a library wire imports its size and colours;
selecting a cable imports the corresponding conductor's construction, size,
colours, label, shield and twist groups.

## Further simplification plan

These are proposals, not implemented behavior:

- Introduce termination presets (direct, signal pigtail, shield pigtail) so
  repeated end definitions can be applied with one selection. Measure the
  number of clicks and corrections on a representative four-end coax assembly.
- Offer a focused assembly editor with Signal/Shield tabs and collapsible end
  panels. Keep the table for comparing pin assignments across assemblies.
- Combine the legacy grouping and unification toolbar into an Assembly menu;
  surface actions only when a relevant selection exists.
- Let a pending BOM row open the exact missing part or length field. Measure
  the time to resolve an incomplete BOM before adopting the interaction.
- Test first-time creation and editing of an existing harness separately;
  compare task completion time, errors, horizontal scrolling and discovery of
  advanced controls against the previous fourteen-column editor.
