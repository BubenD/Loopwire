// The contextual "Template" ribbon tab: what a sheet is made of, shown only while a template is being
// drawn. It appears when the template editor opens and goes away when it closes, the way the hatch and
// table-cell tabs do - so the parameters are to hand exactly when they mean something, and never
// cluttering a drawing.
//
// Changing anything here redraws the sheet on the tab straight away: the frame, the zone labels and the
// revision table are generated, so laying a template out is a matter of saying what it has on it rather
// than drawing every line by hand. What IS drawn by hand - a company logo, a note, a border of your own
// - is kept exactly as drawn, because only the generated parts are regenerated.
#include "app/MainWindow.h"

#include "app/Editor.h"
#include "app/PartsLibraryDialog.h"
#include "app/TemplateDialog.h"
#include "app/ribbon/RibbonIcons.h"
#include "harness/Harness.h"
#include "harness/PartsLibrary.h"
#include "model/Document.h"
#include "model/DrawingTemplate.h"
#include "render/CanvasWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

void MainWindow::buildTemplateRibbonTab() {
    templateTab_ = new QWidget(ribbon_);
    auto* lay = new QHBoxLayout(templateTab_);
    lay->setContentsMargins(6, 4, 6, 2);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignLeft);
    auto group = [&](const QString& title, QWidget* content) {
        lay->addWidget(ribbonGroup(title, content));
        auto* rule = new QFrame(templateTab_);
        rule->setFrameShape(QFrame::VLine);
        rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
        lay->addWidget(rule);
    };
    auto caption = [this](QWidget* parent, const QString& text) {
        auto* l = new QLabel(text, parent);
        l->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        return l;
    };

    // ---- The sheet ----
    {
        auto* w = new QWidget(templateTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        tplSheetBox_ = new QComboBox(w);
        for (const TemplateSheet& s : templateSheets())
            tplSheetBox_->addItem(QStringLiteral("%1  (%2 × %3)")
                                      .arg(s.name)
                                      .arg(s.widthMm, 0, 'f', 0)
                                      .arg(s.heightMm, 0, 'f', 0),
                                  s.name);
        tplSheetBox_->setToolTip(QStringLiteral("The sheet this template is for"));
        connect(tplSheetBox_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (tplRibbonSyncing_) return;
            // Changing the sheet changes the SIZE of the ruler, not how much of it there is: the
            // division is rescaled first so the same zones span the bigger sheet.
            if (const TemplateSheet* to = templateSheetByName(tplSheetBox_->currentData().toString()))
                keepTemplateZoneCount(tplBeingDrawn_.sheet, *to);
            applyTemplateRibbon();
        });
        g->addWidget(caption(w, QStringLiteral("Size")), 0, 0);
        g->addWidget(tplSheetBox_, 0, 1);
        group(QStringLiteral("Sheet"), w);
    }

    // ---- The zone frame ----
    {
        auto* w = new QWidget(templateTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        tplZonesChk_ = new QCheckBox(QStringLiteral("Zone frame"), w);
        tplZonesChk_->setToolTip(QStringLiteral(
            "Numbers along the top and bottom, letters down the sides, so anything can be called out "
            "by the zone it is in"));
        connect(tplZonesChk_, &QCheckBox::toggled, this, [this](bool) {
            if (!tplRibbonSyncing_) applyTemplateRibbon();
        });
        tplZoneSizeSpin_ = new QDoubleSpinBox(w);
        tplZoneSizeSpin_->setRange(10.0, 200.0);
        tplZoneSizeSpin_->setDecimals(0);
        tplZoneSizeSpin_->setSuffix(QStringLiteral(" mm"));
        tplZoneSizeSpin_->setToolTip(QStringLiteral(
            "The division aimed for; divisions are equal, so the count is what comes nearest"));
        connect(tplZoneSizeSpin_, &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (!tplRibbonSyncing_) applyTemplateRibbon();
        });
        g->addWidget(tplZonesChk_, 0, 0, 1, 2);
        g->addWidget(caption(w, QStringLiteral("Division")), 1, 0);
        g->addWidget(tplZoneSizeSpin_, 1, 1);
        group(QStringLiteral("Frame"), w);
    }

    // ---- The revision table ----
    {
        auto* w = new QWidget(templateTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        tplRevChk_ = new QCheckBox(QStringLiteral("Revision table"), w);
        tplRevChk_->setToolTip(QStringLiteral(
            "Top-right corner. Which columns it has is set in the template window."));
        connect(tplRevChk_, &QCheckBox::toggled, this, [this](bool) {
            if (!tplRibbonSyncing_) applyTemplateRibbon();
        });
        tplRevRowsSpin_ = new QSpinBox(w);
        tplRevRowsSpin_->setRange(0, 40);
        tplRevRowsSpin_->setToolTip(QStringLiteral("Blank rows under the heading row"));
        connect(tplRevRowsSpin_, &QSpinBox::valueChanged, this, [this](int) {
            if (!tplRibbonSyncing_) applyTemplateRibbon();
        });
        g->addWidget(tplRevChk_, 0, 0, 1, 2);
        g->addWidget(caption(w, QStringLiteral("Rows")), 1, 0);
        g->addWidget(tplRevRowsSpin_, 1, 1);
        group(QStringLiteral("Revisions"), w);
    }

    // ---- What it came out as, and everything else in the template window ----
    {
        auto* w = new QWidget(templateTab_);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        tplRibbonInfo_ = new QLabel(QString(), w);
        tplRibbonInfo_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
        v->addWidget(tplRibbonInfo_);
        auto* more = new QToolButton(w);
        more->setText(QStringLiteral("All the settings…"));
        more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        more->setAutoRaise(true);
        more->setFocusPolicy(Qt::NoFocus);
        more->setToolTip(QStringLiteral(
            "The template window: the signatures, the layers, the boxes to fill in, the columns of the "
            "revision table"));
        connect(more, &QToolButton::clicked, this, [this] { showTemplateDialog(); });
        v->addWidget(more);
        v->addStretch(1);
        group(QStringLiteral("This template"), w);
    }

    // ---- Finishing ----
    // The same three actions as in Home's Template group. Both sets are held in tplSessionBtns_ and
    // enabled together, which is what went wrong the first time they were in two places: only the
    // pointers kept in three single members were ever switched on.
    {
        auto* w = new QWidget(templateTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        for (int which = 0; which < 3; ++which)
            g->addWidget(makeTemplateSessionButton(w, which), 0, which);
        group(QStringLiteral("Template"), w);
    }
    lay->addStretch(1);
    // Built but NOT shown: it is a child of the ribbon and only becomes a page when a template is
    // opened. Without this it was visible from start-up, painting the sheet's parameters over a
    // drawing's ribbon and covering the tab bar - because a child that has never been hidden appears
    // as soon as its parent does.
    templateTab_->hide();
}

// The contextual "Capture" tab: up only while the library window has stepped aside and is waiting for
// objects to be selected as a part's drawing. It says which part and which of its two drawings, and
// carries the two ways of finishing - because a mode with no visible way out is a trap.
void MainWindow::buildCaptureRibbonTab() {
    captureTab_ = new QWidget(ribbon_);
    auto* lay = new QHBoxLayout(captureTab_);
    lay->setContentsMargins(6, 4, 6, 2);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignLeft);
    auto group = [&](const QString& title, QWidget* content) {
        lay->addWidget(ribbonGroup(title, content));
        auto* rule = new QFrame(captureTab_);
        rule->setFrameShape(QFrame::VLine);
        rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
        lay->addWidget(rule);
    };
    {
        auto* w = new QWidget(captureTab_);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        captureWhat_ = new QLabel(QString(), w);
        captureWhat_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
        v->addWidget(captureWhat_);
        auto* hint = new QLabel(QStringLiteral("Select the objects on the sheet — Enter takes them"), w);
        hint->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        v->addWidget(hint);
        v->addStretch(1);
        group(QStringLiteral("Capturing for"), w);
    }
    {
        auto* w = new QWidget(captureTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        auto button = [&](const QString& text, const QString& tip, bool take) {
            auto* b = new QToolButton(w);
            b->setText(text);
            b->setToolTip(tip);
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setMinimumWidth(88);
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [this, take] {
                if (take) { if (captureTake_) captureTake_(); }
                else if (captureDrop_) captureDrop_();
            });
            return b;
        };
        g->addWidget(button(QStringLiteral("Take the\nselection"),
                            QStringLiteral("Use what is selected as this part's drawing (Enter)"),
                            true),
                     0, 0);
        g->addWidget(button(QStringLiteral("Cancel\ncapture"),
                            QStringLiteral("Leave the drawing as it was (Esc)"), false),
                     0, 1);
        group(QStringLiteral("Finish"), w);
    }
    lay->addStretch(1);
    captureTab_->hide();   // a child of the ribbon, and not a page until it is needed

    // Enter and Esc, live only while a capture is waiting: any other time they belong to the drawing
    // tools (Enter repeats the last command, Esc cancels one), and taking them away would be worse
    // than not having the shortcut at all.
    captureTakeSc_ = new QShortcut(QKeySequence(Qt::Key_Return), this);
    captureTakeSc_->setContext(Qt::WindowShortcut);
    connect(captureTakeSc_, &QShortcut::activated, this, [this] {
        if (captureTake_) captureTake_();
    });
    captureDropSc_ = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    captureDropSc_->setContext(Qt::WindowShortcut);
    connect(captureDropSc_, &QShortcut::activated, this, [this] {
        if (captureDrop_) captureDrop_();
    });
    captureTakeSc_->setEnabled(false);
    captureDropSc_->setEnabled(false);
}

