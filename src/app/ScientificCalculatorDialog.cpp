#include "app/ScientificCalculatorDialog.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace Loopwire {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

struct Token {
    enum Kind {
        Number, Name, Plus, Minus, Times, Divide, Caret, Percent, Bang,
        Left, Right, Comma, End
    } kind = End;
    double number = 0.0;
    QString name;
};

bool startsNumber(const QString& s, int at) {
    if (at >= s.size()) return false;
    return s[at].isDigit() ||
           (s[at] == QLatin1Char('.') && at + 1 < s.size() && s[at + 1].isDigit());
}

bool lex(const QString& source, std::vector<Token>& out, QString& error) {
    for (int i = 0; i < source.size();) {
        const QChar c = source[i];
        if (c.isSpace()) { ++i; continue; }
        if (startsNumber(source, i)) {
            int j = i;
            bool dot = false;
            while (j < source.size()) {
                if (source[j].isDigit()) { ++j; continue; }
                if (source[j] == QLatin1Char('.') && !dot) { dot = true; ++j; continue; }
                break;
            }
            // An exponent is part of the number only when digits follow it: "2e" is 2 multiplied
            // by Euler's number, not a half-written scientific notation.
            if (j < source.size() && (source[j] == QLatin1Char('e') || source[j] == QLatin1Char('E'))) {
                int k = j + 1;
                if (k < source.size() && (source[k] == QLatin1Char('+') || source[k] == QLatin1Char('-')))
                    ++k;
                if (k < source.size() && source[k].isDigit()) {
                    while (k < source.size() && source[k].isDigit()) ++k;
                    j = k;
                }
            }
            bool ok = false;
            const double v = source.mid(i, j - i).toDouble(&ok);
            if (!ok) { error = QStringLiteral("'%1' is not a number").arg(source.mid(i, j - i)); return false; }
            Token t; t.kind = Token::Number; t.number = v;
            out.push_back(t);
            i = j;
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_') || c == QChar(0x03c0)) {  // π
            int j = i;
            while (j < source.size() &&
                   (source[j].isLetterOrNumber() || source[j] == QLatin1Char('_') ||
                    source[j] == QChar(0x03c0)))
                ++j;
            Token t; t.kind = Token::Name; t.name = source.mid(i, j - i).toLower();
            out.push_back(t);
            i = j;
            continue;
        }
        Token t;
        if (c == QLatin1Char('+')) t.kind = Token::Plus;
        else if (c == QLatin1Char('-') || c == QChar(0x2212)) t.kind = Token::Minus;
        else if (c == QLatin1Char('*') || c == QChar(0x00d7)) t.kind = Token::Times;
        else if (c == QLatin1Char('/') || c == QChar(0x00f7)) t.kind = Token::Divide;
        else if (c == QLatin1Char('^')) t.kind = Token::Caret;
        else if (c == QLatin1Char('%')) t.kind = Token::Percent;
        else if (c == QLatin1Char('!')) t.kind = Token::Bang;
        else if (c == QLatin1Char('(')) t.kind = Token::Left;
        else if (c == QLatin1Char(')')) t.kind = Token::Right;
        else if (c == QLatin1Char(',')) t.kind = Token::Comma;
        else if (c == QChar(0x221a)) { t.kind = Token::Name; t.name = QStringLiteral("sqrt"); }
        else {
            error = QStringLiteral("'%1' is not something the calculator understands").arg(c);
            return false;
        }
        // "**" is the spelling programmers reach for; it means the same as ^.
        if (t.kind == Token::Times && i + 1 < source.size() && source[i + 1] == QLatin1Char('*')) {
            t.kind = Token::Caret;
            ++i;
        }
        out.push_back(t);
        ++i;
    }
    Token t; t.kind = Token::End;
    out.push_back(t);
    return true;
}

class Parser {
public:
    Parser(const std::vector<Token>& tokens, bool degrees, double answer)
        : t_(tokens), degrees_(degrees), answer_(answer) {}

    bool parse(double& out, QString& error) {
        out = expression();
        if (error_.isEmpty() && t_[at_].kind != Token::End)
            error_ = QStringLiteral("Something after the expression was not expected");
        if (!error_.isEmpty()) { error = error_; return false; }
        if (!std::isfinite(out)) {
            error = QStringLiteral("The answer is not a finite number");
            return false;
        }
        return true;
    }

private:
    const Token& peek() const { return t_[at_]; }
    bool accept(Token::Kind kind) {
        if (peek().kind != kind) return false;
        ++at_;
        return true;
    }
    bool startsValue(Token::Kind kind) const {
        return kind == Token::Number || kind == Token::Name || kind == Token::Left;
    }
    double fail(const QString& what) {
        if (error_.isEmpty()) error_ = what;
        return 0.0;
    }

