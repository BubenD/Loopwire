#include "app/HarnessBuilderDialog.h"

#include "app/Editor.h"
#include "harness/Harness.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace Edraw_Desktop {

namespace {

// A builder pattern is saved to a FILE, so a harness definition can be kept beside the
// drawing, mailed, or version-controlled like any other document. These two only remember
// where the last one went (and the colours the user has invented), so the file dialogs open
// somewhere sensible.
constexpr auto kLastPathKey = "edraw/harnessBuilder/lastFile";
constexpr auto kCustomColorsKey = "edraw/harnessBuilder/customColors";
// Builder Configuration File.
constexpr auto kFileFilter = "Harness builder pattern (*.bcf);;All files (*)";
constexpr auto kFileSuffix = ".bcf";
// Marks the colour combo entry that invents a new colour, kept out of the code namespace.
constexpr auto kCustomColorItem = "\x01" "add-custom";

// Scrolling the wheel over the page must never change a value. A spin box or a drop-down
// under the cursor would otherwise quietly step to the next value as the user scrolled past it,
// which on this dialog silently re-pitches a connector or re-colours a wire. The event is eaten
// and turned into a scroll of the enclosing page instead, so the wheel does what it looks like
// it should do and nothing else.
class WheelGuard : public QObject {
public:
    static WheelGuard* instance() {
        static WheelGuard g;
        return &g;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        if (ev->type() != QEvent::Wheel) return QObject::eventFilter(watched, ev);
        auto* w = qobject_cast<QWidget*>(watched);
        const int dy = static_cast<QWheelEvent*>(ev)->angleDelta().y();
        for (QWidget* up = w; up; up = up->parentWidget())
            if (auto* area = qobject_cast<QScrollArea*>(up)) {
                if (QScrollBar* bar = area->verticalScrollBar())
                    bar->setValue(bar->value() - dy);
                break;
            }
        return true; // never reaches the field
    }
};
// Fields are guarded as they are built, because the tables rebuild their rows on every reload.
void guardWheel(QWidget* w) {
    w->installEventFilter(WheelGuard::instance());
    // Without this a guarded spin box would still take the wheel when it happened to have
    // focus; click-to-focus is the only way to change a value now.
    w->setFocusPolicy(Qt::StrongFocus);
}

// Size a table's columns to what is actually IN them: a cell widget's own width hint, not just
// the header text, so a drop-down never has its text clipped. `stretch` names a column that
// takes the slack. `bump` widens one column by a fraction of its fitted width.
void fitColumns(QTableWidget* t, const QHash<int, double>& bump = {}) {
    for (int c = 0; c < t->columnCount(); ++c) {
        int want = t->fontMetrics().horizontalAdvance(
                       t->horizontalHeaderItem(c) ? t->horizontalHeaderItem(c)->text() : QString()) +
                   18;
        for (int r = 0; r < t->rowCount(); ++r) {
            // A group header spans the whole row; its width says nothing about this column.
            if (t->columnSpan(r, c) > 1) continue;
            QWidget* cell = t->cellWidget(r, c);
            if (!cell) continue;
            want = std::max(want, cell->sizeHint().width() + 8);
            // A drop-down's own hint is only as wide as the text it happens to be showing, so
            // a longer entry would be clipped the moment it was chosen. Measure the widest.
            if (auto* box = qobject_cast<QComboBox*>(cell)) {
                int widest = 0;
                for (int i = 0; i < box->count(); ++i)
                    widest = std::max(widest, box->fontMetrics().horizontalAdvance(box->itemText(i)));
                want = std::max(want, widest + 52); // arrow, swatch and frame
            }
        }
        want = static_cast<int>(want * bump.value(c, 1.0));
        t->setColumnWidth(c, want);
    }
}

// Every field in the dialog reads centred, which is how a table of pins, pitches and counts
// is read. Spin boxes take an alignment directly; a combo box only has one if it is given an
// (read-only) editor, which also leaves its drop-down working as before.
void centerField(QAbstractSpinBox* sp) {
    sp->setAlignment(Qt::AlignCenter);
    guardWheel(sp);
}
void centerCombo(QComboBox* box) {
    guardWheel(box);
    box->setEditable(true);
    box->lineEdit()->setReadOnly(true);
    box->lineEdit()->setAlignment(Qt::AlignCenter);
    box->lineEdit()->setCursor(Qt::ArrowCursor);
    // An editable combo paints its own background; keep it looking like the plain one.
    box->lineEdit()->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
}
void centerEdit(QLineEdit* ed) { ed->setAlignment(Qt::AlignCenter); }

// A colour swatch for a combo entry, so a wire colour is picked by eye rather than by code.
QIcon colorSwatch(const QString& hex) {
    QPixmap pm(16, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QPen(QColor("#20242b"), 1));
    p.setBrush(QColor(hex));
    p.drawRect(0, 0, 15, 11);
    return QIcon(pm);
}

// Cross-sections offered for a wire, labelled the way a harness drawing reads them.
struct GaugeEntry { const char* label; const char* mm2; };
const GaugeEntry kGauges[] = {
    {"24 AWG (0.25mm²)", "0.25"}, {"22 AWG (0.34mm²)", "0.34"},
    {"20 AWG (0.5mm²)", "0.5"},   {"18 AWG (0.75mm²)", "0.75"},
    {"16 AWG (1.0mm²)", "1.0"},   {"14 AWG (2.5mm²)", "2.5"},
    {"12 AWG (4.0mm²)", "4.0"},   {"10 AWG (6.0mm²)", "6.0"},
};

QString gaugeLabelFor(const QString& mm2) {
    for (const auto& g : kGauges)
        if (mm2 == QLatin1String(g.mm2)) return QString::fromUtf8(g.label);
    return mm2;
}
QString gaugeValueFor(const QString& label) {
    for (const auto& g : kGauges)
        if (label == QString::fromUtf8(g.label)) return QString::fromLatin1(g.mm2);
    return label;
}

// The custom colours live in the settings so an invented colour is still there next session.
void loadCustomColors() {
    const QJsonArray arr =
        QJsonDocument::fromJson(QSettings().value(QLatin1String(kCustomColorsKey))
                                    .toString().toUtf8()).array();
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        registerCustomWireColor(o.value("code").toString(), o.value("name").toString(),
                                o.value("hex").toString());
    }
}
void saveCustomColor(const QString& code, const QString& name, const QString& hex) {
    QSettings st;
    QJsonArray arr =
        QJsonDocument::fromJson(st.value(QLatin1String(kCustomColorsKey)).toString().toUtf8())
            .array();
    for (int i = 0; i < arr.size(); ++i)
        if (arr[i].toObject().value("code").toString() == code) arr.removeAt(i--);
    QJsonObject o;
    o["code"] = code;
    o["name"] = name;
    o["hex"] = hex;
    arr.append(o);
    st.setValue(QLatin1String(kCustomColorsKey),
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

} // namespace

// A small drawing of one connector with the wires that reach its pins, so the pin map shows
// at a glance what is landing where: the body with its pins in their CURRENT order, and a
// stub in each wire's own colour for every populated pin. Free pins get a short grey tick.
class ConnectorPreview : public QWidget {
public:
    ConnectorPreview(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(112, 92);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("Click to see this connector full size"));
    }
    // Clicking a thumbnail opens the same drawing large enough to read. A plain callback
    // rather than a signal: the class lives in this translation unit and has no moc.
    void setOnActivate(std::function<void()> cb) { onActivate_ = std::move(cb); }
    QStringList pinLabels() const { return labels_; }
    QStringList pinColors() const { return colors_; }
    bool exitsRight() const { return exitRight_; }
    // `wireColors` is one entry per pin ROW (top-down), empty where the pin is free.
    void setPins(const QStringList& labels, const QStringList& wireColors, bool pinsExitRight) {
        labels_ = labels;
        colors_ = wireColors;
        exitRight_ = pinsExitRight;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && onActivate_) onActivate_();
        e->accept();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int n = labels_.size();
        if (n <= 0) return;
        // Everything scales off the widget, so the same drawing reads at thumbnail size and
        // blown up in the pop-up.
        const double s = std::max(1.0, std::min(width() / 112.0, height() / 92.0));
        const double margin = 6.0 * s;
        const double bodyW = 26.0 * s;
        const double h = height() - 2 * margin;
        const double pitch = h / std::max(1, n);
        const double bodyX = exitRight_ ? margin : width() - margin - bodyW;
        p.setPen(QPen(QColor("#cfd8e6"), 1.2));
        p.drawRect(QRectF(bodyX, margin, bodyW, h));
        QFont f = p.font();
        f.setPixelSize(std::max(8, static_cast<int>(8 * s)));
        p.setFont(f);
        const double stub = 8.0 * s;
        for (int i = 0; i < n; ++i) {
            const double y = margin + pitch * (i + 0.5);
            // The pin stub leaves the body toward the harness; the wire (if any) carries on
            // in its own colour so the pinout reads like the drawing it will produce.
            const double from = exitRight_ ? bodyX + bodyW : bodyX;
            const double to = exitRight_ ? width() - margin : margin;
            const bool wired = i < colors_.size() && !colors_[i].isEmpty();
            p.setPen(QPen(QColor("#cfd8e6"), 1.0 * s));
            p.drawLine(QPointF(from, y), QPointF(from + (exitRight_ ? stub : -stub), y));
            if (wired)
                p.setPen(QPen(QColor(wireColorHex(colors_[i])), 1.6 * s));
            else
                p.setPen(QPen(QColor("#5b6572"), 1.0 * s, Qt::DotLine));
            p.drawLine(QPointF(from + (exitRight_ ? stub : -stub), y), QPointF(to, y));
            // The pin label sits inside the body.
            p.setPen(QColor("#9fb4c8"));
            p.drawText(QRectF(bodyX, y - pitch / 2.0, bodyW, pitch),
                       Qt::AlignCenter, labels_.value(i));
        }
    }

private:
    QStringList labels_;
    QStringList colors_;
    bool exitRight_ = true;
    std::function<void()> onActivate_;
};

// A thin grab strip under a section: dragging it changes THAT section's height and nothing
// else's. The sections then simply move down (or up) the scrolling column, instead of one
// growing at another's expense the way a splitter would — enlarging the wire list must not
// squeeze the connector table shut.
class SectionResizer : public QWidget {
public:
    SectionResizer(QWidget* target, QWidget* parent)
        : QWidget(parent), target_(target) {
        setFixedHeight(7);
        setCursor(Qt::SizeVerCursor);
        setToolTip(QStringLiteral("Drag to make this section taller or shorter"));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        // Three short bars, the usual "grab me" hint.
        p.setPen(QPen(QColor("#5b6572"), 1.4));
        const double cx = width() / 2.0, cy = height() / 2.0;
        for (int i = -1; i <= 1; ++i)
            p.drawLine(QPointF(cx + i * 14 - 5, cy), QPointF(cx + i * 14 + 5, cy));
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton || !target_) return;
        dragging_ = true;
        startY_ = e->globalPosition().toPoint().y();
        startH_ = target_->height();
        e->accept();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging_ || !target_) return;
        const int dy = e->globalPosition().toPoint().y() - startY_;
        // A floor rather than a collapse: a section can be made small but never shut, so the
        // controls inside it stay reachable.
        target_->setFixedHeight(std::max(60, startH_ + dy));
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        dragging_ = false;
        e->accept();
    }

private:
    QWidget* target_ = nullptr;
    bool dragging_ = false;
    int startY_ = 0;
    int startH_ = 0;
};

// The whole harness drawn small: the left-hand connectors down the left, the right-hand ones
// down the right STACKED IN PLACEMENT ORDER, and every wire drawn pin to pin in its own
// colour. It sits beside the pin tables so the effect of a pin drag can be seen immediately —
// the full wiring, not one connector at a time.
class HarnessWiringPreview : public QWidget {
public:
    // Where one connector's slice of the height sits. At class scope because a type declared
    // inside the function cannot be named in a lambda's trailing return type.
    struct Slot {
        const HarnessConnector* c = nullptr;
        double top = 0.0;
        double pitch = 0.0;
        bool left = true;
    };

