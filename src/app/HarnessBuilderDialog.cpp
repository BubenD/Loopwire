#include "app/HarnessBuilderDialog.h"

#include "io/AppPaths.h"

#include "app/Editor.h"
#include "app/UiBits.h"
#include "app/PartViewDialog.h"
#include "harness/CableSpec.h"
#include "harness/Harness.h"
#include "harness/PartsLibrary.h"

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
#include <QMenu>
#include <QShowEvent>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QItemSelectionModel>
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

// The parts library, read once per dialog: the rows pick from it, and a harness described here is
// then a harness of REAL parts - which is what lets a bill of materials carry the manufacturer's
// numbers and the units each thing is bought in.
PartsLibrary& sharedLibraryStore() {
    static PartsLibrary lib;
    return lib;
}
bool& sharedLibraryLoaded() {
    static bool loaded = false;
    return loaded;
}

const PartsLibrary& sharedLibrary() {
    // Every connected library, merged: a harness may be built from parts kept in more than one.
    if (!sharedLibraryLoaded()) {
        PartsCatalog catalog;
        catalog.openRemembered();
        sharedLibraryStore() = catalog.merged();
        sharedLibraryLoaded() = true;
    }
    return sharedLibraryStore();
}

// Read the libraries again next time they are asked for.
//
// They used to be read ONCE per run, which meant a part added in the library window - a cable, say -
// could not be placed in a harness until the application had been restarted. Nobody would guess that;
// they would decide the cable had not saved.
void forgetSharedLibrary() { sharedLibraryLoaded() = false; }

// A cell that names the library part a row is made of. `kinds` are the kinds worth offering for
// that row - a connector row offers connectors, a wire row offers wires and cables - and the empty
// first entry means "not from the library", which is how a harness drawn before there was one keeps
// working.
QComboBox* libraryPartBox(QWidget* parent, const std::vector<PartKind>& kinds,
                          const QString& current) {
    auto* box = new QComboBox(parent);
    box->addItem(QStringLiteral("—"), QString());
    for (PartKind k : kinds)
        for (const LibraryPart* p : sharedLibrary().ofKind(k))
            box->addItem(QStringLiteral("%1 · %2").arg(partKindName(k), p->title()), p->id);
    int at = box->findData(current);
    // A part the row already names but the list does not offer - its library disconnected since,
    // or a kind this column does not stock - is put in anyway. Dropping it would silently unname
    // the row and take the part off the bill of materials, which is not something a list of
    // choices is allowed to decide by itself.
    if (at < 0 && !current.isEmpty()) {
        const LibraryPart* p = sharedLibrary().byId(current);
        box->addItem(p ? QStringLiteral("%1 · %2").arg(partKindName(p->kind), p->title())
                       : QStringLiteral("(part not in a connected library)"),
                     current);
        at = box->count() - 1;
    }
    box->setCurrentIndex(at >= 0 ? at : 0);
    box->setToolTip(QStringLiteral(
        "The library part this row is made of. What the bill of materials orders: its "
        "manufacturer's number, and the unit it is bought in."));
    return box;
}


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

// The background a bundled row is shown on. Every cable/coax gets one of its own, so a bundle
// can be seen at a glance and told from its neighbours; the colour follows from the name, so it
// does not change as rows are added or the list is re-grouped.
QString bundleTint(const QString& bundle) {
    if (bundle.isEmpty()) return {};
    // Deliberately a different family from endpointTint(): a bundle is a property of the ROW,
    // an endpoint is a property of a FIELD, and the two must never be mistaken for each other.
    // These are the warm/violet end; the endpoint tints are the cool blues and greens.
    static const char* kTints[] = {"#4a3550", "#523344", "#4a3c2a", "#553a30",
                                   "#42304a", "#4e3a24", "#3f2f3a", "#57402c"};
    uint h = 0;
    for (const QChar& c : bundle) h = h * 31u + c.unicode();
    return QString::fromLatin1(kTints[h % (sizeof(kTints) / sizeof(kTints[0]))]);
}

// The background an endpoint field is shown on. Each CONNECTOR gets a colour of its own and
// each SCREEN gets one from a second, greener family, so a row says at a glance which connector
// and which screen it runs between - telling P2 from P3 mattered more than telling left from
// right, which the two-colour scheme could only do. Derived from the key, so it holds still as
// connectors are added and the list is re-ordered.
QString endpointTint(const QString& key) {
    if (key.isEmpty()) return QStringLiteral("#2b3a44");
    if (harnessIsOpenEndpoint(key)) return QStringLiteral("#3d3a2a"); // open: no colour of a part
    static const char* kConnTints[] = {"#2b3a44", "#3a3040", "#2f3a2c", "#3a3328",
                                       "#2c3444", "#382a34", "#26383a", "#333044"};
    static const char* kShieldTints[] = {"#2f4038", "#26423a", "#354a3c", "#2a3c34"};
    uint h = 0;
    for (const QChar& c : key) h = h * 33u + c.unicode();
    if (harnessIsShieldEndpoint(key))
        return QString::fromLatin1(kShieldTints[h % (sizeof(kShieldTints) / sizeof(char*))]);
    return QString::fromLatin1(kConnTints[h % (sizeof(kConnTints) / sizeof(char*))]);
}

// A colour dot for a combo entry, so a wire colour is picked by eye. A circle rather than a
// square: the wire is round, and a square reads as a fill/hatch swatch.
QIcon colorSwatch(const QString& hex) {
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#20242b"), 1));
    p.setBrush(QColor(hex));
    p.drawEllipse(QRectF(0.5, 0.5, 13.0, 13.0));
    return QIcon(pm);
}

