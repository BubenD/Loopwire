// Drawing templates and where the application keeps its files.
#include "harness/PartsLibrary.h"
#include "io/AppPaths.h"
#include "model/Block.h"
#include "model/Document.h"
#include "model/DrawingTemplate.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++failures;
    }
}

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

} // namespace

int run_template_tests() {
    std::printf("template and folder tests:\n");

    // ---- The three folders ----
    {
        const QString lib = AppPaths::libraryDir();
        const QString tpl = AppPaths::templateDir();
        const QString bld = AppPaths::builderDir();
        std::printf("    (library: %s)\n", qPrintable(QFileInfo(lib).fileName()));
        check(lib.endsWith(QLatin1String("/library")), "folders: libraries go in library");
        check(tpl.endsWith(QLatin1String("/template")), "folders: templates go in template");
        check(bld.endsWith(QLatin1String("/schbuilder")), "folders: builder presets go in schbuilder");
        // Asking for one creates it, so nothing has to remember to.
        for (const QString& d : {lib, tpl, bld})
            check(QDir(d).exists(), "folders: asking for one is enough to have it");
        // All three under the one root, and all three different.
        check(lib.startsWith(AppPaths::root()) && tpl.startsWith(AppPaths::root()) &&
                  bld.startsWith(AppPaths::root()),
              "folders: all under the application's own folder");
        check(lib != tpl && tpl != bld && lib != bld, "folders: and each its own");

        // A file in an older place is MOVED, not copied - two copies of a library is how the wrong
        // one gets edited.
        const QString from = AppPaths::root() + QStringLiteral("/adopt-me.tmp");
        const QString to = tpl + QStringLiteral("/adopted.tmp");
        QFile::remove(from);
        QFile::remove(to);
        {
            QFile f(from);
            check(f.open(QIODevice::WriteOnly), "adopt: a file to move");
            f.write("x");
            f.close();
        }
        check(AppPaths::adopt(from, to), "adopt: it moves");
        check(!QFile::exists(from) && QFile::exists(to), "adopt: and the old one is gone");
        // It never overwrites: a file already in its place wins.
        {
            QFile f(from);
            f.open(QIODevice::WriteOnly);
            f.write("y");
            f.close();
        }
        check(!AppPaths::adopt(from, to), "adopt: it will not overwrite what is already there");
        check(QFile::exists(from), "adopt: and leaves the other one alone rather than losing it");
        QFile::remove(from);
        QFile::remove(to);
    }

    // ---- The sheets ----
    {
        const TemplateSheet* a3 = templateSheetByName(QStringLiteral("A3"));
        check(a3 && near(a3->widthMm, 420.0) && near(a3->heightMm, 297.0),
              "sheets: A3 is 420 by 297");
        check(templateSheetByName(QStringLiteral("a3")) != nullptr,
              "sheets: found whatever the case");
        check(templateSheetByName(QStringLiteral("nope")) == nullptr,
              "sheets: and nothing invented for a name that is not one");
        // Portrait swaps which way round it is drawn, without changing the size it IS.
        TemplateSheet p = *a3;
        p.landscape = false;
        check(near(p.drawnWidthMm(), 297.0) && near(p.drawnHeightMm(), 420.0),
              "sheets: portrait is the same sheet the other way up");
        check(near(p.widthMm, 420.0), "sheets: and the size itself is untouched");
    }

    // ---- The zone frame: numbers along the top and bottom, letters down the sides ----
    {
        DrawingTemplate t;   // A3 landscape, 50 mm zones, 8 mm band by default
        const int cols = templateZoneColumns(t.sheet, t.zones);
        const int rows = templateZoneRows(t.sheet, t.zones);
        std::printf("    (A3: %d x %d zones)\n", cols, rows);
        // 420 - 16 = 404 across, 297 - 16 = 281 up: 8 and 6 divisions come nearest to 50 mm.
        check(cols == 8 && rows == 6, "zones: the count is what comes nearest the size asked for");
        // Equal divisions, so the real size is not the nominal one - and that is the point.
        const double cw = (t.sheet.drawnWidthMm() - 2 * t.zones.bandMm) / cols;
        check(cw > 45.0 && cw < 55.0, "zones: divisions are equal and near the nominal");

        // Labels.
        check(templateZoneColumnLabel(0) == QLatin1String("1") &&
                  templateZoneColumnLabel(7) == QLatin1String("8"),
              "zones: columns are numbered from 1");
        check(templateZoneRowLabel(0) == QLatin1String("A") &&
                  templateZoneRowLabel(1) == QLatin1String("B"),
              "zones: rows are lettered from A");
        // I and O are skipped: printed small on a border they read as 1 and 0.
        QString letters;
        for (int i = 0; i < 24; ++i) letters += templateZoneRowLabel(i);
        std::printf("    (row letters: %s)\n", qPrintable(letters));
        check(!letters.contains(QLatin1Char('I')) && !letters.contains(QLatin1Char('O')),
              "zones: I and O are left out, so they cannot be read as 1 and 0");
        check(letters.size() == 24, "zones: which leaves 24 letters");
        check(templateZoneRowLabel(24) == QLatin1String("AA") &&
                  templateZoneRowLabel(25) == QLatin1String("AB"),
              "zones: past the alphabet it doubles up");

        // Which zone a point is in. A1 is the top-left corner with the letters running down.
        check(templateZoneAt(t, Vec2{12.0, 289.0}) == QLatin1String("A1"),
              "zones: the top-left corner is A1");
        check(templateZoneAt(t, Vec2{12.0, 12.0}) == QLatin1String("F1"),
              "zones: the bottom-left is the last letter");
        check(templateZoneAt(t, Vec2{412.0, 12.0}) == QLatin1String("F8"),
              "zones: and the bottom-right is the last of both");
        check(templateZoneAt(t, Vec2{210.0, 148.5}) == QLatin1String("C5"),
              "zones: the middle of the sheet");
        // Outside the border - in the label band, or off the sheet - belongs to no zone.
        check(templateZoneAt(t, Vec2{3.0, 148.5}).isEmpty(),
              "zones: the label band itself is not in a zone");
        check(templateZoneAt(t, Vec2{-50.0, 0.0}).isEmpty(), "zones: nor is anywhere off the sheet");

        // A at the bottom instead, for a sheet that wants it that way.
        DrawingTemplate up = t;
        up.zones.lettersFromTop = false;
        check(templateZoneAt(up, Vec2{12.0, 12.0}) == QLatin1String("A1"),
              "zones: with A at the bottom, the bottom-left is A1");
        check(templateZoneAt(up, Vec2{12.0, 289.0}) == QLatin1String("F1"),
              "zones: and the top-left is the last letter");

        // Switched off: no frame drawn, and nothing is in a zone.
        DrawingTemplate off = t;
        off.zones.enabled = false;
        check(templateZoneAt(off, Vec2{210.0, 148.5}).isEmpty(),
              "zones: switched off, nothing is in a zone");
        const int withFrame = defaultSheetContent(t).size();
        const int without = defaultSheetContent(off).size();
        std::printf("    (sheet: %d objects with the frame, %d without)\n", withFrame, without);
        check(withFrame > without + 2 * (cols + rows) - 4,
              "zones: the frame puts a label and a tick on the sheet for every division");

        // A bigger sheet gets more zones; a smaller one fewer.
        DrawingTemplate a1 = t;
        a1.sheet = *templateSheetByName(QStringLiteral("A1"));
        DrawingTemplate a4 = t;
        a4.sheet = *templateSheetByName(QStringLiteral("A4"));
        check(templateZoneColumns(a1.sheet, a1.zones) > cols &&
                  templateZoneColumns(a4.sheet, a4.zones) < cols,
              "zones: the count follows the sheet size");
        // And they survive a round trip.
        DrawingTemplate saved = t;
        saved.zones.sizeMm = 40.0;
        saved.zones.bandMm = 10.0;
        saved.zones.lettersFromTop = false;
        const QString zpath = DrawingTemplate::pathFor(QStringLiteral("zone-test"));
        check(saved.save(zpath), "zones: a template with a frame writes");
        DrawingTemplate back;
        check(back.load(zpath), "zones: and reads back");
        check(back.zones.enabled && near(back.zones.sizeMm, 40.0) &&
                  near(back.zones.bandMm, 10.0) && !back.zones.lettersFromTop,
              "zones: with every setting of the frame intact");
        QFile::remove(zpath);
    }

    // ---- A template round trip ----
    {
        DrawingTemplate t;
        t.name = QStringLiteral("Test house style");
        t.description = QStringLiteral("for the tests");
        t.author = QStringLiteral("nobody");
        t.revision = QStringLiteral("B");
        t.sheet = *templateSheetByName(QStringLiteral("A2"));
        t.layers = Document::defaultLayers();
        Layer extra;
        extra.name = QStringLiteral("harness");
        extra.color = QStringLiteral("#7ee0a0");
        extra.weight = 0.5;
        extra.locked = true;
        t.layers.push_back(extra);
        t.activeLayer = QStringLiteral("harness");
        t.unit = QStringLiteral("mm");
        t.textHeight = 3.5;
        t.dimDecimals = 1;
        t.gaugeAwg = true;
        t.content = defaultSheetContent(t.sheet);
        check(t.hasContent(), "template: a default sheet has something on it");

        const QString path = DrawingTemplate::pathFor(t.name);
        std::printf("    (saved as %s)\n", qPrintable(QFileInfo(path).fileName()));
        check(path.endsWith(QLatin1String(".etp")), "template: the extension is .etp");
        check(path.contains(QLatin1String("/template/")), "template: and it goes in the folder");
        check(t.save(path), "template: it writes");

        DrawingTemplate back;
        check(back.load(path), "template: and reads back");
        check(back.name == t.name && back.description == t.description &&
                  back.author == t.author && back.revision == t.revision,
              "template: what it is called survives");
        check(back.sheet.name == QLatin1String("A2") && near(back.sheet.widthMm, 594.0),
              "template: the sheet survives");
        check(back.layers.size() == t.layers.size(), "template: every layer survives");
        bool sawHarness = false;
        for (const Layer& l : back.layers)
            if (l.name == QLatin1String("harness"))
                sawHarness = l.color == QLatin1String("#7ee0a0") && near(l.weight, 0.5) && l.locked;
        check(sawHarness, "template: with its colour, its weight and its lock");
        check(back.activeLayer == QLatin1String("harness"), "template: and which one is active");
        check(near(back.textHeight, 3.5) && back.dimDecimals == 1 && back.gaugeAwg,
              "template: the defaults survive");
        check(back.content.size() == t.content.size(), "template: and everything on the sheet");
        check(!back.created.isEmpty(), "template: saving stamps the date");

        // Only what this application wrote is a template.
        const QString junk = AppPaths::templateDir() + QStringLiteral("/not-a-template.etp");
        {
            QFile f(junk);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write("{\"kind\":\"something-else\",\"name\":\"x\"}");
            f.close();
        }
        DrawingTemplate rejected;
        check(!rejected.load(junk),
              "template: a JSON file that is not a template is refused, not half-read");
        QFile::remove(junk);
        check(!rejected.load(AppPaths::templateDir() + QStringLiteral("/no-such-file.etp")),
              "template: and a missing file is refused too");

        // It shows up in the folder listing.
        bool listed = false;
        for (const QString& p : DrawingTemplate::installed())
            if (p == path) listed = true;
        check(listed, "template: a saved template is found in the folder");
        QFile::remove(path);

        // A name with a slash in it is still a file name, because a person will type one.
        const QString messy = DrawingTemplate::pathFor(QStringLiteral("A3 / rev B: draft"));
        check(!QFileInfo(messy).fileName().contains(QLatin1Char('/')) &&
                  !QFileInfo(messy).fileName().contains(QLatin1Char(':')),
              "template: a name that is not a file name is made into one");
    }

    // ---- The revision table is a TABLE, not a rectangle with lines drawn over it ----
    {
        DrawingTemplate t;
        t.revisions.enabled = true;
        t.revisions.rows = 4;
        const QJsonArray content = defaultSheetContent(t);
        // Find it: there is exactly one, and it says what it is.
        // An entity's meta is written flat into its object, so the keys are read straight off it.
        int tables = 0;
        QJsonObject meta;
        for (const auto& v : content) {
            const QJsonObject o = v.toObject();
            if (!o.value(QStringLiteral("revisionTable")).toBool()) continue;
            ++tables;
            meta = o;
        }
        check(tables == 1, "revision table: one object, not a heap of lines");
        check(meta.value(QStringLiteral("table")).toBool(),
              "revision table: it IS a table, so it behaves like every other table");
        const QStringList heads = t.revisions.headings();
        check(meta.value(QStringLiteral("cols")).toInt() == heads.size(),
              "revision table: a column per heading switched on");
        check(meta.value(QStringLiteral("rows")).toInt() == t.revisions.rows + 1,
              "revision table: the heading row plus a row per issue");
        const QJsonArray cells = meta.value(QStringLiteral("cells")).toArray();
        check(cells.size() == heads.size() * (t.revisions.rows + 1),
              "revision table: a cell for every row and column");
        bool headingsInRow0 = !heads.isEmpty();
        for (int c = 0; c < heads.size(); ++c)
            if (cells.at(c).toString() != heads.at(c)) headingsInRow0 = false;
        check(headingsInRow0, "revision table: the headings are the top row's cell text");
        bool restBlank = true;
        for (int i = heads.size(); i < cells.size(); ++i)
            if (!cells.at(i).toString().isEmpty()) restBlank = false;
        check(restBlank, "revision table: the rest is blank, to be filled in per issue");
        const QJsonArray colFrac = meta.value(QStringLiteral("colFrac")).toArray();
        double sum = 0.0;
        for (const auto& v : colFrac) sum += v.toDouble();
        check(colFrac.size() == heads.size() && near(sum, 1.0, 1e-9),
              "revision table: the column widths are fractions of the whole and add up");
        check(meta.value(QStringLiteral("headerRow")).toBool(),
              "revision table: its top row is a header row");
        // Switching a column off makes it a narrower table, not a wider one with a gap.
        DrawingTemplate fewer = t;
        fewer.revisions.showEco = false;
        fewer.revisions.showChecked = false;
        int cols2 = 0;
        for (const auto& v : defaultSheetContent(fewer)) {
            const QJsonObject m = v.toObject();
            if (m.value(QStringLiteral("revisionTable")).toBool())
                cols2 = m.value(QStringLiteral("cols")).toInt();
        }
        check(cols2 == heads.size() - 2, "revision table: a column switched off is not there at all");
    }

    // ---- Applying one to a drawing, and taking one off a drawing ----
    {
        DrawingTemplate t;
        t.name = QStringLiteral("apply me");
        t.layers = Document::defaultLayers();
        Layer extra;
        extra.name = QStringLiteral("border");
        extra.color = QStringLiteral("#8fb7ff");
        t.layers.push_back(extra);
        t.activeLayer = QStringLiteral("border");
        t.content = defaultSheetContent(t.sheet);

        Document doc;
        doc.entities().clear();
        // Something already drawn, which applying a template must not throw away.
        Entity mine;
        mine.type = EntityType::Line;
        mine.a = Vec2{0.0, 0.0};
        mine.b = Vec2{10.0, 0.0};
        mine.id = QStringLiteral("mine");
        doc.entities().push_back(mine);
        const size_t before = doc.entities().size();

        applyTemplate(t, doc);
        std::printf("    (applied: %zu object(s) on the sheet, %zu layer(s))\n",
                    doc.entities().size(), doc.layers().size());
        check(doc.entities().size() == before + static_cast<size_t>(t.content.size()),
              "apply: the template's content is added");
        bool keptMine = false;
        for (const Entity& e : doc.entities())
            if (e.id == QLatin1String("mine")) keptMine = true;
        check(keptMine, "apply: and what was already drawn is still there");
        check(doc.findLayer(QStringLiteral("border")) != nullptr, "apply: its layers are created");
        check(doc.activeLayer() == QLatin1String("border"), "apply: and the active one is set");
        // Every object of the frame gets an id of its own. It used to arrive with none, on the theory
        // that something downstream would fill them in - nothing did, and an object with no id cannot
        // be picked, moved or grouped.
        int blankIds = 0;
        QStringList seen;
        bool duplicated = false;
        for (const Entity& e : doc.entities()) {
            if (e.id.isEmpty()) ++blankIds;
            else if (seen.contains(e.id)) duplicated = true;
            else seen << e.id;
        }
        check(blankIds == 0, "apply: every object of the frame has an id");
        check(!duplicated, "apply: and no two share one");
        // And one group key over the lot, so the frame is picked and moved as one thing.
        QStringList groups;
        int stamped = 0;
        for (const Entity& e : doc.entities()) {
            const QString g = entityTemplateGroup(e);
            if (g.isEmpty()) continue;
            ++stamped;
            if (!groups.contains(g)) groups << g;
        }
        check(stamped == t.content.size(), "apply: every object of the frame is stamped");
        check(groups.size() == 1, "apply: with ONE group key, so it moves as one");
        check(entityTemplateGroup(*std::find_if(doc.entities().begin(), doc.entities().end(),
                                               [](const Entity& e) {
                                                   return e.id == QLatin1String("mine");
                                               }))
                  .isEmpty(),
              "apply: and what was already drawn is not part of it");

        // Capturing goes the other way, and round-trips.
        const DrawingTemplate taken = templateFromDocument(doc, QStringLiteral("taken"));
        check(taken.content.size() == static_cast<int>(doc.entities().size()),
              "capture: everything on the sheet comes across");
        check(taken.layers.size() == doc.layers().size(), "capture: with the layers");
        check(taken.name == QLatin1String("taken"), "capture: under the name asked for");
        bool anyId = false;
        for (const auto v : taken.content)
            if (v.toObject().contains(QStringLiteral("id"))) anyId = true;
        check(!anyId, "capture: and without the ids, which belong to that drawing and not to a "
                      "template");
    }

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}