void MainWindow::showCaptureRibbon(bool on, const QString& what, bool mechanical) {
    if (!ribbon_) return;
    if (!captureTab_) buildCaptureRibbonTab();
    const int idx = ribbon_->indexOf(captureTab_);
    if (on) {
        if (captureWhat_) captureWhat_->setText(what);
        const int at = idx < 0 ? ribbon_->addTab(captureTab_, QStringLiteral("Capture")) : idx;
        ribbon_->setCurrentIndex(at);
    } else if (idx >= 0) {
        ribbon_->removeTab(idx);
        ribbon_->setCurrentIndex(0);
    }
    if (captureTakeSc_) captureTakeSc_->setEnabled(on);
    if (captureDropSc_) captureDropSc_->setEnabled(on);
    normaliseRibbonPages();
}

// The harness tabs come off the ribbon while a template is being drawn: a sheet has no wires,
// connectors or bundles on it, and the tabs that make them only get in the way. They go back exactly
// where they were the moment a drawing is in front again.
void MainWindow::showHarnessRibbonTabs(bool on) {
    if (!ribbon_) return;
    if (!on) {
        if (!stashedTabs_.isEmpty()) return;   // already put away
        // Note them all down FIRST, then take them out from the last one backwards. Removing as we
        // went shifted every tab after it down one, so the second one was noted at the first one's
        // index and the two came back the wrong way round.
        for (const char* want : {"Schematic build", "Harness mechanical"}) {
            const QString title = QString::fromLatin1(want);
            for (int i = 0; i < ribbon_->count(); ++i)
                if (ribbon_->tabText(i) == title) {
                    stashedTabs_.push_back({ribbon_->widget(i), title, i});
                    break;
                }
        }
        std::sort(stashedTabs_.begin(), stashedTabs_.end(),
                  [](const StashedTab& a, const StashedTab& b) { return a.at > b.at; });
        for (const StashedTab& t : stashedTabs_) {
            const int at = ribbon_->indexOf(t.page);
            if (at >= 0) ribbon_->removeTab(at);   // detached, not deleted
        }
        return;
    }
    // Lowest index first, so each lands where it came from rather than in reverse.
    std::sort(stashedTabs_.begin(), stashedTabs_.end(),
              [](const StashedTab& a, const StashedTab& b) { return a.at < b.at; });
    for (const StashedTab& t : stashedTabs_) {
        if (!t.page) continue;
        ribbon_->insertTab(std::min(t.at, ribbon_->count()), t.page, t.title);
    }
    stashedTabs_.clear();
    // NOT shown by hand: the tab widget shows whichever page is current, and forcing a page that is
    // not current to show paints it over the ribbon and its tab bar.
    normaliseRibbonPages();
}