// Signal and shield are not colours, so a coloured dot says nothing about them. They are drawn
// as what they are - the two parts of a coax seen end on: the SIGNAL is the core, the SHIELD is
// the braid around it. Which of the two is filled says which conductor the row is.
QIcon coaxIcon(bool shield) {
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor line("#cfd8e6");
    if (shield) {
        // The braid: a ring, with the core left hollow behind it.
        p.setPen(QPen(line, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(1.5, 1.5, 11.0, 11.0));
        p.setPen(QPen(line, 1.0));
        p.drawEllipse(QRectF(5.5, 5.5, 3.0, 3.0));
    } else {
        // The core: filled, inside a thin insulation outline.
        p.setPen(QPen(line, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(1.5, 1.5, 11.0, 11.0));
        p.setBrush(line);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(4.5, 4.5, 5.0, 5.0));
    }
    return QIcon(pm);
}

// The icon for a colour code: a coax part gets its own picture, everything else a colour dot.
QIcon codeIcon(const QString& code) {
    if (code == QLatin1String("SIGNAL")) return coaxIcon(false);
    if (code == QLatin1String("SHIELD")) return coaxIcon(true);
    return colorSwatch(wireColorHex(code));
}

// Cross-sections offered for a wire, labelled the way a harness drawing reads them.


// Shown in whichever notation the general options are set to - "20 AWG" or "0.5 mm2" - while
// the definition always stores the mm2 figure, so switching notation re-labels the list
// without touching the harness.
QString gaugeLabelFor(const QString& mm2) {
    const QString label = wireGaugeLabel(mm2);
    return label.isEmpty() ? mm2 : label;
}
QString gaugeValueFor(const QString& label) {
    for (const QString& mm2 : wireGaugeChoices())
        if (label == gaugeLabelFor(mm2)) return mm2;
    // A label written in the other notation (the general option changed while the dialog was
    // open), so both spellings are accepted.
    for (const QString& mm2 : wireGaugeChoices())
        if (label.startsWith(mm2) ||
            label.startsWith(wireGaugeAwg(mm2) + QStringLiteral(" AWG")))
            return mm2;
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

// Holds one widget at its own natural height and CLIPS it. Making a section shorter should
// slide its contents out of sight under the edge, not squeeze them: a layout asked to fit into
// less space than it needs compresses every row until the fields overlap and the text is cut
// through the middle, which is what dragging a section closed used to do.
class ClipBox : public QWidget {
public:
    ClipBox(QWidget* body, QWidget* parent) : QWidget(parent), body_(body) {
        setObjectName(QStringLiteral("clipBox")); // findable without a meta-object of its own
        // Scoped to THIS widget: an unqualified rule would also reach the drop-down lists of the
        // fields inside, leaving them with no background - which paints them white.
        setStyleSheet(QStringLiteral("#clipBox { background: transparent; }"));
        body_->setParent(this);
        body_->move(0, 0);
        setMinimumHeight(0);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setFixedHeight(body_->sizeHint().height());
    }
    // The natural height, so a section opens showing all of itself.
    int naturalHeight() const { return std::max(body_->sizeHint().height(), body_->minimumSizeHint().height()); }
    QSize sizeHint() const override { return QSize(body_->sizeHint().width(), height()); }

protected:
    void resizeEvent(QResizeEvent*) override {
        // Full width, own height: the body never learns that it has less room than it wants.
        body_->resize(width(), std::max(naturalHeight(), height()));
    }

private:
    QWidget* body_ = nullptr;
};

// Draws a box round each run of picked rows in a table. It lives over the viewport with mouse
// events passing straight through, so it can outline rows whose cells are widgets - which paint
// over the ordinary selection colour and would otherwise leave a pick invisible.
class PickOverlay : public QWidget {
public:
    explicit PickOverlay(QTableWidget* table)
        : QWidget(table->viewport()), table_(table) {
        setObjectName(QStringLiteral("pickOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        // Nothing but the outlines: the application stylesheet gives every plain QWidget a
        // background colour, and inheriting it here would cover the rows completely.
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet(QStringLiteral("#pickOverlay { background: transparent; }"));
        setGeometry(table->viewport()->rect());
        raise();
        show();
        table->viewport()->installEventFilter(this);
    }
    // Called when the selection changes and after the rows are rebuilt - the new cell widgets
    // are stacked above this one, so it has to be lifted back on top.
    void refresh() {
        setGeometry(table_->viewport()->rect());
        raise();
        update();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        // Resize ONLY. Reacting to a paint would ask for another paint, and the loop that
        // starts never lets the cells underneath finish drawing.
        if (watched == table_->viewport() && ev->type() == QEvent::Resize) {
            setGeometry(table_->viewport()->rect());
            update();
        }
        return QWidget::eventFilter(watched, ev);
    }
    void paintEvent(QPaintEvent*) override {
        if (!table_->selectionModel()) return;
        QSet<int> rows;
        for (const QModelIndex& ix : table_->selectionModel()->selectedIndexes())
            rows.insert(ix.row());
        if (rows.isEmpty()) return;
        QList<int> sorted(rows.begin(), rows.end());
        std::sort(sorted.begin(), sorted.end());
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor("#7ee0a0"));
        pen.setWidth(2);
        p.setPen(pen);
        p.setBrush(QColor(126, 224, 160, 26));
        // One box per RUN of neighbouring rows, so picking three in a row reads as one block
        // rather than three stripes.
        for (int i = 0; i < sorted.size();) {
            int j = i;
            while (j + 1 < sorted.size() && sorted[j + 1] == sorted[j] + 1) ++j;
            QRect box = table_->visualRect(table_->model()->index(sorted[i], 0));
            for (int c = 1; c < table_->columnCount(); ++c)
                box = box.united(table_->visualRect(table_->model()->index(sorted[j], c)));
            if (box.isValid()) p.drawRoundedRect(box.adjusted(1, 1, -2, -2), 3, 3);
            i = j + 1;
        }
    }

private:
    QTableWidget* table_ = nullptr;
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
    void setShowShields(bool on) {
        showShields_ = on;
        update();
    }
    bool showShields() const { return showShields_; }
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

        // The screens, drawn across the wires they enclose: an overall one just inboard of its
        // connector's pins, an internal one across its group mid-run. Sized to the band of rows
        // it covers, which is what says at a glance whether a shield can enclose the group at
        // all - a split band shows up here as an outline swallowing wires that are not in it.
        auto drawScreens = [&](bool afterWires) {
            if (!showShields_ || !afterWires) return;
            p.setPen(QPen(QColor("#cfe3ff"), 1.2));
            auto bandOf = [&](const std::vector<QString>& ids, double& lo, double& hi) {
                bool any = false;
                for (const auto& w : def_.wires) {
                    if (std::find(ids.begin(), ids.end(), w.id) == ids.end()) continue;
                    for (int end = 0; end < 2; ++end) {
                        const Slot* sl = slotOf(end == 0 ? w.fromConn : w.toConn);
                        if (!sl) continue;
                        double x = 0.0, y = 0.0;
                        pinPoint(*sl, end == 0 ? w.fromPin : w.toPin, x, y);
                        if (!any) { lo = hi = y; any = true; }
                        lo = std::min(lo, y);
                        hi = std::max(hi, y);
                    }
                }
                return any;
            };
            // Overall screens: one per connector end that asks for one. Several connectors on
            // one side would otherwise draw their outlines at the same x, one on top of another
            // and unreadable - so each is pushed along until it clears the ones already placed.
            struct Oval { double x, cy, ry; bool left; };
            std::vector<Oval> placedOvals;
            auto placeOval = [&](double wantX, double cy, double ry, bool left) {
                const double stepAside = 15.0;
                double x = wantX;
                for (int guard = 0; guard < 40; ++guard) {
                    bool clash = false;
                    for (const Oval& o : placedOvals)
                        if (std::abs(o.x - x) < 12.0 &&
                            std::abs(o.cy - cy) < (o.ry + ry)) { clash = true; break; }
                    if (!clash) break;
                    x += left ? stepAside : -stepAside;
                }
                placedOvals.push_back({x, cy, ry, left});
                p.drawEllipse(QRectF(x - 5.0, cy - ry, 10.0, 2.0 * ry));
            };
            for (const Slot& sl : slotList) {
                const bool wants = sl.left ? sl.c->shieldLeft : sl.c->shieldRight;
                if (!wants) continue;
                std::vector<QString> ids;
                for (const auto& w : def_.wires)
                    if (w.fromConn == sl.c->id || w.toConn == sl.c->id) ids.push_back(w.id);
                double lo = 0.0, hi = 0.0;
                if (!bandOf(ids, lo, hi)) continue;
                const double ry = (hi - lo) / 2.0 + 5.0;
                const int amount = std::max(1, def_.layout.shieldAmount);
                for (int k = 0; k < amount; ++k) {
                    const double step = 14.0 * k;
                    const double x = sl.left ? margin + bodyW + 16.0 + step
                                             : width() - margin - bodyW - 16.0 - step;
                    placeOval(x, (lo + hi) / 2.0, ry, sl.left);
                }
            }
            // Internal screens: one per shield group, across the middle of its run.
            QStringList groups;
            for (const auto& w : def_.wires)
                if (!w.shieldGroup.isEmpty() && !groups.contains(w.shieldGroup))
                    groups << w.shieldGroup;
            for (const QString& g : groups) {
                std::vector<QString> ids;
                for (const auto& w : def_.wires)
                    if (w.shieldGroup == g) ids.push_back(w.id);
                double lo = 0.0, hi = 0.0;
                if (!bandOf(ids, lo, hi)) continue;
                const double ry = (hi - lo) / 2.0 + 4.0;
                // As MANY as the Layout says will be drawn, spread along the corridor - so the
                // preview shows the number of symbols the drawing will actually carry.
                const int amount = std::max(1, def_.layout.wireShieldAmount);
                const double laneLo = margin + bodyW + 20.0;
                const double laneHi = width() - margin - bodyW - 20.0;
                for (int k = 0; k < amount; ++k) {
                    const double t = (k + 1.0) / (amount + 1.0);
                    const double x = laneLo + (laneHi - laneLo) * t;
                    placeOval(x, (lo + hi) / 2.0, ry, /*left=*/true);
                }
            }
        };

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
        drawScreens(/*afterWires=*/true);
    }

private:
    HarnessDef def_;
    bool showShields_ = true;
    std::function<void()> onActivate_;
};

namespace {

// The rule tick-boxes get their own captioned group, so they read as the drawing's rules
// rather than as four loose checkboxes at the end of the layout fields.
QWidget* ribbonRules(QWidget* body, QWidget* parent) {
    auto* host = new QWidget(parent);
    auto* v = new QVBoxLayout(host);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto* cap = new QLabel(QStringLiteral("Rules"), host);
    cap->setStyleSheet(QStringLiteral("color:#9fb4c8;font-weight:600;background:transparent;"));
    v->addWidget(cap);
    body->setParent(host);
    v->addWidget(body);
    v->addStretch(1);
    return host;
}

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
// `clipBody` is for a section whose body is a fixed grid of fields: dragging the section
// shorter should slide the rows out of sight under its edge, not compress them into each other.
// A table or a preview is left alone - those genuinely want the room the drag gives them.
QWidget* section(const QString& title, QWidget* body, QWidget* parent,
                 const QList<QWidget*>& headerWidgets = {}, bool clipBody = false) {
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
    if (clipBody) v->addWidget(new ClipBox(body, box));
    else v->addWidget(body);
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
                        {buildLayoutSection(), 0},      {buildIssuesSection(), 110}};
    for (const Sec& s : secs) {
        // 0 = open at its natural height: a grid of fields has one right size, and starting it
        // shorter than that is what made the rows overlap.
        s.w->setFixedHeight(s.height > 0 ? s.height : s.w->sizeHint().height());
        sections_.push_back(s.w);
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
    auto* clearBtn = new QPushButton(QStringLiteral("Clear"), this);
    clearBtn->setToolTip(QStringLiteral(
        "Throw this harness away and start again from an empty pair of connectors"));
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(
                this, QStringLiteral("Schematic harness builder"),
                QStringLiteral("Clear the whole definition and start again?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        def_ = HarnessDef();
        connSeq_ = 0;
        wireSeq_ = 0;
        grouping_ = WireGrouping::None;
        collapsedGroups_.clear();
        syncing_ = true;
        if (groupByBox_) groupByBox_->setCurrentIndex(0);
        syncing_ = false;
        // The same two connectors a freshly opened builder starts with, so the dialog is never
        // an empty form.
        addConnector(HarnessSide::Left);
        addConnector(HarnessSide::Right);
        reloadAll();
        showIssues({}, false);
        summaryLabel_->setText(QStringLiteral("Cleared"));
    });

    connect(saveBtn, &QPushButton::clicked, this, [this] { saveDefinition(); });
    auto* loadBtn = new QPushButton(QStringLiteral("Load"), this);
    loadBtn->setToolTip(QStringLiteral("Bring back the last saved definition"));
    connect(loadBtn, &QPushButton::clicked, this, [this] { loadDefinition(); });
    // Patterns kept in THIS DRAWING. Save and Load go to a file, for a harness to be reused across
    // drawings; a preset belongs to the drawing it was generated on, travels inside it, and can be
    // thrown away from it.
    auto* presetBtn = new QPushButton(QStringLiteral("Presets"), this);
    presetBtn->setToolTip(QStringLiteral(
        "Harness patterns kept inside this drawing: save what is in the builder now, bring one back, "
        "or delete one. They are saved with the drawing, so they are there when it is opened again."));
    connect(presetBtn, &QPushButton::clicked, this, [this, presetBtn] { showPresetMenu(presetBtn); });
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    // Clear sits in the bottom row with the rest, between Generate and Save. (It used to be built
    // and wired but never put in a layout, which left it stranded at the corner of the window.)
    footer->addWidget(generateBtn);
    footer->addWidget(clearBtn);
    footer->addWidget(saveBtn);
    footer->addWidget(loadBtn);
    footer->addWidget(presetBtn);
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
    // Sized to what is in them, so everything is in view from the start. Deferred once so the
    // tables have been laid out and can report a real row height.
    QTimer::singleShot(0, this, [this] { spreadSections(); });
}

// ---- Step 1: connectors ----------------------------------------------------

QWidget* HarnessBuilderDialog::buildConnectorsSection() {
    auto* addLeft = makeAddButton(this, QStringLiteral("Left"));
    auto* addRight = makeAddButton(this, QStringLiteral("Right"));
    connect(addLeft, &QPushButton::clicked, this, [this] {
        addConnector(HarnessSide::Left);
        reloadAll();
    });
    connect(addRight, &QPushButton::clicked, this, [this] {
        addConnector(HarnessSide::Right);
        reloadAll();
    });

    connTable_ = new QTableWidget(this);
    connTable_->setColumnCount(14);
    connTable_->setHorizontalHeaderLabels({QStringLiteral("Side"), QStringLiteral("Des"),
                                          QStringLiteral("P/N"), QStringLiteral("Ann"),
                                          QStringLiteral("Pins"), QStringLiteral("Pitch"),
                                          QStringLiteral("Pin len"), QStringLiteral("Leg"),
                                          QStringLiteral("L overall shield"),
                                          QStringLiteral("L shield→body"),
                                          QStringLiteral("R overall shield"),
                                          QStringLiteral("R shield→body"), QString(),
                                          QStringLiteral("Library part")});
    connTable_->verticalHeader()->setVisible(false);
    connTable_->horizontalHeader()->setStretchLastSection(true);
    connTable_->setSelectionMode(QAbstractItemView::NoSelection);
    connTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(connTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const int row = connTable_->rowAt(at.y());
        if (row < 0 || row >= static_cast<int>(def_.connectors.size())) return;
        const HarnessConnector& c = def_.connectors[static_cast<size_t>(row)];
        showPartMenu(c.libPart, connTable_->viewport()->mapToGlobal(at),
                     QStringLiteral("The connector %1").arg(c.designator));
    });
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
            // What to SAY is held until after the tables are rebuilt: reloadAll writes its own count
            // into the summary line, so anything said before it is wiped out before it is read - and
            // "the library part has been taken off" is the one thing here nobody should miss.
            QString note;
            if (auto* cc = connById(id)) {
                cc->pinCount = v;
                // Drop any stored pin order entry that no longer exists.
                cc->pinOrder.erase(std::remove_if(cc->pinOrder.begin(), cc->pinOrder.end(),
                                                  [v](int p) { return p < 1 || p > v; }),
                                   cc->pinOrder.end());
                // A count the library part does not have means the row is no longer THAT part: better
                // to have no part than the wrong one, because a bill of materials would order it.
                if (!cc->libPart.isEmpty()) {
                    if (const LibraryPart* lp = sharedLibrary().byId(cc->libPart)) {
                        const int pins = lp->value(QStringLiteral("pins")).toInt();
                        if (pins > 0 && pins != v) {
                            note = QStringLiteral("%1 has %2 pins, not %3 — the library part has "
                                                  "been taken off this connector")
                                       .arg(lp->title())
                                       .arg(pins)
                                       .arg(v);
                            cc->libPart.clear();
                        }
                    }
                }
            }
            reloadAll();
            if (!note.isEmpty()) summaryLabel_->setText(note);
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
        // `refresh` re-syncs the whole dialog after the tick. The shield flags decide which
        // SCREENS the harness has, and those are offered as wire endpoints - so a tick has to
        // reach the wire table straight away, not wait until the next time a row is added.
        // Deferred by a timer because the reload rebuilds the very checkbox being clicked.
        auto boolCell = [this](int atRow, int atCol, bool value, const QString& tip,
                               const std::function<void(HarnessConnector&, bool)>& set,
                               const QString& id, const QString& tint = QString(),
                               bool refresh = false) {
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
            connect(box, &QCheckBox::toggled, this, [this, id, set, refresh](bool on) {
                if (syncing_) return;
                if (auto* cc = connById(id)) set(*cc, on);
                if (refresh) QTimer::singleShot(0, this, [this] { reloadAll(); });
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
                     [](HarnessConnector& cc, bool on) { cc.shieldLeft = on; }, id, leftTint,
                     /*refresh=*/true);
            boolCell(row, 9, c.bodyBondLeft,
                     QStringLiteral("Bond this connector's overall shield drain back to its own "
                                    "body joint"),
                     [](HarnessConnector& cc, bool on) { cc.bodyBondLeft = on; }, id, leftTint);
        } else {
            boolCell(row, 10, c.shieldRight,
                     QStringLiteral("Overall shield: screen everything arriving at this "
                                    "connector, drawn just inboard of its pins"),
                     [](HarnessConnector& cc, bool on) { cc.shieldRight = on; }, id, rightTint,
                     /*refresh=*/true);
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
        {
            auto* lib = libraryPartBox(connTable_, {PartKind::Connector}, c.libPart);
            centerCombo(lib);
            connect(lib, &QComboBox::currentIndexChanged, this, [this, id, lib](int i) {
                if (syncing_ || i < 0) return;
                if (auto* cc = connById(id)) {
                    cc->libPart = lib->itemData(i).toString();
                    // The library knows how many pins the connector HAS: a 9-way is a 9-way, and a
                    // row that says otherwise is describing a part that does not exist. So the count
                    // follows the part the moment one is named.
                    if (const LibraryPart* lp = sharedLibrary().byId(cc->libPart)) {
                        const int pins = lp->value(QStringLiteral("pins")).toInt();
                        if (pins > 0 && pins != cc->pinCount) {
                            cc->pinCount = pins;
                            cc->pinOrder.erase(
                                std::remove_if(cc->pinOrder.begin(), cc->pinOrder.end(),
                                               [pins](int p) { return p < 1 || p > pins; }),
                                cc->pinOrder.end());
                            summaryLabel_->setText(
                                QStringLiteral("%1 has %2 pins — the row now says so")
                                    .arg(lp->title())
                                    .arg(pins));
                        }
                    }
                    // The library knows the part number: take it, so the drawing and the bill of
                    // materials say the same thing.
                    if (const LibraryPart* p = sharedLibrary().byId(cc->libPart))
                        if (!p->mfrPn.isEmpty()) cc->partNumber = p->mfrPn;
                }
                reloadConnectors();
                refreshPreview();
            });
            connTable_->setCellWidget(row, 13, lib);
        }
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
        big->setShowShields(shieldPreviewBox_ && shieldPreviewBox_->isChecked());
        showPreviewPopup(this, QStringLiteral("Harness wiring"), big);
    });
    row->addWidget(wiringPreview_, 2);

    // The screens can be switched off: on a busy harness the outlines cover the wiring they
    // are drawn over, and the pinout is easier to read without them.
    shieldPreviewBox_ = new QCheckBox(QStringLiteral("Show shields"), this);
    shieldPreviewBox_->setChecked(true);
    shieldPreviewBox_->setToolTip(QStringLiteral(
        "Draw the screens over the preview: an overall one inboard of its connector's pins, an "
        "internal one across its group"));
    connect(shieldPreviewBox_, &QCheckBox::toggled, this, [this](bool on) {
        if (wiringPreview_) wiringPreview_->setShowShields(on);
    });
    // Clipped, not squeezed: dragging the section shorter slides the previews under its edge
    // rather than crushing the pictures out of shape.
    return section(QStringLiteral("Pin map"), host, this, {shieldPreviewBox_},
                   /*clipBody=*/true);
}

// Push the definition into the wiring preview and redraw it. A layout field - the shield
// amounts, the pin pitch, the wire colours - changes what the preview shows without changing a
// row, so those handlers call this rather than rebuilding the whole pin map: the picture keeps up
// with every parameter, and typing in a spin box stays responsive.
void HarnessBuilderDialog::refreshPreview() {
    if (!wiringPreview_) return;
    wiringPreview_->setDefinition(def_);
    if (shieldPreviewBox_) wiringPreview_->setShowShields(shieldPreviewBox_->isChecked());
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
    if (wiringPreview_) {
        wiringPreview_->setDefinition(def_);
        if (shieldPreviewBox_) wiringPreview_->setShowShields(shieldPreviewBox_->isChecked());
    }
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

// Every endpoint a wire end can name: the connectors, then the SCREENS the harness has - the
// overall one at each shielded connector end, and each internal shield group. A wire run to a
// screen is a drain: it is brought to that screen's leg and joined to it with a splice.
QList<QPair<QString, QString>> HarnessBuilderDialog::endpointChoices() const {
    QList<QPair<QString, QString>> out;
    for (const auto& c : def_.connectors)
        out << qMakePair(c.id, QStringLiteral("%1 (%2)")
                                   .arg(c.designator.isEmpty() ? c.id : c.designator,
                                        c.side == HarnessSide::Left ? QStringLiteral("left")
                                                                    : QStringLiteral("right")));
    for (const QString& key : harnessShieldEndpoints(def_))
        out << qMakePair(key, harnessShieldEndpointLabel(def_, key));
    // A flying lead: the wire runs out and stops.
    out << qMakePair(harnessOpenEndpointKey(), QStringLiteral("Open (flying lead)"));
    return out;
}

QStringList HarnessBuilderDialog::gaugeChoices() {
    QStringList out;
    for (const QString& mm2 : wireGaugeChoices()) out << gaugeLabelFor(mm2);
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
    auto* addBtn = makeAddButton(this, QStringLiteral("Add wire"),
                                QStringLiteral("One more conductor between two connectors"));
    connect(addBtn, &QPushButton::clicked, this, [this] {
        // Grouped, a new row has to belong somewhere: the group decides which connectors it runs
        // between (or which shield / twisted group it joins), and guessing would drop it into a
        // group the user was not looking at. So the click arms a pick, the headers light up, and
        // the one clicked takes the wire.
        //
        // Unless there is nothing to choose between. With one group - a harness of one cable, say,
        // or a first pair of connectors - asking which group to use has exactly one answer, and the
        // click did nothing visible while it waited to be told it. With no groups at all there is
        // not even a header to click, so waiting left the list impossible to add to. Both go
        // straight in; the pick is for when the answer is genuinely a choice.
        if (grouping_ != WireGrouping::None) {
            QStringList groupKeys;
            for (int r = 0; r < wireTable_->rowCount(); ++r)
                if (QWidget* band = wireTable_->cellWidget(r, 0)) {
                    const QVariant k = band->property("groupKey");
                    if (k.isValid()) groupKeys << k.toString();
                }
            if (groupKeys.size() == 1) {
                addWireToGroup(groupKeys.first());
                return;
            }
            if (!groupKeys.isEmpty()) {
                pendingAddGroup_ = true;
                syncing_ = true;
                reloadWires();
                syncing_ = false;
                summaryLabel_->setText(QStringLiteral(
                    "Click the group to add the wire to (Esc to cancel)"));
                return;
            }
        }
        // Two connectors are what a wire runs between; without them the click used to do nothing
        // at all and look broken - say so instead.
        if (def_.connectors.size() < 2) {
            summaryLabel_->setText(QStringLiteral(
                "A wire runs between two connectors — add a second connector first"));
            return;
        }
        addWire();
        reloadAll();
        spreadSections(); // the new row is in view, not under the section's edge
        if (wireTable_ && wireTable_->rowCount())
            wireTable_->scrollToBottom();
    });
    // A cable is not a wire, and adding one row at a time is not how it goes in: one click places the
    // cable and every conductor it contains, described as the cable describes them.
    auto* placeCableBtn = makeAddButton(this, QStringLiteral("Add cable"));
    placeCableBtn->setToolTip(QStringLiteral(
        "Place a cable from the library. Every conductor it is made of is added at once - the pairs "
        "twisted, the screened groups screened, a coax as a coax, the drain wire there to be landed - "
        "with the colours and cross-sections the cable itself says."));
    connect(placeCableBtn, &QPushButton::clicked, this, [this] {
        if (addCableFromLibrary() > 0) {
            spreadSections();
            if (wireTable_ && wireTable_->rowCount()) wireTable_->scrollToBottom();
        }
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

    // ---- Unify picked rows into one cable or coax ----
    // A cable is several cores that run together; a coax is a signal with its own screen drawn
    // beside it. Both are "this set of rows belongs together", which is why they are made the
    // same way: pick the rows, then say what they are.
    // Unifying is a two-step: the button ARMS a pick, the table becomes selectable, and Enter
    // applies it to whatever was picked. Rows that are only selectable while something is
    // waiting for them cannot be picked by accident, and the outline never lingers.
    auto* cableBtn = new QPushButton(QStringLiteral("Unify as cable"), this);
    cableBtn->setToolTip(QStringLiteral(
        "Click, pick the rows that run together, then Enter: they become one cable and share a "
        "background of their own"));
    auto* coaxBtn = new QPushButton(QStringLiteral("Unify as coax"), this);
    coaxBtn->setToolTip(QStringLiteral(
        "Click, pick the rows, then Enter: each becomes a coax - a signal with its own screen "
        "drawn beside it"));
    auto* looseBtn = new QPushButton(QStringLiteral("Un-bundle"), this);
    looseBtn->setToolTip(QStringLiteral(
        "Click, pick the rows, then Enter: they leave their cable"));
    for (QPushButton* b : {cableBtn, coaxBtn, looseBtn}) b->setCheckable(true);
    unifyButtons_ = {cableBtn, coaxBtn, looseBtn};
    // The way OUT of a pick: it appears when one starts and disappears when it ends, so what to
    // do next is on screen rather than something to remember. Enter still works.
    finishUnifyBtn_ = new QPushButton(QStringLiteral("Finish unify"), this);
    finishUnifyBtn_->setToolTip(QStringLiteral(
        "Apply the pick to the rows marked so far (Enter does the same; Esc abandons it)"));
    finishUnifyBtn_->setStyleSheet(QStringLiteral("font-weight:600;"));
    finishUnifyBtn_->hide();
    connect(finishUnifyBtn_, &QPushButton::clicked, this, [this] { applyUnify(); });
    const QString kinds[] = {QStringLiteral("cable"), QStringLiteral("coax"),
                             QStringLiteral("loose")};
    for (int i = 0; i < 3; ++i) {
        QPushButton* b = unifyButtons_[i];
        const QString kind = kinds[i];
        connect(b, &QPushButton::clicked, this, [this, kind, b] {
            if (!b->isChecked()) { cancelUnify(); return; } // clicking the armed one disarms it
            armUnify(kind);
        });
    }

    // Show-on-wire for the WHOLE column: one tick each for the colour, the cross-section and
    // the label across every wire, plus All for the three together.
    auto* showLabel = new QLabel(QStringLiteral("Show on wire"), this);
    showLabel->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    struct ColTick { const char* text; const char* tip; };
    const ColTick colTicks[] = {
        {"All", "Show the colour, the cross-section and the label on every wire"},
        {"Clr", "Show the colour code on every wire"},
        {"AWG", "Show the cross-section on every wire"},
        {"Lbl", "Show the label on every wire"},
    };
    QList<QWidget*> showAllBoxes;
    for (int i = 0; i < 4; ++i) {
        auto* cb = new QCheckBox(QString::fromLatin1(colTicks[i].text), this);
        cb->setToolTip(QString::fromLatin1(colTicks[i].tip));
        cb->setTristate(false);
        connect(cb, &QCheckBox::clicked, this, [this, i](bool on) {
            for (auto& w : def_.wires) {
                if (i == 0 || i == 1) w.showColor = on;
                if (i == 0 || i == 2) w.showGauge = on;
                if (i == 0 || i == 3) w.showLabel = on;
            }
            reloadAll();
            summaryLabel_->setText(on ? QStringLiteral("Marking shown on every wire")
                                      : QStringLiteral("Marking hidden on every wire"));
        });
        showAllBoxes << cb;
        showColumnBoxes_ << cb;
    }

    auto* groupLabel = new QLabel(QStringLiteral("Group by"), this);
    groupLabel->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    groupByBox_ = new QComboBox(this);
    groupByBox_->addItem(QStringLiteral("None"), static_cast<int>(WireGrouping::None));
    groupByBox_->addItem(QStringLiteral("Shield"), static_cast<int>(WireGrouping::Shield));
    groupByBox_->addItem(QStringLiteral("Twist"), static_cast<int>(WireGrouping::Twist));
    groupByBox_->addItem(QStringLiteral("Connectors"), static_cast<int>(WireGrouping::Connectors));
    groupByBox_->setToolTip(QStringLiteral("Gather the wire rows under folding group headers"));
    groupByBox_->setCurrentIndex(groupByBox_->findData(static_cast<int>(grouping_)));
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
    wireTable_->setColumnCount(14);
    wireTable_->setHorizontalHeaderLabels(
        {QStringLiteral("From"), QStringLiteral("Pin"), QStringLiteral("To"),
         QStringLiteral("Pin"), QStringLiteral("Type"), QStringLiteral("Bundle"),
         QStringLiteral("Gauge"), QStringLiteral("Color"), QStringLiteral("Label"),
         QStringLiteral("Show on wire"), QStringLiteral("Internal shield"),
         QStringLiteral("Twist"), QString(), QStringLiteral("Library part")});
    wireTable_->verticalHeader()->setVisible(false);
    wireTable_->horizontalHeader()->setStretchLastSection(true);
    // Rows are pickable so several can be gathered into one cable or coax - that is what the
    // Unify buttons work on. Picking a row does not edit anything.
    wireTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Not selectable until a Unify button asks for a pick (see armUnify).
    wireTable_->setSelectionMode(QAbstractItemView::NoSelection);
    // A picked row cannot be seen through its own cells - every cell is a widget, and a widget
    // paints over the selection colour behind it. So the picked rows are outlined instead, by an
    // overlay that sits above the cells and draws a box round each run of them.
    pickOverlay_ = new PickOverlay(wireTable_);
    // The dialog watches the rows so a pick can take the press for itself.
    wireTable_->viewport()->installEventFilter(this);
    connect(wireTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] {
                if (pickOverlay_) pickOverlay_->refresh();
                if (pendingUnify_.isEmpty()) return;
                const int n = pickedWireIds().size();
                summaryLabel_->setText(
                    QStringLiteral("%1 row(s) picked — Enter to apply, Esc to cancel").arg(n));
            });
    // Right-clicking a row asks about the part it is made of - a cable bundle, a wire, a braid. The
    // rows of one cable all name the same part, so right-clicking any of them asks about the cable.
    // This catches the gaps between the cells; armRowMenu catches the cells themselves.
    wireTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(wireTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const int row = wireTable_->rowAt(at.y());
        if (row < 0) return;
        // A row carries its wire's id on its first cell widget, the way pickedWireIds reads it.
        QWidget* cell = wireTable_->cellWidget(row, 0);
        showWireRowMenu(cell ? cell->property("wireId").toString() : QString(),
                        wireTable_->viewport()->mapToGlobal(at));
    });
    wireTable_->setMinimumHeight(200);
    return section(QStringLiteral("Wire connections"), wireTable_, this,
                   QList<QWidget*>{addBtn, placeCableBtn, fillBtn, cableBtn, coaxBtn, looseBtn,
                                   finishUnifyBtn_,
                                   allGaugeLabel,
                                   allGaugeBox_, showLabel} +
                       showAllBoxes +
                       QList<QWidget*>{groupLabel, groupByBox_, expandBtn, collapseBtn});
}

// The wires whose rows are picked in the table, read off the id each row carries. Group header
// rows have none, so they drop out on their own.
// Turn on row picking for a pending unify. Only one can be waiting at a time.
// While a pick is armed, a press on the wire rows picks the row it landed on - whatever cell it
// landed in. The table's own handler cannot be relied on here: every cell of a row holds a widget,
// which the table treats as a persistent editor, and a press on one is spent giving that editor
// the focus rather than selecting anything.
bool HarnessBuilderDialog::eventFilter(QObject* watched, QEvent* ev) {
    if (wireTable_ && watched == wireTable_->viewport() && !pendingUnify_.isEmpty() &&
        ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton) {
            const QModelIndex ix = wireTable_->indexAt(me->position().toPoint());
            if (ix.isValid()) {
                wireTable_->selectionModel()->select(
                    ix, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, ev);
}

// Let the mouse through the wire rows' cell widgets (or take it back), so a click during a pick
// reaches the table instead of the field it happened to land on. The fields' children are
// included: a centred combo is an editable one, and its line edit would take the press.
void HarnessBuilderDialog::setRowsPickable(bool pickable) {
    if (!wireTable_) return;
    for (int r = 0; r < wireTable_->rowCount(); ++r)
        for (int c = 0; c < wireTable_->columnCount(); ++c) {
            QWidget* cell = wireTable_->cellWidget(r, c);
            if (!cell) continue;
            cell->setAttribute(Qt::WA_TransparentForMouseEvents, pickable);
            for (QWidget* kid : cell->findChildren<QWidget*>())
                kid->setAttribute(Qt::WA_TransparentForMouseEvents, pickable);
        }
}

void HarnessBuilderDialog::armUnify(const QString& kind) {
    pendingUnify_ = kind;
    for (QPushButton* b : unifyButtons_) {
        if (!b) continue;
        const QSignalBlocker block(b);
        b->setChecked(b->text().contains(kind == QLatin1String("loose")
                                             ? QStringLiteral("Un-bundle")
                                             : (harnessIsCoaxKind(kind)
                                                    ? QStringLiteral("coax")
                                                    : QStringLiteral("cable"))));
    }
    if (wireTable_) {
        wireTable_->setSelectionMode(QAbstractItemView::MultiSelection);
        // Focus FIRST, and clear after it: taking focus in multi-selection mode gives the table a
        // current row and selects it, so a pick that cleared before focusing started with row 1
        // already picked - and the user's first click on it read as doing nothing (it was
        // un-picking it).
        wireTable_->setFocus();
        wireTable_->clearSelection();
        setRowsPickable(true);
    }
    if (finishUnifyBtn_) finishUnifyBtn_->show();
    summaryLabel_->setText(
        kind == QLatin1String("loose")
            ? QStringLiteral("Pick the rows to take out of their cable, then press Enter (Esc to "
                           "cancel)")
            : QStringLiteral("Pick the rows to unify as one %1, then press Enter (Esc to cancel)")
                  .arg(kind));
}

// Nothing is waiting any more: the rows stop being selectable, so a click cannot leave a
// half-made pick behind.
void HarnessBuilderDialog::cancelUnify() {
    const bool was = !pendingUnify_.isEmpty();
    pendingUnify_.clear();
    for (QPushButton* b : unifyButtons_) {
        if (!b) continue;
        const QSignalBlocker block(b);
        b->setChecked(false);
    }
    if (wireTable_) {
        setRowsPickable(false);
        wireTable_->clearSelection();
        wireTable_->setSelectionMode(QAbstractItemView::NoSelection);
    }
    if (finishUnifyBtn_) finishUnifyBtn_->hide();
    if (pickOverlay_) pickOverlay_->refresh();
    if (was) summaryLabel_->setText(QStringLiteral("Nothing unified"));
}

// Enter: gather the picked rows into a cable / coax, or take them out of one.
void HarnessBuilderDialog::applyUnify() {
    const QString kind = pendingUnify_;
    if (kind.isEmpty()) return;
    const QStringList picked = pickedWireIds();
    if (picked.isEmpty()) {
        summaryLabel_->setText(QStringLiteral("No rows picked — pick some, or Esc to cancel"));
        return;
    }
    int n = 0;
    QString name;
    if (kind == QLatin1String("loose")) {
        for (const QString& wid : picked)
            for (auto& w : def_.wires)
                if (w.id == wid && !w.bundle.isEmpty()) {
                    w.bundle.clear();
                    w.kind = QStringLiteral("wire");
                    ++n;
                }
    } else {
        // A fresh bundle name, so unifying twice makes two bundles rather than merging them.
        const QString stem = harnessIsCoaxKind(kind) ? QStringLiteral("COAX")
                                                            : QStringLiteral("CABLE");
        for (int i = 1;; ++i) {
            name = QStringLiteral("%1%2").arg(stem).arg(i);
            bool taken = false;
            for (const auto& w : def_.wires)
                if (w.bundle == name) taken = true;
            if (!taken) break;
        }
        for (auto& w : def_.wires)
            if (picked.contains(w.id)) {
                w.bundle = name;
                // A cable is a BUNDLE of ordinary wires; only a coax changes what a row is.
                if (harnessIsCoaxKind(kind)) w.kind = kind;
                ++n;
            }
    }
    pendingUnify_.clear();
    for (QPushButton* b : unifyButtons_) {
        if (!b) continue;
        const QSignalBlocker block(b);
        b->setChecked(false);
    }
    if (wireTable_) {
        setRowsPickable(false); // the fields take the mouse again
        wireTable_->clearSelection();
        wireTable_->setSelectionMode(QAbstractItemView::NoSelection);
    }
    if (finishUnifyBtn_) finishUnifyBtn_->hide();
    reloadAll();
    summaryLabel_->setText(name.isEmpty()
                               ? QStringLiteral("%1 row(s) un-bundled").arg(n)
                               : QStringLiteral("%1 row(s) unified as %2").arg(n).arg(name));
}

// Enter applies a pending pick and Esc abandons it — both taken before QDialog can read them as
// "press the default button" and "close the window".
void HarnessBuilderDialog::showEvent(QShowEvent* e) {
    forgetSharedLibrary();   // a part added since this window was last open is pickable now
    QDialog::showEvent(e);
}

void HarnessBuilderDialog::keyPressEvent(QKeyEvent* e) {
    if (pendingAddGroup_ && e->key() == Qt::Key_Escape) {
        pendingAddGroup_ = false;
        syncing_ = true;
        reloadWires();
        syncing_ = false;
        summaryLabel_->setText(QStringLiteral("No wire added"));
        e->accept();
        return;
    }
    if (!pendingUnify_.isEmpty()) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            applyUnify();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            cancelUnify();
            e->accept();
            return;
        }
    }
    QDialog::keyPressEvent(e);
}

QStringList HarnessBuilderDialog::pickedWireIds() const {
    QStringList out;
    if (!wireTable_ || !wireTable_->selectionModel()) return out;
    QSet<int> rows;
    for (const QModelIndex& ix : wireTable_->selectionModel()->selectedIndexes())
        rows.insert(ix.row());
    for (int r : rows) {
        QWidget* cell = wireTable_->cellWidget(r, 0);
        const QString wid = cell ? cell->property("wireId").toString() : QString();
        if (!wid.isEmpty() && !out.contains(wid)) out << wid;
    }
    return out;
}

// A cable placed into the harness, expanded into what it is made of.
//
// This is the point of describing a cable's make-up at all. Dropping a 4-pair screened cable into a
// harness used to mean adding eight rows, setting eight colours, remembering which pairs go together,
// inventing a twist group per pair and a shield group for the lot, and getting the coax's two rows
// right. Now the cable says all of that itself: the library knows what it is, and this turns it into
// rows - the pairs twisted, the screened groups screened, the coax drawn as a coax, the drain wire
// there to be landed on a pin.
// The menu a part gets when it is right-clicked: see what it is, see its two drawings, or go to it in
// the library. Wherever a part is NAMED - a wire row, a cable bundle, a connector - the question is the
// same one, so it is the same menu.
void HarnessBuilderDialog::showPartMenu(const QString& libPartId, const QPoint& at,
                                        const QString& what) {
    if (libPartId.trimmed().isEmpty()) {
        summaryLabel_->setText(
            QStringLiteral("%1 is not made of a library part yet — name one in its Library column, "
                           "and there will be something to look at.")
                .arg(what.isEmpty() ? QStringLiteral("That row") : what));
        return;
    }
    const LibraryPart* part = sharedLibrary().byId(libPartId);
    if (!part) {
        summaryLabel_->setText(QStringLiteral(
            "The part this is made of is not in any connected library — connect the library it lives "
            "in and it will be found."));
        return;
    }
    QMenu menu(this);
    const QString title = part->title();
    menu.addAction(QStringLiteral("%1 — %2").arg(partKindName(part->kind), title))->setEnabled(false);
    menu.addSeparator();
    const QString noun = part->kind == PartKind::Cable ? QStringLiteral("cable")
                                                       : partKindName(part->kind).toLower();
    menu.addAction(QStringLiteral("See the %1's schematic").arg(noun), this, [this, part] {
        PartViewDialog::showPart(*part, PartViewDialog::Show::Schematic, this);
    });
    menu.addAction(QStringLiteral("See the %1's mechanical drawing").arg(noun), this, [this, part] {
        PartViewDialog::showPart(*part, PartViewDialog::Show::Mechanical, this);
    });
    menu.addAction(QStringLiteral("See what it is"), this, [this, part] {
        PartViewDialog::showPart(*part, PartViewDialog::Show::Data, this);
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("See the %1 in the library").arg(noun), this,
                   [this, libPartId] { emit openLibraryAt(libPartId); });
    menu.exec(at);
}

int HarnessBuilderDialog::nextFreePin(const HarnessConnector& c) const {
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
}

int HarnessBuilderDialog::addCableFromLibrary(const QString& libPartId) {
    if (def_.connectors.size() < 2) {
        summaryLabel_->setText(QStringLiteral(
            "A cable runs between two connectors — add the connectors first."));
        return 0;
    }
    // Which cable. Asked for when it is not named, out of the cables the connected libraries hold.
    QString id = libPartId;
    const PartsLibrary lib = sharedLibrary();
    if (id.isEmpty()) {
        QStringList titles;
        QStringList ids;
        for (const LibraryPart& p : lib.parts) {
            if (p.kind != PartKind::Cable) continue;
            // cableSpecOf, not the raw parameter: the make-up has its own key now, and a cable described
            // before that has it under the old one. Reading the old key directly is what made a
            // perfectly good cable report "no make-up described" and refuse to be placed.
            const CableSpec spec = cableSpecOf(p);
            titles << QStringLiteral("%1  —  %2").arg(p.title(), spec.isEmpty()
                                                                     ? QStringLiteral("no make-up "
                                                                                      "described")
                                                                     : spec.summary());
            ids << p.id;
        }
        if (ids.isEmpty()) {
            summaryLabel_->setText(QStringLiteral(
                "No cables in the library yet — add one there (its kind is Cable) and describe its "
                "make-up, then it can be placed here."));
            return 0;
        }
        bool ok = false;
        const QString picked = QInputDialog::getItem(this, QStringLiteral("Place a cable"),
                                                     QStringLiteral("Which cable?"), titles, 0,
                                                     false, &ok);
        if (!ok) return 0;
        const int at = titles.indexOf(picked);
        if (at < 0) return 0;
        id = ids.at(at);
    }
    const LibraryPart* part = lib.byId(id);
    if (!part) return 0;
    const CableSpec spec = cableSpecOf(*part);
    if (spec.isEmpty()) {
        summaryLabel_->setText(
            QStringLiteral("%1 has no make-up described — open it in the library and say what is "
                           "inside the jacket.")
                .arg(part->title()));
        return 0;
    }

    // The bundle name: the cable's own designation, made unique so two of the same cable in one
    // harness are two cables and not one.
    QString bundle = part->mfrPn.isEmpty() ? part->description : part->mfrPn;
    if (bundle.trimmed().isEmpty()) bundle = QStringLiteral("cable");
    {
        int n = 1;
        QString candidate = bundle;
        auto taken = [this](const QString& name) {
            for (const auto& w : def_.wires)
                if (w.bundle == name) return true;
            return false;
        };
        while (taken(candidate)) candidate = QStringLiteral("%1 (%2)").arg(bundle).arg(++n);
        bundle = candidate;
    }

    // Which connectors it runs between, and the next free pin at each end - the same defaults a new
    // wire takes, so a placed cable behaves like the rows a person would have added.
    const HarnessConnector* from = nullptr;
    const HarnessConnector* to = nullptr;
    for (const auto& c : def_.connectors) {
        if (!from && c.side == HarnessSide::Left) from = &c;
        if (!to && c.side == HarnessSide::Right) to = &c;
    }
    if (!from) from = &def_.connectors.front();
    if (!to) to = &def_.connectors.back();

    const std::vector<CableConductor> cores = cableConductors(spec, bundle);
    for (const CableConductor& cc : cores) {
        HarnessWire w;
        w.id = uniqueWireId();
        ++wireSeq_;
        w.fromConn = from->id;
        w.toConn = to->id;
        w.gauge = cc.gauge;
        w.color = cc.colour;
        w.kind = cc.kind;
        w.bundle = cc.bundle;
        w.twistGroup = cc.twistGroup;
        w.shieldGroup = cc.shieldGroup;
        w.label = cc.label;
        w.libPart = part->id;   // so the bill of materials orders the CABLE, by its own number
        w.showColor = true;
        def_.wires.push_back(w);
        // The pins are given out after each row is in, so nextFreePin sees the rows already placed.
        HarnessWire& placed = def_.wires.back();
        placed.fromPin = nextFreePin(*from);
        placed.toPin = nextFreePin(*to);
    }
    // A screen around the whole cable is asked for ON THE CONNECTORS it arrives at, one tick per
    // end - the same ticks a person would use for a screen they were drawing by hand. That is what
    // draws the screen just inboard of the pins and carries its drain back to the shell, and it is
    // why the cable's conductors are no longer marked as if each of them were screened.
    QString shieldNote;
    if (cableWantsOverallScreen(spec)) {
        for (auto& c : def_.connectors) {
            if (c.id != from->id && c.id != to->id) continue;
            // Only the flag for the connector's own end means anything.
            if (c.side == HarnessSide::Left) c.shieldLeft = true;
            else c.shieldRight = true;
        }
        QStringList screens;
        if (spec.overallFoil) screens << QStringLiteral("foil");
        if (spec.overallBraid) screens << QStringLiteral("braid");
        shieldNote = QStringLiteral(", overall %1 ticked on %2 and %3")
                         .arg(screens.join(QStringLiteral(" + ")),
                              from->designator.isEmpty() ? from->id : from->designator,
                              to->designator.isEmpty() ? to->id : to->designator);
    }
    // The rows, the pin map and the preview are brought up to date here rather than by whoever
    // called: a public call that changes the harness must not be able to leave the window showing
    // something else, whatever the caller remembers to do afterwards. The note is set last, since a
    // rebuild writes its own.
    const QString note = QStringLiteral("%1 placed: %2 conductor(s) — %3%4")
                             .arg(part->title())
                             .arg(cores.size())
                             .arg(spec.summary(), shieldNote);
    reloadAll();
    summaryLabel_->setText(note);
    return static_cast<int>(cores.size());
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
    w.fromPin = nextFreePin(*from);
    w.toPin = nextFreePin(*to);
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
    // Grouped by connector pair, the groups read in the order the connectors are PLACED - down
    // the left side, then down the right - so the list matches the block map and the drawing
    // rather than the order the wires happened to be typed in.
    if (grouping_ == WireGrouping::Connectors) {
        auto placementRank = [this](const QString& id) {
            const HarnessConnector* c = harnessConnById(def_, id);
            if (!c) return 1000; // a screen or an open end sorts after the connectors
            return (c->side == HarnessSide::Left ? 0 : 500) + c->order;
        };
        std::stable_sort(groups.begin(), groups.end(),
                         [&placementRank](const Group& a, const Group& b) {
                             const QString ka = a.key.mid(2), kb = b.key.mid(2); // "c:<from>><to>"
                             const int af = placementRank(ka.section(QLatin1Char('>'), 0, 0));
                             const int bf = placementRank(kb.section(QLatin1Char('>'), 0, 0));
                             if (af != bf) return af < bf;
                             return placementRank(ka.section(QLatin1Char('>'), 1, 1)) <
                                    placementRank(kb.section(QLatin1Char('>'), 1, 1));
                         });
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
    // The rows of one cable, screen or twisted pair say so once, in a cell of their own.
    mergeGroupCells();
    // Sized to the widgets, not the header text: a connector name or a gauge must never be
    // clipped to "0.5 mm...".
    fitColumns(wireTable_);
    // The rows just built their cell widgets on top of the pick overlay; put it back above them.
    if (pickOverlay_) pickOverlay_->refresh();
    // Rows rebuilt during a pick get fresh cell widgets, which would start taking the mouse again.
    if (!pendingUnify_.isEmpty()) setRowsPickable(true);
    // The column-wide ticks reflect what every wire actually has, so they never claim a state
    // the rows do not share.
    if (showColumnBoxes_.size() == 4 && !def_.wires.empty()) {
        bool allClr = true, allGa = true, allLbl = true;
        for (const auto& w : def_.wires) {
            if (!w.showColor) allClr = false;
            if (!w.showGauge) allGa = false;
            if (!w.showLabel) allLbl = false;
        }
        const bool flags[4] = {allClr && allGa && allLbl, allClr, allGa, allLbl};
        for (int i = 0; i < 4; ++i) {
            const QSignalBlocker block(showColumnBoxes_[i]);
            showColumnBoxes_[i]->setChecked(flags[i]);
        }
    }
    // A rebuild must not quietly make the rows pickable again.
    wireTable_->setSelectionMode(pendingUnify_.isEmpty() ? QAbstractItemView::NoSelection
                                                         : QAbstractItemView::MultiSelection);
}

// The columns that describe a GROUP of rows rather than one row, merged. Rows that run together as
// one cable, one screen or one twisted pair are one thing, and a column repeating its name once per
// row said the opposite - it read as loose wires that happen to share a word. Consecutive rows with
// the same value are spanned into a single tall cell with that value centred in it, and editing there
// applies to every row the cell now stands for. A row on its own keeps an ordinary cell: that is how
// a group is named in the first place.
//
// The merged cell also SAYS what the group is - a twisted pair, a screened pair, the cable's part
// number - which is the difference between a column of repeated text and a marked-up block a wireman
// can read at a glance. Several pairs simply make several blocks.
void HarnessBuilderDialog::mergeGroupCells() {
    struct GroupCol {
        int col;
        QString HarnessWire::*field;
        bool isPart;
    };
    static const GroupCol kCols[] = {{5, &HarnessWire::bundle, false},
                                     {10, &HarnessWire::shieldGroup, false},
                                     {11, &HarnessWire::twistGroup, false},
                                     {13, &HarnessWire::libPart, true}};
    const int rows = wireTable_->rowCount();
    auto wireAt = [this](int row) -> const HarnessWire* {
        // A group header spans the full width and is not a wire at all.
        if (wireTable_->columnSpan(row, 0) > 1) return nullptr;
        QWidget* cell = wireTable_->cellWidget(row, 0);
        const QString id = cell ? cell->property("wireId").toString() : QString();
        if (id.isEmpty()) return nullptr;
        for (const auto& w : def_.wires)
            if (w.id == id) return &w;
        return nullptr;
    };
    for (const GroupCol& gc : kCols) {
        auto valueAt = [&](int row) -> QString {
            const HarnessWire* w = wireAt(row);
            return w ? (w->*gc.field) : QString();
        };
        for (int row = 0; row < rows;) {
            const QString value = valueAt(row);
            if (value.isEmpty()) { ++row; continue; }
            int end = row + 1;
            while (end < rows && valueAt(end) == value) ++end;
            const int count = end - row;
            if (count < 2) { row = end; continue; }
            // The ids the merged cell speaks for - all of them, so an edit reaches every row.
            QStringList ids;
            for (int r = row; r < end; ++r)
                if (QWidget* c = wireTable_->cellWidget(r, 0))
                    ids << c->property("wireId").toString();
            // The covered rows' own editors go away first: a widget left in a cell a span covers is
            // still a live widget sitting on top of the table.
            for (int r = row; r < end; ++r) wireTable_->removeCellWidget(r, gc.col);
            wireTable_->setSpan(row, gc.col, count, 1);
            const QString tint = bundleTint(wireAt(row) ? wireAt(row)->bundle : QString());

            if (gc.isPart) {
                // One cable, one part number. Repeating it down the rows invited them to disagree,
                // and a bill of materials cannot be built out of rows that disagree about what they
                // are made of. Choosing here names the part for the whole bundle at once.
                auto* box = libraryPartBox(wireTable_,
                                           {PartKind::Wire, PartKind::Cable, PartKind::ShieldBraid},
                                           value);
                centerCombo(box);
                box->setToolTip(QStringLiteral(
                    "The part all %1 of these rows are made of — one cable, one part number. "
                    "Choosing here names it for every one of them.").arg(count));
                if (!tint.isEmpty())
                    box->setStyleSheet(QStringLiteral("background:%1;").arg(tint));
                connect(box, &QComboBox::currentIndexChanged, this, [this, ids, box](int i) {
                    if (syncing_ || i < 0) return;
                    const QString part = box->itemData(i).toString();
                    const LibraryPart* p = sharedLibrary().byId(part);
                    const QString gauge = p ? p->value(QStringLiteral("gauge")) : QString();
                    for (auto& w : def_.wires)
                        if (ids.contains(w.id)) {
                            w.libPart = part;
                            if (!gauge.isEmpty()) w.gauge = gauge;
                        }
                    reloadWires();
                    refreshPreview();
                });
                wireTable_->setCellWidget(row, gc.col, box);
                armCellMenu(box, ids.value(0));
                row = end;
                continue;
            }

            auto* one = new QLineEdit(value, wireTable_);
            one->setAlignment(Qt::AlignCenter);
            one->setFrame(false);
            // What this block IS, said in the cell rather than left to be worked out from the
            // column heading and the row count. A pair of twisted wires is a twisted pair; screened,
            // it is a screened pair - and its screen is caught at BOTH ends, which is what the
            // symbols on the drawing show.
            QString what;
            if (gc.col == 11)
                what = count == 2 ? QStringLiteral("twisted pair")
                                  : QStringLiteral("twisted group of %1").arg(count);
            else if (gc.col == 10)
                what = count == 2 ? QStringLiteral("screened pair")
                                  : QStringLiteral("screened group of %1").arg(count);
            if (!what.isEmpty()) {
                one->addAction(gc.col == 11 ? twistIcon() : shieldIcon(), QLineEdit::LeadingPosition);
                one->setToolTip(QStringLiteral(
                    "%1 — these %2 rows are %3 together, and the drawing marks it at both ends. "
                    "The name is theirs as a group: change it here and it changes on all of them, "
                    "empty it to break them apart.")
                                    .arg(what.left(1).toUpper() + what.mid(1))
                                    .arg(count)
                                    .arg(gc.col == 11 ? QStringLiteral("twisted")
                                                      : QStringLiteral("screened")));
            } else {
                one->setToolTip(QStringLiteral(
                    "These %1 rows are one cable. Renaming it here renames it on all of them; empty "
                    "the field to break the cable up into separate wires again.").arg(count));
            }
            // Centred in the tall cell, and carrying the bundle's own colour so the merged block
            // reads as one object rather than a hole in the tinted rows.
            QString sheet = QStringLiteral("border:none;font-weight:600;");
            if (!tint.isEmpty()) sheet += QStringLiteral("background:%1;").arg(tint);
            one->setStyleSheet(sheet);
            const QString before = value;
            QString HarnessWire::*field = gc.field;
            connect(one, &QLineEdit::editingFinished, this, [this, ids, one, before, field] {
                if (syncing_) return;
                const QString name = one->text().trimmed();
                if (name == before) return;
                for (auto& w : def_.wires)
                    if (ids.contains(w.id)) (w.*field) = name;
                reloadWires();
                refreshPreview();
            });
            wireTable_->setCellWidget(row, gc.col, one);
            armCellMenu(one, ids.value(0));
            row = end;
        }
    }
}

// Right-clicking a row asks about the part the row is made of. Every cell of the row is a WIDGET,
// and a widget takes the press before the table ever sees it - which is why the table's own context
// menu never opened and Qt answered instead with a line edit's Copy / Select All over the rows. Each
// cell is asked to defer to this.
void HarnessBuilderDialog::showWireRowMenu(const QString& wireId, const QPoint& globalPos) {
    QString libPart, what;
    for (const auto& w : def_.wires)
        if (w.id == wireId) {
            libPart = w.libPart;
            what = w.bundle.isEmpty() ? QStringLiteral("That wire")
                                      : QStringLiteral("The cable %1").arg(w.bundle);
        }
    showPartMenu(libPart, globalPos, what);
}

void HarnessBuilderDialog::armCellMenu(QWidget* cell, const QString& wireId) {
    if (!cell) return;
    cell->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(cell, &QWidget::customContextMenuRequested, this,
            [this, cell, wireId](const QPoint& at) {
                showWireRowMenu(wireId, cell->mapToGlobal(at));
            });
}

void HarnessBuilderDialog::armRowMenu(int row, const QString& wireId) {
    for (int c = 0; c < wireTable_->columnCount(); ++c) armCellMenu(wireTable_->cellWidget(row, c), wireId);
}

// Put a new wire into the group `key` names: for a connector pair that means the two connectors
// it runs between, for a shield or twist group that group's name. This is what clicking a group
// header does while an Add is waiting.
void HarnessBuilderDialog::addWireToGroup(const QString& key) {
    pendingAddGroup_ = false;
    const size_t before = def_.wires.size();
    addWire();
    if (def_.wires.size() == before) {
        summaryLabel_->setText(QStringLiteral("Add at least two connectors first"));
        return;
    }
    HarnessWire& w = def_.wires.back();
    if (key.startsWith(QLatin1String("c:"))) {
        const QString rest = key.mid(2);
        const QString from = rest.section(QLatin1Char('>'), 0, 0);
        const QString to = rest.section(QLatin1Char('>'), 1, 1);
        if (!from.isEmpty()) w.fromConn = from;
        if (!to.isEmpty()) w.toConn = to;
        autoFillPins(); // onto pins the group's own wires have left free
    } else if (key.startsWith(QLatin1String("s:"))) {
        w.shieldGroup = key.mid(2);
    } else if (key.startsWith(QLatin1String("t:"))) {
        w.twistGroup = key.mid(2);
    }
    reloadAll();
    spreadSections();
    summaryLabel_->setText(QStringLiteral("Wire added"));
}

void HarnessBuilderDialog::buildGroupHeader(int row, const QString& key, const QString& title,
                                            int memberCount) {
    const bool folded = collapsedGroups_.contains(key);
    // One clickable band across the whole width, so the fold target is obvious and large.
    wireTable_->setSpan(row, 0, 1, wireTable_->columnCount());
    auto* btn = new QToolButton(wireTable_);
    // The group this band stands for, readable from outside: Add wire needs to know which groups
    // there are to add to.
    btn->setProperty("groupKey", key);
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
    // While an Add is waiting for a group, a header click is where the new wire goes; otherwise
    // it folds the group as usual. The headers are outlined so which they are is not a guess.
    if (pendingAddGroup_) {
        btn->setToolTip(QStringLiteral("Click to add the new wire to this group"));
        btn->setStyleSheet(QStringLiteral(
            "QToolButton{background:#2f4038;color:#cfe3ff;font-weight:600;text-align:left;"
            "padding:3px 8px;border:1px solid #7ee0a0;}QToolButton:hover{background:#3a4d43;}"));
    }
    connect(btn, &QToolButton::clicked, this, [this, key] {
        if (pendingAddGroup_) {
            addWireToGroup(key);
            return;
        }
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

        // From / To, and the pin at each end. Either end can be a connector or a SHIELD.
        const QList<QPair<QString, QString>> ends = endpointChoices();
        for (int end = 0; end < 2; ++end) {
            const QString key = (end == 0) ? w.fromConn : w.toConn;
            const bool isShield = harnessIsShieldEndpoint(key);
            const bool isOpen = harnessIsOpenEndpoint(key);
            const QString tint = endpointTint(key);
            auto* box = new QComboBox(wireTable_);
            for (const auto& ep : ends) box->addItem(ep.second, ep.first);
            {
                const int at = box->findData(key);
                if (at >= 0) box->setCurrentIndex(at);
            }
            centerCombo(box);
            // The row carries its own wire id on its first cell, which is how the Unify buttons
            // know what a picked row is: the table holds group headers between the wire rows, so
            // a row NUMBER identifies nothing for long.
            if (end == 0) box->setProperty("wireId", id);
            // Each connector and each screen has its own colour (see endpointTint), so a row
            // says which parts it runs between without reading the text.
            box->setStyleSheet(QStringLiteral("background:%1;").arg(tint));
            connect(box, &QComboBox::currentIndexChanged, this,
                    [this, id, end, box, wireById](int i) {
                        if (syncing_ || i < 0) return;
                        const QString picked = box->itemData(i).toString();
                        if (picked.isEmpty()) return;
                        if (auto* ww = wireById(id)) {
                            if (end == 0) ww->fromConn = picked;
                            else ww->toConn = picked;
                        }
                        reloadAll();
                    });
            wireTable_->setCellWidget(row, end == 0 ? 0 : 2, box);

            const HarnessConnector* c = harnessConnById(def_, key);
            auto* pin = new QSpinBox(wireTable_);
            pin->setRange(1, c ? std::max(1, c->pinCount) : 1);
            pin->setValue(end == 0 ? w.fromPin : w.toPin);
            pin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            centerField(pin);
            // Neither a screen nor an open end has pins: the field says so rather than offering
            // a number that means nothing. A drain lands on the screen's leg; a flying lead
            // lands on nothing at all.
            if (isShield || isOpen) {
                pin->setEnabled(false);
                pin->setSpecialValueText(QStringLiteral("—"));
                pin->setRange(0, 0);
                pin->setToolTip(isShield
                                    ? QStringLiteral("A drain lands on the screen's leg, not on "
                                                   "a pin")
                                    : QStringLiteral("An open end lands on nothing"));
            }
            pin->setStyleSheet(QStringLiteral("background:%1;").arg(tint));
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

        // What the row IS. A coax is drawn as two lines - the signal and its screen - so it
        // also needs to say where that screen is terminated.
        auto* kind = new QComboBox(wireTable_);
        kind->addItem(QStringLiteral("Wire"), QStringLiteral("wire"));
        kind->addItem(QStringLiteral("Coax"), QStringLiteral("coax"));
        kind->addItem(QStringLiteral("Coax-splice"), QStringLiteral("coax-splice"));
        kind->setCurrentIndex(std::max(0, kind->findData(w.kind)));
        kind->setToolTip(QStringLiteral(
            "Wire: one conductor. Coax: a signal with its own screen drawn beside it, running "
            "pin to pin. Coax-splice: the same cable, but joined to the harness through a splice "
            "at each end - past the splice the two conductors are separate wires. A CABLE is "
            "not a kind of conductor - it is a bundle: unify the rows that run together."));
        centerCombo(kind);
        connect(kind, &QComboBox::currentIndexChanged, this,
                [this, id, kind, wireById](int i) {
                    if (syncing_ || i < 0) return;
                    if (auto* ww = wireById(id)) ww->kind = kind->itemData(i).toString();
                    reloadAll();
                });
        wireTable_->setCellWidget(row, 4, kind);

        auto* bundleEdit = new QLineEdit(w.bundle, wireTable_);
        bundleEdit->setPlaceholderText(QStringLiteral("—"));
        bundleEdit->setToolTip(QStringLiteral(
            "Rows sharing a bundle name are one cable or coax and share a background colour. "
            "The Unify buttons fill this in from the rows you pick."));
        centerEdit(bundleEdit);
        connect(bundleEdit, &QLineEdit::editingFinished, this,
                [this, id, bundleEdit, wireById] {
                    if (syncing_) return;
                    if (auto* ww = wireById(id)) ww->bundle = bundleEdit->text().trimmed();
                    reloadWires();
                });
        wireTable_->setCellWidget(row, 5, bundleEdit);

        auto* gauge = new QComboBox(wireTable_);
        gauge->addItems(gaugeChoices());
        gauge->setCurrentText(gaugeLabelFor(w.gauge));
        centerCombo(gauge);
        connect(gauge, &QComboBox::currentTextChanged, this,
                [this, id, wireById](const QString& t) {
                    if (syncing_) return;
                    if (auto* ww = wireById(id)) ww->gauge = gaugeValueFor(t);
                    refreshPreview();
                });
        wireTable_->setCellWidget(row, 6, gauge);

        // Colour: a swatch per entry, so a colour is chosen by eye. The code is carried in the
        // item DATA rather than parsed back out of the text, which is what lets an invented
        // colour have any name at all. The last entry adds one.
        auto* color = new QComboBox(wireTable_);
        // A coax has no colour: it has a SIGNAL and a SHIELD, and which of the two this row is
        // decides how it is drawn. A cable core can be a shield as well as a colour. Only a
        // plain wire gets the full colour list.
        const bool isCoax = harnessIsCoaxKind(w.kind);
        const bool isCable = (w.kind == QLatin1String("cable"));
        auto addCode = [&color](const QString& code) {
            color->addItem(codeIcon(code),
                           QStringLiteral("%1 - %2").arg(code, wireColorName(code)), code);
        };
        if (isCoax) {
            // One cable can hold several coaxes, and then each one's pair has to be named:
            // Signal 1 / Shield 1, Signal 2 / Shield 2. The stored code stays SIGNAL / SHIELD -
            // the number follows from the row's place among the coaxes of its bundle, so
            // re-ordering or re-bundling renumbers them rather than leaving a stale name.
            const int coaxNo = harnessCoaxIndex(def_, w);
            const QString suffix = coaxNo > 0 ? QStringLiteral(" %1").arg(coaxNo) : QString();
            color->addItem(codeIcon(QStringLiteral("SIGNAL")),
                           QStringLiteral("Signal") + suffix, QStringLiteral("SIGNAL"));
            color->addItem(codeIcon(QStringLiteral("SHIELD")),
                           QStringLiteral("Shield") + suffix, QStringLiteral("SHIELD"));
        } else {
            for (const QString& code : wireColorCodes()) {
                // Signal/Shield belong to a coax; the drain colour is offered everywhere.
                if (code == QLatin1String("SIGNAL")) continue;
                if (code == QLatin1String("SHIELD") && !isCable) continue;
                addCode(code);
            }
            color->addItem(QStringLiteral("Add custom..."), QString(kCustomColorItem));
        }
        // The closed field shows ONE dot - the one drawn inside it below. Without this the
        // item's own icon appears beside it and the colour is shown twice.
        color->setIconSize(QSize(0, 0));
        centerCombo(color);
        {
            const int at = color->findData(w.color);
            if (at >= 0) color->setCurrentIndex(at);
        }
        // A centred field cannot show the item's icon, so the swatch is put INTO the field as
        // a leading action - the chosen colour is visible without opening the list.
        QAction* swatch =
            color->lineEdit()->addAction(codeIcon(w.color), QLineEdit::LeadingPosition);
        connect(color, &QComboBox::currentIndexChanged, this,
                [this, id, color, wireById, swatch](int i) {
                    if (syncing_ || i < 0) return;
                    const QString data = color->itemData(i).toString();
                    if (data != QLatin1String(kCustomColorItem)) {
                        swatch->setIcon(codeIcon(data));
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
        wireTable_->setCellWidget(row, 7, color);

        auto* label = new QLineEdit(w.label, wireTable_);
        label->setPlaceholderText(QStringLiteral("Label"));
        label->setToolTip(QStringLiteral("Printed above the wire in the drawing"));
        centerEdit(label);
        connect(label, &QLineEdit::editingFinished, this, [this, id, label, wireById] {
            if (syncing_) return;
            if (auto* ww = wireById(id)) ww->label = label->text().trimmed();
            refreshPreview();
        });
        wireTable_->setCellWidget(row, 8, label);

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
                {"AWG", "Show the cross-section on the wire", &HarnessWire::showGauge},
                {"Lbl", "Show the label above the wire", &HarnessWire::showLabel},
            };
            // Just the three ticks per row. "All" is a column-wide control in the section
            // header - one per row was a lot of clicking to say the same thing, and it said
            // nothing a row could not already show.
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
            wireTable_->setCellWidget(row, 9, cell);
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
            refreshPreview(); // an internal screen shows in the preview
        });
        wireTable_->setCellWidget(row, 10, shield);

        auto* twist = new QLineEdit(w.twistGroup, wireTable_);
        twist->setPlaceholderText(QStringLiteral("—"));
        twist->setToolTip(QStringLiteral("Twist group: wires with the same name are twisted "
                                       "together (they must be neighbours)"));
        centerEdit(twist);
        connect(twist, &QLineEdit::editingFinished, this, [this, id, twist, wireById] {
            if (syncing_) return;
            if (auto* ww = wireById(id)) ww->twistGroup = twist->text().trimmed();
            refreshPreview();
        });
        wireTable_->setCellWidget(row, 11, twist);

        // The bundle's own background, so a cable reads as one block of rows.
        if (const QString tint = bundleTint(w.bundle); !tint.isEmpty())
            for (int c = 0; c < wireTable_->columnCount(); ++c) {
                if (QWidget* cell = wireTable_->cellWidget(row, c)) {
                    // A field that already carries a colour of its own (an endpoint, a pin)
                    // keeps it: which end a wire is on matters more than which cable it is in.
                    if (cell->styleSheet().isEmpty())
                        cell->setStyleSheet(QStringLiteral("background:%1;").arg(tint));
                }
                if (!wireTable_->item(row, c)) wireTable_->setItem(row, c, new QTableWidgetItem);
                wireTable_->item(row, c)->setBackground(QColor(tint));
            }

        auto* del = new QToolButton(wireTable_);
        del->setText(QStringLiteral("×"));
        del->setStyleSheet(QStringLiteral("color:#e06c6c;font-weight:700;"));
        del->setToolTip(QStringLiteral("Remove this wire"));
        connect(del, &QToolButton::clicked, this, [this, id] {
            for (int i = 0; i < static_cast<int>(def_.wires.size()); ++i)
                if (def_.wires[i].id == id) { removeWire(i); return; }
        });
        wireTable_->setCellWidget(row, 12, del);
        {
            // A conductor may be a discrete wire, one core of a CABLE, or a braid. All three are
            // offered, because all three are what a row can be made of - and a row placed by "Add
            // cable" IS a cable core, so a list without cables in it could not even show what the row
            // already was, let alone let a hook-up wire be chosen for it afterwards.
            auto* lib = libraryPartBox(wireTable_,
                                       {PartKind::Wire, PartKind::Cable, PartKind::ShieldBraid},
                                       w.libPart);
            centerCombo(lib);
            connect(lib, &QComboBox::currentIndexChanged, this, [this, id, lib, wireById](int i) {
                if (syncing_ || i < 0) return;
                if (auto* ww = wireById(id)) {
                    ww->libPart = lib->itemData(i).toString();
                    // The library's own gauge and colour, when it gives them: a row made of a real
                    // wire is drawn as that wire.
                    if (const LibraryPart* p = sharedLibrary().byId(ww->libPart)) {
                        const QString g = p->value(QStringLiteral("gauge"));
                        if (!g.isEmpty()) ww->gauge = g;
                    }
                }
                reloadWires();
                refreshPreview();
            });
            wireTable_->setCellWidget(row, 13, lib);
        }
        // Every cell of the row now exists, so every cell can hand a right-click to the row's menu.
        armRowMenu(row, id);
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
    numField(gaps, QStringLiteral("Flying lead length"), def_.layout.leadLength, 0, 5000,
             &leadLenSpin_);
    leadLenSpin_->setSpecialValueText(QStringLiteral("auto"));
    leadLenSpin_->setToolTip(QStringLiteral(
        "How far a wire with an OPEN end reaches out of its pin before it is terminated. Left on "
        "auto it reaches far enough to lie inside the screens of the cable it belongs to; a lead is "
        "otherwise as long as you leave it, and the drawn wire can still be pulled by its end."));
    numField(gaps, QStringLiteral("Text gap over line"), def_.layout.textGap, 0, 200,
             &textGapSpin_);
    numField(gaps, QStringLiteral("Text height"), def_.layout.textHeight, 0.5, 100,
             &textHeightSpin_);
    textHeightSpin_->setToolTip(QStringLiteral(
        "How tall the colour/gauge marking and the wire label are drawn"));
    numField(gaps, QStringLiteral("Splice size"), def_.layout.spliceSize, 0.5, 50,
             &spliceSizeSpin_);
    spliceSizeSpin_->setToolTip(QStringLiteral("How big the joint dot is drawn"));
    textGapSpin_->setToolTip(QStringLiteral(
        "How far the colour/gauge marking and the wire label sit above the wire"));

    Col bundle = makeCol();
    // How many twist symbols a group gets is the only count worth setting: the crossings drawn
    // inside one symbol are part of the symbol's own shape.
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
    comboField(bundle, QStringLiteral("Color names"),
               {{QStringLiteral("short"), QStringLiteral("Short")},
                {QStringLiteral("long"), QStringLiteral("Long")}},
               def_.layout.colorNameMode, &colorNameModeBox_);
    colorNameModeBox_->setToolTip(QStringLiteral(
        "Short writes the two-letter code on the wire (RD); Long spells it out (Red)"));

    comboField(bundle, QStringLiteral("Wire color"),
               {{QStringLiteral("bylayer"), QStringLiteral("By layer")},
                {QStringLiteral("actual"), QStringLiteral("Actual")}},
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
    terminateWiresBox_ = new QCheckBox(QStringLiteral("Terminate unconnected wires"), optCol);
    terminateWiresBox_->setToolTip(QStringLiteral(
        "Put an X on a wire end that reaches no pin, so a dangling wire shows on the sheet"));
    spliceBox_ = new QCheckBox(QStringLiteral("Splice where wires meet"), optCol);
    spliceBox_->setToolTip(QStringLiteral(
        "Mark a joint where two wires meet end to end; without it a join cannot be told from "
        "one wire passing over another"));
    optLay->addWidget(bridgeBox_);
    optLay->addWidget(terminateBox_);
    optLay->addWidget(terminateWiresBox_);
    optLay->addWidget(spliceBox_);
    optLay->addStretch(1);
    grid->addWidget(ribbonRules(optCol, this));
    grid->addStretch(1);

    // Every control writes straight into the layout block.
    auto bindNum = [this](QDoubleSpinBox* sp, double HarnessLayout::*field) {
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this, field](double v) {
            if (syncing_) return;
            def_.layout.*field = v;
            refreshPreview(); // the preview draws from these, so it keeps up
        });
    };
    bindNum(leftXSpin_, &HarnessLayout::leftX);
    bindNum(leftYSpin_, &HarnessLayout::leftY);
    bindNum(rightXSpin_, &HarnessLayout::rightX);
    bindNum(gapSpin_, &HarnessLayout::connectorGap);
    bindNum(legGapSpin_, &HarnessLayout::legGap);
    bindNum(shieldLegSpin_, &HarnessLayout::shieldLegLength);
    bindNum(leadLenSpin_, &HarnessLayout::leadLength);
    bindNum(textGapSpin_, &HarnessLayout::textGap);
    bindNum(spliceSizeSpin_, &HarnessLayout::spliceSize);
    bindNum(textHeightSpin_, &HarnessLayout::textHeight);
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
            refreshPreview();
        });
    };
    bindInt(twistAmountSpin_, &HarnessLayout::twistAmount);
    bindInt(shieldAmountSpin_, &HarnessLayout::shieldAmount);
    bindInt(wireShieldAmountSpin_, &HarnessLayout::wireShieldAmount);
    connect(twistTypeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_) return;
        def_.layout.twistType = twistTypeBox_->itemData(i).toString();
        refreshPreview();
    });
    connect(shieldTypeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_) return;
        def_.layout.shieldType = shieldTypeBox_->itemData(i).toString();
        refreshPreview();
    });
    connect(wireColorModeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_) return;
        def_.layout.wireColorMode = wireColorModeBox_->itemData(i).toString();
        refreshPreview();
    });
    connect(colorNameModeBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_) return;
        def_.layout.colorNameMode = colorNameModeBox_->itemData(i).toString();
        refreshPreview();
    });

    auto bindBool = [this](QCheckBox* cb, bool HarnessLayout::*field) {
        connect(cb, &QCheckBox::toggled, this, [this, field](bool on) {
            if (syncing_) return;
            def_.layout.*field = on;
            refreshPreview();
        });
    };
    bindBool(bridgeBox_, &HarnessLayout::bridgeCrossings);
    bindBool(terminateBox_, &HarnessLayout::terminateUnconnected);
    bindBool(terminateWiresBox_, &HarnessLayout::terminateUnconnectedWires);
    bindBool(spliceBox_, &HarnessLayout::spliceAtJoins);
    return section(QStringLiteral("Layout"), host, this, {}, /*clipBody=*/true);
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
    twistAmountSpin_->setValue(def_.layout.twistAmount);
    shieldAmountSpin_->setValue(def_.layout.shieldAmount);
    wireShieldAmountSpin_->setValue(def_.layout.wireShieldAmount);
    shieldLegSpin_->setValue(def_.layout.shieldLegLength);
    leadLenSpin_->setValue(def_.layout.leadLength);
    pinPitchSpin_->setValue(def_.layout.pinPitch);
    {
        const int at = twistTypeBox_->findData(def_.layout.twistType);
        twistTypeBox_->setCurrentIndex(at >= 0 ? at : 1);
        const int sat = shieldTypeBox_->findData(def_.layout.shieldType);
        shieldTypeBox_->setCurrentIndex(sat >= 0 ? sat : 0);
        const int wcm = wireColorModeBox_->findData(def_.layout.wireColorMode);
        wireColorModeBox_->setCurrentIndex(wcm >= 0 ? wcm : 0);
        const int cnm = colorNameModeBox_->findData(def_.layout.colorNameMode);
        colorNameModeBox_->setCurrentIndex(cnm >= 0 ? cnm : 0);

    }
    bridgeBox_->setChecked(def_.layout.bridgeCrossings);
    terminateBox_->setChecked(def_.layout.terminateUnconnected);
    terminateWiresBox_->setChecked(def_.layout.terminateUnconnectedWires);
    spliceBox_->setChecked(def_.layout.spliceAtJoins);
    shieldLegSpin_->setValue(def_.layout.shieldLegLength);
    leadLenSpin_->setValue(def_.layout.leadLength);
    textGapSpin_->setValue(def_.layout.textGap);
    spliceSizeSpin_->setValue(def_.layout.spliceSize);
    textHeightSpin_->setValue(def_.layout.textHeight);
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
    // What is being generated is kept WITH THE DRAWING, as a preset, without being asked for: a
    // harness that has been drawn is a harness worth being able to bring back, and having to remember
    // to save it first is how it gets lost. It is named after the ends it runs between, so generating
    // the same harness again replaces its own preset instead of piling up copies, while a second
    // harness on the same sheet keeps one of its own. This happens before the placement rather than
    // after it, so abandoning the placement with Esc still leaves the pattern kept - the work of
    // describing the harness was done either way. Presets are listed, loaded and deleted under
    // Presets.
    if (Document* doc = editor_->document(); doc && !def_.connectors.empty()) {
        QStringList ends;
        for (const auto& c : def_.connectors)
            ends << (c.designator.isEmpty() ? c.id : c.designator);
        if (ends.size() > 3) ends = QStringList{ends.first(), QStringLiteral("…"), ends.last()};
        const QString name = ends.join(QStringLiteral(" → "));
        QJsonObject presets = doc->harnessPresets();
        presets[name] = definitionToJson();
        doc->harnessPresets() = presets;
        doc->setDirty(true);
        lastPresetName_ = name;
    }
    // Hand the harness to the cursor rather than dropping it at its designed coordinates: the window
    // steps out of the way, the whole harness ghosts under the crosshair, and a click places it.
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
        o["libPart"] = c.libPart;
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
        o["libPart"] = w.libPart;
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
    lay["leadLength"] = def_.layout.leadLength;
    lay["pinPitch"] = def_.layout.pinPitch;
    lay["twistType"] = def_.layout.twistType;
    lay["shieldType"] = def_.layout.shieldType;
    lay["wireColorMode"] = def_.layout.wireColorMode;
    lay["textGap"] = def_.layout.textGap;
    lay["spliceSize"] = def_.layout.spliceSize;
    lay["textHeight"] = def_.layout.textHeight;
    lay["colorNameMode"] = def_.layout.colorNameMode;
    lay["terminateUnconnectedWires"] = def_.layout.terminateUnconnectedWires;
    lay["spliceAtJoins"] = def_.layout.spliceAtJoins;
    root["format"] = QStringLiteral("edraw-harness-builder");
    // Version 2: an overall shield is drawn ONCE per connector by default. A version-1 file was
    // written before that was the default, so its stored 2 was nobody's choice - see the loader.
    root["version"] = 2;
    root["connectors"] = conns;
    root["wires"] = wires;
    root["layout"] = lay;
    return root;
}

