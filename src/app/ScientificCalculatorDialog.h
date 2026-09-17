#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QToolButton;

namespace Edraw_Desktop {

// A calculator for expressions rather than a row of disconnected one-key operations. The field takes
// ordinary arithmetic as well as functions - sin(30), sqrt(2*pi), 2^10 - and the keypad only writes the
// same expression somebody could type. Degrees or radians are a switch because trigonometry without that
// is a wrong answer waiting for a convenient time to happen.
//
// It is a panel rather than a modal window: it can be docked on a side, left floating, or folded down to
// its strip with the other tools.
class ScientificCalculatorDialog : public QWidget {
    Q_OBJECT
public:
    explicit ScientificCalculatorDialog(QWidget* parent = nullptr);

    // The parser, reachable directly so tests do not have to pretend to be a keyboard.
    bool evaluateForTest(const QString& expression, bool degrees, double& out, QString* error = nullptr);
    void setExpressionForTest(const QString& expression);
    void calculateForTest() { calculate(); }
    void setDegreesForTest(bool on);
    QString displayForTest() const;
    int historyCountForTest() const;
    void focusExpression() const;

private:
    void buildUi();
    void appendToken(const QString& token, bool continuesAnswer = false);
    void wrapExpression(const QString& before, const QString& after);
    void calculate();
    void clearAll();
    void copyDisplay();

    QLineEdit* expression_ = nullptr;
    QLineEdit* result_ = nullptr;
    QLabel* status_ = nullptr;
    QListWidget* history_ = nullptr;
    QToolButton* angleBtn_ = nullptr;
    double answer_ = 0.0;
    bool resultShown_ = false;
};

} // namespace Edraw_Desktop
