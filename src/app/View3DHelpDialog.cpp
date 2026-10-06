#include "app/View3DHelpDialog.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMovie>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace Edraw_Desktop {

namespace {

struct Topic {
    const char* title;
    const char* gif;   // empty: words only
    const char* steps;
};

// WHAT EACH RECORDING SHOWS, written out - for reading at one's own pace, and for what a picture cannot say. In the
// order a newcomer needs them: what the view is for and the whole of it once, then each part of it on its own.
const Topic kTopics[] = {
    {"1. Getting started",
     ":/help3d/start.gif",
     "<p>The 3D view routes a harness through the real thing it goes in - a chassis, a panel, an enclosure - so each "
     "cable's length is read off the path it really takes, not guessed.</p>"
     "<p><b>The whole of it, in four steps:</b></p>"
     "<ol>"
     "<li><b>The model.</b> <i>Environment: Load...</i> and choose its STEP, STL or OBJ file. (To try it first, press "
     "<i>Open the example</i> below - a plate with two boxes and a rib on it.)</li>"
     "<li><b>The connectors.</b> Choose <i>Connector</i> and click the face each one sits on.</li>"
     "<li><b>The cable.</b> Choose <i>Route</i>, click the first connector, click the places it runs over (each a "
     "corner), and click the second connector.</li>"
     "<li><b>The length.</b> It is in the table on the right. <i>Lengths to harness</i> gives it to the harness.</li>"
     "</ol>"
     "<p>The topics that follow take each step on its own. <i>Full screen</i> (or F11) gives the view the whole screen "
     "while you work; F11 or <i>Back</i> comes back.</p>"},
    {"2. Turning the view, all the way round",
     ":/help3d/view.gif",
     "<p><b>With the mouse:</b> hold the wheel button down and drag to turn (left and right) and tilt (up and down) - "
     "all the way round, over the top and underneath. Hold <i>Shift</i> as well to roll. Drag with the right button (or "
     "<i>Ctrl</i> and the wheel button) to move the view; turning the wheel zooms. The left button picks and places, "
     "and does not turn the view. Double-click (with <i>Select</i>) fits everything in.</p>"
     "<p><b>By the numbers:</b> <i>Turn</i>, <i>Tilt</i> and <i>Roll</i> in the top row are the same three turns in "
     "degrees, each going all the way round (-180&deg; to 180&deg;, and on). Their arrows step by 15&deg;; type any "
     "figure.</p>"
     "<p><b>Back to a known view:</b> <i>Iso</i>, <i>Top</i>, <i>Front</i>, <i>Side</i> and <i>Fit</i>.</p>"
     "<p><b>The wheel</b> in the picture's top right corner does it by clicks: <i>Turn</i> left and right, "
     "<i>Tilt</i> up and down, <i>Roll</i> either way, <i>Zoom</i> in and out, <i>Fit</i>, <i>Iso</i>, and "
     "<i>Lock</i>. Each click turns it by a step - 15&deg;, or what <i>Options</i> (the cog, or the wheel's middle) "
     "says: 5&deg;, 15&deg;, 45&deg; or 90&deg;. <i>Wheel</i> in the top row shows or hides it.</p>"},
    {"3. Where a click lands - the faces, the ground, the section",
     ":/help3d/plane.gif",
     "<p>Every click lands on the face of the model under the pointer. That point, and the way the face looks, is what "
     "a connector, a branch, a clip or a corner of a cable is put on.</p>"
     "<p>Where there is no model under the pointer, the click lands on the <b>ground</b>: the plane the drawing lies in "
     "(Z = 0).</p>"
     "<p>To reach a face inside the model, cut it open with the <b>section</b>: <i>Section X</i>, <i>Y</i> or "
     "<i>Z</i>, and the slider beside it moves the cut. A click goes through what is cut away to the first face left. "
     "The see-through slider lets you look through the model instead.</p>"},
    {"4. Routing up and down - the work plane",
     ":/help3d/workplane.gif",
     "<p>On its own a click lands on a face - so a cable lies on the model. A real cable also runs <b>through the "
     "air</b>: up a riser, across a gap, along a frame at some height. For that, choose a <b>work plane</b> in "
     "<i>Route to land:</i> (the 3D Preview ribbon: <b>Route to land</b>)</p>"
     "<ul>"
     "<li><b>a plane across Z (level)</b> - every click lands at that <b>height</b>. Set the height in <i>at</i>.</li>"
     "<li><b>a plane across X</b> or <b>across Y</b> - upright planes, for laying a cable <b>up or down</b>: its points "
     "go wherever on that wall of air you click, high or low.</li>"
     "</ul>"
     "<p>The plane is drawn see-through, so you see where your clicks will land. <i>Through point</i> puts it "
     "through the node selected or the point last picked on the model. You can change the plane in the middle of laying "
     "a cable - lay a few corners high, then go back to <i>the model's faces</i> and click the connector to finish.</p>"},
    {"5. Connectors - putting them, turning them",
     ":/help3d/connector.gif",
     "<p><b>Putting one:</b> choose <i>Connector</i> and click the face it sits on. It points out of that face - its "
     "cable leaves it that way, straight for the <i>Lead</i> length before it may turn. With the harness's parts "
     "listed on the right, choose a part first and the connector is that part's (click again elsewhere to move it).</p>"
     "<p><b>Turning one:</b> select it (<i>Select</i>, click it). The <i>Connector</i> box appears on the right:</p>"
     "<ul>"
     "<li><i>Points</i> - which way it points: out of its face, or along an axis (+X, -X, +Y, -Y, up, down).</li>"
     "<li><i>Turned</i> and <i>Tilted</i> - point it any way at all: round the upright, and up or down from level.</li>"
     "<li><i>Spun</i> - round its own axis: how its rectangular housing lies. <b>R</b> in the picture spins it a "
     "quarter turn.</li>"
     "</ul>"
     "<p>The cable follows: it leaves the connector the new way.</p>"},
    {"6. Route a cable from connector to connector",
     ":/help3d/route.gif",
     "<ol>"
     "<li>Choose <b>Route</b> and click the first connector.</li>"
     "<li>Click the places the cable runs over - each click is a corner of it. A cable follows the pointer to show "
     "where it would go next.</li>"
     "<li>Click the other connector: the cable is laid, and its length is in the table.</li>"
     "</ol>"
     "<p><i>Esc</i> gives up the cable being laid; <i>Backspace</i> takes back its last corner. A double-click ends it at "
     "a new branch instead of a connector. A cable can also start or end at a branch or a clip.</p>"},
    {"7. Bend the cable",
     ":/help3d/bend.gif",
     "<p><b>A cable bends at the corners you click</b> while laying it - each one rounded on the bend radius, as a real "
     "cable bends, rather than turning on a point.</p>"
     "<p><b>Bend R</b> is the tightest it may bend. <i>Auto</i> is six times its diameter; type a radius to bend wider "
     "or tighter. <b>&Oslash;</b> is the diameter (the table sets one cable's own). A corner with no room for its bend "
     "is counted under the table.</p>"
     "<p><b>Changing your mind:</b> while laying, <i>Backspace</i> takes back the last corner. Once laid, <i>Undo</i> "
     "(Ctrl+Z) takes a change back. To take a laid cable another way, <i>Lock view</i> and drag it: a corner is "
     "put where you take hold of it, and moves with the pointer (topic 10). Or delete it and lay it again.</p>"},
    {"8. Branch - where a cable divides",
     ":/help3d/branch.gif",
     "<p>A <b>branch</b> is a point where a cable divides - one bundle coming in, two or more going out, as at a "
     "breakout or a Y. It is drawn as a small yellow block.</p>"
     "<p><b>To make one:</b> choose <i>Branch</i> and click on a laid cable. The cable is divided there into two, each "
     "with its own length. Then choose <i>Route</i>, click the branch, and lay the new cable from it to the next "
     "connector.</p>"
     "<p>A branch can also be put on a face on its own and cables laid to and from it, and a double-click while "
     "laying ends a cable at a new branch.</p>"},
    {"9. Clip - where a cable is held",
     ":/help3d/clip.gif",
     "<p>A <b>clip</b> is a point where a cable is held to the structure - a P-clip, a cable tie on a mount, a "
     "saddle. It is drawn as a small green block.</p>"
     "<p><b>To put one:</b> choose <i>Clip</i> and click on a laid cable where it is held. The cable is divided there, "
     "so each piece's length - from the connector to the clip, from clip to clip - is known. The clip goes exactly "
     "where the cable is drawn: the cable does not jump to it. A clip can also be put on a face first, and cables laid "
     "through it with <i>Route</i>.</p>"
     "<p><b>A clip holds the cable only at its point.</b> The cable either side of it is still free: drag it with the "
     "view locked and it bends round, while the clip and the cable on its other side stay where they are.</p>"
     "<p>Select a clip (or a branch) to see the length from it to each end of the harness on the status line.</p>"},
    {"10. Lock view - moving cables and connectors",
     ":/help3d/lock.gif",
     "<p><b>Lock view</b> (top row, or the padlock on the wheel) holds the view still: the mouse no longer turns, "
     "moves or zooms it. Instead a <b>drag moves what is under the pointer</b> - across the view and up and down it, "
     "at the depth it is:</p>"
     "<ul>"
     "<li><b>a cable</b> - taken hold of anywhere along it, a corner is put there and moved: the cable bends round "
     "it. Taken by a corner it has, that corner moves.</li>"
     "<li><b>a connector, a branch or a clip</b> - it moves, and its cables with it.</li>"
     "</ul>"
     "<p>A cable keeps its length as it is moved - the length shown on it stays the same, its bends and all: the rest "
     "of it is pulled along behind the point, its far end held, and it goes no further than it reaches.</p>"
     "<p>Each drag is one change: <i>Undo</i> takes it back. To move something the other way - nearer or further - "
     "unlock, turn the view, lock again and drag. <i>View locked</i> again, or the padlock, unlocks.</p>"},
    {"11. Full screen",
     ":/help3d/fullscreen.gif",
     "<p><i>Full screen</i> (top right of the view), or <b>F11</b>, opens the 3D view in a window of its own, filling "
     "the screen - with every row and the panel on the right, so nothing is lost. It is a window still: drag it by its "
     "title bar, make it smaller, or put it on another screen. <b>F11</b>, <i>Back</i> or closing the window "
     "returns it to where it was.</p>"},
    {"12. Measuring - point to point, surface to surface",
     ":/help3d/measure.gif",
     "<p>Choose <i>Measure</i>, click one end, then the other. The distance is written by the ruler, and in full at the "
     "foot of the picture: straight, along X, Y and Z, and across each plane.</p>"
     "<p><b>Surface to surface</b> (the arrow on <i>Measure</i>, or the box beside the tools): click a face, then the "
     "face opposite - the ruler goes square from the second face back to the first, so it is the gap between two faces "
     "however far apart along them you clicked. <i>Example:</i> the inside of a box 40 deep - click the floor, then "
     "the lid anywhere: 40.0 mm. The two faces measured between are lit (orange; blue on the ruler selected).</p>"
     "<p><b>Rulers stay</b> until you take them away: click one (it lights) and press <i>Delete</i>, or right-click it "
     "and <i>Delete this measure</i>. <i>Remove all measures</i> on any right-click menu takes every one away (it is "
     "greyed while there are none). <i>Esc</i> gives up only a ruler you are in the middle of making.</p>"},
    {"13. Moving and pulling a cable - its length kept",
     ":/help3d/pull.gif",
     "<p><b>Move:</b> choose <i>Move</i> - the wheel turns into arrows along X, Y and Z (red, green, blue) and turns "
     "round each, each arrow the way its axis runs on the screen - and click a connector, branch or clip. On the right: "
     "where it is (X, Y, Z), a step, its colour and turns of 15&deg; round each axis. The wheel's middle, or another "
     "tool, gives the view's wheel back. Drag it, too. Drag a "
     "<b>stretch of a cable</b> between two nodes - between a clip and a connector, say - and that stretch moves.</p>"
     "<p><b>The length is kept.</b> A cable pulled is pulled along behind the point like a real one: its length stays "
     "what it was (the figure on it does not change), its far end holds, and it goes no further than it reaches. "
     "<i>Example:</i> a 78 mm cable from a connector to a clip, the connector dragged across - the cable bends round, "
     "still 78 mm.</p>"
     "<p><b>To make it longer:</b> select a cable and press "
     "<i>Add length</i> (or right-click it) and say by how much - it is laid looser, bowed off the face it lies on, its "
     "ends where they are.</p>"
     "<p><b>Through clips:</b> a clip guides the cable - pulled further than it reaches, the cable slides through the "
     "clip, the stretch beyond it giving up length until it is straight; the cable as a whole stays as long as it was.</p>"
     "<p><b>Which way a drag goes:</b> across the view - right, left, up, down as the pointer goes. Hold <i>Ctrl</i> and "
     "move the pointer up or down to push it away from you or pull it toward you; hold <i>Shift</i> to slide a node "
     "over the model's faces. <i>Esc</i> gives up what is half done, then puts the tool down (Select again).</p>"
     "<p><b>Stretch:</b> take hold of a cable and pull - it is pulled tight through its connector, which stays where "
     "it is, and the rest is cut off; every other cable stays as it was. A cable put in taut is stiff as it is pulled: "
     "it bows round smoothly and does not kink or loop (see Tension and gravity).</p>"},
    {"14. Connectors - forms, Tab, Same as, colours",
     ":/help3d/connforms.gif",
     "<p><b>Its form:</b> the arrow on <i>Connector</i> chooses the form the next ones are put in - rectangular, "
     "cylindrical, T (a flange and a body), right angle, or a D-sub shell. Select one put already and change its form and "
     "size on the right. Each size has an <b>A</b> beside it: on, it is Auto (from the cable's diameter and the lead) "
     "and the field shows what that comes to; off, type your own.</p>"
     "<p><b>Tab:</b> with <i>Connector</i> chosen, the connector follows the pointer over the model. Press <i>Tab</i> "
     "and it stays where it is - set its form, size and how it lies on the right - then click (or <i>Enter</i>) to put "
     "it there. <i>Tab</i> again or <i>Esc</i> lets it follow the pointer again.</p>"
     "<p><b>Same as:</b> right-click a connector, <i>Same as...</i>, then click another - the first is made like it: "
     "its form, size, how it lies and its colour.</p>"
     "<p><b>Colours:</b> right-click a connector or a cable and <i>Colour...</i>.</p>"
     "<p><b>Front or back:</b> a connector fitted through a panel is mated outside and has its wires inside - set "
     "<i>Cable out of: Its back</i> on the right (or right-click it, <i>Cable out of its back</i>) and the cable leaves "
     "behind its face, through the cutout. With <i>Branch</i> chosen, click a connector and choose <i>from the front</i> "
     "or <i>from the back</i>: a cable is started from there, as with <i>Route</i>. The cable always comes straight out of "
     "the end of the connector's body (a cylinder's flat end), never through its side.</p>"
     "<p><b>Never through a solid:</b> a cable that would pass through the model is laid over it - lifted over what is "
     "low, and taken up and over a wall or a rim - its length the length over it.</p>"
     "<p>A clip is drawn as a band round the cable with its foot on the face; a route given up (Esc) takes away the "
     "branch it was started from. A <b>branch</b> gets heatshrink: a sleeve over the cable either side of it, and one on "
     "each leg out of it. The table's last row is the <b>total</b> of every cable's length.</p>"},
    {"15. Display - background and shading",
     ":/help3d/display.gif",
     "<p><i>Display</i> (on the ribbon, or <i>Options</i> on the wheel) sets what is behind the picture - dark, light, "
     "a gradient either way, or a colour of your own (kept for next time) - and how it is shaded: shaded, wireframe, "
     "and the model see-through so the harness shows in it. The <i>Opacity</i> slider sets the whole model; for one "
     "solid (or a harness part) on its own, right-click it, <i>Opacity of...</i>, and choose how solid it is.</p>"
     "<p>The view's name is in the picture's top left corner: Top, Bottom, Front, Back, Left, Right, Iso - or Custom "
     "once it is turned away from those.</p>"
     "<p>At the side of the window the 3D view is the picture alone, with <i>Refresh</i> and <i>Open below</i> over it; "
     "<i>Open below</i> puts it under the drawing with everything, and there <i>To the left</i> or <i>To the right</i> "
     "puts it at that side.</p>"
     "<p>Under the wheel is said what the pointer is on - a connector, a clip, a cable (its length and diameter), a "
     "solid of the model or a part of the harness.</p>"
     "<p><b>Saved with the drawing:</b> the route, the model it is routed in (found again where it was, or beside the "
     "drawing), and how it was shown - the view, gravity and its direction, the solids hidden or see-through, the "
     "section and the rulers.</p>"},
    {"16. Tension and gravity - where a cable falls",
     ":/help3d/gravity.gif",
     "<p><b>Tension</b> is how taut a cable is put in: Slack, Loose (0.5 N), Normal (2 N), Firm (10 N) or Taut (50 N). "
     "<i>Cable tension</i> sets it for every cable with none of its own. One cable's own: click it and choose its "
     "<i>Tension</i> in the <b>Cable</b> box on the right (or in the table's <i>Tension</i> column); <i>As all cables</i> "
     "takes it back to the common one. A taut cable is stiff: pulled or moved, it keeps its form and bows round smoothly instead of kinking "
     "or looping - and still follows the pointer; a slack one follows it loosely.</p>"
     "<p><b>Attach to surface</b> lays the cable selected (every cable, where none is) on the faces under it, the way "
     "gravity pulls (<i>Down</i>) - onto a wall beside it where there is nothing under it near - and holds it there. "
     "<i>Undo</i> takes it back, <i>Redo</i> (Ctrl+Y) puts it back.</p>"
     "<p><b>A bundle of wires:</b> set <i>Wires</i> and <i>Wire &Oslash;</i> (the Bundle settings) and the cables are "
     "drawn wire by wire, as thick as the wires pack to - <i>&Oslash;</i>'s <b>A</b> on, showing it (seven wires of "
     "0.6 mm: about 1.8 mm). One cable's own: right-click it, <i>Wires in it...</i>.</p>"
     "<p><b>The Cable box</b> (right, when a cable is selected) has what is its own: its <i>Length</i> - type one and it "
     "is laid so long, its ends where they are; its <i>&Oslash;</i> (A: as all cables, or as its wires pack to); its "
     "<i>Wires</i> and <i>Wire &Oslash;</i>; <i>Wire colours</i> - each its own (<i>Each...</i> to choose each wire's) "
     "or all one colour; its <i>Tension</i>; its <i>Colour</i>.</p>"
     "<p><b>A real harness:</b> open it in the harness wizard (or place it on the sheet), then <i>Route from harness</i> "
     "(right, by Link): a connector for each of its parts, where the harness is drawn in 3D, and its cables between "
     "them - through a branch where a cable goes to several parts - each as long as the design says. Drag (Move) each "
     "connector to where it goes in the model, add clips, <i>Attach to surface</i>; then <i>Lengths to harness</i> gives "
     "each cable of the design its length along the route.</p>"
     "<p><b>Lead</b> is how far a cable comes straight out of a connector before it may bend - the back shell and strain "
     "relief a real cable cannot bend inside; past it the cable turns on its bend radius.</p>"
     "<p><b>Gravity</b> (the button by them) is a simulation preview: each cable hung under its own weight - worked out "
     "from its diameter, a bundle of wires being about 1.8 g/cm3. A cable is held at its connectors (straight out for "
     "the lead), its clips and branches, and wherever it lies on something - a face right under it. A cable laid along a "
     "floor therefore stays exactly as it is laid. Where a stretch of it has nothing under it - laid through the air on a "
     "work plane, or up a wall with nothing holding it - it falls: under tension it sags between where it is held "
     "(w s&sup2; / 8T at the middle of a span s - <i>example:</i> an 8 mm cable over 300 mm at Normal sags 5 mm, at Loose "
     "20 mm), slack it hangs by its own length, and it comes to rest on whatever it falls onto.</p>"
     "<p>What falls is drawn faint red where it would hang, with a red line straight down at its lowest point and how "
     "far (\"5.0 mm down\"). The status line names each cable that falls, how far, and - put in with a tension - how "
     "much more cable it needs to hang so. <i>Down</i> is the way gravity pulls in the model's axes: -Z, unless the "
     "model was drawn another way up. Nothing falls? Every cable is held or lies on something - add a clip where one "
     "does fall.</p>"},
    {"17. What every command does",
     "",
     "<p><b>The top row - what is shown, and the view</b></p><dl>"
     "<dt><i>The drawing / The harness wizard</i></dt><dd>Which harness to show: the one on the sheet, or the one open in "
     "the wizard.</dd>"
     "<dt><i>Parts as boxes / turned / extruded</i></dt><dd>How a part with no 3D model is made from its drawing.</dd>"
     "<dt><i>Refresh</i></dt><dd>Build it again from the drawing now (it is built again by itself as the drawing "
     "changes).</dd>"
     "<dt><i>Fit, Iso, Top, Front, Side</i></dt><dd>Everything in view; seen at an angle, from above, from the front, "
     "from the side.</dd>"
     "<dt><i>Wireframe</i></dt><dd>Edges only - to see through to what is behind.</dd>"
     "<dt><i>Turn, Tilt, Roll</i></dt><dd>The view's three turns in degrees, each all the way round.</dd>"
     "<dt><i>Wheel</i></dt><dd>Show or hide the navigation wheel.</dd>"
     "<dt><i>Full screen</i></dt><dd>The view in a window of its own, filling the screen.</dd>"
     "<dt><i>Lock view</i></dt><dd>Hold the view still, so a drag moves a cable, a corner or a connector.</dd>"
     "<dt><i>Flip</i></dt><dd>Look from the opposite side, through the same middle.</dd>"
     "<dt><i>Parts...</i></dt><dd>A list of the model's parts, each ticked to show it or unticked to hide it. A "
     "hidden part is not drawn, and clicks and cables go through it.</dd>"
     "</dl><p><b>Environment - the model routed in</b></p><dl>"
     "<dt><i>Load...</i></dt><dd>Read the model (STEP, STL, OBJ); a bar shows how far the reading has got, up to "
     "100.</dd>"
     "<dt><i>Clear</i></dt><dd>Take the model away (<i>Undo</i> brings it back).</dd>"
     "<dt><i>Show</i></dt><dd>Show or hide the model.</dd>"
     "<dt><i>Opacity</i></dt><dd>How solid the model is drawn - to see through it.</dd>"
     "<dt><i>Harness</i></dt><dd>Show or hide the harness from the drawing.</dd>"
     "<dt><i>Section X / Y / Z</i> and its slider</dt><dd>Cut the model open across that axis; the slider moves the "
     "cut. A faint plane shows the cut, only across the parts it goes through.</dd>"
     "</dl><p><b>Route - laying the cables</b></p><dl>"
     "<dt><i>Select</i></dt><dd>Pick a point of the model, or select a node or a cable (to turn a connector, or "
     "delete it with the Delete key).</dd>"
     "<dt><i>Connector</i></dt><dd>Put a connector on the face clicked.</dd>"
     "<dt><i>Branch</i></dt><dd>Divide a cable where it is clicked - where cables part or meet.</dd>"
     "<dt><i>Clip</i></dt><dd>Put a clip on a cable where it is held to the structure.</dd>"
     "<dt><i>Route</i></dt><dd>Lay a cable: the node it starts from, the corners, the node it ends at. A cable "
     "never goes through a solid of the model: where a straight run would, it is laid over it instead, and its length "
     "is the length over it.</dd>"
     "<dt><i>Delete</i></dt><dd>Take away the node or cable clicked.</dd>"
     "<dt><i>Move</i></dt><dd>Drag a node somewhere else on the model; its cables keep their lengths.</dd>"
     "<dt><i>Stretch</i></dt><dd>Take hold of a cable and pull: it is pulled tight through its connector (the nearer "
     "one, where it has a connector at each end) - as far as the pointer goes, up to straight - and what comes through "
     "is cut off, the cable that much shorter; the connector stays where it is, and so does every other cable. A "
     "connector with only one cable can be taken hold of itself; where it has two or more, click the cable, or select "
     "it first. A clip or a branch at the cable's other end holds it.</dd>"
     "<dt><i>Tension</i></dt><dd>How taut the cables are put in - Slack, Loose (0.5 N), Normal (2 N), Firm (10 N), "
     "Taut (50 N). <i>Cable tension</i> is for every cable that has none of its own. To give one cable its own, click "
     "it and choose its <i>Tension</i> in the Cable box on the right, or in the table's <i>Tension</i> column. The less "
     "tension, the more a cable sags between where it is held; Slack, it hangs by its own length.</dd>"
     "<dt><i>Route from harness</i></dt><dd>The harness open in the wizard put in as a route - its parts and cables - "
     "to move into the model.</dd>"
     "<dt><i>Attach</i></dt><dd>Attach to surface: lay the cable selected (all of them, where none is) on the faces "
     "under it, the way gravity pulls, and hold it there.</dd>"
     "<dt><i>Wires / Wire &Oslash;</i></dt><dd>A bundle of so many wires, each so thick - drawn wire by wire; "
     "&Oslash;'s A on, it is as thick as they pack to. Right-click a cable: <i>Wires in it...</i> for its own.</dd>"
     "<dt><i>Gravity</i></dt><dd>A simulation preview: each cable hung under its own weight - held at its connectors "
     "(straight out for the lead), clips and branches, pulled by its tension, resting on whatever it falls onto. Where "
     "it would hang is drawn faint red over it, and each place it falls from is marked with how far down; the status "
     "line says which cables fall, how far, and how much more cable a tension needs to hang so. <i>Down</i> is the way "
     "gravity pulls in the model's axes (-Z unless the model is drawn another way up).</dd>"
     "<dt><i>Measure</i></dt><dd>A ruler: click two points. It shows the distance straight between them, along X, Y "
     "and Z, and in the XY, YZ and XZ planes. Esc takes it away.</dd>"
     "<dt><i>&Oslash;</i></dt><dd>A cable's diameter (the table sets one cable's own).</dd>"
     "<dt><i>Bend R</i></dt><dd>The tightest a cable may bend; Auto, six times its diameter.</dd>"
     "<dt><i>Lead</i></dt><dd>How far a cable comes straight out of a connector before it may turn.</dd>"
     "<dt><i>Undo</i></dt><dd>Take back the last change to the route (Ctrl+Z).</dd>"
     "<dt><i>Redo</i></dt><dd>Put back what Undo took back (Ctrl+Y or Ctrl+Shift+Z).</dd>"
     "<dt><i>Help</i></dt><dd>This.</dd>"
     "<dt><i>Route to land / at / Through point</i></dt><dd>Where a click lands: the model's faces, or a "
     "level or upright plane at the place given - to lay a cable through the air.</dd>"
     "</dl><p><b>The panel on the right</b></p><dl>"
     "<dt>The parts list</dt><dd>The harness's parts: choose one, and the Connector tool puts that part.</dd>"
     "<dt><i>Link / Unlink</i></dt><dd>Make the node selected that part's, or no part's.</dd>"
     "<dt><i>Lengths to harness</i></dt><dd>Give each cable of the harness its length along the route.</dd>"
     "<dt><i>Connector: Points, Turned, Tilted, Spun</i></dt><dd>Which way the connector selected points, and how it "
     "is turned round its own axis.</dd>"
     "<dt>The table</dt><dd>Each cable, its length, and its diameter (type one in to set it).</dd>"
     "</dl><p><b>Keys</b></p><dl>"
     "<dt>Esc</dt><dd>Give up the cable being laid.</dd>"
     "<dt>Backspace</dt><dd>Take back the last corner of the cable being laid.</dd>"
     "<dt>Delete</dt><dd>Delete the node or cable selected.</dd>"
     "<dt>R</dt><dd>Spin the connector selected a quarter turn.</dd>"
     "<dt>Ctrl+Z</dt><dd>Undo.</dd>"
     "<dt>Ctrl+Y</dt><dd>Redo.</dd>"
     "<dt>F11</dt><dd>Full screen, and back.</dd>"
     "</dl>"},
    {"18. Words used here",
     "",
     "<dl>"
     "<dt><b>Environment</b></dt><dd>The CAD model the harness is routed in (STEP, STL or OBJ).</dd>"
     "<dt><b>Node</b></dt><dd>A point of the route: a connector, a branch or a clip.</dd>"
     "<dt><b>Connector</b></dt><dd>An end of the route, on a face of the model; it points the way its cable leaves "
     "it.</dd>"
     "<dt><b>Branch</b></dt><dd>Where a cable divides into two or more.</dd>"
     "<dt><b>Clip</b></dt><dd>Where a cable is held to the structure.</dd>"
     "<dt><b>Bundle / cable</b></dt><dd>What runs between two nodes, through the corners clicked; its length is in the "
     "table.</dd>"
     "<dt><b>Corner</b></dt><dd>A point clicked while laying; the cable bends there.</dd>"
     "<dt><b>&Oslash;</b></dt><dd>The cable's diameter.</dd>"
     "<dt><b>Bend R</b></dt><dd>The tightest the cable may bend (Auto: six times its diameter).</dd>"
     "<dt><b>Lead</b></dt><dd>How far a cable comes straight out of a connector before it may turn.</dd>"
     "<dt><b>Work plane</b></dt><dd>A level or upright plane clicks land on, for laying a cable through the air.</dd>"
     "<dt><b>Section</b></dt><dd>A cut through the model, to see and click inside it.</dd>"
     "</dl>"},
};

} // namespace

