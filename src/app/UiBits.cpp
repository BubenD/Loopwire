#include "app/UiBits.h"

#include "harness/Harness.h"
#include "app/Chrome.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCoreApplication>
#include <QColorDialog>
#include <QEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSize>
#include <QStyle>
#include <QStyleFactory>
#include <QToolButton>

#include <algorithm>

namespace Edraw_Desktop {

namespace {
class SectionGripWidget : public QWidget {
public:
    SectionGripWidget(QWidget* section, QWidget* parent, std::function<void()> onPress, std::function<void()> onReset)
        : QWidget(parent), section_(section), onPress_(std::move(onPress)), onReset_(std::move(onReset)) {
        setFixedHeight(8);
        setCursor(Qt::SizeVerCursor);
        setToolTip(QStringLiteral("Drag to make the section above taller or shorter - the rest move with it. "
                                  "Double-click to let every section size itself again."));
        setVisible(section->isVisibleTo(section->parentWidget()));
        section->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == section_ && (e->type() == QEvent::Show || e->type() == QEvent::Hide))
            setVisible(e->type() == QEvent::Show);
        return false;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        // A LINE ACROSS, where one section ends and the next begins - so the border that is dragged can be
        // seen - and the two short strokes of a handle in the middle of it.
        p.setPen(QColor(0xd8, 0xdc, 0xe2));
        p.drawLine(0, height() / 2, width(), height() / 2);
        p.setPen(QColor(0x7a, 0x84, 0x90));
        const int c = width() / 2;
        p.drawLine(c - 16, 2, c + 16, 2);
        p.drawLine(c - 16, height() - 3, c + 16, height() - 3);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (onPress_) onPress_();
        from_ = e->globalPosition().y();
        start_ = section_->height();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!(e->buttons() & Qt::LeftButton)) return;
        const int want = std::max(section_->minimumSizeHint().height() > 0 ? 40 : 20,
                                  int(start_ + e->globalPosition().y() - from_));
        section_->setFixedHeight(want);
        if (QWidget* column = parentWidget()) {
            if (column->layout()) {
                column->layout()->invalidate();
                column->layout()->activate();
            }
            column->updateGeometry();
            column->adjustSize();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent*) override {
        if (onReset_) onReset_();
    }

private:
    QWidget* section_;
    std::function<void()> onPress_, onReset_;
    double from_ = 0.0;
    int start_ = 0;
};
} // namespace

QWidget* makeSectionGrip(QWidget* section, QWidget* parent, std::function<void()> onPress,
                         std::function<void()> onReset) {
    return new SectionGripWidget(section, parent, std::move(onPress), std::move(onReset));
}

namespace {

// The stroke of a drawn sign: thick enough to read at 16 px, thin enough not to look like a block.
QPixmap signPixmap(const QColor& ink, int size, bool horizontal, bool vertical, bool diagonal) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(std::max(1.8, size / 8.0));
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    const double m = size * 0.22;           // the margin the sign sits inside
    const double c = size / 2.0;
    if (horizontal) p.drawLine(QPointF(m, c), QPointF(size - m, c));
    if (vertical) p.drawLine(QPointF(c, m), QPointF(c, size - m));
    if (diagonal) {
        p.drawLine(QPointF(m, m), QPointF(size - m, size - m));
        p.drawLine(QPointF(size - m, m), QPointF(m, size - m));
    }
    return pm;
}

// Where a custom colour is kept, so one defined once is offered ever after. The same key the harness
// builder has always used, so the colours added there are the colours offered here.
constexpr auto kCustomColorsKey = "edraw/harnessBuilder/customColors";
// The marker on the "define a new one" entry. A control character, so no real code can collide.
constexpr auto kCustomItem = "\x01" "add-custom";

void loadCustomColours() {
    const QJsonArray arr =
        QJsonDocument::fromJson(QSettings().value(QLatin1String(kCustomColorsKey)).toString().toUtf8())
            .array();
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        registerCustomWireColor(o.value(QStringLiteral("code")).toString(),
                                o.value(QStringLiteral("name")).toString(),
                                o.value(QStringLiteral("hex")).toString());
    }
}

void saveCustomColour(const QString& code, const QString& name, const QString& hex) {
    QSettings st;
    QJsonArray arr =
        QJsonDocument::fromJson(st.value(QLatin1String(kCustomColorsKey)).toString().toUtf8())
            .array();
    QJsonObject o;
    o[QStringLiteral("code")] = code;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("hex")] = hex;
    arr.append(o);
    st.setValue(QLatin1String(kCustomColorsKey),
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

} // namespace

