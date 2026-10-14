#include "app/Editor.h"
#include "app/MainWindow.h"
#include "model/Document.h"
#include "render/CanvasWidget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFile>
#include <QMouseEvent>
#include <QLabel>
#include <QLineF>
#include <QPaintEvent>
#include <QPainter>
#include <QStatusBar>
#include <QSettings>
#include <QTextStream>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace Loopwire;

namespace {
class PaintWork : public QObject {
public:
    QHash<QString, qint64> pixels;
    QHash<QString, int> layouts;
    bool eventFilter(QObject* object, QEvent* event) override {
        const QString name = QString::fromLatin1(object->metaObject()->className());
        if (event->type() == QEvent::Paint) {
            const QRect bounds = static_cast<QPaintEvent*>(event)->region().boundingRect();
            pixels[name] += qint64(bounds.width()) * bounds.height();
        } else if (event->type() == QEvent::LayoutRequest) ++layouts[name];
        return false;
    }
};

class MeasuredCanvas : public CanvasWidget {
public:
    QElapsedTimer clock;
    qint64 inputStarted = 0;
    qint64 paintEnded = 0;
    double paintMs = 0;
    int paints = 0;
    MeasuredCanvas() { clock.start(); }
protected:
    void paintEvent(QPaintEvent* event) override {
        const qint64 start = clock.nsecsElapsed();
        CanvasWidget::paintEvent(event);
        paintEnded = clock.nsecsElapsed();
        paintMs = (paintEnded - start) / 1e6;
        ++paints;
    }
};

double percentile(std::vector<double> samples, double fraction) {
    std::sort(samples.begin(), samples.end());
    return samples[std::min(samples.size() - 1, size_t(fraction * (samples.size() - 1)))];
}

void report(QTextStream* csv, int objects, const char* tool, bool snapping, const char* stage,
            const std::vector<double>& samples) {
    const double p50 = percentile(samples, .50), p95 = percentile(samples, .95);
    const double worst = *std::max_element(samples.begin(), samples.end());
    std::printf("  latency %5d %-4s snap=%d %-12s p50=%7.3f p95=%7.3f max=%7.3f ms\n",
                objects, tool, int(snapping), stage, p50, p95, worst);
    if (csv) *csv << objects << ',' << tool << ',' << int(snapping) << ',' << stage
                  << ',' << p50 << ',' << p95 << ',' << worst << '\n';
}
}