QStringList View3DHelpDialog::recordings() {
    QStringList out;
    for (const Topic& t : kTopics)
        if (*t.gif) out << QString::fromLatin1(t.gif);
    return out;
}

View3DHelpDialog::View3DHelpDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("3D view - how to"));
    resize(1240, 820);
    auto* lay = new QHBoxLayout(this);
    auto* left = new QVBoxLayout;
    topics_ = new QListWidget(this);
    topics_->setObjectName(QStringLiteral("help3dTopics"));
    for (const Topic& t : kTopics) topics_->addItem(QString::fromLatin1(t.title));
    topics_->setFixedWidth(290);
    left->addWidget(topics_, 1);
    // TO TRY IT: the example the recordings use, loaded into the view.
    auto* example = new QPushButton(QStringLiteral("Open the example"), this);
    example->setObjectName(QStringLiteral("help3dExample"));
    example->setToolTip(QStringLiteral("Load the example model into the 3D view - a plate with two boxes and a rib - to try "
                                       "each step on"));
    connect(example, &QPushButton::clicked, this, [this] { emit exampleRequested(); });
    left->addWidget(example);
    lay->addLayout(left);

    auto* right = new QVBoxLayout;
    picture_ = new QLabel(this);
    picture_->setObjectName(QStringLiteral("help3dPicture"));
    picture_->setAlignment(Qt::AlignCenter);
    picture_->setMinimumSize(320, 200);
    picture_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    picture_->setStyleSheet(QStringLiteral("QLabel{background:#1e2228;color:#8fa2b8;}"));
    picture_->installEventFilter(this);
    right->addWidget(picture_, 1);
    auto* controls = new QHBoxLayout;
    pause_ = new QToolButton(this);
    pause_->setObjectName(QStringLiteral("help3dPause"));
    pause_->setText(QStringLiteral("Pause"));
    pause_->setCheckable(true);
    controls->addWidget(pause_);
    again_ = new QToolButton(this);
    again_->setObjectName(QStringLiteral("help3dRestart"));
    again_->setText(QStringLiteral("From the start"));
    controls->addWidget(again_);
    controls->addStretch(1);
    right->addLayout(controls);
    steps_ = new QLabel(this);
    steps_->setObjectName(QStringLiteral("help3dSteps"));
    steps_->setWordWrap(true);
    steps_->setTextFormat(Qt::RichText);
    steps_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("help3dStepsArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(steps_);
    scroll->viewport()->installEventFilter(this);
    stepsArea_ = scroll;
    right->addWidget(scroll, 0);
    lay->addLayout(right, 1);

    movie_ = new QMovie(this);
    movie_->setCacheMode(QMovie::CacheAll);
    connect(topics_, &QListWidget::currentRowChanged, this, [this](int row) { showTopic(row); });
    connect(pause_, &QToolButton::toggled, this, [this](bool on) {
        movie_->setPaused(on);
        pause_->setText(on ? QStringLiteral("Play") : QStringLiteral("Pause"));
    });
    connect(again_, &QToolButton::clicked, this, [this] {
        movie_->stop();
        movie_->start();
        if (pause_->isChecked()) pause_->setChecked(false);
    });
    topics_->setCurrentRow(0);
}