// The inks that carry a meaning, per scheme - see the note beside their declarations in UiBits.h.
QColor capInk() { return Chrome::subInk(); }
QColor faintInk() { return Chrome::dark() ? QColor("#7f8896") : QColor("#6b7280"); }
QColor ownInk() { return Chrome::dark() ? QColor("#7ee0a0") : QColor("#1e7a48"); }
QColor infoInk() { return Chrome::dark() ? QColor("#8fb0d6") : QColor("#245a99"); }
QColor idleInk() { return Chrome::dark() ? QColor("#8b929c") : QColor("#6b7280"); }
QColor missInk() { return Chrome::dark() ? QColor("#e6b45a") : QColor("#8a5a00"); }
QColor warnInk() { return Chrome::dark() ? QColor("#e0a0a0") : QColor("#a4302f"); }

QIcon plusIcon(const QColor& ink, int size) {
    return QIcon(signPixmap(ink, size, true, true, false));
}

QIcon minusIcon(const QColor& ink, int size) {
    return QIcon(signPixmap(ink, size, true, false, false));
}

QIcon crossIcon(const QColor& ink, int size) {
    return QIcon(signPixmap(ink, size, false, false, true));
}

// ---- The file glyphs ----------------------------------------------------------------------------

namespace {

// One pixmap, set up the way all four are drawn: square, transparent, antialiased, and a pen thin
// enough that an 18-pixel glyph still reads as line-work rather than as a blob.
QPixmap glyphPixmap(const QColor& ink, int size, double weight = 1.4) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(weight * size / 18.0);   // drawn at 18 and scaled, so any size keeps its weight
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.end();
    return pm;
}

// A sheet of paper with the top-right corner turned down: the outline, then the fold. Drawn into
// `p` in a box `size` across, inset by `inset`, so the same sheet can be a whole icon or the front
// one of a pair.
void drawSheet(QPainter& p, const QRectF& box) {
    const double fold = box.width() * 0.30;
    QPainterPath sheet;
    sheet.moveTo(box.left(), box.top());
    sheet.lineTo(box.right() - fold, box.top());
    sheet.lineTo(box.right(), box.top() + fold);
    sheet.lineTo(box.right(), box.bottom());
    sheet.lineTo(box.left(), box.bottom());
    sheet.closeSubpath();
    p.drawPath(sheet);
    // The fold itself, which is what tells a sheet from a plain rectangle.
    QPainterPath corner;
    corner.moveTo(box.right() - fold, box.top());
    corner.lineTo(box.right() - fold, box.top() + fold);
    corner.lineTo(box.right(), box.top() + fold);
    p.drawPath(corner);
}

// A DISKETTE: the body, the shutter across the top, and the label across the bottom. The shutter is
// filled, because on a real one it is the metal and on an icon this size it is what stops the glyph
// reading as three stacked rectangles.
void drawDiskette(QPainter& p, const QRectF& box, const QColor& ink) {
    const double clip = box.width() * 0.22;
    QPainterPath body;
    body.moveTo(box.left(), box.top());
    body.lineTo(box.right() - clip, box.top());
    body.lineTo(box.right(), box.top() + clip);
    body.lineTo(box.right(), box.bottom());
    body.lineTo(box.left(), box.bottom());
    body.closeSubpath();
    p.drawPath(body);
    // The shutter: a filled block across the top third, inset from both sides.
    const QRectF shutter(box.left() + box.width() * 0.28, box.top(),
                         box.width() * 0.30, box.height() * 0.32);
    p.save();
    p.setBrush(ink);
    p.drawRect(shutter);
    p.restore();
    // The label: an empty block across the bottom half.
    p.drawRect(QRectF(box.left() + box.width() * 0.20, box.top() + box.height() * 0.55,
                      box.width() * 0.60, box.height() * 0.45));
}

} // namespace

QIcon newFileIcon(const QColor& ink, int size) {
    QPixmap pm = glyphPixmap(ink, size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.4 * size / 18.0);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    const double m = size * 0.14;
    drawSheet(p, QRectF(m + size * 0.08, m, size * 0.62, size - 2 * m));
    return QIcon(pm);
}