    HarnessWiringPreview(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumWidth(320);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setToolTip(QStringLiteral("The whole harness as it will be drawn — click to enlarge"));
        setCursor(Qt::PointingHandCursor);
    }
    void setDefinition(const HarnessDef& def) {
        def_ = def;
        update();
    }
    const HarnessDef& definition() const { return def_; }
    void setOnActivate(std::function<void()> cb) { onActivate_ = std::move(cb); }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && onActivate_) onActivate_();
        e->accept();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor("#20242b"));
        if (def_.connectors.empty()) return;
        const double margin = 10.0;
        const double bodyW = 22.0;
        QFont f = p.font();
        // Scaled off the widget width so the same drawing reads at thumbnail size and enlarged.
        f.setPixelSize(std::max(7, static_cast<int>(7 * std::min(3.0, width() / 320.0))));
        p.setFont(f);

        // Lay each side out top-down in placement order, giving every connector a slice of the
        // height in proportion to its pin count so the rows line up with the real drawing.
        // NB: not named `slots` — Qt defines that as a macro for the signals/slots
        // syntax, so it expands to nothing and silently eats the declaration.
        std::vector<Slot> slotList;
        for (bool left : {true, false}) {
            std::vector<const HarnessConnector*> side;
            for (const auto& c : def_.connectors)
                if ((c.side == HarnessSide::Left) == left) side.push_back(&c);
            std::sort(side.begin(), side.end(),
                      [](const HarnessConnector* a, const HarnessConnector* b) {
                          if (a->order != b->order) return a->order < b->order;
                          return a->designator < b->designator;
                      });
            int totalPins = 0;
            for (const HarnessConnector* c : side) totalPins += std::max(1, c->pinCount);
            if (totalPins <= 0) continue;
            // One gap-worth of space between stacked connectors.
            const int gaps = std::max(0, static_cast<int>(side.size()) - 1);
            const double usable = height() - 2.0 * margin - 10.0 * gaps;
            const double perPin = usable / totalPins;
            double y = margin;
            for (const HarnessConnector* conn : side) {
                Slot sl;
                sl.c = conn;
                sl.top = y;
                sl.pitch = perPin;
                sl.left = left;
                slotList.push_back(sl);
                y += perPin * std::max(1, conn->pinCount) + 10.0;
            }
        }
        auto slotOf = [&slotList](const QString& id) -> const Slot* {
            for (const Slot& s : slotList)
                if (s.c->id == id) return &s;
            return nullptr;
        };
        // Where a pin sits: its row within its connector's slice.
        auto pinPoint = [&](const Slot& s, int pin, double& x, double& y) {
            const std::vector<int> seq = harnessPinSequence(*s.c);
            int row = 0;
            for (int i = 0; i < static_cast<int>(seq.size()); ++i)
                if (seq[i] == pin) row = i;
            y = s.top + s.pitch * (row + 0.5);
            x = s.left ? margin + bodyW : width() - margin - bodyW;
        };

        // Bodies + pin labels.
        for (const Slot& s : slotList) {
            const double h = s.pitch * std::max(1, s.c->pinCount);
            const double x = s.left ? margin : width() - margin - bodyW;
            p.setPen(QPen(QColor("#cfd8e6"), 1.1));
            p.drawRect(QRectF(x, s.top, bodyW, h));
            p.setPen(QColor("#9fb4c8"));
            const std::vector<int> seq = harnessPinSequence(*s.c);
            for (int row = 0; row < static_cast<int>(seq.size()); ++row)
                p.drawText(QRectF(x, s.top + s.pitch * row, bodyW, s.pitch), Qt::AlignCenter,
                           harnessPinLabel(*s.c, seq[row]));
            // The designator, outboard of the body.
            p.setPen(QColor("#cfe3ff"));
            const QRectF tag = s.left ? QRectF(0, s.top - 10, margin + bodyW, 10)
                                      : QRectF(width() - margin - bodyW, s.top - 10,
                                               margin + bodyW, 10);
            p.drawText(tag, Qt::AlignCenter,
                       s.c->designator.isEmpty() ? s.c->id : s.c->designator);
        }

        // Wires, each in its own colour, routed out to a middle lane like the real drawing.
        // Work the ends out first so the lanes can be ORDERED: a wire's vertical run cuts
        // across another's horizontal whenever it turns inside that wire while reaching past
        // its row, so a wire that reaches over others has to turn outside them. Ranking each
        // wire by how many other wires' rows it spans gives that nesting — the same rule the
        // generator uses, so the preview shows the routing that will actually be drawn instead
        // of an arbitrary lane per wire.
        struct Leg {
            const HarnessWire* w = nullptr;
            double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
            bool knee = false;
            int rank = 0;
        };
        std::vector<Leg> legs;
        for (const auto& w : def_.wires) {
            const Slot* a = slotOf(w.fromConn);
            const Slot* b = slotOf(w.toConn);
            if (!a || !b) continue;
            Leg lg;
            lg.w = &w;
            pinPoint(*a, w.fromPin, lg.ax, lg.ay);
            pinPoint(*b, w.toPin, lg.bx, lg.by);
            lg.knee = std::abs(lg.ay - lg.by) >= 0.75;
            legs.push_back(lg);
        }
        {
            std::vector<Leg*> kneed;
            std::vector<std::pair<double, double>> rows;
            for (Leg& lg : legs)
                if (lg.knee) {
                    kneed.push_back(&lg);
                    // Left row first, whichever end the wire was defined from - the same
                    // orientation the generator scores its lanes in.
                    rows.push_back(lg.ax < lg.bx ? std::make_pair(lg.ay, lg.by)
                                                 : std::make_pair(lg.by, lg.ay));
                }
            const std::vector<int> lanes = harnessKneeLanes(rows);
            for (size_t i = 0; i < kneed.size(); ++i) kneed[i]->rank = lanes[i];
        }
        // The turns form one regular fan: a single start line in the middle of the corridor and
        // an even gap between neighbours, the way the generator lays them out.
        const double laneLo = margin + bodyW + 14.0;
        const double laneHi = width() - margin - bodyW - 14.0;
        int kneeCount = 0;
        for (const Leg& lg : legs)
            if (lg.knee) ++kneeCount;
        const double laneStep =
            std::max(4.0, (laneHi - laneLo) / std::max(2, kneeCount + 1));
        const double laneStart =
            (laneLo + laneHi) / 2.0 - (kneeCount - 1) * laneStep / 2.0;
        for (const Leg& lg : legs) {
            QColor col(wireColorHex(lg.w->color));
            if (!col.isValid()) col = QColor("#cfd8e6");
            p.setPen(QPen(col, 1.3));
            if (!lg.knee) {
                p.drawLine(QPointF(lg.ax, lg.ay), QPointF(lg.bx, lg.by));
                continue;
            }
            const double mx = laneStart + lg.rank * laneStep;
            p.drawLine(QPointF(lg.ax, lg.ay), QPointF(mx, lg.ay));
            p.drawLine(QPointF(mx, lg.ay), QPointF(mx, lg.by));
            p.drawLine(QPointF(mx, lg.by), QPointF(lg.bx, lg.by));
        }
    }

private:
    HarnessDef def_;
    std::function<void()> onActivate_;
};

namespace {

// Show one of the preview widgets full size. A plain frameless-ish dialog holding a fresh,
// larger copy of the same drawing: nothing to keep in sync, and Esc closes it.
void showPreviewPopup(QWidget* parent, const QString& title, QWidget* big) {
    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle(title);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->resize(760, 620);
    auto* lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(10, 10, 10, 10);
    big->setParent(dlg);
    big->setCursor(Qt::ArrowCursor);
    lay->addWidget(big, 1);
    auto* close = new QPushButton(QStringLiteral("Close"), dlg);
    QObject::connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    footer->addWidget(close);
    lay->addLayout(footer);
    dlg->show();
}

// A section frame with a small caption, matching the builder's banded look.
QWidget* section(const QString& title, QWidget* body, QWidget* parent,
                 const QList<QWidget*>& headerWidgets = {}) {
    auto* box = new QWidget(parent);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 6, 0, 0);
    v->setSpacing(4);
    auto* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    auto* cap = new QLabel(title.toUpper(), box);
    cap->setStyleSheet(QStringLiteral("color:#4aa8d8;font-weight:700;letter-spacing:1px;"));
    head->addWidget(cap);
    for (QWidget* w : headerWidgets) head->addWidget(w);
    head->addStretch(1);
    v->addLayout(head);
    v->addWidget(body);
    return box;
}

} // namespace

HarnessBuilderDialog::HarnessBuilderDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    loadCustomColors(); // an invented wire colour is still there next session
    setWindowTitle(QStringLiteral("Schematic harness builder"));
    setModal(false); // the drawing stays usable while a harness is being described
    // Wide enough for the connector and wire tables to show every column without scrolling
    // sideways, clamped to whatever the screen can actually hold.
    {
        const QRect avail = QGuiApplication::primaryScreen()
                                ? QGuiApplication::primaryScreen()->availableGeometry()
                                : QRect(0, 0, 1440, 900);
        resize(std::min(1460, avail.width() - 60), std::min(980, avail.height() - 60));
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(6);

    // The middle of the dialog scrolls, so a harness with many connectors/wires still fits
    // on a small screen without the footer buttons ever leaving the view.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // The sections stack in a plain scrolling column, each with its own grab strip under it.
    // Dragging one changes only ITS height and pushes the rest along — growing the wire list
    // never squeezes the connector table shut, which is what a splitter would have done.
    auto* page = new QWidget(scroll);
    auto* pageLay = new QVBoxLayout(page);
    pageLay->setContentsMargins(0, 0, 0, 0);
    pageLay->setSpacing(0);
    struct Sec { QWidget* w; int height; };
    const Sec secs[] = {{buildConnectorsSection(), 190}, {buildBlockMapSection(), 150},
                        {buildPinMapSection(), 250},    {buildWiresSection(), 260},
                        {buildLayoutSection(), 120},    {buildIssuesSection(), 110}};
    for (const Sec& s : secs) {
        s.w->setFixedHeight(s.height); // a starting height the user can drag from
        pageLay->addWidget(s.w);
        pageLay->addWidget(new SectionResizer(s.w, page));
    }
    pageLay->addStretch(1);
    scroll->setWidget(page);
    root->addWidget(scroll, 1);

    // ---- Footer: validate / minimise on the left, the commit buttons on the right ----
    auto* footer = new QHBoxLayout();
    auto* validateBtn = new QPushButton(QStringLiteral("Validate"), this);
    validateBtn->setToolTip(QStringLiteral(
        "Check the definition: repeated pin names, shields or twisted groups split by "
        "another wire, wires on missing pins, and pins carrying two wires"));
    connect(validateBtn, &QPushButton::clicked, this, [this] { runValidate(); });
    auto* minBtn = new QPushButton(QStringLiteral("Minimise crossings"), this);
    minBtn->setToolTip(QStringLiteral(
        "Re-order the pins of every connector for the fewest crossings while keeping "
        "twisted pairs and shielded wires on neighbouring pins (the connectors themselves "
        "stay where they are)"));
    connect(minBtn, &QPushButton::clicked, this, [this] { runMinimizeCrossings(); });
    // Targeted versions of the same optimiser, for when one kind of bundling has to win.
    auto* shieldBtn = new QPushButton(QStringLiteral("Optimise by shield"), this);
    shieldBtn->setToolTip(QStringLiteral(
        "Re-order the pins so every shielded group lands on neighbouring pins, so each "
        "screen encloses a solid band"));
    connect(shieldBtn, &QPushButton::clicked, this, [this] { runOptimizeBy(/*shield=*/true); });
    auto* twistBtn = new QPushButton(QStringLiteral("Optimise by twist"), this);
    twistBtn->setToolTip(QStringLiteral(
        "Re-order the pins so every twisted group lands on neighbouring pins, so each pair "
        "can actually be braided"));
    connect(twistBtn, &QPushButton::clicked, this, [this] { runOptimizeBy(/*shield=*/false); });
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    footer->addWidget(validateBtn);
    footer->addWidget(minBtn);
    footer->addWidget(shieldBtn);
    footer->addWidget(twistBtn);
    footer->addWidget(summaryLabel_, 1);

    auto* generateBtn = new QPushButton(QStringLiteral("Generate"), this);
    generateBtn->setDefault(true);
    generateBtn->setToolTip(QStringLiteral("Draw the harness on the sheet (one undo step)"));
    connect(generateBtn, &QPushButton::clicked, this, [this] { generate(); });
    auto* saveBtn = new QPushButton(QStringLiteral("Save"), this);
    saveBtn->setToolTip(QStringLiteral("Remember this definition for next time"));
    connect(saveBtn, &QPushButton::clicked, this, [this] { saveDefinition(); });
    auto* loadBtn = new QPushButton(QStringLiteral("Load"), this);
    loadBtn->setToolTip(QStringLiteral("Bring back the last saved definition"));
    connect(loadBtn, &QPushButton::clicked, this, [this] { loadDefinition(); });
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    footer->addWidget(generateBtn);
    footer->addWidget(saveBtn);
    footer->addWidget(loadBtn);
    footer->addWidget(closeBtn);
    root->addLayout(footer);

    // The window hides itself while a generated harness is being positioned; when the
    // placement ends — landed or abandoned with Esc — it comes back so the definition can be
    // adjusted and generated again.
    if (editor_)
        connect(editor_, &Editor::harnessPlacementFinished, this, [this](bool placed) {
            show();
            raise();
            activateWindow();
            summaryLabel_->setText(placed ? QStringLiteral("Harness placed")
                                          : QStringLiteral("Placement cancelled — nothing drawn"));
        });

    // A plausible two-connector harness, so the dialog is never an empty form. A saved
    // pattern is opened deliberately with Load rather than dragged in behind the user's back.
    if (def_.connectors.empty()) {
        addConnector(HarnessSide::Left);
        addConnector(HarnessSide::Right);
    }
    reloadAll();
}