int View3DHelpDialog::topicCount() const { return static_cast<int>(std::size(kTopics)); }

int View3DHelpDialog::currentTopic() const { return topics_->currentRow(); }

void View3DHelpDialog::showTopic(int index) {
    if (index < 0 || index >= topicCount()) return;
    if (topics_->currentRow() != index) {
        topics_->setCurrentRow(index);   // comes back here
        return;
    }
    const Topic& t = kTopics[index];
    movie_->stop();
    const bool recorded = *t.gif;
    pause_->setVisible(recorded);
    again_->setVisible(recorded);
    native_ = QSize();
    if (recorded) {
        movie_->setFileName(QString::fromLatin1(t.gif));
        native_ = QImageReader(QString::fromLatin1(t.gif)).size();
        picture_->setMovie(movie_);
        fitPicture();
        movie_->start();
        if (pause_->isChecked()) pause_->setChecked(false);
    } else {
        picture_->setMovie(nullptr);
        picture_->setText(QString::fromLatin1(t.title).section(QLatin1Char(' '), 1));
    }
    steps_->setText(QString::fromUtf8(t.steps));
    fitWindow();
}

bool View3DHelpDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize) {
        if (watched == picture_) fitPicture();
        else if (stepsArea_ && watched == stepsArea_->viewport()) fitText();
    }
    return QDialog::eventFilter(watched, event);
}