int run_latency_tests() {
    std::printf("latency tests (platform=%s; CPU paint completion, not display scan-out):\n",
                qPrintable(QApplication::platformName()));
    QFile file(QString::fromLocal8Bit(qgetenv("LOOPWIRE_LATENCY_CSV")));
    const bool writeCsv = !file.fileName().isEmpty() && file.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream csv(&file);
    if (writeCsv) csv << "objects,tool,snapping,stage,p50_ms,p95_ms,max_ms\n";
    int failures = 0;
    // The OS pointer and QWidget backing store have independent presentation
    // paths. During a draft, verify that the visible crosshair is painted at
    // the same snapped position as the preview, and restores on cancellation.
    {
        QSettings settings;
        const QString foldedKey = QStringLiteral("canvas/minimizeDrawingDetails");
        const QString visibleKey = QStringLiteral("canvas/showDrawingDetails");
        const QVariant wasFolded = settings.value(foldedKey);
        const QVariant wasVisible = settings.value(visibleKey);
        settings.setValue(foldedKey, false);
        Document doc;
        doc.view().x = 100; doc.view().y = 160; doc.view().scale = 1;
        Entity target;
        target.id = QStringLiteral("snap-target");
        target.type = EntityType::Line;
        target.a = {80, 50}; target.b = {120, 50};
        doc.entities().push_back(target);
        Editor editor;
        editor.setDocument(&doc);
        MeasuredCanvas canvas;
        if (wasFolded.isValid()) settings.setValue(foldedKey, wasFolded);
        else settings.remove(foldedKey);
        canvas.resize(480, 320);
        canvas.setDocument(&doc);
        canvas.setEditor(&editor);
        canvas.setPlainCanvas(true);
        canvas.show();
        QApplication::processEvents();
        const QPointF at(181, 111);
        QEnterEvent enter(at, at, canvas.mapToGlobal(at));
        QApplication::sendEvent(&canvas, &enter);
        editor.setTool(ToolId::Line);
        editor.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        editor.onPointerRelease({0, 0}, Qt::LeftButton);
        QMouseEvent move(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &move);
        QApplication::processEvents();
        const QPointF tip = canvas.screenFromWorld(editor.snappedCursor());
        const QImage frame = canvas.grab().toImage();
        const int pick = canvas.pickboxSize();
        const QPoint corner = (tip + QPointF(pick, pick)).toPoint();
        const qreal dpr = frame.devicePixelRatio();
        const QColor pixel = frame.pixelColor(qRound(corner.x() * dpr), qRound(corner.y() * dpr));
        if (canvas.cursor().shape() != Qt::BlankCursor ||
            pixel.rgb() != canvas.crosshairColor().rgb()) {
            std::printf("  [FAIL] draft crosshair was not in the preview frame at its snapped tip\n");
            ++failures;
        }
        editor.snapSettings().objectSnap = false;
        const QPointF fractional(211.25, 142.75);
        QMouseEvent fineMove(QEvent::MouseMove, fractional, canvas.mapToGlobal(fractional), Qt::NoButton,
                             Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &fineMove);
        if (QLineF(canvas.crosshairScreenPos(), canvas.screenFromWorld(editor.snappedCursor())).length() > 1e-9) {
            std::printf("  [FAIL] fractional cursor and preview positions diverged\n");
            ++failures;
        }
        editor.onKey(Qt::Key_Escape, Qt::NoModifier);
        QApplication::processEvents();
        if (canvas.cursor().shape() != Qt::BitmapCursor) {
            std::printf("  [FAIL] native idle crosshair was not restored after drawing\n");
            ++failures;
        }
        // Caching the details lookup must retain inherited title-block fields
        // and refresh both edited fields and declared document metadata.
        Entity field;
        field.id = QStringLiteral("inherited-title");
        field.type = EntityType::Text;
        field.a = {-10000, -10000};
        field.meta = {{QStringLiteral("field"), QStringLiteral("title")},
                      {QStringLiteral("text"), QStringLiteral("Inherited title A")}};
        doc.drawingDetails().remove(QStringLiteral("title"));
        doc.entities().push_back(field);
        canvas.setDrawingDetailsVisible(true);
        canvas.invalidateScene();
        const QImage inherited = canvas.grab().toImage();
        canvas.requestRepaint();
        const QImage same = canvas.grab().toImage();
        doc.entities().back().meta[QStringLiteral("text")] = QStringLiteral("Inherited title B");
        emit editor.changed();
        const QImage edited = canvas.grab().toImage();
        doc.drawingDetails()[QStringLiteral("title")] = QStringLiteral("Declared title");
        const QImage declared = canvas.grab().toImage();
        if (inherited != same || edited == inherited || declared == edited) {
            std::printf("  [FAIL] cached details did not preserve or refresh drawing fields\n");
            ++failures;
        }
        if (wasVisible.isValid()) settings.setValue(visibleKey, wasVisible);
        else settings.remove(visibleKey);
    }
    if (qEnvironmentVariableIsSet("LOOPWIRE_LATENCY_MAINWINDOW")) {
        if (QFile style(QStringLiteral(":/loopwire.qss")); style.open(QIODevice::ReadOnly))
            qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        QApplication::processEvents();
        auto* canvas = window.findChild<CanvasWidget*>();
        auto* editor = window.findChild<Editor*>();
        if (!canvas || !editor) return 1;
        if (qEnvironmentVariableIsSet("LOOPWIRE_LATENCY_NO_CROSSHAIR_OVERLAY"))
            for (auto* child : canvas->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
                if (child->testAttribute(Qt::WA_TransparentForMouseEvents)) child->hide();
        Document* doc = editor->document();
        doc->entities().clear();
        editor->setTool(ToolId::Line);
        editor->onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        editor->onPointerRelease({0, 0}, Qt::LeftButton);
        QApplication::processEvents();
        if (qEnvironmentVariableIsSet("LOOPWIRE_LATENCY_FIXED_COORDS")) {
            for (auto* label : window.statusBar()->findChildren<QLabel*>())
                if (label->text().contains(QLatin1Char(',')))
                    label->setFixedSize(label->fontMetrics().horizontalAdvance(
                        QStringLiteral("-999999999.999, -999999999.999")) + 8,
                        label->sizeHint().height());
            QApplication::processEvents();
        }
        if (qEnvironmentVariableIsSet("LOOPWIRE_LATENCY_NO_READOUT"))
            QObject::disconnect(canvas, &CanvasWidget::cursorWorldMoved, nullptr, nullptr);
        PaintWork work;
        qApp->installEventFilter(&work);
        std::vector<double> input, frame;
        for (int i = 0; i < 140; ++i) {
            if (i == 20) { work.pixels.clear(); work.layouts.clear(); }
            const QPointF at(canvas->width() * .3 + (i * 7) % 120,
                             canvas->height() * .4 + (i * 3) % 100);
            QMouseEvent move(QEvent::MouseMove, at, canvas->mapToGlobal(at), Qt::NoButton,
                             Qt::NoButton, Qt::NoModifier);
            QElapsedTimer timer;
            timer.start();
            QApplication::sendEvent(canvas, &move);
            const double in = timer.nsecsElapsed() / 1e6;
            QApplication::processEvents();
            const double end = timer.nsecsElapsed() / 1e6;
            if (i >= 20) { input.push_back(in); frame.push_back(end); }
        }
        report(writeCsv ? &csv : nullptr, 0, "app", true, "input", input);
        report(writeCsv ? &csv : nullptr, 0, "app", true, "input-flush", frame);
        qApp->removeEventFilter(&work);
        for (auto it = work.pixels.cbegin(); it != work.pixels.cend(); ++it)
            std::printf("  paint work %-30s %lld pixels\n", qPrintable(it.key()), it.value());
        for (auto it = work.layouts.cbegin(); it != work.layouts.cend(); ++it)
            std::printf("  layout work %-30s %d requests\n", qPrintable(it.key()), it.value());
        if (work.layouts.value(QStringLiteral("QStatusBar")) != 0) {
            std::printf("  [FAIL] cursor readout caused status-bar layout during measured frames\n");
            ++failures;
        }
        auto* readout = window.findChild<QLabel*>(QStringLiteral("cursorCoordinates"));
        if (!qEnvironmentVariableIsSet("LOOPWIRE_LATENCY_NO_READOUT") &&
            (!readout || readout->text() == QLatin1String("0.000, 0.000"))) {
            std::printf("  [FAIL] coordinate readout stopped following the pointer\n");
            ++failures;
        }
        return failures;
    }
    for (const int count : {0, 2000, 20000}) {
        Document doc;
        doc.view().scale = 1;
        doc.view().x = 640;
        doc.view().y = 400;
        for (int i = 0; i < count; ++i) {
            Entity e;
            e.id = QStringLiteral("latency-%1").arg(i);
            e.type = i % 4 == 0 ? EntityType::Circle : EntityType::Line;
            const double x = (i % 200) * 12.0 - 1200, y = (i / 200) * 12.0 - 600;
            e.a = {x, y}; e.b = {x + 8, y + 6};
            e.center = e.a; e.radius = 3;
            doc.entities().push_back(std::move(e));
        }
        Editor editor;
        editor.setDocument(&doc);
        MeasuredCanvas canvas;
        canvas.resize(1280, 800);
        canvas.setDocument(&doc);
        canvas.setEditor(&editor);
        canvas.show();
        QApplication::processEvents();
        QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        for (const ToolId tool : {ToolId::Line, ToolId::Rect}) {
            for (const bool snapping : {false, true}) {
                editor.onKey(Qt::Key_Escape, Qt::NoModifier);
                editor.setTool(tool);
                editor.snapSettings() = SnapSettings{};
                editor.snapSettings().objectSnap = snapping;
                editor.onPointerPress({-240, -130}, Qt::LeftButton, Qt::NoModifier);
                editor.onPointerRelease({-240, -130}, Qt::LeftButton);
                QApplication::processEvents();
                const int rebuilt = canvas.sceneRebuildsForTest();
                std::vector<double> input, overlay, paint, total;
                for (int i = 0; i < 140; ++i) {
                    const QPointF at(400 + (i * 13) % 400, 250 + (i * 7) % 240);
                    QMouseEvent move(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton,
                                     Qt::NoButton, Qt::NoModifier);
                    canvas.inputStarted = canvas.clock.nsecsElapsed();
                    QApplication::sendEvent(&canvas, &move);
                    const double in = (canvas.clock.nsecsElapsed() - canvas.inputStarted) / 1e6;
                    const int before = canvas.paints;
                    QApplication::processEvents();
                    if (canvas.paints == before) {
                        std::printf("  [FAIL] preview was not painted on next event-loop pass\n");
                        ++failures;
                        break;
                    }
                    const double endToEnd = (canvas.paintEnded - canvas.inputStarted) / 1e6;
                    QPainter painter(&image);
                    QElapsedTimer timer;
                    timer.start();
                    editor.drawOverlay(painter, canvas.inputTransform());
                    const double draw = timer.nsecsElapsed() / 1e6;
                    if (i >= 20) {
                        input.push_back(in); overlay.push_back(draw);
                        paint.push_back(canvas.paintMs); total.push_back(endToEnd);
                    }
                }
                if (!input.empty()) {
                    const char* name = tool == ToolId::Line ? "line" : "rect";
                    report(writeCsv ? &csv : nullptr, count, name, snapping, "input", input);
                    report(writeCsv ? &csv : nullptr, count, name, snapping, "overlay", overlay);
                    report(writeCsv ? &csv : nullptr, count, name, snapping, "paint", paint);
                    report(writeCsv ? &csv : nullptr, count, name, snapping, "input-paint", total);
                }
                if (canvas.sceneRebuildsForTest() != rebuilt) {
                    std::printf("  [FAIL] drawing preview rebuilt the static scene\n");
                    ++failures;
                }
            }
        }
    }
    return failures;
}