// ---- Step 1: connectors ----------------------------------------------------

QWidget* HarnessBuilderDialog::buildConnectorsSection() {
    auto* addLeft = new QPushButton(QStringLiteral("+ Left"), this);
    auto* addRight = new QPushButton(QStringLiteral("+ Right"), this);
    connect(addLeft, &QPushButton::clicked, this, [this] {
        addConnector(HarnessSide::Left);
        reloadAll();
    });
    connect(addRight, &QPushButton::clicked, this, [this] {
        addConnector(HarnessSide::Right);
        reloadAll();
    });

    connTable_ = new QTableWidget(this);
    connTable_->setColumnCount(13);
    connTable_->setHorizontalHeaderLabels({QStringLiteral("Side"), QStringLiteral("Des"),
                                          QStringLiteral("P/N"), QStringLiteral("Ann"),
                                          QStringLiteral("Pins"), QStringLiteral("Pitch"),
                                          QStringLiteral("Pin len"), QStringLiteral("Leg"),
                                          QStringLiteral("L overall shield"),
                                          QStringLiteral("L shield→body"),
                                          QStringLiteral("R overall shield"),
                                          QStringLiteral("R shield→body"), QString()});
    connTable_->verticalHeader()->setVisible(false);
    connTable_->horizontalHeader()->setStretchLastSection(true);
    connTable_->setSelectionMode(QAbstractItemView::NoSelection);
    connTable_->setMinimumHeight(140);
    return section(QStringLiteral("Connectors"), connTable_, this, {addLeft, addRight});
}

void HarnessBuilderDialog::addConnector(HarnessSide side) {
    HarnessConnector c;
    c.id = uniqueConnectorId();
    c.side = side;
    ++connSeq_;
    c.designator = QStringLiteral("P%1").arg(connSeq_);
    c.pinCount = 4;
    // Place it last down its own side.
    int maxOrder = -1;
    for (const auto& other : def_.connectors)
        if (other.side == side) maxOrder = std::max(maxOrder, other.order);
    c.order = maxOrder + 1;
    def_.connectors.push_back(c);
}

void HarnessBuilderDialog::removeConnector(int row) {
    if (row < 0 || row >= static_cast<int>(def_.connectors.size())) return;
    const QString id = def_.connectors[row].id;
    def_.connectors.erase(def_.connectors.begin() + row);
    // Wires that pointed at it have nowhere to go, so they go too.
    def_.wires.erase(std::remove_if(def_.wires.begin(), def_.wires.end(),
                                    [&id](const HarnessWire& w) {
                                        return w.fromConn == id || w.toConn == id;
                                    }),
                     def_.wires.end());
    reloadAll();
}

void HarnessBuilderDialog::reloadConnectors() {
    connTable_->setRowCount(0);
    for (int row = 0; row < static_cast<int>(def_.connectors.size()); ++row) {
        const HarnessConnector& c = def_.connectors[row];
        connTable_->insertRow(row);
        const QString id = c.id;

        auto* sideBox = new QComboBox(connTable_);
        sideBox->addItems({QStringLiteral("Left"), QStringLiteral("Right")});
        sideBox->setCurrentIndex(c.side == HarnessSide::Right ? 1 : 0);
        centerCombo(sideBox);
        connect(sideBox, &QComboBox::currentIndexChanged, this, [this, id](int i) {
            if (syncing_) return;
            if (auto* cc = connById(id)) {
                cc->side = (i == 1) ? HarnessSide::Right : HarnessSide::Left;
                // Moving sides puts it last on the side it joins.
                int maxOrder = -1;
                for (const auto& o : def_.connectors)
                    if (o.side == cc->side && o.id != cc->id) maxOrder = std::max(maxOrder, o.order);
                cc->order = maxOrder + 1;
            }
            reloadAll();
        });
        connTable_->setCellWidget(row, 0, sideBox);

        auto* des = new QLineEdit(c.designator, connTable_);
        des->setPlaceholderText(QStringLiteral("P1"));
        centerEdit(des);
        connect(des, &QLineEdit::editingFinished, this, [this, id, des] {
            if (syncing_) return;
            if (auto* cc = connById(id)) cc->designator = des->text().trimmed();
            reloadAll();
        });
        connTable_->setCellWidget(row, 1, des);

        auto* pn = new QLineEdit(c.partNumber, connTable_);
        pn->setPlaceholderText(QStringLiteral("part number"));
        centerEdit(pn);
        connect(pn, &QLineEdit::editingFinished, this, [this, id, pn] {
            if (syncing_) return;
            if (auto* cc = connById(id)) cc->partNumber = pn->text().trimmed();
        });
        connTable_->setCellWidget(row, 2, pn);

        auto* ann = new QComboBox(connTable_);
        // How the pins are named: 1,2,3... / A,B,C... / or a letter for each block of ten with
        // the position inside it (A1..A10, B1..B10), which is how a multi-row connector reads.
        ann->addItem(QStringLiteral("Num"), QStringLiteral("numbers"));
        ann->addItem(QStringLiteral("Alpha"), QStringLiteral("letters"));
        ann->addItem(QStringLiteral("Mixed"), QStringLiteral("mixed"));
        ann->setToolTip(QStringLiteral("Pin names: 1,2,3 / A,B,C / A1,A2..B1,B2 (mixed)"));
        ann->setCurrentIndex(std::max(0, ann->findData(c.annotation)));
        centerCombo(ann);
        connect(ann, &QComboBox::currentIndexChanged, this, [this, id, ann](int i) {
            if (syncing_) return;
            if (auto* cc = connById(id)) cc->annotation = ann->itemData(i).toString();
            reloadAll();
        });
        connTable_->setCellWidget(row, 3, ann);

        auto* pins = new QSpinBox(connTable_);
        pins->setRange(1, 128);
        pins->setValue(std::max(1, c.pinCount));
        // Plain fields to type into: the +/- steppers cost width and invite mis-clicks.
        pins->setButtonSymbols(QAbstractSpinBox::NoButtons);
        centerField(pins);
        connect(pins, &QSpinBox::valueChanged, this, [this, id](int v) {
            if (syncing_) return;
            if (auto* cc = connById(id)) {
                cc->pinCount = v;
                // Drop any stored pin order entry that no longer exists.
                cc->pinOrder.erase(std::remove_if(cc->pinOrder.begin(), cc->pinOrder.end(),
                                                  [v](int p) { return p < 1 || p > v; }),
                                   cc->pinOrder.end());
            }
            reloadAll();
        });
        connTable_->setCellWidget(row, 4, pins);

        auto makeNum = [this](double value, double lo, double hi) {
            auto* sp = new QDoubleSpinBox(connTable_);
            sp->setRange(lo, hi);
            sp->setDecimals(1);
            sp->setValue(value);
            sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
            centerField(sp);
            return sp;
        };
        auto* pitch = makeNum(c.pitch, 1.0, 500.0);
        connect(pitch, &QDoubleSpinBox::valueChanged, this, [this, id](double v) {
            if (syncing_) return;
            if (auto* cc = connById(id)) cc->pitch = v;
        });
        connTable_->setCellWidget(row, 5, pitch);
        auto* plen = makeNum(c.pinLength, 1.0, 500.0);
        connect(plen, &QDoubleSpinBox::valueChanged, this, [this, id](double v) {
            if (syncing_) return;
            if (auto* cc = connById(id)) cc->pinLength = v;
        });
        connTable_->setCellWidget(row, 6, plen);

        // Y-split leg: connectors sharing a leg name are stacked together and their wires
        // run down that leg's own trunk. Blank = the main trunk.
        auto* leg = new QLineEdit(c.leg, connTable_);
        leg->setPlaceholderText(QStringLiteral("trunk"));
        centerEdit(leg);
        leg->setToolTip(QStringLiteral(
            "Y-split leg. Connectors with the same leg name form one branch; leave blank "
            "for the main trunk."));
        connect(leg, &QLineEdit::editingFinished, this, [this, id, leg] {
            if (syncing_) return;
            if (auto* cc = connById(id)) cc->leg = leg->text().trimmed();
            reloadAll();
        });
        connTable_->setCellWidget(row, 7, leg);

        // Which face of this connector a shield covering its wires hangs off, and whether
        // the drain is bonded back to the body. A centred checkbox per column reads as a
        // column of ticks down the table.
        auto boolCell = [this](int atRow, int atCol, bool value, const QString& tip,
                               const std::function<void(HarnessConnector&, bool)>& set,
                               const QString& id, const QString& tint = QString()) {
            auto* host = new QWidget(connTable_);
            if (!tint.isEmpty()) {
                host->setAutoFillBackground(true);
                host->setStyleSheet(tint);
            }
            auto* lay = new QHBoxLayout(host);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setAlignment(Qt::AlignCenter);
            auto* box = new QCheckBox(host);
            box->setChecked(value);
            box->setToolTip(tip);
            connect(box, &QCheckBox::toggled, this, [this, id, set](bool on) {
                if (syncing_) return;
                if (auto* cc = connById(id)) set(*cc, on);
            });
            lay->addWidget(box);
            connTable_->setCellWidget(atRow, atCol, host);
        };
        // The left pair and the right pair are tinted differently, so at a glance it is clear
        // which end of the harness each tick belongs to. Only the pair for the connector's OWN
        // side is offered: a left-hand connector lives at the left end of the harness, so a
        // right-end shield on it means nothing and its cells are left empty.
        const QString leftTint = QStringLiteral("background:#2b3a44;");
        const QString rightTint = QStringLiteral("background:#3a3040;");
        if (c.side == HarnessSide::Left) {
            boolCell(row, 8, c.shieldLeft,
                     QStringLiteral("Overall shield: screen everything arriving at this "
                                    "connector, drawn just inboard of its pins"),
                     [](HarnessConnector& cc, bool on) { cc.shieldLeft = on; }, id, leftTint);
            boolCell(row, 9, c.bodyBondLeft,
                     QStringLiteral("Bond this connector's overall shield drain back to its own "
                                    "body joint"),
                     [](HarnessConnector& cc, bool on) { cc.bodyBondLeft = on; }, id, leftTint);
        } else {
            boolCell(row, 10, c.shieldRight,
                     QStringLiteral("Overall shield: screen everything arriving at this "
                                    "connector, drawn just inboard of its pins"),
                     [](HarnessConnector& cc, bool on) { cc.shieldRight = on; }, id, rightTint);
            boolCell(row, 11, c.bodyBondRight,
                     QStringLiteral("Bond this connector's overall shield drain back to its own "
                                    "body joint"),
                     [](HarnessConnector& cc, bool on) { cc.bodyBondRight = on; }, id, rightTint);
        }

        auto* delW = new QWidget(connTable_);
        auto* delLay = new QHBoxLayout(delW);
        delLay->setContentsMargins(4, 0, 4, 0);
        auto* del = new QToolButton(delW);
        del->setText(QStringLiteral("×"));
        del->setToolTip(QStringLiteral("Remove this connector (and its wires)"));
        del->setStyleSheet(QStringLiteral("color:#e06c6c;font-weight:700;"));
        connect(del, &QToolButton::clicked, this, [this, id] {
            for (int i = 0; i < static_cast<int>(def_.connectors.size()); ++i)
                if (def_.connectors[i].id == id) { removeConnector(i); return; }
        });
        delLay->addStretch(1);
        delLay->addWidget(del);
        connTable_->setCellWidget(row, 12, delW);
    }
    // Every column as wide as what is in it, with Side given a little extra so the drop-down
    // arrow does not crowd the word.
    fitColumns(connTable_, {{0, 1.15}});
}