void View3DHelpDialog::fitPicture() {
    if (!native_.isValid() || !movie_) return;
    const QSize room = picture_->size();
    QSize s = native_;
    if (s.width() > room.width() || s.height() > room.height()) s = native_.scaled(room, Qt::KeepAspectRatio);
    if (s.isValid() && s != movie_->scaledSize()) movie_->setScaledSize(s);
}

void View3DHelpDialog::fitText() {
    if (!stepsArea_ || fitting_) return;
    fitting_ = true;
    // AS TALL AS ALL ITS WORDS at the width there is - the window cannot be made smaller than that - up to most of the
    // screen, past which it scrolls.
    const int width = std::max(200, stepsArea_->viewport()->width());
    const int words = steps_->heightForWidth(width) + 6;
    const QRect screen = this->screen() ? this->screen()->availableGeometry() : QRect(0, 0, 1600, 1000);
    stepsArea_->setMinimumHeight(std::min(words, screen.height() * 55 / 100));
    fitting_ = false;
}

void View3DHelpDialog::fitWindow() {
    // THE WINDOW TO THE RECORDING: wide and tall enough for it whole at its own size, and for every word under it - as
    // far as the screen goes.
    const QRect screen = this->screen() ? this->screen()->availableGeometry() : QRect(0, 0, 1600, 1000);
    const int column = std::max(560, native_.isValid() ? native_.width() : 640);
    const int words = steps_->heightForWidth(column) + 6;
    const int side = topics_->width() + 40;
    const int controls = pause_->sizeHint().height() + 12;
    QSize want(side + column, (native_.isValid() ? native_.height() : 420) + controls + std::min(words, screen.height() * 55 / 100) + 40);
    want = want.boundedTo(screen.size() - QSize(20, 40));
    setMinimumSize(std::min(side + 560, want.width()), std::min(200 + controls + std::min(words, screen.height() * 55 / 100) + 40, want.height()));
    resize(want.expandedTo(minimumSize()));
    fitText();
    fitPicture();
}

} // namespace Edraw_Desktop