QIcon openFileIcon(const QColor& ink, int size) {
    QPixmap pm = glyphPixmap(ink, size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.4 * size / 18.0);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    // AN OPEN FOLDER: the back of it square behind, and the front leaning out and away, which is
    // what says "open" rather than "a folder".
    const double w = size, h = size;
    QPainterPath back;
    back.moveTo(w * 0.10, h * 0.78);
    back.lineTo(w * 0.10, h * 0.26);
    back.lineTo(w * 0.40, h * 0.26);
    back.lineTo(w * 0.48, h * 0.38);
    back.lineTo(w * 0.80, h * 0.38);
    back.lineTo(w * 0.80, h * 0.50);
    p.drawPath(back);
    QPainterPath front;
    front.moveTo(w * 0.10, h * 0.78);
    front.lineTo(w * 0.72, h * 0.78);
    front.lineTo(w * 0.92, h * 0.50);
    front.lineTo(w * 0.28, h * 0.50);
    front.closeSubpath();
    p.drawPath(front);
    return QIcon(pm);
}

QIcon saveFileIcon(const QColor& ink, int size) {
    QPixmap pm = glyphPixmap(ink, size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.4 * size / 18.0);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    const double m = size * 0.14;
    drawDiskette(p, QRectF(m, m, size - 2 * m, size - 2 * m), ink);
    return QIcon(pm);
}

QIcon saveAllFilesIcon(const QColor& ink, int size) {
    QPixmap pm = glyphPixmap(ink, size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.3 * size / 18.0);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    // TWO OF THEM, one behind the other: "all" said by there being more than one, which is how every
    // save-all icon says it. The one behind is drawn first and only its two showing edges are needed -
    // a whole second diskette behind the front one is ink nobody can see.
    const double w = size, h = size;
    QPainterPath behind;
    behind.moveTo(w * 0.34, h * 0.10);
    behind.lineTo(w * 0.90, h * 0.10);
    behind.lineTo(w * 0.90, h * 0.66);
    p.drawPath(behind);
    drawDiskette(p, QRectF(w * 0.08, h * 0.26, w * 0.62, h * 0.62), ink);
    return QIcon(pm);
}

namespace {

QPushButton* signButton(QWidget* parent, const QIcon& icon, const QString& text,
                        const QString& tip) {
    auto* b = new QPushButton(parent);
    b->setIcon(icon);
    b->setIconSize(QSize(14, 14));
    if (!text.isEmpty()) b->setText(text);
    else {
        // A square button with nothing but the sign on it: sized so the icon is not cramped.
        b->setFixedWidth(28);
        b->setMaximumHeight(26);
    }
    if (!tip.isEmpty()) b->setToolTip(tip);
    return b;
}

} // namespace

QPushButton* makeAddButton(QWidget* parent, const QString& text, const QString& tip) {
    return signButton(parent, plusIcon(), text, tip);
}

QPushButton* makeRemoveButton(QWidget* parent, const QString& text, const QString& tip,
                              bool cross) {
    return signButton(parent, cross ? crossIcon() : minusIcon(), text, tip);
}

// ---- Colours -------------------------------------------------------------------------------------

QIcon colorCircleIcon(const QString& hex, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#20242b"), 1));
    const QColor c(hex);
    p.setBrush(c.isValid() ? c : QColor("#8b929c"));
    p.drawEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));
    if (!c.isValid()) {
        // Nothing chosen: a circle with a line through it, so "none" does not look like a colour.
        p.setPen(QPen(QColor("#20242b"), 1.4));
        p.drawLine(QPointF(size * 0.25, size * 0.75), QPointF(size * 0.75, size * 0.25));
    }
    return QIcon(pm);
}

QIcon twistIcon(int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#cfe3ff"));
    pen.setWidthF(std::max(1.2, size / 11.0));
    p.setPen(pen);
    // Two conductors crossing twice: the twist symbol, small.
    const double h = size / 2.0;
    const double a = size * 0.12, b = size - a;
    QPainterPath one, two;
    one.moveTo(a, h - size * 0.22);
    one.cubicTo(size * 0.4, h - size * 0.22, size * 0.6, h + size * 0.22, b, h + size * 0.22);
    two.moveTo(a, h + size * 0.22);
    two.cubicTo(size * 0.4, h + size * 0.22, size * 0.6, h - size * 0.22, b, h - size * 0.22);
    p.drawPath(one);
    p.drawPath(two);
    return QIcon(pm);
}