// ---- Step 2: the placement block map (drag to reorder) ---------------------

QWidget* HarnessBuilderDialog::buildBlockMapSection() {
    auto* host = new QWidget(this);
    auto* lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    auto makeSideList = [this](const QString& caption) {
        auto* col = new QWidget(this);
        auto* v = new QVBoxLayout(col);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        auto* cap = new QLabel(caption, col);
        cap->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        v->addWidget(cap);
        auto* list = new QListWidget(col);
        // Real drag-and-drop reordering: the order of the blocks IS the order the
        // connectors are stacked down that side of the drawing.
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setDefaultDropAction(Qt::MoveAction);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setMinimumHeight(110);
        list->setToolTip(QStringLiteral("Drag the blocks to set the order the connectors are "
                                       "placed down this side"));
        v->addWidget(list);
        return std::make_pair(col, list);
    };
    auto left = makeSideList(QStringLiteral("Left side (top → bottom)"));
    auto right = makeSideList(QStringLiteral("Right side (top → bottom)"));
    leftBlocks_ = left.second;
    rightBlocks_ = right.second;
    lay->addWidget(left.first, 1);
    lay->addWidget(right.first, 1);

    // A drop finishes the move; read the new order back into the definition. Deferred to
    // the next event loop pass because applying it rebuilds these very lists, which must
    // not happen while the model is still inside its own rowsMoved signal.
    for (QListWidget* list : {leftBlocks_, rightBlocks_})
        connect(list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
            if (syncing_) return;
            QTimer::singleShot(0, this, [this] { applyBlockOrder(); });
        });
    return section(QStringLiteral("Placement map"), host, this);
}

void HarnessBuilderDialog::reloadBlockMap() {
    for (QListWidget* list : {leftBlocks_, rightBlocks_}) list->clear();
    // Build each side in its current order, grouping by leg so a Y-split reads as blocks
    // belonging to a branch.
    for (const HarnessSide side : {HarnessSide::Left, HarnessSide::Right}) {
        QListWidget* list = (side == HarnessSide::Left) ? leftBlocks_ : rightBlocks_;
        std::vector<const HarnessConnector*> conns;
        for (const auto& c : def_.connectors)
            if (c.side == side) conns.push_back(&c);
        std::sort(conns.begin(), conns.end(),
                  [](const HarnessConnector* a, const HarnessConnector* b) {
                      if (a->order != b->order) return a->order < b->order;
                      return a->designator < b->designator;
                  });
        for (const HarnessConnector* c : conns) {
            const QString name = c->designator.isEmpty() ? c->id : c->designator;
            QString text = QStringLiteral("  %1   ·  %2 pins").arg(name).arg(c->pinCount);
            if (!c->leg.isEmpty()) text += QStringLiteral("   ·  leg %1").arg(c->leg);
            auto* item = new QListWidgetItem(text, list);
            item->setData(Qt::UserRole, c->id);
            item->setToolTip(QStringLiteral("Drag to move %1 up or down this side").arg(name));
        }
    }
}

void HarnessBuilderDialog::applyBlockOrder() {
    for (QListWidget* list : {leftBlocks_, rightBlocks_})
        for (int i = 0; i < list->count(); ++i) {
            const QString id = list->item(i)->data(Qt::UserRole).toString();
            if (auto* c = connById(id)) c->order = i;
        }
    reloadAll();
}

// ---- Step 3: the pin map ---------------------------------------------------

QWidget* HarnessBuilderDialog::buildPinMapSection() {
    // The pin tables on the left, and the whole wiring drawn beside them on the right, so a
    // pin drag can be judged against the finished picture straight away.
    auto* host = new QWidget(this);
    auto* row = new QHBoxLayout(host);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);

    auto* tables = new QScrollArea(host);
    tables->setWidgetResizable(true);
    tables->setFrameShape(QFrame::NoFrame);
    tables->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    pinMapHost_ = new QWidget(tables);
    pinMapLayout_ = new QHBoxLayout(pinMapHost_);
    pinMapLayout_->setContentsMargins(0, 0, 0, 0);
    pinMapLayout_->setSpacing(14);
    pinMapLayout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    tables->setWidget(pinMapHost_);
    row->addWidget(tables, 3);

    // Tall enough for a column's caption, its pin list and its little connector picture, so
    // the inner scroll area never has to cut the designator off at the top.
    pinMapHost_->setMinimumHeight(250);
    tables->setMinimumHeight(250);

    wiringPreview_ = new HarnessWiringPreview(host);
    wiringPreview_->setOnActivate([this] {
        auto* big = new HarnessWiringPreview;
        big->setDefinition(def_);
        showPreviewPopup(this, QStringLiteral("Harness wiring"), big);
    });
    row->addWidget(wiringPreview_, 2);
    return section(QStringLiteral("Pin map"), host, this);
}

void HarnessBuilderDialog::reloadPinMap() {
    // Rebuild the columns from scratch: one per connector, in table order.
    while (QLayoutItem* item = pinMapLayout_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    for (const auto& c : def_.connectors) {
        auto* col = new QWidget(pinMapHost_);
        auto* v = new QVBoxLayout(col);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        auto* cap = new QLabel(c.designator.isEmpty() ? c.id : c.designator, col);
        cap->setAlignment(Qt::AlignHCenter);
        cap->setMinimumHeight(18); // its own row, so nothing can shave the top off the letters
        cap->setStyleSheet(QStringLiteral("color:#cfd8e6;font-weight:600;"));
        v->addWidget(cap);

        auto* list = new QListWidget(col);
        // Dragging a pin chip changes the PIN ORDER — which pin sits at which row — without
        // touching the wiring, so the pinout can be arranged to suit the routing.
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setDefaultDropAction(Qt::MoveAction);
        list->setFixedWidth(120);
        list->setMinimumHeight(120);
        list->setToolTip(QStringLiteral("Drag a pin to change where it sits on the connector"));
        const QString connId = c.id;
        for (int pin : harnessPinSequence(c)) {
            // What is wired to this pin, so the map shows the pinout at a glance.
            QString detail = QStringLiteral("free");
            QString colorCode;
            for (const auto& w : def_.wires) {
                if (w.fromConn == connId && w.fromPin == pin) { detail = w.color; colorCode = w.color; break; }
                if (w.toConn == connId && w.toPin == pin) { detail = w.color; colorCode = w.color; break; }
            }
            auto* item = new QListWidgetItem(
                QStringLiteral("%1  %2").arg(harnessPinLabel(c, pin), detail), list);
            item->setData(Qt::UserRole, pin);
            if (!colorCode.isEmpty())
                item->setForeground(QColor(wireColorHex(colorCode)));
            else
                item->setForeground(QColor("#7d8896")); // free pins read as muted
        }
        // Deferred for the same reason as the placement map: applying the new order
        // rebuilds these columns.
        connect(list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
            if (syncing_) return;
            QTimer::singleShot(0, this, [this] { applyPinOrder(); });
        });
        list->setProperty("connId", connId);
        v->addWidget(list);

        // A small picture of the connector beside its pin list, showing which pins carry a
        // wire and in what colour — the pinout as it will be drawn, next to the pinout being
        // edited.
        auto* preview = new ConnectorPreview(col);
        QStringList labels, colors;
        for (int pin : harnessPinSequence(c)) {
            labels << harnessPinLabel(c, pin);
            QString code;
            for (const auto& w : def_.wires) {
                if (w.fromConn == connId && w.fromPin == pin) { code = w.color; break; }
                if (w.toConn == connId && w.toPin == pin) { code = w.color; break; }
            }
            colors << code;
        }
        preview->setPins(labels, colors, c.side == HarnessSide::Left);
        const QString caption = c.designator.isEmpty() ? c.id : c.designator;
        preview->setOnActivate([this, preview, caption] {
            auto* big = new ConnectorPreview;
            big->setPins(preview->pinLabels(), preview->pinColors(), preview->exitsRight());
            showPreviewPopup(this, QStringLiteral("%1 pin map").arg(caption), big);
        });
        v->addWidget(preview);
        pinMapLayout_->addWidget(col);
    }
    pinMapLayout_->addStretch(1);
    // Back to the top after a rebuild: left part-scrolled, the first thing to go out of sight
    // is the row of designators.
    if (auto* area = qobject_cast<QScrollArea*>(pinMapHost_->parentWidget()
                                                    ? pinMapHost_->parentWidget()->parentWidget()
                                                    : nullptr))
        if (QScrollBar* bar = area->verticalScrollBar()) bar->setValue(0);
    if (wiringPreview_) wiringPreview_->setDefinition(def_);
}

void HarnessBuilderDialog::applyPinOrder() {
    // Read every pin column back into its connector's order.
    for (QListWidget* list : pinMapHost_->findChildren<QListWidget*>()) {
        const QString id = list->property("connId").toString();
        auto* c = connById(id);
        if (!c) continue;
        std::vector<int> order;
        for (int i = 0; i < list->count(); ++i) order.push_back(list->item(i)->data(Qt::UserRole).toInt());
        c->pinOrder = order;
    }
    reloadAll();
}

// ---- Helpers ---------------------------------------------------------------

QString HarnessBuilderDialog::uniqueConnectorId() const {
    int n = static_cast<int>(def_.connectors.size()) + 1;
    for (;; ++n) {
        const QString id = QStringLiteral("c%1").arg(n);
        bool taken = false;
        for (const auto& c : def_.connectors)
            if (c.id == id) { taken = true; break; }
        if (!taken) return id;
    }
}

QString HarnessBuilderDialog::uniqueWireId() const {
    int n = static_cast<int>(def_.wires.size()) + 1;
    for (;; ++n) {
        const QString id = QStringLiteral("w%1").arg(n);
        bool taken = false;
        for (const auto& w : def_.wires)
            if (w.id == id) { taken = true; break; }
        if (!taken) return id;
    }
}

HarnessConnector* HarnessBuilderDialog::connById(const QString& id) {
    for (auto& c : def_.connectors)
        if (c.id == id) return &c;
    return nullptr;
}