    double expression() {
        double v = product();
        while (error_.isEmpty()) {
            if (accept(Token::Plus)) v += product();
            else if (accept(Token::Minus)) v -= product();
            else break;
        }
        return v;
    }

    double product() {
        double v = unary();
        while (error_.isEmpty()) {
            if (accept(Token::Times)) v *= unary();
            else if (accept(Token::Divide)) {
                const double rhs = unary();
                if (std::abs(rhs) < 1e-300) return fail(QStringLiteral("Division by zero"));
                v /= rhs;
            } else if (startsValue(peek().kind)) {
                v *= unary();   // 2(3+4), 2pi and 2 sin(30) all mean multiplication
            } else break;
        }
        return v;
    }

    double unary() {
        if (accept(Token::Plus)) return unary();
        if (accept(Token::Minus)) return -unary();
        return power();
    }

    double power() {
        double v = postfix();
        if (!error_.isEmpty() || !accept(Token::Caret)) return v;
        const double rhs = unary();   // right-associative, and 2^-3 works
        v = std::pow(v, rhs);
        if (!std::isfinite(v)) return fail(QStringLiteral("The power overflows or is not real"));
        return v;
    }

    double postfix() {
        double v = primary();
        while (error_.isEmpty()) {
            if (accept(Token::Percent)) {
                v /= 100.0;
            } else if (accept(Token::Bang)) {
                const double n = std::round(v);
                if (v < 0.0 || std::abs(v - n) > 1e-9 || n > 170.0)
                    return fail(QStringLiteral("Factorial wants a whole number from 0 to 170"));
                double f = 1.0;
                for (int i = 2; i <= static_cast<int>(n); ++i) f *= i;
                v = f;
            } else break;
        }
        return v;
    }

    double primary() {
        if (accept(Token::Number)) return t_[at_ - 1].number;
        if (accept(Token::Left)) {
            const double v = expression();
            if (error_.isEmpty() && !accept(Token::Right))
                return fail(QStringLiteral("A closing bracket is missing"));
            return v;
        }
        if (!accept(Token::Name))
            return fail(QStringLiteral("A number, name or bracket is missing"));
        const QString name = t_[at_ - 1].name;
        if (!accept(Token::Left)) {
            if (name == QLatin1String("pi") || name == QStringLiteral("π")) return kPi;
            if (name == QLatin1String("tau")) return 2.0 * kPi;
            if (name == QLatin1String("e")) return std::exp(1.0);
            if (name == QLatin1String("ans")) return answer_;
            return fail(QStringLiteral("'%1' is not a constant or a function").arg(name));
        }
        std::vector<double> args;
        if (peek().kind != Token::Right) {
            args.push_back(expression());
            while (error_.isEmpty() && accept(Token::Comma)) args.push_back(expression());
        }
        if (!error_.isEmpty()) return 0.0;
        if (!accept(Token::Right)) return fail(QStringLiteral("A closing bracket is missing"));
        return function(name, args);
    }

    double trigIn(double v) const { return degrees_ ? v * kPi / 180.0 : v; }
    double trigOut(double v) const { return degrees_ ? v * 180.0 / kPi : v; }

