# Proposed project functions

These are proposed next steps for the harness project workspace. They are not implemented as part of the current changes.

| Priority | Function | User benefit |
| --- | --- | --- |
| 1 | Contact-level interface definitions | Define the expected contact number, signal name, direction and electrical class for each unit port. Compare these definitions with linked schematics to identify incorrect pinouts and incompatible connections. |
| 2 | Project revision comparison | Compare two saved project revisions and highlight changes to units, connector/contact part numbers, ports, cables and drawing revisions. List the cables and drawings affected by each change. |
| 3 | Harness length and BOM synchronization | Read manufacturing lengths and material quantities from linked harness drawings. Show proposed updates to the project cable list and consolidated BOM for review before applying them. |
| 4 | Engineering exception records | Add a reason, reviewer, date and optional review deadline to each bypass. Flag accepted exceptions when their associated drawing or interface changes. |
| 5 | Release package | Export the project diagram, connection matrix, cable list, drawing revision register and DRC/exception report together, with a checklist for missing or outdated linked drawings. |

Start with contact-level interface definitions. They provide the expected engineering data that makes the existing schematic consistency checks more useful. Add revision comparison next so interface and drawing changes can be reviewed before a release.
