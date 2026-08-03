#include "app/UiBits.h"

#include "harness/Harness.h"

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
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>

#include <algorithm>

namespace Edraw_Desktop {

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

QIcon plusIcon(const QColor& ink, int size) {
    return QIcon(signPixmap(ink, size, true, true, false));
}

QIcon minusIcon(const QColor& ink, int size) {
    return QIcon(signPixmap(ink, size, true, false, false));
}

QIcon crossIcon(const QColor& ink, int size) {
    return QIcon(signPixmap(ink, size, false, false, true));
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

// See installOpaqueMenus. Timing is the whole trick. The style sets the flag while the menu handles
// its own Polish event - so clearing it from an event filter, which runs BEFORE that, achieves
// nothing: the style puts it straight back. The flag has to go once the style is done and before the
// popup's window is made, and aboutToShow is exactly that moment. Every popup re-clears it, which
// also covers a menu that is built once and shown again and again.
namespace {
class OpaqueMenuFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        if (ev->type() == QEvent::Polish) {
            if (auto* menu = qobject_cast<QMenu*>(watched)) {
                QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] {
                    menu->setAttribute(Qt::WA_TranslucentBackground, false);
                    menu->setAttribute(Qt::WA_NoSystemBackground, false);
                });
            }
        }
        return QObject::eventFilter(watched, ev);
    }
};
} // namespace

void installOpaqueMenus(QCoreApplication* app) {
    if (!app) return;
    app->installEventFilter(new OpaqueMenuFilter(app));
}

} // namespace Edraw_Desktop