    double function(const QString& name, const std::vector<double>& args) {
        auto one = [&](const QString& want, const std::function<double(double)>& fn) {
            if (args.size() != 1) return fail(QStringLiteral("%1 wants one number").arg(want));
            const double v = fn(args[0]);
            if (!std::isfinite(v)) return fail(QStringLiteral("%1 is outside its domain").arg(want));
            return v;
        };
        if (name == QLatin1String("sqrt")) {
            if (args.size() != 1) return fail(QStringLiteral("sqrt wants one number"));
            if (args[0] < 0.0) return fail(QStringLiteral("sqrt wants a number that is not negative"));
            return std::sqrt(args[0]);
        }
        if (name == QLatin1String("cbrt")) return one(name, [](double v) { return std::cbrt(v); });
        if (name == QLatin1String("exp")) return one(name, [](double v) { return std::exp(v); });
        if (name == QLatin1String("abs")) return one(name, [](double v) { return std::fabs(v); });
        if (name == QLatin1String("floor")) return one(name, [](double v) { return std::floor(v); });
        if (name == QLatin1String("ceil")) return one(name, [](double v) { return std::ceil(v); });
        if (name == QLatin1String("round")) return one(name, [](double v) { return std::round(v); });
        if (name == QLatin1String("ln")) {
            if (args.size() != 1) return fail(QStringLiteral("ln wants one number"));
            if (args[0] <= 0.0) return fail(QStringLiteral("ln wants a positive number"));
            return std::log(args[0]);
        }
        if (name == QLatin1String("log")) {
            if (args.size() != 1) return fail(QStringLiteral("log wants one number"));
            if (args[0] <= 0.0) return fail(QStringLiteral("log wants a positive number"));
            return std::log10(args[0]);
        }
        if (name == QLatin1String("sin")) return one(name, [this](double v) { return std::sin(trigIn(v)); });
        if (name == QLatin1String("cos")) return one(name, [this](double v) { return std::cos(trigIn(v)); });
        if (name == QLatin1String("tan")) return one(name, [this](double v) { return std::tan(trigIn(v)); });
        if (name == QLatin1String("asin")) {
            if (args.size() != 1) return fail(QStringLiteral("asin wants one number"));
            if (args[0] < -1.0 || args[0] > 1.0) return fail(QStringLiteral("asin wants -1 to 1"));
            return trigOut(std::asin(args[0]));
        }
        if (name == QLatin1String("acos")) {
            if (args.size() != 1) return fail(QStringLiteral("acos wants one number"));
            if (args[0] < -1.0 || args[0] > 1.0) return fail(QStringLiteral("acos wants -1 to 1"));
            return trigOut(std::acos(args[0]));
        }
        if (name == QLatin1String("atan")) {
            if (args.size() != 1) return fail(QStringLiteral("atan wants one number"));
            return trigOut(std::atan(args[0]));
        }
        if (name == QLatin1String("deg")) {
            if (args.size() != 1) return fail(QStringLiteral("deg wants one number"));
            return args[0] * 180.0 / kPi;
        }
        if (name == QLatin1String("rad")) {
            if (args.size() != 1) return fail(QStringLiteral("rad wants one number"));
            return args[0] * kPi / 180.0;
        }
        auto two = [&](const QString& want, const std::function<double(double, double)>& fn) {
            if (args.size() != 2) return fail(QStringLiteral("%1 wants two numbers").arg(want));
            const double v = fn(args[0], args[1]);
            if (!std::isfinite(v)) return fail(QStringLiteral("%1 is outside its domain").arg(want));
            return v;
        };
        if (name == QLatin1String("pow"))
            return two(name, [](double a, double b) { return std::pow(a, b); });
        if (name == QLatin1String("min")) return two(name, [](double a, double b) { return std::min(a, b); });
        if (name == QLatin1String("max")) return two(name, [](double a, double b) { return std::max(a, b); });
        if (name == QLatin1String("hypot"))
            return two(name, [](double a, double b) { return std::hypot(a, b); });
        return fail(QStringLiteral("'%1' is not a function the calculator knows").arg(name));
    }

    const std::vector<Token>& t_;
    size_t at_ = 0;
    bool degrees_ = true;
    double answer_ = 0.0;
    QString error_;
};

QString numberText(double v) {
    QString s = QString::number(v, 'g', 14);
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

} // namespace

ScientificCalculatorDialog::ScientificCalculatorDialog(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("calculatorPanel"));
    buildUi();
}

void ScientificCalculatorDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Two displays, not one: the expression stays visible while the answer sits below it. Replacing
    // the input with the answer made it look as though the working had been thrown away.
    expression_ = new QLineEdit(this);
    expression_->setAlignment(Qt::AlignRight);
    expression_->setPlaceholderText(QStringLiteral("Expression — 2 + 3 * sin(30)"));
    QFont expressionFont = expression_->font();
    expressionFont.setPointSizeF(expressionFont.pointSizeF() + 2.0);
    expression_->setFont(expressionFont);
    connect(expression_, &QLineEdit::returnPressed, this, [this] { calculate(); });
    connect(expression_, &QLineEdit::textEdited, this, [this] {
        resultShown_ = false;
        result_->clear();
        status_->clear();
        status_->setStyleSheet(QString());
    });
    root->addWidget(expression_);

    result_ = new QLineEdit(this);
    result_->setReadOnly(true);
    result_->setAlignment(Qt::AlignRight);
    result_->setPlaceholderText(QStringLiteral("Result"));
    QFont resultFont = result_->font();
    resultFont.setPointSizeF(resultFont.pointSizeF() + 7.0);
    resultFont.setBold(true);
    result_->setFont(resultFont);
    result_->setToolTip(QStringLiteral("The answer; Copy takes this first"));
    root->addWidget(result_);

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    status_ = new QLabel(this);
    status_->setMinimumHeight(20);
    controls->addWidget(status_, 1);
    angleBtn_ = new QToolButton(this);
    angleBtn_->setText(QStringLiteral("DEG"));
    angleBtn_->setAutoRaise(true);
    angleBtn_->setToolTip(QStringLiteral("Angles are in degrees; click for radians"));
    connect(angleBtn_, &QToolButton::clicked, this, [this] {
        const bool radians = angleBtn_->text() == QLatin1String("DEG");
        angleBtn_->setText(radians ? QStringLiteral("RAD") : QStringLiteral("DEG"));
        angleBtn_->setToolTip(radians ? QStringLiteral("Angles are in radians; click for degrees")
                                      : QStringLiteral("Angles are in degrees; click for radians"));
    });
    controls->addWidget(angleBtn_);
    auto* copy = new QToolButton(this);
    copy->setText(QStringLiteral("Copy"));
    copy->setAutoRaise(true);
    copy->setToolTip(QStringLiteral("Copy the answer, or the expression if there is no answer"));
    connect(copy, &QToolButton::clicked, this, [this] { copyDisplay(); });
    controls->addWidget(copy);
    root->addLayout(controls);

    auto* keys = new QGridLayout;
    keys->setSpacing(4);
    for (int col = 0; col < 5; ++col) keys->setColumnStretch(col, 1);
    auto key = [&](int row, int col, const QString& text, const QString& token,
                   const QString& tip, bool continuesAnswer = false) {
        auto* b = new QPushButton(text, this);
        b->setMinimumSize(0, 32);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, [this, token, continuesAnswer] {
            appendToken(token, continuesAnswer);
        });
        keys->addWidget(b, row, col);
        return b;
    };

    // Scientific functions first, then the number pad in the shape people already know.
    key(0, 0, QStringLiteral("sin"), QStringLiteral("sin("), QStringLiteral("Sine"));
    key(0, 1, QStringLiteral("cos"), QStringLiteral("cos("), QStringLiteral("Cosine"));
    key(0, 2, QStringLiteral("tan"), QStringLiteral("tan("), QStringLiteral("Tangent"));
    key(0, 3, QStringLiteral("("), QStringLiteral("("), QStringLiteral("Open a bracket"));
    key(0, 4, QStringLiteral(")"), QStringLiteral(")"), QStringLiteral("Close a bracket"));

    key(1, 0, QStringLiteral("sin\xE2\x81\xBB\xC2\xB9"), QStringLiteral("asin("),
        QStringLiteral("Inverse sine"));
    key(1, 1, QStringLiteral("cos\xE2\x81\xBB\xC2\xB9"), QStringLiteral("acos("),
        QStringLiteral("Inverse cosine"));
    key(1, 2, QStringLiteral("tan\xE2\x81\xBB\xC2\xB9"), QStringLiteral("atan("),
        QStringLiteral("Inverse tangent"));
    key(1, 3, QStringLiteral("x\xCA\xB8"), QStringLiteral("^"),
        QStringLiteral("Power: 2^10 or pow(2,10)"), true);
    key(1, 4, QStringLiteral("\xE2\x88\x9A"), QStringLiteral("sqrt("),
        QStringLiteral("Square root"));

    key(2, 0, QStringLiteral("ln"), QStringLiteral("ln("), QStringLiteral("Natural logarithm"));
    key(2, 1, QStringLiteral("log"), QStringLiteral("log("), QStringLiteral("Base-10 logarithm"));
    key(2, 2, QStringLiteral("e\xCB\xA3"), QStringLiteral("exp("), QStringLiteral("e to the power"));
    auto* square = new QPushButton(QStringLiteral("x\xC2\xB2"), this);
    square->setMinimumSize(0, 32);
    square->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    square->setToolTip(QStringLiteral("Square the expression"));
    connect(square, &QPushButton::clicked, this,
            [this] { wrapExpression(QStringLiteral("("), QStringLiteral(")^2")); });
    keys->addWidget(square, 2, 3);
    auto* inverse = new QPushButton(QStringLiteral("1/x"), this);
    inverse->setMinimumSize(0, 32);
    inverse->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    inverse->setToolTip(QStringLiteral("Reciprocal of the expression"));
    connect(inverse, &QPushButton::clicked, this,
            [this] { wrapExpression(QStringLiteral("1/("), QStringLiteral(")")); });
    keys->addWidget(inverse, 2, 4);

    key(3, 0, QStringLiteral("x!"), QStringLiteral("!"), QStringLiteral("Factorial"), true);
    key(3, 1, QStringLiteral("abs"), QStringLiteral("abs("), QStringLiteral("Absolute value"));
    key(3, 2, QStringLiteral("\xCF\x80"), QStringLiteral("pi"), QStringLiteral("Pi"));
    key(3, 3, QStringLiteral("e"), QStringLiteral("e"), QStringLiteral("Euler's number"));
    key(3, 4, QStringLiteral("Ans"), QStringLiteral("ans"), QStringLiteral("The last answer"), true);

    // The number pad: the four familiar rows, with the editing keys on the right.
    key(4, 0, QStringLiteral("7"), QStringLiteral("7"), QString());
    key(4, 1, QStringLiteral("8"), QStringLiteral("8"), QString());
    key(4, 2, QStringLiteral("9"), QStringLiteral("9"), QString());
    key(4, 3, QStringLiteral("\xC3\xB7"), QStringLiteral("/"), QStringLiteral("Divide"), true);
    auto* clear = new QPushButton(QStringLiteral("C"), this);
    clear->setMinimumSize(0, 32);
    clear->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    clear->setToolTip(QStringLiteral("Clear the expression and result"));
    connect(clear, &QPushButton::clicked, this, [this] { clearAll(); });
    keys->addWidget(clear, 4, 4);

    key(5, 0, QStringLiteral("4"), QStringLiteral("4"), QString());
    key(5, 1, QStringLiteral("5"), QStringLiteral("5"), QString());
    key(5, 2, QStringLiteral("6"), QStringLiteral("6"), QString());
    key(5, 3, QStringLiteral("\xC3\x97"), QStringLiteral("*"), QStringLiteral("Multiply"), true);
    auto* back = new QPushButton(QStringLiteral("\xE2\x8C\xAB"), this);
    back->setMinimumSize(0, 32);
    back->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    back->setToolTip(QStringLiteral("Delete the character before the cursor"));
    connect(back, &QPushButton::clicked, this, [this] {
        expression_->backspace();
        resultShown_ = false;
        result_->clear();
        status_->clear();
        status_->setStyleSheet(QString());
    });
    keys->addWidget(back, 5, 4);

    key(6, 0, QStringLiteral("1"), QStringLiteral("1"), QString());
    key(6, 1, QStringLiteral("2"), QStringLiteral("2"), QString());
    key(6, 2, QStringLiteral("3"), QStringLiteral("3"), QString());
    key(6, 3, QStringLiteral("\xE2\x88\x92"), QStringLiteral("-"), QStringLiteral("Subtract"), true);
    key(6, 4, QStringLiteral("%"), QStringLiteral("%"), QStringLiteral("Per cent: 50% is 0.5"), true);

    key(7, 0, QStringLiteral("0"), QStringLiteral("0"), QString());
    key(7, 1, QStringLiteral("."), QStringLiteral("."), QStringLiteral("Decimal point"));
    auto* plusMinus = new QPushButton(QStringLiteral("\xC2\xB1"), this);
    plusMinus->setMinimumSize(0, 32);
    plusMinus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    plusMinus->setToolTip(QStringLiteral("Change the sign of the expression"));
    connect(plusMinus, &QPushButton::clicked, this,
            [this] { wrapExpression(QStringLiteral("-("), QStringLiteral(")")); });
    keys->addWidget(plusMinus, 7, 2);
    key(7, 3, QStringLiteral("+"), QStringLiteral("+"), QStringLiteral("Add"), true);
    auto* equals = new QPushButton(QStringLiteral("="), this);
    equals->setMinimumSize(0, 32);
    equals->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    equals->setDefault(true);
    equals->setToolTip(QStringLiteral("Calculate the expression (Enter does the same)"));
    connect(equals, &QPushButton::clicked, this, [this] { calculate(); });
    keys->addWidget(equals, 7, 4);
    root->addLayout(keys);

    auto* historyHead = new QHBoxLayout;
    historyHead->setContentsMargins(0, 0, 0, 0);
    historyHead->addWidget(new QLabel(QStringLiteral("History"), this), 1);
    auto* historyClear = new QToolButton(this);
    historyClear->setText(QStringLiteral("Clear"));
    historyClear->setAutoRaise(true);
    connect(historyClear, &QToolButton::clicked, this, [this] { history_->clear(); });
    historyHead->addWidget(historyClear);
    root->addLayout(historyHead);

    history_ = new QListWidget(this);
    history_->setMinimumHeight(72);
    history_->setToolTip(QStringLiteral("Earlier expressions; click one to bring it back"));
    connect(history_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        expression_->setText(item->data(Qt::UserRole).toString());
        result_->clear();
        resultShown_ = false;
        status_->clear();
        status_->setStyleSheet(QString());
    });
    root->addWidget(history_, 1);
}