QStringList HarnessBuilderDialog::connectorChoices() const {
    QStringList out;
    for (const auto& c : def_.connectors)
        out << QStringLiteral("%1 (%2)")
                   .arg(c.designator.isEmpty() ? c.id : c.designator,
                        c.side == HarnessSide::Left ? QStringLiteral("left")
                                                    : QStringLiteral("right"));
    return out;
}

QStringList HarnessBuilderDialog::gaugeChoices() {
    QStringList out;
    for (const auto& g : kGauges) out << QString::fromUtf8(g.label);
    return out;
}

QStringList HarnessBuilderDialog::colorChoices() {
    QStringList out;
    for (const QString& code : wireColorCodes())
        out << QStringLiteral("%1 – %2").arg(code, wireColorName(code));
    return out;
}

// ---- Step 4: wire connections ----------------------------------------------

QWidget* HarnessBuilderDialog::buildWiresSection() {
    auto* addBtn = new QPushButton(QStringLiteral("+ Add wire"), this);
    connect(addBtn, &QPushButton::clicked, this, [this] {
        addWire();
        reloadAll();
    });
    auto* fillBtn = new QPushButton(QStringLiteral("Auto-fill pins"), this);
    fillBtn->setToolTip(QStringLiteral("Give every wire the next free pin at each end, in "
                                      "order, without disturbing the pins already set"));
    connect(fillBtn, &QPushButton::clicked, this, [this] { autoFillPins(); });

    // Grouping: a long wire list is much easier to work through gathered by shield, by
    // twisted group, or by the connector pair it joins — with the groups you are not
    // touching folded away.
    // One gauge for the whole harness: most harnesses are drawn on a single cross-section, and
    // setting it row by row is a chore. Picking a value here writes it to every wire; a wire
    // that needs its own can still be changed in its row afterwards.
    auto* allGaugeLabel = new QLabel(QStringLiteral("All gauges"), this);
    allGaugeLabel->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    allGaugeBox_ = new QComboBox(this);
    allGaugeBox_->addItem(QStringLiteral("—"), QString()); // idle: touches nothing
    for (const QString& g : gaugeChoices()) allGaugeBox_->addItem(g, gaugeValueFor(g));
    allGaugeBox_->setToolTip(QStringLiteral("Give every wire this cross-section"));
    guardWheel(allGaugeBox_);
    connect(allGaugeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_ || i <= 0) return;
        const QString mm2 = allGaugeBox_->itemData(i).toString();
        if (mm2.isEmpty()) return;
        for (auto& w : def_.wires) w.gauge = mm2;
        reloadAll();
        summaryLabel_->setText(QStringLiteral("All %1 wire(s) set to %2 mm²")
                                   .arg(def_.wires.size())
                                   .arg(mm2));
    });

    auto* groupLabel = new QLabel(QStringLiteral("Group by"), this);
    groupLabel->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    groupByBox_ = new QComboBox(this);
    groupByBox_->addItem(QStringLiteral("None"), static_cast<int>(WireGrouping::None));
    groupByBox_->addItem(QStringLiteral("Shield"), static_cast<int>(WireGrouping::Shield));
    groupByBox_->addItem(QStringLiteral("Twist"), static_cast<int>(WireGrouping::Twist));
    groupByBox_->addItem(QStringLiteral("Connectors"), static_cast<int>(WireGrouping::Connectors));
    groupByBox_->setToolTip(QStringLiteral("Gather the wire rows under folding group headers"));
    guardWheel(groupByBox_);
    connect(groupByBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_) return;
        grouping_ = static_cast<WireGrouping>(groupByBox_->itemData(i).toInt());
        collapsedGroups_.clear(); // a new grouping starts fully open
        syncing_ = true;
        reloadWires();
        syncing_ = false;
    });
    auto* expandBtn = new QToolButton(this);
    expandBtn->setText(QStringLiteral("Expand all"));
    expandBtn->setAutoRaise(true);
    connect(expandBtn, &QToolButton::clicked, this, [this] {
        collapsedGroups_.clear();
        syncing_ = true;
        reloadWires();
        syncing_ = false;
    });
    auto* collapseBtn = new QToolButton(this);
    collapseBtn->setText(QStringLiteral("Collapse all"));
    collapseBtn->setAutoRaise(true);
    connect(collapseBtn, &QToolButton::clicked, this, [this] {
        // Fold whatever groups the current grouping produces, by re-reading their keys.
        if (grouping_ == WireGrouping::None) return;
        for (const auto& w : def_.wires) {
            QString key;
            switch (grouping_) {
            case WireGrouping::Shield: key = QStringLiteral("s:") + w.shieldGroup; break;
            case WireGrouping::Twist: key = QStringLiteral("t:") + w.twistGroup; break;
            case WireGrouping::Connectors:
                key = QStringLiteral("c:%1>%2").arg(w.fromConn, w.toConn);
                break;
            case WireGrouping::None: break;
            }
            collapsedGroups_.insert(key);
        }
        syncing_ = true;
        reloadWires();
        syncing_ = false;
    });

    wireTable_ = new QTableWidget(this);
    wireTable_->setColumnCount(11);
    wireTable_->setHorizontalHeaderLabels(
        {QStringLiteral("From"), QStringLiteral("Pin"), QStringLiteral("To"),
         QStringLiteral("Pin"), QStringLiteral("Gauge"), QStringLiteral("Color"),
         QStringLiteral("Label"), QStringLiteral("Show on wire"),
         QStringLiteral("Internal shield"), QStringLiteral("Twist"), QString()});
    wireTable_->verticalHeader()->setVisible(false);
    wireTable_->horizontalHeader()->setStretchLastSection(true);
    wireTable_->setSelectionMode(QAbstractItemView::NoSelection);
    wireTable_->setMinimumHeight(200);
    return section(QStringLiteral("Wire connections"), wireTable_, this,
                   {addBtn, fillBtn, allGaugeLabel, allGaugeBox_, groupLabel, groupByBox_,
                    expandBtn, collapseBtn});
}

void HarnessBuilderDialog::addWire() {
    if (def_.connectors.size() < 2) return;
    HarnessWire w;
    w.id = uniqueWireId();
    ++wireSeq_;
    // Default to the first left-hand connector and the first right-hand one, on their next
    // free pins, which is what a new row almost always wants to be.
    const HarnessConnector* from = nullptr;
    const HarnessConnector* to = nullptr;
    for (const auto& c : def_.connectors) {
        if (!from && c.side == HarnessSide::Left) from = &c;
        if (!to && c.side == HarnessSide::Right) to = &c;
    }
    if (!from) from = &def_.connectors.front();
    if (!to) to = &def_.connectors.back();
    w.fromConn = from->id;
    w.toConn = to->id;
    auto nextFree = [this](const HarnessConnector& c) {
        for (int p = 1; p <= std::max(1, c.pinCount); ++p) {
            bool used = false;
            for (const auto& e : def_.wires)
                if ((e.fromConn == c.id && e.fromPin == p) || (e.toConn == c.id && e.toPin == p)) {
                    used = true;
                    break;
                }
            if (!used) return p;
        }
        return 1;
    };
    w.fromPin = nextFree(*from);
    w.toPin = nextFree(*to);
    def_.wires.push_back(w);
}

void HarnessBuilderDialog::removeWire(int row) {
    if (row < 0 || row >= static_cast<int>(def_.wires.size())) return;
    def_.wires.erase(def_.wires.begin() + row);
    reloadAll();
}

void HarnessBuilderDialog::autoFillPins() {
    // Walk the wires in order handing out the lowest pin nobody has taken yet at each end.
    // Pins already claimed are left alone, so this completes a part-built mapping rather
    // than overwriting it.
    QSet<QString> taken;
    for (const auto& w : def_.wires) {
        taken.insert(QStringLiteral("%1/%2").arg(w.fromConn).arg(w.fromPin));
        taken.insert(QStringLiteral("%1/%2").arg(w.toConn).arg(w.toPin));
    }
    auto claim = [&taken, this](const QString& connId, int& pin) {
        const HarnessConnector* c = harnessConnById(def_, connId);
        if (!c) return;
        const QString key = QStringLiteral("%1/%2").arg(connId).arg(pin);
        // Keep a pin that is valid and only claimed by this wire.
        if (pin >= 1 && pin <= std::max(1, c->pinCount)) return;
        for (int p = 1; p <= std::max(1, c->pinCount); ++p) {
            const QString k = QStringLiteral("%1/%2").arg(connId).arg(p);
            if (taken.contains(k)) continue;
            taken.remove(key);
            taken.insert(k);
            pin = p;
            return;
        }
    };
    // First fix anything out of range, then spread duplicates onto free pins.
    for (auto& w : def_.wires) {
        claim(w.fromConn, w.fromPin);
        claim(w.toConn, w.toPin);
    }
    QSet<QString> seen;
    for (auto& w : def_.wires)
        for (auto* side : {&w.fromConn, &w.toConn}) {
            int* pin = (side == &w.fromConn) ? &w.fromPin : &w.toPin;
            const HarnessConnector* c = harnessConnById(def_, *side);
            if (!c) continue;
            QString key = QStringLiteral("%1/%2").arg(*side).arg(*pin);
            if (!seen.contains(key)) { seen.insert(key); continue; }
            for (int p = 1; p <= std::max(1, c->pinCount); ++p) {
                const QString k = QStringLiteral("%1/%2").arg(*side).arg(p);
                if (seen.contains(k)) continue;
                *pin = p;
                seen.insert(k);
                break;
            }
        }
    reloadAll();
}

void HarnessBuilderDialog::reloadWires() {
    wireTable_->clearSpans();
    wireTable_->setRowCount(0);
    const QStringList conns = connectorChoices();

    // Work out each wire's group, keeping the groups in the order they first appear so the
    // list never reshuffles under the user. The catch-all bucket (wires with no shield /
    // no twist) is pushed last, because it is the leftovers rather than a real group.
    struct Group {
        QString key;
        QString title;
        std::vector<const HarnessWire*> members;
        bool leftovers = false;
    };
    std::vector<Group> groups;
    auto groupFor = [this](const HarnessWire& w, QString& key, QString& title, bool& leftovers) {
        leftovers = false;
        switch (grouping_) {
        case WireGrouping::Shield:
            key = QStringLiteral("s:") + w.shieldGroup;
            leftovers = w.shieldGroup.isEmpty();
            title = leftovers ? QStringLiteral("Unshielded")
                              : QStringLiteral("Shield %1").arg(w.shieldGroup);
            break;
        case WireGrouping::Twist:
            key = QStringLiteral("t:") + w.twistGroup;
            leftovers = w.twistGroup.isEmpty();
            title = leftovers ? QStringLiteral("Not twisted")
                              : QStringLiteral("Twisted %1").arg(w.twistGroup);
            break;
        case WireGrouping::Connectors: {
            key = QStringLiteral("c:%1>%2").arg(w.fromConn, w.toConn);
            const HarnessConnector* a = harnessConnById(def_, w.fromConn);
            const HarnessConnector* b = harnessConnById(def_, w.toConn);
            const QString an = a ? (a->designator.isEmpty() ? a->id : a->designator) : w.fromConn;
            const QString bn = b ? (b->designator.isEmpty() ? b->id : b->designator) : w.toConn;
            title = QStringLiteral("%1  →  %2").arg(an, bn);
            break;
        }
        case WireGrouping::None:
            key.clear();
            title.clear();
            break;
        }
    };
    for (const auto& w : def_.wires) {
        QString key, title;
        bool leftovers = false;
        groupFor(w, key, title, leftovers);
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&key](const Group& g) { return g.key == key; });
        if (it == groups.end()) {
            groups.push_back({key, title, {&w}, leftovers});
        } else {
            it->members.push_back(&w);
        }
    }
    std::stable_partition(groups.begin(), groups.end(),
                          [](const Group& g) { return !g.leftovers; });

    int row = 0;
    for (const Group& g : groups) {
        if (grouping_ != WireGrouping::None) {
            wireTable_->insertRow(row);
            buildGroupHeader(row, g.key, g.title, static_cast<int>(g.members.size()));
            ++row;
        }
        const bool folded = collapsedGroups_.contains(g.key);
        for (const HarnessWire* w : g.members) {
            wireTable_->insertRow(row);
            buildWireRow(row, *w, conns);
            // A folded group keeps its rows built (so nothing loses its editor state) and
            // simply hides them.
            wireTable_->setRowHidden(row, folded && grouping_ != WireGrouping::None);
            ++row;
        }
    }
    // Sized to the widgets, not the header text: a connector name or a gauge must never be
    // clipped to "0.5 mm...".
    fitColumns(wireTable_);
}