QIcon shieldIcon(int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // The screen: a dashed outline with the wires it catches inside it.
    QPen pen(QColor("#9fb4c8"));
    pen.setWidthF(std::max(1.0, size / 13.0));
    pen.setStyle(Qt::DotLine);
    p.setPen(pen);
    p.drawEllipse(QRectF(size * 0.08, size * 0.2, size * 0.84, size * 0.6));
    QPen wire(QColor("#cfe3ff"));
    wire.setWidthF(std::max(1.0, size / 14.0));
    p.setPen(wire);
    p.drawLine(QPointF(size * 0.22, size * 0.42), QPointF(size * 0.78, size * 0.42));
    p.drawLine(QPointF(size * 0.22, size * 0.58), QPointF(size * 0.78, size * 0.58));
    return QIcon(pm);
}

QComboBox* makeColorBox(QWidget* parent, const QString& current, bool allowNone) {
    static bool loaded = false;
    if (!loaded) {
        loadCustomColours();
        loaded = true;
    }
    auto* box = new QComboBox(parent);
    box->setToolTip(QStringLiteral(
        "Pick a colour by eye. The last entry defines a new one — named and coloured as you like, "
        "and offered here from then on."));
    if (allowNone) box->addItem(colorCircleIcon(QString()), QStringLiteral("none"), QString());
    for (const QString& code : wireColorCodes())
        box->addItem(colorCircleIcon(wireColorHex(code)),
                     QStringLiteral("%1 — %2").arg(code, wireColorName(code)), code);
    box->addItem(QStringLiteral("Custom…"), QLatin1String(kCustomItem));
    const int at = box->findData(current);
    if (at >= 0) box->setCurrentIndex(at);
    else if (!current.isEmpty()) {
        // A colour the list does not have (a hex written by hand, or one from another machine): shown
        // as itself rather than silently dropped.
        box->insertItem(allowNone ? 1 : 0, colorCircleIcon(wireColorHex(current)), current, current);
        box->setCurrentIndex(allowNone ? 1 : 0);
    }
    return box;
}

QString colorBoxCode(const QComboBox* box) {
    if (!box) return {};
    const QString data = box->currentData().toString();
    return data == QLatin1String(kCustomItem) ? QString() : data;
}