void ScientificCalculatorDialog::appendToken(const QString& token, bool continuesAnswer) {
    if (resultShown_) {
        expression_->setText(continuesAnswer ? QStringLiteral("ans") : QString());
        result_->clear();
        resultShown_ = false;
    }
    expression_->insert(token);
    status_->clear();
    status_->setStyleSheet(QString());
}

void ScientificCalculatorDialog::wrapExpression(const QString& before, const QString& after) {
    QString expr = resultShown_ ? QStringLiteral("ans") : expression_->text().trimmed();
    if (expr.isEmpty()) expr = QStringLiteral("ans");
    expression_->setText(before + expr + after);
    expression_->setCursorPosition(expression_->text().size());
    result_->clear();
    resultShown_ = false;
    status_->clear();
    status_->setStyleSheet(QString());
}

void ScientificCalculatorDialog::calculate() {
    const QString expr = expression_->text().trimmed();
    if (expr.isEmpty()) return;
    double out = 0.0;
    QString error;
    if (!evaluateForTest(expr, angleBtn_->text() == QLatin1String("DEG"), out, &error)) {
        result_->clear();
        status_->setText(error);
        status_->setStyleSheet(QStringLiteral("color:#e0a0a0;"));
        return;
    }
    answer_ = out;
    const QString answer = numberText(out);
    auto* item = new QListWidgetItem(QStringLiteral("%1  =  %2").arg(expr, answer), history_);
    item->setData(Qt::UserRole, expr);
    history_->addItem(item);
    history_->setCurrentRow(history_->count() - 1);
    result_->setText(answer);
    status_->setStyleSheet(QString());
    status_->setText(QStringLiteral("Ans ready"));
    resultShown_ = true;
}

