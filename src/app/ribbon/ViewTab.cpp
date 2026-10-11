// The View ribbon tab: which panels are on the screen, and which side each one is on.
//
// The panels were there before this - properties, layers, the bill of materials - but the only way to bring
// one back once it had been closed was to know that its title bar had an x and that dragging a floating one to
// an edge re-docks it. A drawing office does not learn a program that way. So each panel is a button here:
// pressed in, it is on the screen; pressed out, it is not. And each says which side it is on, because which
// side a panel belongs on is a matter of how somebody works, not of how the program was written.
//
// The buttons are wired to the panels BY NAME rather than by pointer, because the ribbon is built before the
// panels are. buildPanels() calls syncViewPanelButtons() when they exist, and that is what enables them.
#include "app/MainWindow.h"

#include "app/Chrome.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QToolButton>
#include <QWidget>

namespace Edraw_Desktop {

namespace {

// A small drawing of each panel: a frame with the panel's own side shaded, and its contents suggested by a
// few lines. Drawn rather than lettered so the buttons read at a glance and at any font.
QIcon panelIcon(const QString& key) {
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    // The scheme's own ink: drawn in pale grey, these read on a dark ribbon and vanish on a light one.
    const QColor ink = Chrome::ink();
    const QColor dim = Chrome::subInk();
    p.setPen(QPen(dim, 1.0));
    p.drawRect(QRectF(2.5, 3.5, 19, 17));
    p.setPen(QPen(ink, 1.0));
    if (key == QLatin1String("props")) {
        // A field and its value, twice: what a property grid is.
        for (int i = 0; i < 3; ++i) {
            p.drawLine(5, 8 + i * 4, 11, 8 + i * 4);
            p.drawLine(13, 8 + i * 4, 19, 8 + i * 4);
        }
    } else if (key == QLatin1String("layers")) {
        // Sheets stacked: three offset rectangles.
        for (int i = 0; i < 3; ++i) p.drawRect(QRectF(5.5 + i * 2, 6.5 + i * 4, 11, 4));
    } else if (key == QLatin1String("bom")) {
        // A table with a heading row.
        p.drawLine(4, 9, 20, 9);
        p.drawLine(12, 6, 12, 19);
        for (int i = 0; i < 3; ++i) p.drawLine(4, 12 + i * 3, 20, 12 + i * 3);
    } else if (key == QLatin1String("parts")) {
        // A list with bullets: the shelf.
        for (int i = 0; i < 3; ++i) {
            p.drawEllipse(QRectF(5.5, 7.5 + i * 4, 2, 2));
            p.drawLine(10, 8 + i * 4, 19, 8 + i * 4);
        }
    } else if (key == QLatin1String("blocks")) {
        // Two blocks, one of them a copy of the other.
        p.drawRect(QRectF(5.5, 6.5, 7, 6));
        p.drawRect(QRectF(11.5, 12.5, 7, 6));
    } else if (key == QLatin1String("view3d")) {
        // A cube in perspective: three faces.
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPointF a(7, 9), b(14, 6), c(19, 9), d(12, 12), a2(7, 17), d2(12, 20), c2(19, 17);
        p.drawPolygon(QPolygonF({a, b, c, d}));
        p.drawPolygon(QPolygonF({a, d, d2, a2}));
        p.drawPolygon(QPolygonF({d, c, c2, d2}));
    } else if (key == QLatin1String("calculator")) {
        // A calculator: a small body, its display and a keypad.
        p.drawRect(QRectF(7.5, 5.5, 10, 14));
        p.drawLine(9, 8, 16, 8);
        for (int y = 12; y <= 16; y += 4)
            for (int x = 10; x <= 15; x += 3) p.drawPoint(x, y);
    }
    p.end();
    return QIcon(pm);
}

// Half a sun, half a moon: the two schemes in one glyph, so the button says what it does without a word.
QIcon schemeGlyph() {
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(Chrome::dark() ? QColor("#f0c85a") : QColor("#4a4f57"));
    p.drawEllipse(QPointF(12, 12), 6.5, 6.5);
    // The dark half, cut out - which half is filled says which scheme is on.
    p.setBrush(Chrome::dark() ? QColor("#2b2f36") : QColor("#f5f5f7"));
    p.drawChord(QRectF(5.5, 5.5, 13, 13), 90 * 16, 180 * 16);
    p.setPen(QPen(Chrome::ink(), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(12, 12), 6.5, 6.5);
    p.end();
    return QIcon(pm);
}

} // namespace

QIcon MainWindow::schemeIcon() const { return schemeGlyph(); }

void MainWindow::buildViewTab() {
    auto [page, lay] = makeTabPage();

    struct PanelEntry { const char* key; QString title; QString what; };
    const QList<PanelEntry> panels = {
        {"props", QStringLiteral("Properties"), QStringLiteral("what the selected object is")},
        {"layers", QStringLiteral("Layers"), QStringLiteral("the drawing's layers")},
        {"bom", QStringLiteral("Bill of materials"), QStringLiteral("what the drawing is made of")},
        {"parts", QStringLiteral("Parts list"), QStringLiteral("the library, to place from")},
        {"blocks", QStringLiteral("Blocks list"), QStringLiteral("the blocks, to place from")},
        {"calculator", QStringLiteral("Calculator"), QStringLiteral("a scientific calculator")},
        {"view3d", QStringLiteral("3D view"), QStringLiteral("the harness in 3D - turn it, zoom it, each part as its 3D model")},
    };

    auto* w = new QWidget(page);
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    for (const PanelEntry& entry : panels) {
        const QString key = QString::fromLatin1(entry.key);
        auto* b = new QToolButton(w);
        b->setText(entry.title);
        b->setIcon(panelIcon(key));
        b->setIconSize(QSize(24, 24));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setAutoRaise(true);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setEnabled(false);   // until the panels exist; see syncViewPanelButtons()
        b->setToolTip(QStringLiteral("%1 — %2. The arrow says which side it sits on.")
                          .arg(entry.title, entry.what));
        b->setPopupMode(QToolButton::MenuButtonPopup);
        connect(b, &QToolButton::toggled, this, [this, key](bool on) {
            if (QDockWidget* d = panelByKey(key)) {
                // The two list panels are read afresh on the way in: a part added to the library, or a block
                // made, since the last look would otherwise be missing from a list that was filled at start-up.
                if (on && key == QLatin1String("parts")) refreshPartsPanel();
                if (on && key == QLatin1String("blocks")) refreshBlocksPanel();
                d->setVisible(on);
                if (on) d->raise();
            }
        });
        auto* menu = new QMenu(b);
        // Which side. A panel goes where it is wanted, and it stays there until it is moved again.
        menu->addAction(QStringLiteral("On the left"), this, [this, key] {
            if (QDockWidget* d = panelByKey(key)) {
                d->setFloating(false);
                addDockWidget(Qt::LeftDockWidgetArea, d);
                d->show();
            }
        });
        menu->addAction(QStringLiteral("On the right"), this, [this, key] {
            if (QDockWidget* d = panelByKey(key)) {
                d->setFloating(false);
                addDockWidget(Qt::RightDockWidgetArea, d);
                d->show();
            }
        });
        menu->addAction(QStringLiteral("Floating"), this, [this, key] {
            if (QDockWidget* d = panelByKey(key)) {
                d->setFloating(true);
                d->show();
            }
        });
        b->setMenu(menu);
        row->addWidget(b);
        viewPanelButtons_.append({key, b});
    }
    addGroup(lay, QStringLiteral("Panels"), w);

    {
        // The two things anybody wants after moving panels about: all of them, or none and the whole screen
        // for the drawing.
        auto* both = new QWidget(page);
        auto* bh = new QHBoxLayout(both);
        bh->setContentsMargins(0, 0, 0, 0);
        bh->setSpacing(2);
        auto* all = new QToolButton(both);
        all->setText(QStringLiteral("Show all"));
        all->setToolButtonStyle(Qt::ToolButtonTextOnly);
        all->setAutoRaise(true);
        all->setFocusPolicy(Qt::NoFocus);
        connect(all, &QToolButton::clicked, this, [this] { setPanelsVisible(true); });
        auto* none = new QToolButton(both);
        none->setText(QStringLiteral("Hide all"));
        none->setToolButtonStyle(Qt::ToolButtonTextOnly);
        none->setAutoRaise(true);
        none->setFocusPolicy(Qt::NoFocus);
        none->setToolTip(QStringLiteral("The whole screen for the drawing"));
        connect(none, &QToolButton::clicked, this, [this] { setPanelsVisible(false); });
        bh->addWidget(all);
        bh->addWidget(none);
        addGroup(lay, QStringLiteral("All of them"), both);
    }

    {
        // The scheme, where what the screen looks like is already being set. It is in Options too - this is
        // the same setting, not a second one - because a drawing office works in whatever light the room has
        // and changes its mind about this in the afternoon, not in a dialog.
        auto* w2 = new QWidget(page);
        auto* h = new QHBoxLayout(w2);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(2);
        auto* light = new QToolButton(w2);
        light->setText(QStringLiteral("Light chrome"));
        light->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        light->setIconSize(QSize(24, 24));
        light->setAutoRaise(true);
        light->setCheckable(true);
        light->setChecked(!Chrome::dark());
        light->setFocusPolicy(Qt::NoFocus);
        light->setToolTip(QStringLiteral(
            "The ribbon, the menus and the panels in light grey instead of charcoal. The drawing itself "
            "stays dark either way - a drawing is read against a dark ground."));
        light->setIcon(schemeIcon());
        connect(light, &QToolButton::toggled, this, [this, light](bool on) {
            applyTheme(!on);
            light->setIcon(schemeIcon());   // drawn, so it is drawn again in the new scheme
        });
        schemeButton_ = light;
        h->addWidget(light);
        addGroup(lay, QStringLiteral("Scheme"), w2);
    }

    {
        // THE 3D VIEW SPLIT FROM THE DRAWING, and THE OPEN DRAWINGS SHOWN TOGETHER - each button a small picture of it.
        auto picture = [](int kind) {
            QPixmap pm(48, 48);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing, true);
            const QColor ink = Chrome::ink(), accent(232, 128, 34);
            auto frame = [&](const QRectF& r, bool lit) {
                p.setPen(QPen(lit ? accent : ink, 2.4));
                p.setBrush(lit ? QColor(232, 128, 34, 70) : Qt::NoBrush);
                p.drawRoundedRect(r, 3, 3);
            };
            if (kind == 0) { frame(QRectF(5, 6, 38, 20), false); frame(QRectF(5, 28, 38, 14), true); }        // 3D below
            else if (kind == 1) { frame(QRectF(5, 6, 22, 36), false); frame(QRectF(29, 6, 14, 36), true); }   // 3D beside
            else if (kind == 2) { frame(QRectF(4, 8, 13, 32), true); frame(QRectF(18, 8, 12, 32), false); frame(QRectF(31, 8, 13, 32), false); }
            else if (kind == 3) { frame(QRectF(6, 4, 36, 13), true); frame(QRectF(6, 18, 36, 12), false); frame(QRectF(6, 31, 36, 13), false); }
            else { frame(QRectF(6, 8, 36, 32), true); }
            return QIcon(pm);
        };
        auto button = [&](QWidget* parent, QHBoxLayout* row, const char* name, const QString& text, int kind, const QString& tip) {
            auto* b = new QToolButton(parent);
            b->setObjectName(QLatin1String(name));
            b->setText(text);
            b->setIcon(picture(kind));
            b->setIconSize(QSize(24, 24));
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
            b->setToolTip(tip);
            row->addWidget(b);
            return b;
        };
        auto* splitW = new QWidget(page);
        auto* splitRow = new QHBoxLayout(splitW);
        splitRow->setContentsMargins(0, 0, 0, 0);
        splitRow->setSpacing(2);
        connect(button(splitW, splitRow, "viewSplit3DHorizontal", QStringLiteral("Split horizontal"), 0,
                       QStringLiteral("The 3D view under the drawing - drag the line between them up to give it as much as four fifths")),
                &QToolButton::clicked, this, [this] { splitView3D(Qt::Horizontal); });
        connect(button(splitW, splitRow, "viewSplit3DVertical", QStringLiteral("Split vertical"), 1,
                       QStringLiteral("The 3D view beside the drawing - drag the line between them to give it as much as four fifths")),
                &QToolButton::clicked, this, [this] { splitView3D(Qt::Vertical); });
        addGroup(lay, QStringLiteral("3D split"), splitW);

        auto* tileW = new QWidget(page);
        auto* tileRow = new QHBoxLayout(tileW);
        tileRow->setContentsMargins(0, 0, 0, 0);
        tileRow->setSpacing(2);
        connect(button(tileW, tileRow, "viewCascadeVertical", QStringLiteral("Cascade vertical"), 2,
                       QStringLiteral("Every open drawing side by side, in columns - the one worked on first; double-click another to work on it")),
                &QToolButton::clicked, this, [this] { tileDrawings(Qt::Horizontal); });
        connect(button(tileW, tileRow, "viewCascadeHorizontal", QStringLiteral("Cascade horizontal"), 3,
                       QStringLiteral("Every open drawing one under another, in rows - the one worked on first; double-click another to work on it")),
                &QToolButton::clicked, this, [this] { tileDrawings(Qt::Vertical); });
        connect(button(tileW, tileRow, "viewCascadeSingle", QStringLiteral("One drawing"), 4,
                       QStringLiteral("Back to one drawing at a time, the others in their tabs")),
                &QToolButton::clicked, this, [this] { tileDrawings(0); });
        addGroup(lay, QStringLiteral("Open drawings"), tileW);
    }

    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("View"));
}

