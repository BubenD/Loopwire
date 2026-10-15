#include "app/Editor.h"
#include "app/MainWindow.h"
#include "app/HarnessBuilderDialog.h"
#include "app/DrawingPreview.h"
#include "app/UiBits.h"
#include <QVBoxLayout>
#include "harness/HarnessBuilderBom.h"
#include "harness/CableSpec.h"
#include "harness/Harness3D.h"
#include "render/Render3D.h"
#include <QCheckBox>
#include "model/Document.h"
#include "render/EntityRenderer.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTableWidget>
#include <QToolButton>
#include <QMenu>
#include <QTimer>
#include <QContextMenuEvent>
#include <QTemporaryDir>
#include <QWheelEvent>
#include "geometry/Geometry.h"
#include <cmath>
#include <cstdio>

using namespace Loopwire;
namespace {
int failures = 0;
void check(bool ok, const char* what) { if (!ok) { ++failures; std::printf("  [FAIL] %s\n", what); } }
void events() { QApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QApplication::processEvents(); }
PartsLibrary library() {
    PartsLibrary lib;
    for (const auto& entry : std::vector<std::pair<QString, PartKind>>{{"connector", PartKind::Connector}, {"coax", PartKind::Cable}, {"tail", PartKind::Wire}, {"joint", PartKind::Splice}}) {
        LibraryPart p; p.id = entry.first; p.kind = entry.second; p.mfrPn = entry.first.toUpper() + QStringLiteral("-PN");
        if (p.kind == PartKind::Wire) {
            p.params["gaugeMode"] = "AWG"; p.params["gauge"] = "24"; p.params["colour"] = "RD-WH";
        }
        p.description = entry.first; p.unit = p.kind == PartKind::Cable || p.kind == PartKind::Wire ? "m" : "each";
        lib.parts.push_back(p);
    }
    return lib;
}
HarnessDef fixture() {
    HarnessDef d; d.physicalBom = true; d.layout.wireColorMode = QStringLiteral("actual");
    HarnessConnector from; from.id = "c1"; from.designator = "P1"; from.pinCount = 4; from.libPart = "connector";
    auto to = from; to.id = "c2"; to.designator = "P2"; to.side = HarnessSide::Right; d.connectors = {from, to};
    for (int role = 0; role < 2; ++role) {
        HarnessWire w; w.id = role == 0 ? "signal" : "shield"; w.bundle = "CX1"; w.kind = "coax";
        w.coaxRole = role == 0 ? "signal" : "shield"; w.color = role == 0 ? "WH-BL" : "BK";
        w.fromConn = from.id; w.toConn = to.id; w.fromPin = w.toPin = role + 1;
        w.libPart = "coax"; w.cutLengthMm = 1000;
        for (auto* end : {&w.fromTermination, &w.toTermination}) {
            end->via = "wire"; end->wireLibPart = "tail"; end->spliceLibPart = "joint";
            end->wireGauge = "0.5"; end->wireColor = "WH-BL"; end->lengthMm = 100;
        }
        d.wires.push_back(w);
    }
    return d;
}
double quantity(const std::vector<BomRow>& rows, const QString& id) {
    double sum = 0; for (const auto& row : rows) if (row.partId == id) sum += convertQuantity(row.qty, row.unit, id == "tail" || id == "coax" ? "m" : "each");
    return sum;
}
HarnessDef shieldFixture() {
    HarnessDef d;
    HarnessConnector a; a.id = "p1"; a.designator = "P1"; a.pinCount = 8; a.shieldLeft = true;
    HarnessConnector b; b.id = "p2"; b.designator = "P2"; b.side = HarnessSide::Right; b.pinCount = 4;
    HarnessConnector c = b; c.id = "p3"; c.designator = "P3"; c.order = 1; c.pinCount = 3;
    c.shieldRight = true; c.drainTo = "pin:p3:3"; c.drainVia = "wire"; c.drainGauge = "0.5"; c.drainColor = "WH-BL";
    d.connectors = {a, b, c}; d.layout.terminateUnconnected = true;
    int n = 0;
    for (auto pins : std::vector<std::pair<int,int>>{{1,1}, {2,2}, {3,3}, {5,4}, {4,1}, {8,2}, {6,7}}) {
        HarnessWire w; w.id = QString::number(++n); w.fromConn = "p1"; w.fromPin = pins.first;
        w.toConn = n <= 4 ? "p2" : n <= 6 ? "p3" : "p1"; w.toPin = pins.second;
        d.wires.push_back(w);
    }
    return d;
}
bool simpleRoute(const Entity& e) {
    if (e.type != EntityType::Wire) return true;
    for (size_t i = 1; i + 1 < e.points.size(); ++i) {
        const auto a = e.points[i] - e.points[i - 1], b = e.points[i + 1] - e.points[i];
        if (std::abs(a.x * b.y - a.y * b.x) < 1e-8 && a.x * b.x + a.y * b.y < -1e-8) return false;
    }
    for (size_t i = 0; i + 1 < e.points.size(); ++i)
        for (size_t j = i + 2; j + 1 < e.points.size(); ++j) {
            if (segmentSegmentIntersection(e.points[i], e.points[i + 1], e.points[j], e.points[j + 1])) return false;
            for (const Vec2 p : {e.points[j], e.points[j + 1]}) {
                const auto a = e.points[i], b = e.points[i + 1];
                if (std::abs(distance(a,p) + distance(p,b) - distance(a,b)) < 1e-8) return false;
            }
        }
    return true;
}
}
int run_schematicwizard_tests() {
    failures = 0; std::printf("schematic wizard tests:\n");
    const QString oldStyle = qApp->styleSheet();
    if (QFile style(QStringLiteral(":/loopwire.qss")); style.open(QIODevice::ReadOnly)) qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
    const auto lib = library(); auto d = fixture();
    {
        QScrollArea page; page.resize(500, 300); page.setWidgetResizable(true);
        auto* column = new QWidget; auto* layout = new QVBoxLayout(column);
        auto* upper = new QWidget(column); upper->setFixedHeight(400); layout->addWidget(upper);
        auto* grip = makeSectionGrip(upper, column); layout->addWidget(grip);
        auto* lower = new QWidget(column); lower->setFixedHeight(700); layout->addWidget(lower);
        auto* field = new QLineEdit(column); layout->addWidget(field);
        page.setWidget(column); page.show(); events();
        field->setFocus(); events();
        page.verticalScrollBar()->setValue(300); events();
        const int anchor = page.verticalScrollBar()->value();
        const QPointF local(4,4), global = grip->mapToGlobal(QPoint(4,4));
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(grip, &press);
        for (int dy : {-20, -40, -60}) {
            QMouseEvent move(QEvent::MouseMove, local + QPointF(0,dy), global + QPointF(0,dy), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(grip, &move); events();
            check(page.verticalScrollBar()->value() == anchor, "pulling a separator up resizes its section without scrolling the page");
        }
        QMouseEvent release(QEvent::MouseButtonRelease, local, global + QPointF(0,-60), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(grip, &release); events();
        check(upper->height() == 340 && page.verticalScrollBar()->value() == anchor, "separator drag preserves scroll position after release and deferred layout");
    }
    const auto shieldCase = shieldFixture(); const auto shieldBuild = buildHarness(shieldCase);
    bool labelled = false, exitsDown = false, halfJoint = false, connectedMarked = false;
    Vec2 pinOne; bool foundPin = false;
    for (const auto& entity : shieldBuild.entities) if (entity.type == EntityType::Connector) {
        const auto spec = connectorSpecFromMeta(entity.meta);
        if (spec.designator == "P3") { pinOne = connectorGeometry(entity.a, spec).pinEnd[0]; foundPin = true; }
    }
    for (const auto& entity : shieldBuild.entities) {
        if (entity.type == EntityType::Wire && entity.meta.value("shieldLeg").toString() == harnessOverallShieldKey("p3", HarnessSide::Right))
            labelled |= entity.meta.value("showGaugeName").toBool() && entity.meta.value("showColorName").toBool() &&
                        entity.meta.value("wireGauge") == QJsonValue("0.5") && entity.meta.value("wireColor") == QJsonValue("WH-BL");
        if (entity.type == EntityType::Screen && entity.meta.value("overallShield") == QJsonValue("P3"))
            exitsDown |= entity.meta.value("legDir") == QJsonValue("bottom");
        if (entity.type == EntityType::NotConnected && distance(entity.a, pinOne) < 1e-6) connectedMarked = true;
        if (entity.type == EntityType::Splice) halfJoint |= entity.meta.value("size").toDouble() == shieldCase.layout.spliceSize * .5;
        check(simpleRoute(entity), "shield example conductor never retraces or intersects itself");
    }
    check(labelled && exitsDown, "P3 hook-up drain shows gauge/colour and exits towards its lower pin");
    check(foundPin && !connectedMarked, "connected P3 pin 1 is never marked unconnected");
    int p3Unconnected = 0;
    for (const auto& entity : shieldBuild.entities) if (entity.type == EntityType::NotConnected)
        for (const auto& connector : shieldBuild.entities) if (connector.type == EntityType::Connector) {
            const auto spec = connectorSpecFromMeta(connector.meta);
            if (spec.designator == "P3") for (const auto& pin : connectorGeometry(connector.a, spec).pinEnd)
                if (distance(pin, entity.a) < 1e-6) ++p3Unconnected;
        }
    check(p3Unconnected == 0, "derived hook-up drain pin is connected even when its wire row was absent in the saved definition");
    check(halfJoint, "connection dots use half the previous size");
    {
        DrawingPreview preview(QString(), nullptr); preview.resize(1200, 600);
        preview.setEntities(harnessPreviewEntities(shieldCase, true)); preview.show(); events(); preview.fitToView(); events();
        preview.setKeepView(true); preview.grab();
        const auto beforeA = preview.screenAt({100,300}), beforeB = preview.screenAt({700,200});
        preview.resize(1200, 700); events(); preview.grab();
        check((preview.screenAt({100,300}) - beforeA).manhattanLength() < .01 &&
              (preview.screenAt({700,200}) - beforeB).manhattanLength() < .01,
              "resizing a kept drawing preview preserves drawing position and scale");
        QDir().mkpath("build/wizard-review"); preview.grab().save("build/wizard-review/p3-shield-routing.png");
    }
    for (double right : {160., 300., 700.}) for (int pitch : {5, 15, 40}) {
        auto narrow = fixture(); narrow.layout.rightX = right; narrow.layout.pinPitch = pitch;
        for (const auto& entity : buildHarness(narrow).entities) check(simpleRoute(entity), "coax routes remain simple in narrow and wide corridors");
    }
    Entity labelMask; labelMask.type = EntityType::Wipeout;
    labelMask.points = {{-12, -4}, {12, -4}, {12, 4}, {-12, 4}};
    labelMask.meta["labelMask"] = true; labelMask.meta["labelGroup"] = "label-1";
    Entity clear; clear.type = EntityType::Line; clear.a = {-14, -5}; clear.b = {14, -5};
    clear.meta["labelGroup"] = "label-1"; clear.meta["linetype"] = "HIDDEN";
    Document tieDoc; Editor tieEditor; tieEditor.setDocument(&tieDoc);
    for (double y : {-2.0, 0.0, 2.0}) { Entity wire; wire.type = EntityType::Wire; wire.a = {-40, y}; wire.b = {40, y}; wire.points = {wire.a, wire.b}; tieDoc.entities().push_back(wire); }
    auto physical = tieDoc.entities();
    for (auto& wire : physical) { wire.type = EntityType::Polyline; wire.meta["double"] = true; wire.meta["gap"] = 1.2; }
    auto tie = tieEditor.lacingTieFromBand({25, -6}, {25, 6});
    physical.insert(physical.end(), tie.begin(), tie.end()); physical.push_back(labelMask); physical.push_back(clear);
    const auto scene = buildHarness3D(physical, {});
    bool yellow = false, transparent = false, wrapped = false;
    std::vector<DrawItem3D> draw;
    for (const auto& item : scene.items) {
        yellow |= item.what == "Label" && item.color.red() > 240 && item.color.green() > 180;
        transparent |= item.what == "Clear label sleeve" && item.alpha < 0.3;
        V3 lo, hi; if (item.what == "Lacing tie" && item.mesh.bounds(lo, hi)) wrapped |= lo.z < -0.5 && hi.z > 0.5;
        draw.push_back({&item.mesh, item.color, item.alpha});
    }
    check(yellow && transparent && wrapped, "3D fittings: yellow label, clear sleeve and cord wrapped round the bundle");
    QImage model(900, 500, QImage::Format_ARGB32_Premultiplied);
    Camera3D camera; camera.fit({-40, -7, -7}, {40, 7, 7}, 1.8);
    renderScene3D(model, draw, camera, false, QColor(32, 36, 42));
    QDir().mkpath("build/wizard-review"); model.save("build/wizard-review/label-tie-3d.png");
    tieEditor.beginLabelPlacement({labelMask}, 1.0); tieEditor.onKey(Qt::Key_Escape, Qt::NoModifier);
    tieEditor.onKey(Qt::Key_Space, Qt::NoModifier);
    check(tieEditor.placingLabel(), "Space repeats the last label placement function");
    {
        MainWindow window; window.show(); events();
        auto* activeEditor = window.findChild<Editor*>();
        auto* button = window.findChild<QToolButton*>();
        check(activeEditor && button, "Space repeat test has a connected editor and ribbon control");
        if (activeEditor && button) {
            activeEditor->setTool(ToolId::Circle); activeEditor->setTool(ToolId::Select);
            QKeyEvent key(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
            QApplication::sendEvent(button, &key);
            check(activeEditor->tool() == ToolId::Circle, "Space repeats the last function while a ribbon control has focus");
            activeEditor->setTool(ToolId::Select);
            QLineEdit text(&window); text.show(); text.setFocus();
            QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
            QApplication::sendEvent(&text, &space);
            check(text.text() == " " && activeEditor->tool() == ToolId::Select, "Space still enters text in an input field");
            QLineEdit* command = nullptr;
            for (auto* input : window.findChildren<QLineEdit*>()) if (input->property("spaceRepeatWhenEmpty").toBool()) command = input;
            check(command, "the command bar supports empty-field Space repeat");
            if (command) {
                command->clear(); QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
                QApplication::sendEvent(command, &repeat);
                check(activeEditor->tool() == ToolId::Circle && command->text().isEmpty(), "Space in the empty command bar repeats the last function");
            }
            activeEditor->beginLabelPlacement({labelMask}, 1.0);
            auto* repeatButton = window.findChild<QToolButton*>("repeatLastFunction");
            check(repeatButton && repeatButton->isEnabled() && repeatButton->text().contains("Label"),
                  "Repeat button names and enables the last label placement function");
        }
    }
    HarnessWire imported;
    imported.gauge = "1.5"; imported.color = "BK"; imported.partNumber = "OLD";
    applyHarnessWirePart(imported, *lib.byId("tail"));
    check(imported.gauge == "0.25" && imported.color == "RD-WH" && imported.partNumber.isEmpty(),
          "library wire selection imports cross section with units and both colours, replacing manual P/N");
    LibraryPart cable; cable.id = "pair"; cable.kind = PartKind::Cable;
    setCableSpecOn(cable, cableSpecFromText("FTP 1x2x24AWG"));
    HarnessWire firstCore, secondCore; firstCore.bundle = secondCore.bundle = "CB1";
    applyHarnessWirePart(firstCore, cable, 0); applyHarnessWirePart(secondCore, cable, 1);
    check(firstCore.gauge == "0.25" && firstCore.twistGroup == secondCore.twistGroup &&
          !firstCore.twistGroup.isEmpty() && !firstCore.shieldGroup.isEmpty(),
          "library cable selection imports conductor cross section, twist and shield groups");
    const auto saved = harnessDefToJson(d); const auto loaded = harnessDefFromJson(saved);
    check(harnessDefToJson(loaded) == saved, "pattern round-trip retains physical lengths, per-end parts and explicit conductor roles");
    auto old = saved; old.remove("physicalBom"); old["version"] = 3;
    QJsonArray wires = old["wires"].toArray();
    for (int i = 0; i < wires.size(); ++i) { auto row = wires[i].toObject(); row.remove("fromTermination"); row.remove("toTermination"); row.remove("coaxRole"); row["kind"] = "coax-splice"; wires[i] = row; }
    old["wires"] = wires;
    check(harnessTailAt(harnessDefFromJson(old).wires.front(), 0), "legacy coax-splice patterns preserve splices at both ends");
    check(wireColorName("wh-bl") == "White/Blue" && wireColorHex("WH-BL") == wireColorHex("WH"), "two-colour names and blue alias resolve");
    std::vector<BomRow> rows; for (const auto& line : harnessMaterials(d, lib)) { rows.push_back(line.row); check(!line.missingPart && !line.missingLength, "complete materials have no missing assignments"); }
    check(std::abs(quantity(rows, "coax") - 1) < 1e-9, "one coax is counted once, despite signal and shield rows");
    check(std::abs(quantity(rows, "tail") - .4) < 1e-9 && quantity(rows, "joint") == 4, "four tails and four separate splices are counted");
    Document generated; generated.entities() = buildHarness(d).entities;
    const auto drawingRows = bomFromDrawing(generated, lib);
    check(std::abs(quantity(drawingRows, "coax") - 1) < 1e-9 && std::abs(quantity(drawingRows, "tail") - .4) < 1e-9 && quantity(drawingRows, "joint") == 4,
          "generated drawing BOM agrees with live physical BOM");
    int tails = 0, joints = 0;
    for (const auto& e : generated.entities()) {
        if (e.type == EntityType::Wire && e.meta.value("libPart") == QJsonValue("tail")) ++tails;
        if (e.type == EntityType::Splice && e.meta.value("libPart") == QJsonValue("joint")) ++joints;
    }
    check(tails == 4 && joints == 4, "generated tails and joints keep their independent library parts");
    d.wires[0].toTermination.via = "direct";
    rows.clear(); for (const auto& line : harnessMaterials(d, lib)) rows.push_back(line.row);
    check(quantity(rows, "joint") == 3 && std::abs(quantity(rows, "tail") - .3) < 1e-9, "one end can be direct independently of the other conductor");
    d = fixture(); d.wires[0].cutLengthMm = d.wires[1].cutLengthMm = 0;
    bool unset = false; for (const auto& line : harnessMaterials(d, lib)) if (line.row.partId == "coax") unset = line.missingLength && line.row.qty == 0;
    check(unset, "missing manufacturing length remains unset rather than inferred from geometry");

    QImage stripe(240, 80, QImage::Format_ARGB32_Premultiplied); stripe.fill(Qt::black);
    Entity coloured; coloured.type = EntityType::Wire; coloured.points = {{10, 0}, {220, 0}}; coloured.meta["wireColor"] = "WH-BL";
    { QPainter p(&stripe); EntityRenderer::draw(p, coloured, Document(), ViewTransform{{0, 40, 1}}); }
    int blue = 0, white = 0; for (int x = 10; x < 220; ++x) for (int y = 38; y <= 42; ++y) {
        const auto c = stripe.pixelColor(x, y); if (c.blue() > c.red() + 40) ++blue; if (c.red() > 180 && c.green() > 180) ++white;
    }
    check(blue > 0 && white > 0, "drawing displays both base and stripe colours");

    QSettings settings; const QString key = "harnessBuilder/ui/v2";
    settings.beginGroup(key); const QStringList keys = settings.allKeys(); QMap<QString, QVariant> prior;
    for (const auto& k : keys) prior[k] = settings.value(k); settings.endGroup(); settings.remove(key);
    Document doc; Editor editor; editor.setDocument(&doc);
    QByteArray columns; int customWidth = 0;
    {
        HarnessBuilderDialog wizard(&editor); wizard.show(); events(); useHarnessBuilderLibraryForTest(lib);
        wizard.loadDemand(fixture(), "test"); wizard.setStep(1); events();
        check(wizard.windowTitle() == "Schematic build wizzard", "wizard name is consistent");
        auto* steps = wizard.findChild<QListWidget*>("harnessBuilderSteps");
        auto* checks = wizard.findChild<QPushButton*>("wizardChecks");
        check(steps && !steps->item(1)->data(Qt::UserRole + 2).toBool(), "populating a step does not mark it checked");
        if (checks) checks->click(); events();
        check(steps && steps->item(1)->data(Qt::UserRole + 2).toBool() && !steps->item(0)->data(Qt::UserRole + 2).toBool(),
              "Checks approves only the current clean step");
        auto* stepScroll = wizard.findChild<QScrollArea*>("wizardStepScroll1");
        check(stepScroll && stepScroll->layoutDirection() == Qt::RightToLeft &&
              stepScroll->widget()->layoutDirection() == Qt::LeftToRight,
              "overall scrollbar is on the left while form contents remain left-to-right");
        auto* wireTable = wizard.findChild<QTableWidget*>("wizardWireTable");
        if (!wireTable) for (auto* table : wizard.findChildren<QTableWidget*>()) if (table->columnCount() == 14) wireTable = table;
        check(wireTable && (wireTable->horizontalScrollBar()->maximum() == 0 ||
              wireTable->columnWidth(2) >= 80), "compact columns stay readable, with scrolling when the side preview leaves too little room");
        if (wireTable) {
            int visible = 0; for (int c = 0; c < 14; ++c) if (!wireTable->isColumnHidden(c)) ++visible;
            check(visible == 7, "six essential columns and the delete button are shown; original advanced fields remain available");
            check(wireTable->columnWidth(2) >= 80 && wireTable->columnWidth(13) >= 80,
                  "hidden-step initial sizing leaves readable connector and part columns");
            check(wireTable->visibleRegion().boundingRect().height() > 42, "connection table remains visible beside/before its preview");
            wireTable->setColumnWidth(0, 173); customWidth = wireTable->columnWidth(0);
            wizard.loadDemand(fixture(), "reload"); events();
            check(wireTable->columnWidth(0) == customWidth, "editing/reloading does not overwrite user column widths");
            check(!wireTable->isColumnHidden(12) && !wireTable->isColumnHidden(13) && wireTable->isColumnHidden(5) &&
                      wireTable->horizontalHeader()->visualIndex(12) == wireTable->columnCount() - 1,
                  "compact columns survive row reloads; the delete button stays the last column");
            columns = wireTable->horizontalHeader()->saveState();
        }
        auto* bom = wizard.findChild<QPushButton*>("wizardLiveBom"); check(bom, "live BOM is available from every step"); if (bom) bom->click(); events();
        auto* bomTable = wizard.findChild<QTableWidget*>("wizardBomTable"); check(bomTable && bomTable->rowCount() == 4, "live BOM has connectors, one cable, tails and splices");
        if (bomTable) { QDir().mkpath("build/wizard-review"); bomTable->window()->grab().save("build/wizard-review/bom.png"); }
        wizard.showPinMap(true); events();
        auto* picture = wizard.findChild<QWidget*>("harnessBuilderPreviewPicture");
        check(picture && picture->isVisible(), "drawing preview stays visible while the pin map is open");
        QListWidget* pins = nullptr; for (auto* list : wizard.findChildren<QListWidget*>()) if (list->property("connId") == QVariant("c1")) pins = list;
        check(pins, "pin map exposes connector pins");
        if (pins) {
            auto* pin = pins->item(0); emit pins->itemClicked(pin);
            check(wizard.litPins().contains("c1:1") && wizard.litPins().contains("c2:1") && !wizard.litPins().contains("c2:2"), "click traces signal connection without shorting to shield");
            QEvent leave(QEvent::Leave); QApplication::sendEvent(pins->viewport(), &leave);
            check(!wizard.litPins().isEmpty(), "pin selection remains highlighted after pointer leaves");
        }
        auto* materialPn = wizard.findChild<QLineEdit*>("wizardMaterialPn");
        auto* material = wizard.findChild<QComboBox*>("wizardMaterial");
        check(material && material->toolTip() == material->currentText(), "library field hover exposes the entire selected text");
        check(materialPn && materialPn->text() == "COAX-PN", "wire details show library part P/N");
        if (materialPn) emit materialPn->editingFinished(); events();
        check(wizard.definition().wires[0].libPart == "coax", "leaving an unchanged library P/N keeps the library assignment");
        auto* via = wizard.findChild<QComboBox*>("wizardFromVia");
        check(via, "selected coax exposes independent end termination controls");
        if (via) {
            via->setCurrentIndex(0); events();
            check(wizard.definition().wires[0].fromTermination.via == "direct", "termination control edits the selected conductor end");
            check(bomTable && bomTable->item(2, 2)->text().toDouble() == .3 && bomTable->item(3, 2)->text().toInt() == 3,
                  "live BOM updates immediately after removing one tail and splice");
            wizard.undo(); events();
            check(wizard.definition().wires[0].fromTermination.via == "wire", "undo restores the termination edit");
        }
        auto* stripeBox = wizard.findChild<QComboBox*>("wizardStripeColor");
        if (stripeBox) { stripeBox->setCurrentIndex(0); events(); }
        check(wizard.definition().wires[0].color == "WH" && !harnessCoaxShield(wizard.definition().wires[0]),
              "colour control changes stripe without changing coax conductor role");
        check(steps && !steps->item(1)->data(Qt::UserRole + 2).toBool(), "editing invalidates a previous step checkmark");
        wizard.undo(); events();
        auto* tailPart = wizard.findChild<QComboBox*>("wizardFromWirePart");
        check(tailPart && tailPart->findData("tail") >= 0, "termination hook-up wire offers parts-list selection");
        auto* splicePart = wizard.findChild<QComboBox*>("wizardFromSplicePart");
        check(splicePart && splicePart->findData("joint") >= 0, "termination splice offers its own parts-list selection");
        auto* details = wizard.findChild<QWidget*>("wizardConnectionDetails");
        check(details && !wizard.findChild<QScrollArea*>("wizardWireDetailsScroll"), "wire details have no independent nested scrolling area");
        if (details && stepScroll) {
            auto* scroll = wizard.findChild<QScrollArea*>("wizardWorkScroll");
            if (!scroll || scroll->verticalScrollBar()->maximum() == 0) scroll = stepScroll;
            scroll->verticalScrollBar()->setValue(0); events();
            const int tableY = wireTable->mapTo(&wizard, QPoint()).y();
            const int detailsY = details->mapTo(&wizard, QPoint()).y();
            QWheelEvent wheel(QPointF(2,2), QPointF(scroll->viewport()->mapToGlobal(QPoint(2,2))), QPoint(), QPoint(0,-120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(scroll->viewport(), &wheel); events();
            const int moved = tableY - wireTable->mapTo(&wizard, QPoint()).y();
            check(scroll->verticalScrollBar()->value() > 0 && moved > 0 &&
                  detailsY - details->mapTo(&wizard, QPoint()).y() == moved,
                  "scrolling the wires page moves the table and details together");
            scroll->verticalScrollBar()->setValue(0); events();
            QApplication::sendEvent(wireTable->viewport(), &wheel); events();
            check(scroll->verticalScrollBar()->value() > 0, "wheel over the connection table scrolls the whole wires section");
            scroll->verticalScrollBar()->setValue(0); events();
        }
        if (auto* pn = wizard.findChild<QLineEdit*>("wizardFromWirePn")) {
            pn->setText("MANUAL-TAIL"); emit pn->editingFinished(); events();
            check(wizard.definition().wires[0].fromTermination.wireLibPart.isEmpty() &&
                  wizard.definition().wires[0].fromTermination.wirePn == "MANUAL-TAIL", "manual tail P/N replaces library assignment");
            auto* replacement = wizard.findChild<QComboBox*>("wizardFromWirePart");
            if (replacement) replacement->setCurrentIndex(replacement->findData("tail")); events();
            check(wizard.definition().wires[0].fromTermination.wireLibPart == "tail" &&
                  wizard.definition().wires[0].fromTermination.wirePn.isEmpty(), "library selection replaces stale manual tail P/N");
        }
        wizard.grab().save("build/wizard-review/pin-map.png");
        wizard.showPinMap(false); events();
        int visibleWidth = 0;
        for (int c = 0; c < 14; ++c) if (!wireTable->isColumnHidden(c)) visibleWidth += wireTable->columnWidth(c);
        check(wireTable->horizontalHeader()->length() == visibleWidth, "hidden advanced columns occupy no space after editing and undo");
        QDir().mkpath("build/wizard-review"); wizard.grab().save("build/wizard-review/connections.png");
        auto* previewChoice = wizard.findChild<QComboBox*>("harnessBuilderPreviewWhere");
        auto* more = wizard.findChild<QCheckBox*>("wizardMoreFields");
        auto* split = wizard.findChild<QSplitter*>("harnessBuilderSplit");
        if (more) more->setChecked(true);
        if (previewChoice) previewChoice->setCurrentIndex(0); events();
        check(split && split->orientation() == Qt::Horizontal, "preview menu keeps the side position with More fields enabled");
        auto* sideScroll = wizard.findChild<QScrollArea*>("wizardPreviewScroll");
        check(sideScroll && sideScroll->horizontalScrollBar()->maximum() == 0,
              "the side preview fits its visible width without horizontal clipping");
        wizard.grab().save("build/wizard-review/side-more-fields.png");
        if (previewChoice) previewChoice->setCurrentIndex(1); events();
        check(split && split->orientation() == Qt::Vertical, "preview menu selects the bottom position");
        if (split && stepScroll) {
            const auto savedSplit = split->saveState();
            stepScroll->verticalScrollBar()->setValue(std::min(40, stepScroll->verticalScrollBar()->maximum())); events();
            const int anchor = stepScroll->verticalScrollBar()->value();
            auto* divider = split->handle(1);
            const QPointF local(4,4), global = divider->mapToGlobal(QPoint(4,4));
            QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(divider, &press);
            QMouseEvent move(QEvent::MouseMove, local + QPointF(0,-40), global + QPointF(0,-40), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(divider, &move); events();
            QMouseEvent release(QEvent::MouseButtonRelease, local, global + QPointF(0,-40), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(divider, &release); events();
            check(stepScroll->verticalScrollBar()->value() == anchor, "pulling the editor/preview divider upward preserves the section scroll position");
            split->restoreState(savedSplit); stepScroll->verticalScrollBar()->setValue(0); events();
        }
        auto* grip = wizard.findChild<QWidget*>("wizardPreviewGrip");
        auto* previewScroll = wizard.findChild<QScrollArea*>("wizardPreviewScroll");
        if (grip && picture) {
            const int oldHeight = picture->height();
            const QPointF local(4, 4), global = grip->mapToGlobal(QPoint(4, 4));
            QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(grip, &press);
            QMouseEvent move(QEvent::MouseMove, local + QPointF(0, 400), global + QPointF(0, 400), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(grip, &move); events();
            auto* workScroll = wizard.findChild<QScrollArea*>("wizardWorkScroll");
            check(picture->height() >= oldHeight + 350 && previewScroll &&
                  (previewScroll->verticalScrollBar()->maximum() > 0 || (workScroll && workScroll->verticalScrollBar()->maximum() > 0)),
                  "dragging the preview edge enlarges it and scrolls the overflowing sections");
            QMouseEvent reset(QEvent::MouseButtonDblClick, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(grip, &reset); events();
        }
        auto* connectionSplit = wizard.findChild<QSplitter*>("wizardConnectionSplit");
        if (connectionSplit) connectionSplit->setSizes({0, 400}); events();
        wizard.loadDemand(fixture(), "loaded"); events();
        check(wireTable->rowCount() > 0 && wireTable->isVisible() && wireTable->height() >= 100,
              "loaded wizard keeps the populated wire table visible after splitter resizing");
        wizard.grab().save("build/wizard-review/loaded-bottom.png");
        if (more) more->setChecked(false);
        wizard.setStep(0); events(); wizard.grab().save("build/wizard-review/connectors.png");
        const QSize originalSize = wizard.size();
        wizard.resize(1240, 840); wizard.setStep(1); events(); wizard.grab().save("build/wizard-review/connections-wide.png");
        wizard.resize(originalSize); events();
        if (auto* add = wizard.findChild<QPushButton*>("wizardAddCoax")) add->click(); events();
        check(wizard.definition().wires.size() == 4 && harnessCoaxShield(wizard.definition().wires.back()),
              "Add coax creates paired signal and shield conductors");
        wizard.undo(); events();
        customWidth = wireTable->columnWidth(0);
        wizard.hide(); events();
    }
    {
        HarnessBuilderDialog reopened(&editor); reopened.show(); events(); useHarnessBuilderLibraryForTest(lib); reopened.loadDemand(fixture(), "restored"); events();
        QTableWidget* wireTable = nullptr; for (auto* table : reopened.findChildren<QTableWidget*>()) if (table->columnCount() == 14) wireTable = table;
        check(wireTable && wireTable->columnWidth(0) == customWidth, "column sizing is restored on a future wizard opening");
        reopened.hide();
    }
    {
        HarnessBuilderDialog wizard(&editor); wizard.show(); events(); useHarnessBuilderLibraryForTest(lib);
        wizard.loadDemand(shieldFixture(), "section flow"); wizard.setStep(0);
        if (auto* choice = wizard.findChild<QComboBox*>("harnessBuilderPreviewWhere")) choice->setCurrentIndex(1);
        events();
        auto drag = [&](QWidget* handle, int dy) {
            const QPointF local(4,4), global = handle->mapToGlobal(QPoint(4,4));
            QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(handle, &press);
            QMouseEvent move(QEvent::MouseMove, local + QPointF(0,dy), global + QPointF(0,dy), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(handle, &move); events();
            QMouseEvent release(QEvent::MouseButtonRelease, local, global + QPointF(0,dy), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(handle, &release); events();
        };
        auto* table = wizard.findChild<QTableWidget*>("wizardConnectorTable");
        auto* split = wizard.findChild<QSplitter*>("harnessBuilderSplit");
        auto* work = wizard.findChild<QScrollArea*>("wizardWorkScroll");
        auto* frame = table ? table->parentWidget() : nullptr;
        auto* grip = frame ? frame->findChild<QWidget*>("sectionResizeGrip") : nullptr;
        auto* map = frame && frame->parentWidget()->layout() ? frame->parentWidget()->layout()->itemAt(1)->widget() : nullptr;
        check(table && split && work && grip && map, "connector resize scenario exposes all real section dividers");
        if (table && split && work && grip && map) {
            wizard.grab().save("build/wizard-review/section-flow-initial.png");
            work->verticalScrollBar()->setValue(0); events();
            const int tableY = table->mapTo(&wizard, QPoint()).y(), tableHeight = table->height();
            const int mapY = map->mapTo(&wizard, QPoint()).y(), mapHeight = map->height();
            auto* preview = split->widget(1);
            const int previewY = preview->mapTo(&wizard, QPoint()).y(), previewHeight = preview->height();
            drag(grip, 60);
            check(table->height() == tableHeight + 60 && table->mapTo(&wizard, QPoint()).y() == tableY,
                  "first divider enlarges the connector table downwards without shifting its content");
            check(map->height() == mapHeight && map->mapTo(&wizard, QPoint()).y() == mapY + 60 &&
                  preview->height() == previewHeight && preview->mapTo(&wizard, QPoint()).y() == previewY + 60,
                  "enlarging the first section pushes both placement and preview down without resizing them");
            drag(grip, -30);
            check(table->height() == tableHeight + 30 && table->mapTo(&wizard, QPoint()).y() == tableY &&
                  map->height() == mapHeight && map->mapTo(&wizard, QPoint()).y() == mapY + 30 &&
                  preview->height() == previewHeight && preview->mapTo(&wizard, QPoint()).y() == previewY + 30,
                  "shrinking the first section pulls all following sections up without scrolling their contents");
            const int upperHeight = split->widget(0)->height(), lowerHeight = preview->height();
            const int lowerY = preview->mapTo(&wizard, QPoint()).y();
            const int protectedTableHeight = table->height(), protectedTableY = table->mapTo(&wizard, QPoint()).y();
            const int protectedMapY = map->mapTo(&wizard, QPoint()).y(), oldMapHeight = map->height();
            drag(split->handle(1), -40);
            check(split->widget(0)->height() == upperHeight - 40 && preview->height() == lowerHeight &&
                  preview->mapTo(&wizard, QPoint()).y() == lowerY - 40,
                  "before-last divider shrinks the upper pane and moves the preview up without enlarging it");
            check(table->height() == protectedTableHeight && table->mapTo(&wizard, QPoint()).y() == protectedTableY &&
                  map->mapTo(&wizard, QPoint()).y() == protectedMapY && map->height() == oldMapHeight - 40,
                  "the editor bottom divider resizes only Placement Map and leaves the connector section boundary fixed");
            drag(split->handle(1), -5000);
            const int minimumMapHeight = map->height();
            drag(split->handle(1), -5000);
            auto* mapClip = map->findChild<QWidget*>("clipBox");
            check(table->height() == protectedTableHeight && table->mapTo(&wizard, QPoint()).y() == protectedTableY &&
                  map->mapTo(&wizard, QPoint()).y() == protectedMapY && map->height() == minimumMapHeight && mapClip && mapClip->height() >= 80,
                  "repeated extreme lower-divider drags stop at a usable Placement Map minimum without collapsing the top section");
            table->setCurrentCell(2,0); table->verticalScrollBar()->setValue(0); events();
            drag(grip, -5000);
            check(table->verticalScrollBar()->value() == 0,
                  "shrinking a table section keeps its first row in place even when a lower row is selected");
            check(table->height() >= table->horizontalHeader()->height() + 2 * table->rowHeight(0),
                  "the table minimum leaves the header and two complete editable rows visible");
            wizard.grab().save("build/wizard-review/section-flow-connectors.png");
        }
        wizard.hide(); events();
    }
    {
        const QString catalogKey = QStringLiteral("partsLibrary/connected");
        const bool hadCatalog = settings.contains(catalogKey); const auto priorCatalog = settings.value(catalogKey);
        QTemporaryDir folder; const QString path = folder.filePath("wizard-parts.plb");
        auto diskLibrary = library();
        for (auto& part : diskLibrary.parts) if (part.id == "tail") { part.params["gauge"] = "0.5"; part.params["colour"] = "WH"; }
        check(diskLibrary.save(path), "library refresh fixture is saved"); settings.setValue(catalogKey, QStringList{path});
        HarnessBuilderDialog wizard(&editor); wizard.show(); events();
        auto plain = fixture(); plain.wires.resize(1); auto& wire = plain.wires[0];
        wire.libPart = "tail"; wire.bundle.clear(); wire.kind = "wire"; wire.gauge = "0.5"; wire.color = "WH";
        wire.fromTermination = {}; wire.toTermination = {};
        wizard.loadDemand(plain, "library refresh"); wizard.setStep(1); events();
        for (auto* list : wizard.findChildren<QListWidget*>()) if (list->property("connId") == QVariant("c1")) { emit list->itemClicked(list->item(0)); break; }
        events();
        QString opened; QObject::connect(&wizard, &HarnessBuilderDialog::openLibraryAt, &wizard, [&](const QString& id) { opened = id; });
        auto invokeMenu = [&](const QString& caption) {
            auto* field = wizard.findChild<QComboBox*>("wizardMaterial");
            check(field, "library context menu field exists"); if (!field) return;
            bool actionFound = false;
            QTimer::singleShot(0, &wizard, [&] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (!menu) { if (auto* active = QApplication::activeModalWidget()) active->close(); return; }
                for (auto* action : menu->actions()) if (action->text() == caption) { actionFound = true; action->trigger(); break; }
                menu->close();
            });
            QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(2,2), field->mapToGlobal(QPoint(2,2)));
            QApplication::sendEvent(field, &context); events();
            check(actionFound, "library field context menu contains requested action");
        };
        invokeMenu("Open in library"); check(opened == "tail", "Open in library opens the selected library part");
        for (auto& part : diskLibrary.parts) if (part.id == "tail") {
            part.params["gauge"] = "1.5"; part.params["colour"] = "RD"; part.mfrPn = "UPDATED-TAIL-PN";
        }
        check(diskLibrary.save(path), "changed library is saved independently of the open wizard");
        invokeMenu("Update from library");
        check(wizard.definition().wires[0].gauge == "1.5" && wizard.definition().wires[0].color == "RD",
              "Update from library re-reads disk and updates physical part properties");
        wizard.undo(); events();
        check(wizard.definition().wires[0].gauge == "0.5", "library refresh can be undone");
        wizard.hide();
        if (hadCatalog) settings.setValue(catalogKey, priorCatalog); else settings.remove(catalogKey);
    }
    settings.remove(key); settings.beginGroup(key); for (auto it = prior.cbegin(); it != prior.cend(); ++it) settings.setValue(it.key(), it.value()); settings.endGroup();
    useHarnessBuilderLibraryForTest(PartsLibrary{});
    qApp->setStyleSheet(oldStyle);
    std::printf(failures ? "  -> %d failed\n" : "  -> ok\n", failures);
    return failures;
}