void HarnessBuilderDialog::buildGroupHeader(int row, const QString& key, const QString& title,
                                            int memberCount) {
    const bool folded = collapsedGroups_.contains(key);
    // One clickable band across the whole width, so the fold target is obvious and large.
    wireTable_->setSpan(row, 0, 1, wireTable_->columnCount());
    auto* btn = new QToolButton(wireTable_);
    btn->setText(QStringLiteral("%1  %2   (%3 wire%4)")
                     .arg(folded ? QStringLiteral("▸") : QStringLiteral("▾"), title)
                     .arg(memberCount)
                     .arg(memberCount == 1 ? QString() : QStringLiteral("s")));
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setAutoRaise(true);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    btn->setStyleSheet(QStringLiteral(
        "QToolButton{background:#2b3138;color:#cfe3ff;font-weight:600;text-align:left;"
        "padding:3px 8px;border:none;}QToolButton:hover{background:#333a43;}"));
    btn->setToolTip(folded ? QStringLiteral("Click to unfold this group")
                           : QStringLiteral("Click to fold this group away"));
    connect(btn, &QToolButton::clicked, this, [this, key] {
        if (collapsedGroups_.contains(key)) collapsedGroups_.remove(key);
        else collapsedGroups_.insert(key);
        syncing_ = true;
        reloadWires();
        syncing_ = false;
    });
    wireTable_->setCellWidget(row, 0, btn);
}

void HarnessBuilderDialog::buildWireRow(int row, const HarnessWire& wRef,
                                        const QStringList& conns) {
    {
        const HarnessWire& w = wRef;
        const QString id = w.id;
        auto wireById = [this](const QString& wid) -> HarnessWire* {
            for (auto& x : def_.wires)
                if (x.id == wid) return &x;
            return nullptr;
        };
        auto indexOfConn = [this](const QString& connId) {
            for (int i = 0; i < static_cast<int>(def_.connectors.size()); ++i)
                if (def_.connectors[i].id == connId) return i;
            return 0;
        };

        // From / To connector, and the pin at each end.
        for (int end = 0; end < 2; ++end) {
            auto* box = new QComboBox(wireTable_);
            box->addItems(conns);
            box->setCurrentIndex(indexOfConn(end == 0 ? w.fromConn : w.toConn));
            centerCombo(box);
            // Tinted by the SIDE of the connector it names, in the same two colours the
            // connector table uses - so which end of the harness a wire leaves from and lands
            // on can be read straight off the row.
            {
                const HarnessConnector* cc =
                    harnessConnById(def_, end == 0 ? w.fromConn : w.toConn);
                const QString tint = (cc && cc->side == HarnessSide::Right)
                                         ? QStringLiteral("#3a3040")
                                         : QStringLiteral("#2b3a44");
                box->setStyleSheet(QStringLiteral("background:%1;").arg(tint));
            }
            connect(box, &QComboBox::currentIndexChanged, this, [this, id, end, wireById](int i) {
                if (syncing_) return;
                if (i < 0 || i >= static_cast<int>(def_.connectors.size())) return;
                if (auto* ww = wireById(id)) {
                    if (end == 0) ww->fromConn = def_.connectors[i].id;
                    else ww->toConn = def_.connectors[i].id;
                }
                reloadAll();
            });
            wireTable_->setCellWidget(row, end == 0 ? 0 : 2, box);

            const QString connId = (end == 0) ? w.fromConn : w.toConn;
            const HarnessConnector* c = harnessConnById(def_, connId);
            auto* pin = new QSpinBox(wireTable_);
            pin->setRange(1, c ? std::max(1, c->pinCount) : 1);
            pin->setValue(end == 0 ? w.fromPin : w.toPin);
            pin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            centerField(pin);
            // Tinted like the connector field beside it, so a pin reads as belonging to that
            // connector rather than floating between the two ends.
            pin->setStyleSheet(QStringLiteral("background:%1;")
                                   .arg((c && c->side == HarnessSide::Right)
                                            ? QStringLiteral("#3a3040")
                                            : QStringLiteral("#2b3a44")));
            connect(pin, &QSpinBox::valueChanged, this, [this, id, end, wireById](int v) {
                if (syncing_) return;
                if (auto* ww = wireById(id)) {
                    if (end == 0) ww->fromPin = v;
                    else ww->toPin = v;
                }
                reloadAll();
            });
            wireTable_->setCellWidget(row, end == 0 ? 1 : 3, pin);
        }

        auto* gauge = new QComboBox(wireTable_);
        gauge->addItems(gaugeChoices());
        gauge->setCurrentText(gaugeLabelFor(w.gauge));
        centerCombo(gauge);
        connect(gauge, &QComboBox::currentTextChanged, this,
                [this, id, wireById](const QString& t) {
                    if (syncing_) return;
                    if (auto* ww = wireById(id)) ww->gauge = gaugeValueFor(t);
                });
        wireTable_->setCellWidget(row, 4, gauge);

        // Colour: a swatch per entry, so a colour is chosen by eye. The code is carried in the
        // item DATA rather than parsed back out of the text, which is what lets an invented
        // colour have any name at all. The last entry adds one.
        auto* color = new QComboBox(wireTable_);
        for (const QString& code : wireColorCodes())
            color->addItem(colorSwatch(wireColorHex(code)),
                           QStringLiteral("%1 - %2").arg(code, wireColorName(code)), code);
        color->addItem(QStringLiteral("Add custom..."), QString(kCustomColorItem));
        centerCombo(color);
        {
            const int at = color->findData(w.color);
            if (at >= 0) color->setCurrentIndex(at);
        }
        // A centred field cannot show the item's icon, so the swatch is put INTO the field as
        // a leading action - the chosen colour is visible without opening the list.
        QAction* swatch =
            color->lineEdit()->addAction(colorSwatch(wireColorHex(w.color)),
                                         QLineEdit::LeadingPosition);
        connect(color, &QComboBox::currentIndexChanged, this,
                [this, id, color, wireById, swatch](int i) {
                    if (syncing_ || i < 0) return;
                    const QString data = color->itemData(i).toString();
                    if (data != QLatin1String(kCustomColorItem)) {
                        swatch->setIcon(colorSwatch(wireColorHex(data)));
                        if (auto* ww = wireById(id)) ww->color = data;
                        reloadPinMap(); // the pin map shows each wire's colour
                        return;
                    }
                    // "Add custom": a name and a colour, remembered for every wire from now on.
                    bool ok = false;
                    const QString name =
                        QInputDialog::getText(this, QStringLiteral("Custom wire colour"),
                                              QStringLiteral("Colour name:"), QLineEdit::Normal,
                                              QString(), &ok)
                            .trimmed();
                    const QColor picked =
                        (ok && !name.isEmpty())
                            ? QColorDialog::getColor(QColor("#e8e8e8"), this,
                                                     QStringLiteral("Custom wire colour"))
                            : QColor();
                    if (!picked.isValid()) { // cancelled: leave the wire on the colour it had
                        syncing_ = true;
                        auto* ww = wireById(id);
                        const int back = color->findData(ww ? ww->color : QString());
                        color->setCurrentIndex(std::max(0, back));
                        syncing_ = false;
                        return;
                    }
                    const QString code = name.toUpper().remove(QLatin1Char(' '));
                    registerCustomWireColor(code, name, picked.name());
                    saveCustomColor(code, name, picked.name());
                    if (auto* ww = wireById(id)) ww->color = code;
                    reloadAll(); // every colour box gains the new entry
                });
        wireTable_->setCellWidget(row, 5, color);

        auto* label = new QLineEdit(w.label, wireTable_);
        label->setPlaceholderText(QStringLiteral("Label"));
        label->setToolTip(QStringLiteral("Printed above the wire in the drawing"));
        centerEdit(label);
        connect(label, &QLineEdit::editingFinished, this, [this, id, label, wireById] {
            if (syncing_) return;
            if (auto* ww = wireById(id)) ww->label = label->text().trimmed();
        });
        wireTable_->setCellWidget(row, 6, label);

        // What this wire is marked with in the drawing: its colour code, its gauge, its label.
        // Three independent ticks, because a drawing usually calls for some but not all of them.
        {
            auto* cell = new QWidget(wireTable_);
            auto* lay = new QHBoxLayout(cell);
            lay->setContentsMargins(2, 0, 2, 0);
            lay->setSpacing(6);
            lay->addStretch(1);
            struct Tick { const char* text; const char* tip; bool HarnessWire::*field; };
            const Tick ticks[] = {
                {"Clr", "Show the colour code on the wire", &HarnessWire::showColor},
                {"Ga", "Show the cross-section on the wire", &HarnessWire::showGauge},
                {"Lbl", "Show the label above the wire", &HarnessWire::showLabel},
            };
            for (const Tick& t : ticks) {
                auto* cb = new QCheckBox(QString::fromLatin1(t.text), cell);
                cb->setToolTip(QString::fromLatin1(t.tip));
                cb->setChecked(w.*(t.field));
                bool HarnessWire::*field = t.field;
                connect(cb, &QCheckBox::toggled, this, [this, id, field, wireById](bool on) {
                    if (syncing_) return;
                    if (auto* ww = wireById(id)) ww->*field = on;
                });
                lay->addWidget(cb);
            }
            lay->addStretch(1);
            wireTable_->setCellWidget(row, 7, cell);
        }

        // Bundling (item 4): wires sharing a name are shielded / twisted together. Free
        // text so a harness can have any number of shields and twisted groups.
        auto* shield = new QLineEdit(w.shieldGroup, wireTable_);
        shield->setPlaceholderText(QStringLiteral("—"));
        shield->setToolTip(QStringLiteral(
            "Internal shield group: wires with the same name get their own screen INSIDE the "
            "overall one (they must be neighbours). The overall shield is asked for on the "
            "connector instead."));
        centerEdit(shield);
        connect(shield, &QLineEdit::editingFinished, this, [this, id, shield, wireById] {
            if (syncing_) return;
            if (auto* ww = wireById(id)) ww->shieldGroup = shield->text().trimmed();
        });
        wireTable_->setCellWidget(row, 8, shield);

        auto* twist = new QLineEdit(w.twistGroup, wireTable_);
        twist->setPlaceholderText(QStringLiteral("—"));
        twist->setToolTip(QStringLiteral("Twist group: wires with the same name are twisted "
                                       "together (they must be neighbours)"));
        centerEdit(twist);
        connect(twist, &QLineEdit::editingFinished, this, [this, id, twist, wireById] {
            if (syncing_) return;
            if (auto* ww = wireById(id)) ww->twistGroup = twist->text().trimmed();
        });
        wireTable_->setCellWidget(row, 9, twist);

        auto* del = new QToolButton(wireTable_);
        del->setText(QStringLiteral("×"));
        del->setStyleSheet(QStringLiteral("color:#e06c6c;font-weight:700;"));
        del->setToolTip(QStringLiteral("Remove this wire"));
        connect(del, &QToolButton::clicked, this, [this, id] {
            for (int i = 0; i < static_cast<int>(def_.wires.size()); ++i)
                if (def_.wires[i].id == id) { removeWire(i); return; }
        });
        wireTable_->setCellWidget(row, 10, del);
    }
}