void ScientificCalculatorDialog::clearAll() {
    expression_->clear();
    result_->clear();
    status_->clear();
    status_->setStyleSheet(QString());
    resultShown_ = false;
}

void ScientificCalculatorDialog::copyDisplay() {
    const QString text = !result_->text().isEmpty() ? result_->text() : expression_->text();
    QGuiApplication::clipboard()->setText(text);
    status_->setStyleSheet(QString());
    status_->setText(QStringLiteral("Copied"));
}

bool ScientificCalculatorDialog::evaluateForTest(const QString& expression, bool degrees,
                                                 double& out, QString* error) {
    std::vector<Token> tokens;
    QString why;
    if (!lex(expression, tokens, why)) {
        if (error) *error = why;
        return false;
    }
    Parser parser(tokens, degrees, answer_);
    if (!parser.parse(out, why)) {
        if (error) *error = why;
        return false;
    }
    return true;
}

void ScientificCalculatorDialog::setExpressionForTest(const QString& expression) {
    expression_->setText(expression);
    result_->clear();
    resultShown_ = false;
}

void ScientificCalculatorDialog::setDegreesForTest(bool on) {
    angleBtn_->setText(on ? QStringLiteral("DEG") : QStringLiteral("RAD"));
}

QString ScientificCalculatorDialog::displayForTest() const { return result_->text(); }
int ScientificCalculatorDialog::historyCountForTest() const { return history_->count(); }
void ScientificCalculatorDialog::focusExpression() const { expression_->setFocus(Qt::OtherFocusReason); }

} // namespace Loopwire
