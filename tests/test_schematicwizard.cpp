#include "app/Editor.h"
#include "app/HarnessBuilderDialog.h"
#include "app/DrawingPreview.h"
#include "harness/HarnessBuilderBom.h"
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
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
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
}
int run_schematicwizard_tests() {
    failures = 0; std::printf("schematic wizard tests:\n");
    const QString oldStyle = qApp->styleSheet();
    if (QFile style(QStringLiteral(":/loopwire.qss")); style.open(QIODevice::ReadOnly)) qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
    const auto lib = library(); auto d = fixture();
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
        auto* wireTable = wizard.findChild<QTableWidget*>("wizardWireTable");
        if (!wireTable) for (auto* table : wizard.findChildren<QTableWidget*>()) if (table->columnCount() == 14) wireTable = table;
        check(wireTable && wireTable->horizontalScrollBar()->maximum() == 0, "initial compact wire columns fit without horizontal scrolling");
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
        QListWidget* pins = nullptr; for (auto* list : wizard.findChildren<QListWidget*>()) if (list->property("connId") == QVariant("c1")) pins = list;
        check(pins, "pin map exposes connector pins");
        if (pins) {
            auto* pin = pins->item(0); emit pins->itemClicked(pin);
            check(wizard.litPins().contains("c1:1") && wizard.litPins().contains("c2:1") && !wizard.litPins().contains("c2:2"), "click traces signal connection without shorting to shield");
            QEvent leave(QEvent::Leave); QApplication::sendEvent(pins->viewport(), &leave);
            check(!wizard.litPins().isEmpty(), "pin selection remains highlighted after pointer leaves");
        }
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
        wizard.undo(); events();
        auto* tailPart = wizard.findChild<QComboBox*>("wizardFromWirePart");
        check(tailPart && tailPart->findData("tail") >= 0, "termination hook-up wire offers parts-list selection");
        auto* splicePart = wizard.findChild<QComboBox*>("wizardFromSplicePart");
        check(splicePart && splicePart->findData("joint") >= 0, "termination splice offers its own parts-list selection");
        if (auto* pn = wizard.findChild<QLineEdit*>("wizardFromWirePn")) {
            pn->setText("MANUAL-TAIL"); emit pn->editingFinished(); events();
            check(wizard.definition().wires[0].fromTermination.wireLibPart.isEmpty() &&
                  wizard.definition().wires[0].fromTermination.wirePn == "MANUAL-TAIL", "manual tail P/N replaces library assignment");
            auto* replacement = wizard.findChild<QComboBox*>("wizardFromWirePart");
            if (replacement) replacement->setCurrentIndex(replacement->findData("tail")); events();
            check(wizard.definition().wires[0].fromTermination.wireLibPart == "tail" &&
                  wizard.definition().wires[0].fromTermination.wirePn.isEmpty(), "library selection replaces stale manual tail P/N");
        }
        wizard.showPinMap(false); events();
        int visibleWidth = 0;
        for (int c = 0; c < 14; ++c) if (!wireTable->isColumnHidden(c)) visibleWidth += wireTable->columnWidth(c);
        check(wireTable->horizontalHeader()->length() == visibleWidth, "hidden advanced columns occupy no space after editing and undo");
        QDir().mkpath("build/wizard-review"); wizard.grab().save("build/wizard-review/connections.png");
        wizard.setStep(0); events(); wizard.grab().save("build/wizard-review/connectors.png");
        const QSize originalSize = wizard.size();
        wizard.resize(1240, 840); wizard.setStep(1); events(); wizard.grab().save("build/wizard-review/connections-wide.png");
        wizard.resize(originalSize); events();
        if (auto* add = wizard.findChild<QPushButton*>("wizardAddCoax")) add->click(); events();
        check(wizard.definition().wires.size() == 4 && harnessCoaxShield(wizard.definition().wires.back()),
              "Add coax creates paired signal and shield conductors");
        wizard.undo(); events();
        wizard.hide(); events();
    }
    {
        HarnessBuilderDialog reopened(&editor); reopened.show(); events(); useHarnessBuilderLibraryForTest(lib); reopened.loadDemand(fixture(), "restored"); events();
        QTableWidget* wireTable = nullptr; for (auto* table : reopened.findChildren<QTableWidget*>()) if (table->columnCount() == 14) wireTable = table;
        check(wireTable && wireTable->columnWidth(0) == customWidth, "column sizing is restored on a future wizard opening");
        reopened.hide();
    }
    settings.remove(key); settings.beginGroup(key); for (auto it = prior.cbegin(); it != prior.cend(); ++it) settings.setValue(it.key(), it.value()); settings.endGroup();
    useHarnessBuilderLibraryForTest(PartsLibrary{});
    qApp->setStyleSheet(oldStyle);
    std::printf(failures ? "  -> %d failed\n" : "  -> ok\n", failures);
    return failures;
}