// A builder pattern goes to a file of its own, so a harness definition can be kept, re-opened
// and shared like any other document instead of living inside the application settings.
void HarnessBuilderDialog::saveDefinition() {
    QSettings st;
    // Presets live in a folder of their own, so the dialog opens where the others already are
    // rather than wherever the file dialog was last used for something unrelated.
    QString where = st.value(QLatin1String(kLastPathKey)).toString();
    if (where.isEmpty() || !where.startsWith(AppPaths::builderDir()))
        where = AppPaths::builderDir();
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save builder pattern"), where,
                                                QLatin1String(kFileFilter));
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

// ---- Patterns kept in the drawing -----------------------------------------------------------------
//
// A pattern saved to a FILE is for reuse across drawings. A PRESET belongs to the drawing it was
// generated on: it is saved inside it, comes back with it when it is opened, and is deleted from it
// when it is not wanted. That is what makes "generate, then keep this arrangement with the sheet"
// possible without leaving a trail of files that have to be matched back up to drawings by name.

void HarnessBuilderDialog::showPresetMenu(QWidget* under) {
    Document* doc = editor_ ? editor_->document() : nullptr;
    QMenu menu(this);
    if (!doc) {
        menu.addAction(QStringLiteral("No drawing to keep a preset in"))->setEnabled(false);
        menu.exec(under ? under->mapToGlobal(QPoint(0, under->height())) : QCursor::pos());
        return;
    }
    menu.addAction(QStringLiteral("Save what is in the builder now…"), this,
                   [this] { savePresetToDrawing(); });
    const QJsonObject presets = doc->harnessPresets();
    QStringList names = presets.keys();
    names.sort(Qt::CaseInsensitive);
    if (names.isEmpty()) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("This drawing has no presets yet"))->setEnabled(false);
    } else {
        menu.addSeparator();
        menu.addAction(QStringLiteral("In this drawing:"))->setEnabled(false);
        for (const QString& name : names) {
            // Each preset says how big it is, so one can be told from another without loading it.
            const QJsonObject def = presets.value(name).toObject();
            const int conns = def.value(QStringLiteral("connectors")).toArray().size();
            const int wires = def.value(QStringLiteral("wires")).toArray().size();
            QMenu* sub = menu.addMenu(QStringLiteral("%1  (%2 connector%3, %4 wire%5)")
                                          .arg(name)
                                          .arg(conns)
                                          .arg(conns == 1 ? QString() : QStringLiteral("s"))
                                          .arg(wires)
                                          .arg(wires == 1 ? QString() : QStringLiteral("s")));
            sub->addAction(QStringLiteral("Load it"), this,
                           [this, name] { loadPresetFromDrawing(name); });
            sub->addAction(QStringLiteral("Delete it"), this,
                           [this, name] { deletePresetFromDrawing(name); });
        }
    }
    menu.exec(under ? under->mapToGlobal(QPoint(0, under->height())) : QCursor::pos());
}

