#pragma once

#include <QString>
#include <QWidget>

class QCheckBox;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace Loopwire {

// THE RULE EXPLAINER: hovered over a design rule in a rules list, a small animated picture of what the rule catches -
// the fault, pulsing red, then the fix, ticked green, round and round - with the rule's name and what it means under
// it. Drawn live, so it needs no picture files and is the same on any machine.
class RuleExplainer : public QWidget {
    Q_OBJECT
public:
    // Put on a widget in a rules list: hovering it shows the explainer for that rule beside it.
    static void attach(QWidget* w, const QString& rule, const QString& title, const QString& text);
    // Shown outside a window: hovering w puts the explainer beyond that window's right edge (or its left, where the
    // screen ends) instead of beside w, so it covers nothing in the window.
    static void attachOutside(QWidget* w, QWidget* window, const QString& rule, const QString& title, const QString& text);
    // WHETHER HOVERING SHOWS IT AT ALL: one choice for every rules window, remembered between sessions.
    static bool helpShown();
    static void setHelpShown(bool shown);
    // The tick box that says so, for a rules window.
    static QCheckBox* addHelpToggle(QVBoxLayout* layout, QWidget* parent);
    // The one explainer there is (made on first use), and whether a rule has a picture of its own.
    static RuleExplainer* instance();
    static bool hasScene(const QString& rule);
    static void paintSeverity(QToolButton* button, int level);
    static void addSeverityLegend(QVBoxLayout* layout, QWidget* parent);

    void showFor(QWidget* anchor, const QString& rule, const QString& title, const QString& text, QWidget* outside = nullptr);
    QString rule() const { return rule_; }
    // Where the animation is, 0..1 round the loop (the first half the fault, the second the fix) - for the tests.
    void setPhase(double t);

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    explicit RuleExplainer();
    void drawScene(class QPainter& p, const QRectF& area, bool fixed, double pulse) const;

    QString rule_, title_, text_;
    double t_ = 0.0;
    QTimer* timer_ = nullptr;
};

} // namespace Loopwire