void MainWindow::restyleViewPanelIcons() {
    // The scheme button on this tab and the combo in Options are the same setting, so whichever was used the
    // button has to say what is actually on the screen.
    if (schemeButton_) {
        const QSignalBlocker block(schemeButton_);
        schemeButton_->setChecked(!Chrome::dark());
        schemeButton_->setIcon(schemeGlyph());
    }
    // Drawn icons, so a change of scheme means drawing them again. Nothing else on the tab needs it: the
    // ribbon's own sheet colours the words.
    for (const auto& pair : viewPanelButtons_)
        if (pair.second) pair.second->setIcon(panelIcon(pair.first));
    restylePreview3DIcons();
}

QDockWidget* MainWindow::panelByKey(const QString& key) const {
    if (key == QLatin1String("props")) return propsDock_;
    if (key == QLatin1String("layers")) return layersDock_;
    if (key == QLatin1String("bom")) return bomDock_;
    if (key == QLatin1String("parts")) return partsDock_;
    if (key == QLatin1String("blocks")) return blocksDock_;
    if (key == QLatin1String("calculator")) return calculatorDock_;
    if (key == QLatin1String("view3d")) return view3dDock_;
    return nullptr;
}

void MainWindow::syncViewPanelButtons() {
    // Called once the panels exist. Each button is enabled, set to say what its panel is doing, and made to
    // follow it: a panel closed by its own x has to leave the button pressed out, or the two disagree about
    // what is on the screen.
    for (const auto& pair : viewPanelButtons_) {
        QToolButton* b = pair.second;
        QDockWidget* dock = panelByKey(pair.first);
        if (!b) continue;
        b->setEnabled(dock != nullptr);
        if (!dock) continue;
        const QSignalBlocker block(b);
        b->setChecked(dock->isVisible());
        connect(dock, &QDockWidget::visibilityChanged, b, [b](bool visible) {
            if (b->isChecked() == visible) return;
            const QSignalBlocker inner(b);
            b->setChecked(visible);
        });
    }
}

void MainWindow::setPanelsVisible(bool on) {
    for (QDockWidget* d : {propsDock_, layersDock_, bomDock_, partsDock_, blocksDock_,
                           calculatorDock_})
        if (d) {
            d->setVisible(on);
            if (on) d->raise();
        }
}

} // namespace Edraw_Desktop