void HarnessBuilderDialog::savePresetToDrawing() {
    Document* doc = editor_ ? editor_->document() : nullptr;
    if (!doc) return;
    // Named after the drawing's own harness by default, numbered so a second one does not overwrite
    // the first by accident.
    QString suggestion = QStringLiteral("Harness 1");
    {
        int n = 1;
        while (doc->harnessPresets().contains(suggestion))
            suggestion = QStringLiteral("Harness %1").arg(++n);
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("Keep this harness with the drawing"),
                              QStringLiteral("Call the preset:"), QLineEdit::Normal, suggestion, &ok)
            .trimmed();
    if (!ok || name.isEmpty()) return;
    if (doc->harnessPresets().contains(name) &&
        QMessageBox::question(this, QStringLiteral("Schematic harness builder"),
                              QStringLiteral("This drawing already has a preset called \"%1\". "
                                             "Replace it?")
                                  .arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes)
        return;
    QJsonObject presets = doc->harnessPresets();
    presets[name] = definitionToJson();
    doc->harnessPresets() = presets;
    doc->setDirty(true);   // the drawing has changed: it now carries this preset
    summaryLabel_->setText(QStringLiteral("\"%1\" kept with the drawing — %2 connector(s), "
                                         "%3 wire(s)")
                               .arg(name)
                               .arg(def_.connectors.size())
                               .arg(def_.wires.size()));
}

void HarnessBuilderDialog::loadPresetFromDrawing(const QString& name) {
    Document* doc = editor_ ? editor_->document() : nullptr;
    if (!doc) return;
    const QJsonObject def = doc->harnessPresets().value(name).toObject();
    if (def.isEmpty() || !def.contains(QStringLiteral("connectors"))) {
        summaryLabel_->setText(QStringLiteral("\"%1\" is empty").arg(name));
        return;
    }
    applyDefinitionJson(def);
    spreadSections();
    summaryLabel_->setText(QStringLiteral("Loaded \"%1\" from the drawing").arg(name));
}

void HarnessBuilderDialog::deletePresetFromDrawing(const QString& name) {
    Document* doc = editor_ ? editor_->document() : nullptr;
    if (!doc) return;
    if (QMessageBox::question(this, QStringLiteral("Schematic harness builder"),
                              QStringLiteral("Delete the preset \"%1\" from this drawing?").arg(name),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    QJsonObject presets = doc->harnessPresets();
    presets.remove(name);
    doc->harnessPresets() = presets;
    doc->setDirty(true);
    summaryLabel_->setText(QStringLiteral("Deleted the preset \"%1\"").arg(name));
}

void HarnessBuilderDialog::loadDefinition() {
    QSettings st;
    QString where = st.value(QLatin1String(kLastPathKey)).toString();
    if (where.isEmpty() || !where.startsWith(AppPaths::builderDir()))
        where = AppPaths::builderDir();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load builder pattern"),
                                                      where, QLatin1String(kFileFilter));
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
    spreadSections(); // a loaded harness is in view without dragging anything
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
        c.libPart = o.value("libPart").toString();
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
        w.libPart = o.value("libPart").toString();
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
    // An overall screen is drawn once unless asked for more. A pattern written before that was
    // the default stored the old 2 without anyone choosing it, so a version-1 file takes the new
    // default instead; from version 2 on, whatever the file says is what the user set.
    const int fileVersion = root.value(QStringLiteral("version")).toInt(1);
    loaded.layout.shieldAmount = fileVersion >= 2 ? lay.value("shieldAmount").toInt(1) : 1;
    loaded.layout.wireShieldAmount = lay.value("wireShieldAmount").toInt(2);
    loaded.layout.shieldLegLength = lay.value("shieldLegLength").toDouble(20.0);
    loaded.layout.leadLength = lay.value("leadLength").toDouble(0.0);
    loaded.layout.pinPitch = lay.value("pinPitch").toDouble(15.0);
    loaded.layout.twistType = lay.value("twistType").toString(QStringLiteral("jog"));
    loaded.layout.shieldType = lay.value("shieldType").toString(QStringLiteral("ellipse"));
    loaded.layout.wireColorMode =
        lay.value("wireColorMode").toString(QStringLiteral("bylayer"));
    loaded.layout.textGap = lay.value("textGap").toDouble(0.0);
    loaded.layout.spliceSize = lay.value("spliceSize").toDouble(3.0);
    loaded.layout.textHeight = lay.value("textHeight").toDouble(3.5);
    loaded.layout.colorNameMode =
        lay.value("colorNameMode").toString(QStringLiteral("short"));
    loaded.layout.terminateUnconnectedWires =
        lay.value("terminateUnconnectedWires").toBool(false);
    loaded.layout.spliceAtJoins = lay.value("spliceAtJoins").toBool(true);
    def_ = loaded;
    // Keep the id sequences ahead of whatever was loaded so new rows never collide.
    connSeq_ = static_cast<int>(def_.connectors.size());
    wireSeq_ = static_cast<int>(def_.wires.size());
    if (connTable_) reloadAll();
}

// Give every section the height its contents actually need, so a harness can be read the
// moment it is opened or loaded instead of being dragged into view section by section. A table
// asks for its rows; everything else asks for its own size hint. Each is capped, because one
// long wire list must not push the rest off the page.
void HarnessBuilderDialog::spreadSections() {
    if (sections_.size() < 6) return;
    auto tableHeight = [](QTableWidget* t, int maxRows) {
        if (!t) return 0;
        int h = t->horizontalHeader()->height() + 8;
        int shown = 0;
        for (int r = 0; r < t->rowCount() && shown < maxRows; ++r) {
            if (t->isRowHidden(r)) continue;
            h += t->rowHeight(r);
            ++shown;
        }
        // Room for one more row, so the next Add lands somewhere visible rather than under
        // the edge.
        h += t->rowCount() ? t->rowHeight(0) : 30;
        if (t->horizontalScrollBar() && t->horizontalScrollBar()->isVisible())
            h += t->horizontalScrollBar()->height();
        return h;
    };
    // caption + body, and the caption is about 22px of the section.
    const int cap = 26;
    const int connH = std::clamp(tableHeight(connTable_, 8) + cap, 150, 340);
    const int wireH = std::clamp(tableHeight(wireTable_, 12) + cap, 170, 460);
    sections_[0]->setFixedHeight(connH);
    sections_[1]->setFixedHeight(150);
    sections_[2]->setFixedHeight(260);
    sections_[3]->setFixedHeight(wireH);
    sections_[4]->setFixedHeight(sections_[4]->sizeHint().height());
    sections_[5]->setFixedHeight(110);
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