// Every object of the frame is given an id and remembered as it was generated.
//
// The id is what makes it an ORDINARY object: without one it cannot be picked, moved, trimmed or
// reshaped, which is exactly what a template being designed has to allow. The remembered copy is how
// a line the user has since changed is told apart from one still as the generator left it - the first
// is theirs and is kept, the second is the generator's and may be replaced.
//
// markAll: everything is the generator's work (a frame just made). Otherwise only what already says
// it is generated counts as such - a template loaded from a file has hand-drawn work in it too.
void MainWindow::adoptGeneratedFrame(std::vector<Entity>& ents, bool markAll) {
    tplGeneratedAsMade_.clear();
    for (Entity& e : ents) {
        if (e.id.isEmpty()) e.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        if (markAll) e.meta.insert(QStringLiteral("generated"), QStringLiteral("sheet"));
        if (e.meta.value(QStringLiteral("generated")).toString() == QLatin1String("sheet"))
            tplGeneratedAsMade_.insert(e.id, e.toJson());
    }
}

// A3 with 8 x 6 zones becomes A0 with 8 x 6 zones, each one bigger - not A0 with 24 x 17 of the same
// small ones. Scaling the sheet is scaling the drawing on it, and a grid reference that changes its
// count under you invalidates every callout already written against it. Typing a division in by hand
// still sets the density outright; this only decides what a SHEET change does to it.
void MainWindow::keepTemplateZoneCount(const TemplateSheet& from, const TemplateSheet& to) {
    if (!tplZoneSizeSpin_ || !tplBeingDrawn_.zones.enabled) return;
    const int cols = templateZoneColumns(from, tplBeingDrawn_.zones);
    const int rows = templateZoneRows(from, tplBeingDrawn_.zones);
    if (cols < 1 || rows < 1) return;
    const double band = templateZoneBandMm(to, tplBeingDrawn_.zones);
    // What division gives that many divisions across, and that many up, and the mean of the two. The
    // divisions are equal in both directions, so no single number is exactly right for both; take
    // whichever candidate actually comes back with the counts asked for, rather than assuming one does.
    // The field is whole millimetres, so the candidates are rounded before being tried.
    const double byX = (to.drawnWidthMm() - 2.0 * band) / cols;
    const double byY = (to.drawnHeightMm() - 2.0 * band) / rows;
    double size = std::clamp(std::round((byX + byY) / 2.0), tplZoneSizeSpin_->minimum(),
                             tplZoneSizeSpin_->maximum());
    for (double candidate : {std::round(byX), std::round(byY), size}) {
        TemplateZones probe = tplBeingDrawn_.zones;
        probe.sizeMm = std::clamp(candidate, tplZoneSizeSpin_->minimum(), tplZoneSizeSpin_->maximum());
        if (templateZoneColumns(to, probe) == cols && templateZoneRows(to, probe) == rows) {
            size = probe.sizeMm;
            break;
        }
    }
    const bool wasSyncing = tplRibbonSyncing_;
    tplRibbonSyncing_ = true;   // setting the field must not regenerate the sheet twice over
    tplZoneSizeSpin_->setValue(size);
    tplRibbonSyncing_ = wasSyncing;
    tplBeingDrawn_.zones.sizeMm = size;
}