// ---- Step 5: layout + options ----------------------------------------------

QWidget* HarnessBuilderDialog::buildLayoutSection() {
    auto* host = new QWidget(this);
    auto* grid = new QHBoxLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(24);

    // Each column is a GRID, labels in column 0 and fields in column 1, so the fields line up
    // in a straight edge however long the labels are — separate row layouts left them ragged.
    // Every field is given the same width for the same reason.
    constexpr int kFieldW = 90;
    struct Col {
        QWidget* w;
        QGridLayout* g;
        int row = 0;
    };
    auto makeCol = [&]() {
        Col c;
        c.w = new QWidget(host);
        c.g = new QGridLayout(c.w);
        c.g->setContentsMargins(0, 0, 0, 0);
        c.g->setHorizontalSpacing(8);
        c.g->setVerticalSpacing(3);
        c.g->setColumnStretch(1, 1);
        grid->addWidget(c.w);
        return c;
    };
    auto addLabel = [this](Col& c, const QString& text) {
        auto* l = new QLabel(text, c.w);
        l->setStyleSheet(QStringLiteral("color:#c9ccd1;background:transparent;"));
        c.g->addWidget(l, c.row, 0, Qt::AlignRight | Qt::AlignVCenter);
    };
    auto numField = [&](Col& c, const QString& label, double value, double lo, double hi,
                        QDoubleSpinBox** out) {
        addLabel(c, label);
        auto* sp = new QDoubleSpinBox(c.w);
        sp->setRange(lo, hi);
        sp->setDecimals(1);
        sp->setValue(value);
        sp->setFixedWidth(kFieldW);
        // A plain field to type into: the +/- steppers add nothing to a coordinate.
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        centerField(sp);
        c.g->addWidget(sp, c.row++, 1);
        *out = sp;
    };
    auto intField = [&](Col& c, const QString& label, int value, int lo, int hi, QSpinBox** out) {
        addLabel(c, label);
        auto* sp = new QSpinBox(c.w);
        sp->setRange(lo, hi);
        sp->setValue(value);
        sp->setFixedWidth(kFieldW);
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        centerField(sp);
        c.g->addWidget(sp, c.row++, 1);
        *out = sp;
    };
    // A drop-down field: the same width and the same centred text as the numeric ones, so a
    // column of fields reads as one column whatever kind of field each row is.
    auto comboField = [&](Col& c, const QString& label, const QList<QPair<QString, QString>>& items,
                          const QString& current, QComboBox** out) {
        addLabel(c, label);
        auto* box = new QComboBox(c.w);
        for (const auto& it : items) box->addItem(it.second, it.first);
        box->setFixedWidth(kFieldW);
        centerCombo(box);
        const int at = box->findData(current);
        box->setCurrentIndex(at >= 0 ? at : 0);
        c.g->addWidget(box, c.row++, 1);
        *out = box;
    };

    Col place = makeCol();
    numField(place, QStringLiteral("Left X"), def_.layout.leftX, -1e6, 1e6, &leftXSpin_);
    numField(place, QStringLiteral("Left Y"), def_.layout.leftY, -1e6, 1e6, &leftYSpin_);
    numField(place, QStringLiteral("Right X"), def_.layout.rightX, -1e6, 1e6, &rightXSpin_);
    numField(place, QStringLiteral("Conn gap"), def_.layout.connectorGap, 0, 1e5, &gapSpin_);

    Col gaps = makeCol();
    numField(gaps, QStringLiteral("Leg gap"), def_.layout.legGap, 0, 1e5, &legGapSpin_);
    numField(gaps, QStringLiteral("Pin pitch"), def_.layout.pinPitch, 1, 500, &pinPitchSpin_);
    numField(gaps, QStringLiteral("Shield leg length"), def_.layout.shieldLegLength, 0, 1000,
             &shieldLegSpin_);

    Col bundle = makeCol();
    intField(bundle, QStringLiteral("Twists"), def_.layout.twistCount, 1, 20, &twistCountSpin_);
    intField(bundle, QStringLiteral("Twist symbols"), def_.layout.twistAmount, 1, 9,
             &twistAmountSpin_);
    // Short names so the field can be the same width as the numeric ones and still read.
    comboField(bundle, QStringLiteral("Twist type"),
               {{QStringLiteral("crossed"), QStringLiteral("Type 1")},
                {QStringLiteral("jog"), QStringLiteral("Type 2")},
                {QStringLiteral("type3"), QStringLiteral("Type 3")}},
               def_.layout.twistType, &twistTypeBox_);
    twistTypeBox_->setToolTip(QStringLiteral("Type 1 crossed, Type 2 braid, Type 3 diamond"));

    Col shields = makeCol();
    intField(shields, QStringLiteral("Overall shield symbols"), def_.layout.shieldAmount, 1, 9,
             &shieldAmountSpin_);
    intField(shields, QStringLiteral("Wire shield symbols"), def_.layout.wireShieldAmount, 1, 9,
             &wireShieldAmountSpin_);
    comboField(bundle, QStringLiteral("Wire colour"),
               {{QStringLiteral("actual"), QStringLiteral("Actual")},
                {QStringLiteral("bylayer"), QStringLiteral("By layer")}},
               def_.layout.wireColorMode, &wireColorModeBox_);
    wireColorModeBox_->setToolTip(QStringLiteral(
        "Actual: every wire is drawn in its own defined colour. By layer: they are all left on "
        "the Wires layer colour, and the colour code stays on the wire as information."));

    comboField(shields, QStringLiteral("Shield type"),
               {{QStringLiteral("ellipse"), QStringLiteral("Ellipse")},
                {QStringLiteral("capsule"), QStringLiteral("Capsule")}},
               def_.layout.shieldType, &shieldTypeBox_);
    shieldTypeBox_->setToolTip(QStringLiteral("Outline a generated screen is drawn with"));

    auto* optCol = new QWidget(host);
    auto* optLay = new QVBoxLayout(optCol);
    optLay->setContentsMargins(0, 0, 0, 0);
    optLay->setSpacing(3);
    bridgeBox_ = new QCheckBox(QStringLiteral("Bridge wire crosses"), optCol);
    bridgeBox_->setToolTip(QStringLiteral(
        "Draw a hop where two wires cross; off leaves them as plain crosses"));
    terminateBox_ = new QCheckBox(QStringLiteral("Terminate unconnected pins"), optCol);
    terminateBox_->setToolTip(QStringLiteral("Put an X on every pin no wire reaches"));
    optLay->addWidget(bridgeBox_);
    optLay->addWidget(terminateBox_);
    optLay->addStretch(1);
    grid->addWidget(optCol);
    grid->addStretch(1);

    // Every control writes straight into the layout block.
    auto bindNum = [this](QDoubleSpinBox* sp, double HarnessLayout::*field) {
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this, field](double v) {
            if (syncing_) return;
            def_.layout.*field = v;
        });
    };
    bindNum(leftXSpin_, &HarnessLayout::leftX);
    bindNum(leftYSpin_, &HarnessLayout::leftY);
    bindNum(rightXSpin_, &HarnessLayout::rightX);
    bindNum(gapSpin_, &HarnessLayout::connectorGap);
    bindNum(legGapSpin_, &HarnessLayout::legGap);
    bindNum(shieldLegSpin_, &HarnessLayout::shieldLegLength);
    // Pin pitch is a whole-harness scale: it writes through to every connector, so the drawing
    // can be re-pitched from one place. A connector needing its own pitch can still be given
    // one in its row afterwards.
    connect(pinPitchSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (syncing_) return;
        def_.layout.pinPitch = v;
        for (auto& c : def_.connectors) c.pitch = v;
        reloadAll();
    });
    auto bindInt = [this](QSpinBox* sp, int HarnessLayout::*field) {
        connect(sp, &QSpinBox::valueChanged, this, [this, field](int v) {
            if (syncing_) return;
            def_.layout.*field = v;
        });
    };
    bindInt(twistCountSpin_, &HarnessLayout::twistCount);
    bindInt(twistAmountSpin_, &HarnessLayout::twistAmount);
    bindInt(shieldAmountSpin_, &HarnessLayout::shieldAmount);
    bindInt(wireShieldAmountSpin_, &HarnessLayout::wireShieldAmount);
    connect(twistTypeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_) return;
        def_.layout.twistType = twistTypeBox_->itemData(i).toString();
    });
    connect(shieldTypeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_) return;
        def_.layout.shieldType = shieldTypeBox_->itemData(i).toString();
    });
    connect(wireColorModeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_) return;
        def_.layout.wireColorMode = wireColorModeBox_->itemData(i).toString();
    });
    auto bindBool = [this](QCheckBox* cb, bool HarnessLayout::*field) {
        connect(cb, &QCheckBox::toggled, this, [this, field](bool on) {
            if (syncing_) return;
            def_.layout.*field = on;
        });
    };
    bindBool(bridgeBox_, &HarnessLayout::bridgeCrossings);
    bindBool(terminateBox_, &HarnessLayout::terminateUnconnected);
    return section(QStringLiteral("Layout"), host, this);
}

void HarnessBuilderDialog::reloadLayout() {
    // The whole-harness gauge picker is an action, not a setting: it drops back to idle so it
    // never reads as "every wire is this gauge" when they are not.
    if (allGaugeBox_) allGaugeBox_->setCurrentIndex(0);
    leftXSpin_->setValue(def_.layout.leftX);
    leftYSpin_->setValue(def_.layout.leftY);
    rightXSpin_->setValue(def_.layout.rightX);
    gapSpin_->setValue(def_.layout.connectorGap);
    legGapSpin_->setValue(def_.layout.legGap);
    twistCountSpin_->setValue(def_.layout.twistCount);
    twistAmountSpin_->setValue(def_.layout.twistAmount);
    shieldAmountSpin_->setValue(def_.layout.shieldAmount);
    wireShieldAmountSpin_->setValue(def_.layout.wireShieldAmount);
    shieldLegSpin_->setValue(def_.layout.shieldLegLength);
    pinPitchSpin_->setValue(def_.layout.pinPitch);
    {
        const int at = twistTypeBox_->findData(def_.layout.twistType);
        twistTypeBox_->setCurrentIndex(at >= 0 ? at : 1);
        const int sat = shieldTypeBox_->findData(def_.layout.shieldType);
        shieldTypeBox_->setCurrentIndex(sat >= 0 ? sat : 0);
        const int wcm = wireColorModeBox_->findData(def_.layout.wireColorMode);
        wireColorModeBox_->setCurrentIndex(wcm >= 0 ? wcm : 0);
    }
    bridgeBox_->setChecked(def_.layout.bridgeCrossings);
    terminateBox_->setChecked(def_.layout.terminateUnconnected);
}

// ---- Step 6: validation, generation, persistence ---------------------------

QWidget* HarnessBuilderDialog::buildIssuesSection() {
    issueView_ = new QTextBrowser(this);
    issueView_->setMinimumHeight(84);
    issueView_->setPlaceholderText(QStringLiteral("Validate reports anything wrong with the "
                                                "definition here."));
    return section(QStringLiteral("Validation"), issueView_, this);
}

void HarnessBuilderDialog::showIssues(const std::vector<HarnessIssue>& issues, bool sayWhenClean) {
    if (issues.empty()) {
        issueView_->setHtml(sayWhenClean
                                ? QStringLiteral("<span style='color:#7ee0a0'>No problems "
                                                 "found — the harness is ready to generate."
                                                 "</span>")
                                : QString());
        return;
    }
    QString html = QStringLiteral("<span style='color:#ffb038'>%1 issue(s):</span><ul>")
                       .arg(issues.size());
    for (const HarnessIssue& i : issues)
        html += QStringLiteral("<li>%1</li>").arg(i.message.toHtmlEscaped());
    html += QStringLiteral("</ul>");
    issueView_->setHtml(html);
}

void HarnessBuilderDialog::runValidate() {
    showIssues(validateHarness(def_), /*sayWhenClean=*/true);
}