// ---- Blocks -------------------------------------------------------------------------------------

int run_block_tests() {
    std::printf("block tests:\n");
    const int before = failures;

    // A block out of a selection: stored about its base point, so it lands where it is clicked.
    std::vector<Entity> sel;
    for (int i = 0; i < 3; ++i) {
        Entity e;
        e.type = EntityType::Line;
        e.a = Vec2{100.0 + i * 10.0, 200.0};
        e.b = Vec2{110.0 + i * 10.0, 210.0};
        e.id = QStringLiteral("src%1").arg(i);
        e.meta.insert(QStringLiteral("block"), QStringLiteral("someone else"));
        sel.push_back(e);
    }
    const BlockDef b = blockFromEntities(QStringLiteral("bracket"), sel, Vec2{100.0, 200.0});
    check(b.entities.size() == 3, "block: every selected object goes in");
    check(b.name == QLatin1String("bracket"), "block: under the name asked for");
    {
        const std::vector<Entity> ents = partDrawingEntities(b.entities);
        check(!ents.empty() && near(ents[0].a.x, 0.0) && near(ents[0].a.y, 0.0),
              "block: stored about its base point, so the first corner is the origin");
        bool anyStamp = false, anyId = false;
        for (const auto v : b.entities) {
            const QJsonObject o = v.toObject();
            if (o.contains(QStringLiteral("id"))) anyId = true;
            if (o.value(QStringLiteral("meta")).toObject().contains(QStringLiteral("block")))
                anyStamp = true;
        }
        check(!anyId, "block: without the ids of the objects it was made from");
        check(!anyStamp, "block: and a copy of another block does not stay part of that one");
    }

    // Dropping it: stamped with the block, a key shared by this copy, and where it went.
    const std::vector<Entity> copy1 = blockInstanceEntities(b, Vec2{500.0, 50.0}, QString());
    const std::vector<Entity> copy2 = blockInstanceEntities(b, Vec2{700.0, 90.0}, QString());
    check(copy1.size() == 3 && copy2.size() == 3, "block: a copy is the whole block");
    check(near(copy1[0].a.x, 500.0) && near(copy1[0].a.y, 50.0),
          "block: dropped where it was asked for");
    check(entityBlockName(copy1[0]) == QLatin1String("bracket"), "block: stamped with the block");
    check(!entityBlockInstance(copy1[0]).isEmpty(), "block: and with a key for this copy");
    check(entityBlockInstance(copy1[0]) == entityBlockInstance(copy1[2]),
          "block: every object in one copy shares that key");
    check(entityBlockInstance(copy1[0]) != entityBlockInstance(copy2[0]),
          "block: and two copies do not share it");
    check(near(entityBlockOrigin(copy2[0]).x, 700.0),
          "block: a copy remembers where it was dropped");

    // Editing the block rebuilds every copy, each where it was.
    Document doc;
    doc.entities().clear();
    for (const Entity& e : copy1) doc.entities().push_back(e);
    for (const Entity& e : copy2) doc.entities().push_back(e);
    Entity other;
    other.type = EntityType::Circle;
    other.id = QStringLiteral("not-a-block");
    other.center = Vec2{0.0, 0.0};
    other.radius = 5.0;
    doc.entities().push_back(other);
    check(blockInstancesIn(doc, QStringLiteral("bracket")).size() == 2,
          "block: both copies are found on the sheet");

    BlockDef edited = b;
    Entity extra;
    extra.type = EntityType::Circle;
    extra.center = Vec2{5.0, 5.0};
    extra.radius = 2.0;
    QJsonObject eo = extra.toJson();
    eo.remove(QStringLiteral("id"));
    edited.entities.append(eo);
    const int rebuilt = updateBlockInstances(doc, edited);
    std::printf("    (rebuilt %d cop(ies); %zu object(s) on the sheet)\n", rebuilt,
                doc.entities().size());
    check(rebuilt == 2, "block: editing it rebuilds both copies");
    check(doc.entities().size() == 2 * 4 + 1, "block: each copy has the new object in it");
    bool keptOther = false;
    for (const Entity& e : doc.entities())
        if (e.id == QLatin1String("not-a-block")) keptOther = true;
    check(keptOther, "block: and what is not part of the block is left alone");
    // Each copy is still where it was.
    bool at500 = false, at700 = false;
    for (const Entity& e : doc.entities()) {
        if (entityBlockName(e) != QLatin1String("bracket")) continue;
        if (near(entityBlockOrigin(e).x, 500.0)) at500 = true;
        if (near(entityBlockOrigin(e).x, 700.0)) at700 = true;
    }
    check(at500 && at700, "block: and both are where they were dropped");

    // Files.
    const QString path = BlockDef::pathFor(QStringLiteral("bracket"));
    check(path.endsWith(QLatin1String(".ebk")), "block: the extension is .ebk");
    check(path.contains(QLatin1String("/block/")), "block: and it goes in the block folder");
    check(b.save(path), "block: it writes");
    BlockDef back;
    check(back.load(path), "block: and reads back");
    check(back.name == b.name && back.entities.size() == b.entities.size(),
          "block: with its name and its contents");
    check(!back.fromLibrary(), "block: a drawn block is not a library one");
    bool listed = false;
    for (const QString& p : BlockDef::installed())
        if (p == path) listed = true;
    check(listed, "block: and is found in the folder");
    QFile::remove(path);

    // A library part's drawing is a block too - that is where most blocks come from.
    {
        PartsCatalog cat;
        cat.setRemembering(false);
        LibraryPart& part = cat.active().add(PartKind::Resistor);
        part.mfrPn = QStringLiteral("R-100");
        part.description = QStringLiteral("100 ohm");
        const std::vector<BlockDef> libs = libraryBlocks(cat);
        std::printf("    (a resistor in the library gives %zu block(s))\n", libs.size());
        check(!libs.empty(), "library blocks: a part with a drawing is a block");
        check(libs.front().fromLibrary(), "library blocks: and says where it came from");
        check(libs.front().libraryPartId() == part.id, "library blocks: naming the part");
        check(libs.front().name.contains(QLatin1String("R-100")),
              "library blocks: called after the part");
        check(libs.front().name.contains(QLatin1String("schematic")),
              "library blocks: and after which of its drawings it is");
        // allBlocks puts the files and the library together, for the code that has to find a block by
        // name whatever it came from (opening one for editing, rebuilding a copy on a sheet).
        const std::vector<BlockDef> all = allBlocks(&cat);
        check(all.size() >= libs.size(), "library blocks: found together with the saved ones");
        // But the BLOCKS WINDOW asks for the saved ones alone, which is what it shows. A part in a
        // library is not a block the user made: it belongs to the library, it is edited there and
        // placed from there, and listing every drawing of every connected library in that window
        // buried the handful of blocks somebody actually made.
        const std::vector<BlockDef> mine = allBlocks(nullptr);
        bool anyFromLibrary = false;
        for (const BlockDef& b : mine)
            if (b.fromLibrary()) anyFromLibrary = true;
        check(!anyFromLibrary,
              "library blocks: asked for the saved blocks alone, no library drawing is among them");
        check(mine.size() + libs.size() == all.size(),
              "library blocks: and the two lists together are the whole of it");
    }

    const int failed = failures - before;
    if (failed == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failed);
    return failed;
}