void MainWindow::showTemplateRibbon(bool on) {
    if (!templateTab_ || !ribbon_) return;
    const int idx = ribbon_->indexOf(templateTab_);
    if (on) {
        if (idx < 0) {
            const int at = ribbon_->addTab(templateTab_, QStringLiteral("Template"));
            ribbon_->setCurrentIndex(at);
        } else {
            ribbon_->setCurrentIndex(idx);
        }
    } else if (idx >= 0) {
        ribbon_->removeTab(idx);      // detached, not deleted
        ribbon_->setCurrentIndex(0);  // back to Home
    }
    normaliseRibbonPages();
}

void MainWindow::syncTemplateRibbon() {
    if (!templateTab_) return;
    tplRibbonSyncing_ = true;
    if (tplSheetBox_) {
        const int at = tplSheetBox_->findData(tplBeingDrawn_.sheet.name);
        if (at >= 0) tplSheetBox_->setCurrentIndex(at);
    }
    if (tplZonesChk_) tplZonesChk_->setChecked(tplBeingDrawn_.zones.enabled);
    if (tplZoneSizeSpin_) {
        tplZoneSizeSpin_->setValue(tplBeingDrawn_.zones.sizeMm);
        tplZoneSizeSpin_->setEnabled(tplBeingDrawn_.zones.enabled);
    }
    if (tplRevChk_) tplRevChk_->setChecked(tplBeingDrawn_.revisions.enabled);
    if (tplRevRowsSpin_) {
        tplRevRowsSpin_->setValue(tplBeingDrawn_.revisions.rows);
        tplRevRowsSpin_->setEnabled(tplBeingDrawn_.revisions.enabled);
    }
    if (tplRibbonInfo_) {
        const int cols = templateZoneColumns(tplBeingDrawn_.sheet, tplBeingDrawn_.zones);
        const int rows = templateZoneRows(tplBeingDrawn_.sheet, tplBeingDrawn_.zones);
        tplRibbonInfo_->setText(
            QStringLiteral("%1 — %2, %3\n%4")
                .arg(tplBeingDrawn_.name.isEmpty() ? QStringLiteral("(unnamed)")
                                                   : tplBeingDrawn_.name)
                .arg(tplBeingDrawn_.sheet.name)
                .arg(tplBeingDrawn_.sheet.landscape ? QStringLiteral("landscape")
                                                    : QStringLiteral("portrait"))
                .arg(tplBeingDrawn_.zones.enabled
                         ? QStringLiteral("%1 × %2 zones").arg(cols).arg(rows)
                         : QStringLiteral("no zone frame")));
    }
    tplRibbonSyncing_ = false;
}