QString colorBoxCommit(QComboBox* box, QWidget* dialogParent) {
    if (!box) return {};
    if (box->currentData().toString() != QLatin1String(kCustomItem)) return colorBoxCode(box);

    // Defining one: a colour, then a code to call it by. Both are asked for, because a harness
    // drawing refers to a wire by its CODE and an unnamed colour cannot be written on a drawing.
    const QColor picked = QColorDialog::getColor(QColor("#ff8800"), dialogParent,
                                                QStringLiteral("A colour of your own"));
    if (!picked.isValid()) {
        box->setCurrentIndex(0);
        return {};
    }
    bool ok = false;
    const QString code =
        QInputDialog::getText(dialogParent, QStringLiteral("A colour of your own"),
                              QStringLiteral("The code to call it by (as it goes on the drawing):"),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed()
            .toUpper();
    if (!ok || code.isEmpty()) {
        box->setCurrentIndex(0);
        return {};
    }
    const QString name =
        QInputDialog::getText(dialogParent, QStringLiteral("A colour of your own"),
                              QStringLiteral("And what it is called:"), QLineEdit::Normal,
                              picked.name(), &ok);
    registerCustomWireColor(code, ok && !name.isEmpty() ? name : picked.name(), picked.name());
    saveCustomColour(code, ok && !name.isEmpty() ? name : picked.name(), picked.name());
    // Into the list, and picked.
    box->insertItem(box->count() - 1, colorCircleIcon(picked.name()),
                    QStringLiteral("%1 — %2").arg(code, wireColorName(code)), code);
    box->setCurrentIndex(box->count() - 2);
    return code;
}

QWidget* makeColorListEditor(QWidget* parent, const QStringList& codes, const QString& placeholder,
                             std::function<void(const QStringList&)> onChange) {
    auto* host = new QWidget(parent);
    auto* lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    auto* edit = new QLineEdit(host);
    edit->setText(codes.join(QLatin1Char('/')));
    edit->setPlaceholderText(placeholder);
    edit->setToolTip(QStringLiteral(
        "The conductors' colour codes, in order, separated by / — WH/BU for a white-blue pair. "
        "Pick them by eye with the button beside, or type them. Left empty, the usual colours for "
        "that kind are used."));
    auto read = [edit] {
        QStringList out;
        for (const QString& c : edit->text().split(QLatin1Char('/'), Qt::SkipEmptyParts))
            out << c.trimmed().toUpper();
        return out;
    };
    QObject::connect(edit, &QLineEdit::editingFinished, host, [read, onChange] { onChange(read()); });
    lay->addWidget(edit, 1);

    // The swatch button: a menu of colours as circles, and one to define a new one. Picking appends,
    // because a list is built up one conductor at a time.
    auto* pick = new QToolButton(host);
    pick->setIcon(colorCircleIcon(QStringLiteral("#8fb7ff")));
    pick->setToolTip(QStringLiteral("Add a colour to the list, picked by eye"));
    pick->setPopupMode(QToolButton::InstantPopup);
    pick->setFixedWidth(30);
    auto* menu = new QMenu(pick);
    static bool loaded = false;
    if (!loaded) {
        loadCustomColours();
        loaded = true;
    }
    auto append = [edit, read, onChange](const QString& code) {
        QStringList list = read();
        list << code;
        edit->setText(list.join(QLatin1Char('/')));
        onChange(list);
    };
    for (const QString& code : wireColorCodes()) {
        QAction* a = menu->addAction(colorCircleIcon(wireColorHex(code)),
                                     QStringLiteral("%1 — %2").arg(code, wireColorName(code)));
        QObject::connect(a, &QAction::triggered, host, [append, code] { append(code); });
    }
    menu->addSeparator();
    {
        QAction* a = menu->addAction(QStringLiteral("Custom…"));
        QObject::connect(a, &QAction::triggered, host, [host, append, menu] {
            const QColor picked = QColorDialog::getColor(QColor("#ff8800"), host,
                                                        QStringLiteral("A colour of your own"));
            if (!picked.isValid()) return;
            bool ok = false;
            const QString code =
                QInputDialog::getText(host, QStringLiteral("A colour of your own"),
                                      QStringLiteral("The code to call it by:"), QLineEdit::Normal,
                                      QString(), &ok)
                    .trimmed()
                    .toUpper();
            if (!ok || code.isEmpty()) return;
            registerCustomWireColor(code, picked.name(), picked.name());
            saveCustomColour(code, picked.name(), picked.name());
            QAction* added = new QAction(colorCircleIcon(picked.name()),
                                         QStringLiteral("%1 — %2").arg(code, wireColorName(code)),
                                         menu);
            QObject::connect(added, &QAction::triggered, menu, [append, code] { append(code); });
            const qsizetype at = std::max<qsizetype>(0, menu->actions().size() - 2);
            menu->insertAction(menu->actions().at(at), added);
            append(code);
        });
    }
    pick->setMenu(menu);
    lay->addWidget(pick);

    auto* clear = makeRemoveButton(host, QString(), QStringLiteral("Empty the list"), true);
    QObject::connect(clear, &QPushButton::clicked, host, [edit, onChange] {
        edit->clear();
        onChange({});
    });
    lay->addWidget(clear);
    return host;
}

// See installOpaqueMenus. Two things are done to every menu, because the see-through popup has two
// halves to it and either one alone leaves a way for it to come back.
//
// The Windows 11 style draws a menu as a ROUNDED, TRANSLUCENT panel: it asks for a window with an
// alpha channel and then paints its own corners into it. Whatever is not painted is a hole. So the
// menu is given a base style that has no such idea - a plain filled panel - and the application
// stylesheet still colours it, since the sheet is layered over whichever base style a widget has.
// The style is set while the menu is being polished, which is before it paints or is shown, and one
// instance is shared by every menu in the application.
//
// And the flags themselves are cleared on the way to being shown. A menu that was polished before
// this was installed - or one whose style refuses to let go - still ends up with an opaque window,
// because aboutToShow runs after the style has had its say and before the popup's window exists,
// which is the one moment they can be taken away and stay away.
namespace {

QStyle* plainMenuStyle() {
    // Owned by the application: a style outlives the menus that use it.
    static QStyle* style = [] {
        QStyle* s = QStyleFactory::create(QStringLiteral("Fusion"));
        if (s) s->setParent(QCoreApplication::instance());
        return s;
    }();
    return style;
}

void makeMenuOpaque(QMenu* menu) {
    menu->setAttribute(Qt::WA_TranslucentBackground, false);
    menu->setAttribute(Qt::WA_NoSystemBackground, false);
}

class OpaqueMenuFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        if (ev->type() == QEvent::Polish) {
            if (auto* menu = qobject_cast<QMenu*>(watched)) {
                if (QStyle* plain = plainMenuStyle(); plain && menu->style() != plain)
                    menu->setStyle(plain);
                makeMenuOpaque(menu);
                QObject::connect(menu, &QMenu::aboutToShow, menu,
                                 [menu] { makeMenuOpaque(menu); });
            }
        }
        return QObject::eventFilter(watched, ev);
    }
};
} // namespace