void HarnessBuilderDialog::runMinimizeCrossings() {
    const int before = harnessCrossings(def_);
    const int shieldBefore = harnessBundleBreaks(def_, /*shield=*/true);
    const int twistBefore = harnessBundleBreaks(def_, /*shield=*/false);
    const int after = minimizeHarnessCrossings(def_);
    reloadAll();
    issueView_->setHtml(
        QStringLiteral("<span style='color:#7ee0a0'>Pinout re-ordered — crossings %1 → %2, "
                       "split shields %3 → %4, split twists %5 → %6.</span>")
            .arg(before)
            .arg(after)
            .arg(shieldBefore)
            .arg(harnessBundleBreaks(def_, true))
            .arg(twistBefore)
            .arg(harnessBundleBreaks(def_, false)));
}

void HarnessBuilderDialog::runOptimizeBy(bool shield) {
    const int before = harnessBundleBreaks(def_, shield);
    const int crossBefore = harnessCrossings(def_);
    const int after = shield ? optimizeHarnessForShields(def_) : optimizeHarnessForTwists(def_);
    reloadAll();
    const QString what = shield ? QStringLiteral("shield") : QStringLiteral("twisted");
    issueView_->setHtml(
        QStringLiteral("<span style='color:#7ee0a0'>Pinout re-ordered for %1 groups — split "
                       "%1 groups %2 → %3, crossings %4 → %5.%6</span>")
            .arg(what)
            .arg(before)
            .arg(after)
            .arg(crossBefore)
            .arg(harnessCrossings(def_))
            .arg(after == 0 ? QStringLiteral(" Every group now sits on neighbouring pins.")
                            : QStringLiteral(" Some groups still cannot be made adjacent — "
                                             "check the wiring.")));
}

void HarnessBuilderDialog::generate() {
    if (!editor_) return;
    if (def_.connectors.empty() || def_.wires.empty()) {
        showIssues({{QStringLiteral("empty"),
                     QStringLiteral("Add at least two connectors and one wire first."), {}, {}}},
                   false);
        return;
    }
    const std::vector<HarnessIssue> issues = validateHarness(def_);
    if (!issues.empty()) {
        showIssues(issues, false);
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("Schematic harness builder"),
            QStringLiteral("The definition has %1 issue(s). Draw it anyway?").arg(issues.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    } else {
        showIssues({}, true);
    }
    // Hand the harness to the cursor rather than dropping it at its designed coordinates:
    // the window steps out of the way, the whole harness ghosts under the crosshair, and a
    // click places it. Esc abandons the placement and brings the window back with the
    // definition untouched, so nothing is lost by trying a position.
    editor_->beginHarnessPlacement(def_);
    if (!editor_->harnessPlacing()) {
        summaryLabel_->setText(QStringLiteral("Nothing to generate"));
        return;
    }
    summaryLabel_->setText(QStringLiteral("Click on the drawing to place the harness"));
    hide();
}

QJsonObject HarnessBuilderDialog::definitionToJson() const {
    QJsonObject root;
    QJsonArray conns;
    for (const auto& c : def_.connectors) {
        QJsonObject o;
        o["id"] = c.id;
        o["side"] = (c.side == HarnessSide::Right) ? QStringLiteral("right") : QStringLiteral("left");
        o["designator"] = c.designator;
        o["partNumber"] = c.partNumber;
        o["annotation"] = c.annotation;
        o["pinCount"] = c.pinCount;
        o["pitch"] = c.pitch;
        o["pinLength"] = c.pinLength;
        o["order"] = c.order;
        o["leg"] = c.leg;
        o["shieldLeft"] = c.shieldLeft;
        o["shieldRight"] = c.shieldRight;
        o["bodyBondLeft"] = c.bodyBondLeft;
        o["bodyBondRight"] = c.bodyBondRight;
        QJsonArray po;
        for (int p : c.pinOrder) po.append(p);
        o["pinOrder"] = po;
        conns.append(o);
    }
    QJsonArray wires;
    for (const auto& w : def_.wires) {
        QJsonObject o;
        o["id"] = w.id;
        o["fromConn"] = w.fromConn;
        o["fromPin"] = w.fromPin;
        o["toConn"] = w.toConn;
        o["toPin"] = w.toPin;
        o["gauge"] = w.gauge;
        o["color"] = w.color;
        o["label"] = w.label;
        o["showColor"] = w.showColor;
        o["showGauge"] = w.showGauge;
        o["showLabel"] = w.showLabel;
        o["shieldGroup"] = w.shieldGroup;
        o["twistGroup"] = w.twistGroup;
        wires.append(o);
    }
    QJsonObject lay;
    lay["leftX"] = def_.layout.leftX;
    lay["leftY"] = def_.layout.leftY;
    lay["rightX"] = def_.layout.rightX;
    lay["connectorGap"] = def_.layout.connectorGap;
    lay["legGap"] = def_.layout.legGap;
    lay["bridgeCrossings"] = def_.layout.bridgeCrossings;
    lay["terminateUnconnected"] = def_.layout.terminateUnconnected;
    lay["twistCount"] = def_.layout.twistCount;
    lay["twistAmount"] = def_.layout.twistAmount;
    lay["shieldAmount"] = def_.layout.shieldAmount;
    lay["wireShieldAmount"] = def_.layout.wireShieldAmount;
    lay["shieldLegLength"] = def_.layout.shieldLegLength;
    lay["pinPitch"] = def_.layout.pinPitch;
    lay["twistType"] = def_.layout.twistType;
    lay["shieldType"] = def_.layout.shieldType;
    lay["wireColorMode"] = def_.layout.wireColorMode;
    root["format"] = QStringLiteral("edraw-harness-builder");
    root["version"] = 1;
    root["connectors"] = conns;
    root["wires"] = wires;
    root["layout"] = lay;
    return root;
}

// A builder pattern goes to a file of its own, so a harness definition can be kept, re-opened
// and shared like any other document instead of living inside the application settings.
void HarnessBuilderDialog::saveDefinition() {
    QSettings st;
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save builder pattern"),
        st.value(QLatin1String(kLastPathKey)).toString(), QLatin1String(kFileFilter));
    if (path.isEmpty()) return;
    if (!path.endsWith(QLatin1String(kFileSuffix), Qt::CaseInsensitive))
        path += QLatin1String(kFileSuffix);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Schematic harness builder"),
                             QStringLiteral("Could not write %1").arg(path));
        return;
    }
    f.write(QJsonDocument(definitionToJson()).toJson(QJsonDocument::Indented));
    f.close();
    st.setValue(QLatin1String(kLastPathKey), path);
    summaryLabel_->setText(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
}

void HarnessBuilderDialog::loadDefinition() {
    QSettings st;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load builder pattern"),
        st.value(QLatin1String(kLastPathKey)).toString(), QLatin1String(kFileFilter));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Schematic harness builder"),
                             QStringLiteral("Could not read %1").arg(path));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (root.isEmpty() || !root.contains(QStringLiteral("connectors"))) {
        QMessageBox::warning(this, QStringLiteral("Schematic harness builder"),
                             QStringLiteral("%1 is not a builder pattern.")
                                 .arg(QFileInfo(path).fileName()));
        return;
    }
    st.setValue(QLatin1String(kLastPathKey), path);
    applyDefinitionJson(root);
    summaryLabel_->setText(QStringLiteral("Loaded %1").arg(QFileInfo(path).fileName()));
}

void HarnessBuilderDialog::applyDefinitionJson(const QJsonObject& root) {
    HarnessDef loaded;
    for (const auto& v : root.value("connectors").toArray()) {
        const QJsonObject o = v.toObject();
        HarnessConnector c;
        c.id = o.value("id").toString();
        c.side = o.value("side").toString() == QLatin1String("right") ? HarnessSide::Right
                                                                     : HarnessSide::Left;
        c.designator = o.value("designator").toString();
        c.partNumber = o.value("partNumber").toString();
        c.annotation = o.value("annotation").toString(QStringLiteral("numbers"));
        c.pinCount = o.value("pinCount").toInt(4);
        c.pitch = o.value("pitch").toDouble(15.0);
        c.pinLength = o.value("pinLength").toDouble(40.0);
        c.order = o.value("order").toInt(0);
        c.leg = o.value("leg").toString();
        c.shieldLeft = o.value("shieldLeft").toBool(false);
        c.shieldRight = o.value("shieldRight").toBool(false);
        c.bodyBondLeft = o.value("bodyBondLeft").toBool(false);
        c.bodyBondRight = o.value("bodyBondRight").toBool(false);
        for (const auto& p : o.value("pinOrder").toArray()) c.pinOrder.push_back(p.toInt());
        if (!c.id.isEmpty()) loaded.connectors.push_back(c);
    }
    for (const auto& v : root.value("wires").toArray()) {
        const QJsonObject o = v.toObject();
        HarnessWire w;
        w.id = o.value("id").toString();
        w.fromConn = o.value("fromConn").toString();
        w.fromPin = o.value("fromPin").toInt(1);
        w.toConn = o.value("toConn").toString();
        w.toPin = o.value("toPin").toInt(1);
        w.gauge = o.value("gauge").toString(QStringLiteral("0.5"));
        w.color = o.value("color").toString(QStringLiteral("WH"));
        w.label = o.value("label").toString();
        w.showColor = o.value("showColor").toBool(false);
        w.showGauge = o.value("showGauge").toBool(false);
        w.showLabel = o.value("showLabel").toBool(true);
        w.shieldGroup = o.value("shieldGroup").toString();
        w.twistGroup = o.value("twistGroup").toString();
        if (!w.id.isEmpty()) loaded.wires.push_back(w);
    }
    const QJsonObject lay = root.value("layout").toObject();
    loaded.layout.leftX = lay.value("leftX").toDouble(100.0);
    loaded.layout.leftY = lay.value("leftY").toDouble(300.0);
    loaded.layout.rightX = lay.value("rightX").toDouble(700.0);
    loaded.layout.connectorGap = lay.value("connectorGap").toDouble(30.0);
    loaded.layout.legGap = lay.value("legGap").toDouble(60.0);
    loaded.layout.bridgeCrossings = lay.value("bridgeCrossings").toBool(true);
    loaded.layout.terminateUnconnected = lay.value("terminateUnconnected").toBool(false);
    loaded.layout.twistCount = lay.value("twistCount").toInt(2);
    loaded.layout.twistAmount = lay.value("twistAmount").toInt(2);
    loaded.layout.shieldAmount = lay.value("shieldAmount").toInt(2);
    loaded.layout.wireShieldAmount = lay.value("wireShieldAmount").toInt(2);
    loaded.layout.shieldLegLength = lay.value("shieldLegLength").toDouble(20.0);
    loaded.layout.pinPitch = lay.value("pinPitch").toDouble(15.0);
    loaded.layout.twistType = lay.value("twistType").toString(QStringLiteral("jog"));
    loaded.layout.shieldType = lay.value("shieldType").toString(QStringLiteral("ellipse"));
    loaded.layout.wireColorMode =
        lay.value("wireColorMode").toString(QStringLiteral("actual"));
    def_ = loaded;
    // Keep the id sequences ahead of whatever was loaded so new rows never collide.
    connSeq_ = static_cast<int>(def_.connectors.size());
    wireSeq_ = static_cast<int>(def_.wires.size());
    if (connTable_) reloadAll();
}

void HarnessBuilderDialog::reloadAll() {
    syncing_ = true;
    reloadConnectors();
    reloadBlockMap();
    reloadPinMap();
    reloadWires();
    reloadLayout();
    syncing_ = false;
    // Keep the running count honest without nagging: crossings now, issues only on demand.
    const int crossings = harnessCrossings(def_);
    summaryLabel_->setText(QStringLiteral("%1 connector(s), %2 wire(s), %3 crossing(s)")
                               .arg(def_.connectors.size())
                               .arg(def_.wires.size())
                               .arg(crossings));
}

} // namespace Edraw_Desktop