void MainWindow::applyTemplateRibbon() {
    if (!doc_ || !templateEdits_.contains(doc_)) return;
    // Take the fields, then regenerate the sheet from them.
    if (tplSheetBox_) {
        if (const TemplateSheet* s = templateSheetByName(tplSheetBox_->currentData().toString()))
            tplBeingDrawn_.sheet = *s;
    }
    if (tplZonesChk_) tplBeingDrawn_.zones.enabled = tplZonesChk_->isChecked();
    if (tplZoneSizeSpin_) tplBeingDrawn_.zones.sizeMm = tplZoneSizeSpin_->value();
    if (tplRevChk_) tplBeingDrawn_.revisions.enabled = tplRevChk_->isChecked();
    if (tplRevRowsSpin_) tplBeingDrawn_.revisions.rows = tplRevRowsSpin_->value();

    // Only what is still exactly as the generator made it is replaced. Anything drawn by hand - a
    // logo, a note, a border of someone's own - is kept, and so is any generated line the user has
    // since MOVED, stretched or restyled: from the moment it is changed it is their line, not the
    // generator's, and it stops being regenerated. Without that, free design would last only until
    // the next time a sheet size or a row count was touched.
    std::vector<Entity> keep;
    for (const Entity& e : doc_->entities()) {
        if (e.meta.value(QStringLiteral("generated")).toString() != QLatin1String("sheet")) {
            keep.push_back(e);
            continue;
        }
        const QJsonObject asMade = tplGeneratedAsMade_.value(e.id);
        if (asMade.isEmpty() || asMade != e.toJson()) {
            Entity mine = e;
            mine.meta.remove(QStringLiteral("generated"));   // theirs now, and staying
            keep.push_back(mine);
        }
    }
    std::vector<Entity> fresh = partDrawingEntities(defaultSheetContent(tplBeingDrawn_));
    adoptGeneratedFrame(fresh, true);
    doc_->entities().clear();
    for (const Entity& e : fresh) doc_->entities().push_back(e);
    for (const Entity& e : keep) doc_->entities().push_back(e);
    doc_->setDirty(true);
    if (editor_) editor_->clearSelection();
    refreshPanels();
    if (canvas_) canvas_->update();
    syncTemplateRibbon();
}

} // namespace Edraw_Desktop