QIcon eyeIcon(bool shown, const QColor& ink, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = size / 20.0;
    QPen pen(shown ? ink : QColor("#8a9099"));
    pen.setWidthF(1.6 * s);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // The lid, as two arcs meeting at the corners of the eye, and the pupil.
    QPainterPath eye;
    eye.moveTo(2.5 * s, 10 * s);
    eye.quadTo(10 * s, 3 * s, 17.5 * s, 10 * s);
    eye.quadTo(10 * s, 17 * s, 2.5 * s, 10 * s);
    p.drawPath(eye);
    p.setBrush(shown ? ink : QColor("#8a9099"));
    p.drawEllipse(QPointF(10 * s, 10 * s), 2.6 * s, 2.6 * s);
    if (!shown) {
        // Struck through, corner to corner: the sign for "not being shown", and it reads at any size.
        p.setBrush(Qt::NoBrush);
        QPen slash(QColor("#e0a0a0"));
        slash.setWidthF(1.8 * s);
        p.setPen(slash);
        p.drawLine(QPointF(3 * s, 17 * s), QPointF(17 * s, 3 * s));
    }
    return QIcon(pm);
}

QToolButton* makeEyeToggle(QWidget* parent, const QString& shownText, const QString& hiddenText,
                           const QString& tip, const QString& settingsKey) {
    auto* btn = new QToolButton(parent);
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setCheckable(true);
    btn->setToolTip(tip);
    btn->setIconSize(QSize(18, 18));
    const QString key = settingsKey.isEmpty() ? QStringLiteral("ui/showConnectionPoints") : settingsKey;
    QSettings settings;
    btn->setChecked(settings.value(key, true).toBool());
    // The button's own face follows the button's own state, wherever the state came from - set here, clicked,
    // or read back out of the settings - so there is one place that decides what it looks like.
    auto dress = [btn, shownText, hiddenText] {
        const bool on = btn->isChecked();
        btn->setIcon(eyeIcon(on));
        btn->setText(on ? shownText : hiddenText);
    };
    dress();
    QObject::connect(btn, &QToolButton::toggled, btn, [btn, dress, key](bool on) {
        dress();
        QSettings().setValue(key, on);
    });
    return btn;
}

void installOpaqueMenus(QCoreApplication* app) {
    if (!app) return;
    app->installEventFilter(new OpaqueMenuFilter(app));
}

namespace {
class WheelPassFilter : public QObject {
public:
    explicit WheelPassFilter(QWidget* window)
        : QObject(window ? static_cast<QObject*>(window) : static_cast<QObject*>(qApp)), window_(window) {}
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::Wheel) return false;
        auto* w = qobject_cast<QWidget*>(o);
        // window_ null: in every window of the app.
        if (!w || (window_ && w->window() != window_)) return false;
        // The box itself, or the line edit inside a spin box or an editable drop-down.
        QWidget* field = nullptr;
        for (QWidget* at = w; at && at != window_; at = at->parentWidget())
            if (qobject_cast<QAbstractSpinBox*>(at) || qobject_cast<QComboBox*>(at)) {
                field = at;
                break;
            }
        if (!field) return false;
        // An open drop-down list scrolls itself - that is its own view, not the box.
        if (auto* combo = qobject_cast<QComboBox*>(field); combo && combo->view() && combo->view()->isVisible())
            return false;
        if (QWidget* behind = field->parentWidget()) QCoreApplication::sendEvent(behind, e);
        return true;
    }

private:
    QWidget* window_;
};
} // namespace

void wheelScrollsNotFields(QWidget* window) {
    if (!window || !qApp) return;
    qApp->installEventFilter(new WheelPassFilter(window));
}

void wheelScrollsNotFieldsAnywhere() {
    if (!qApp) return;
    qApp->installEventFilter(new WheelPassFilter(nullptr));
}

} // namespace Edraw_Desktop
