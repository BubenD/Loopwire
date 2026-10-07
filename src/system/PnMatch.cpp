#include "system/PnMatch.h"

#include <algorithm>

namespace Edraw_Desktop {

namespace {

const QString kJoiners = QStringLiteral("-./_");
const QString kSeparators = QStringLiteral(" \t\r\n-./_");

// Glued to what is before or after it: a letter or digit there, or a joiner and then one.
bool gluedBefore(const QString& t, int i) {
    if (i > 0 && t.at(i - 1).isLetterOrNumber()) return true;
    return i > 1 && kJoiners.contains(t.at(i - 1)) && t.at(i - 2).isLetterOrNumber();
}

bool gluedAfter(const QString& t, int i) {
    if (i < t.size() && t.at(i).isLetterOrNumber()) return true;
    return i + 1 < t.size() && kJoiners.contains(t.at(i)) && t.at(i + 1).isLetterOrNumber();
}

QString collapse(const QString& s) { return s.simplified(); }

} // namespace

QStringList PnMatcher::splitTerms(const QString& text) {
    QStringList out;
    for (QString t : text.split(QRegularExpression(QStringLiteral("[;\\n]")))) {
        t = t.trimmed();
        if (!t.isEmpty() && !out.contains(t)) out << t;
    }
    return out;
}

PnMatcher::PnMatcher(const QStringList& terms, const PnOptions& options) {
    QStringList bodies;
    for (const QString& term : terms) {
        QString body;
        if (options.ignoreSeparators) {
            QStringList chars;
            for (const QChar c : term)
                if (!kSeparators.contains(c)) chars << QRegularExpression::escape(QString(c));
            body = chars.join(QStringLiteral("[\\s\\-./_]*"));
        } else {
            body = QRegularExpression::escape(term.trimmed());
        }
        if (!body.isEmpty() && !bodies.contains(body)) bodies << body;
    }
    if (bodies.isEmpty()) return;
    // The longest first, so "ABC-12" does not stand in the way of "ABC-123".
    std::sort(bodies.begin(), bodies.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    QString core = bodies.join(QLatin1Char('|'));
    if (!options.partial)
        core = QStringLiteral("(?<![^\\W_])(?<![^\\W_][\\-./_])(?:%1)(?![^\\W_])(?![\\-./_][^\\W_])").arg(core);
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive) flags |= QRegularExpression::CaseInsensitiveOption;
    re_ = QRegularExpression(core, flags);
    valid_ = re_.isValid();
}

std::vector<PnMatch> PnMatcher::find(const QString& text) const {
    std::vector<PnMatch> out;
    if (!valid_ || text.isEmpty()) return out;
    QRegularExpressionMatchIterator it = re_.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength() == 0) continue;
        PnMatch p;
        p.start = static_cast<int>(m.capturedStart());
        p.end = static_cast<int>(m.capturedEnd());
        p.text = m.captured();
        p.exact = !gluedBefore(text, p.start) && !gluedAfter(text, p.end);
        out.push_back(p);
    }
    return out;
}

void PnMatcher::snippet(const QString& text, int start, int end, QString& before, QString& match, QString& after, int width) {
    const int a = std::max(0, start - width);
    const int b = std::min(static_cast<int>(text.size()), end + width);
    before = collapse(text.mid(a, start - a));
    after = collapse(text.mid(end, b - end));
    if (start > a && text.at(start - 1).isSpace()) before += QLatin1Char(' ');
    if (b > end && text.at(end).isSpace()) after.prepend(QLatin1Char(' '));
    if (a > 0) before.prepend(QStringLiteral("..."));
    if (b < text.size()) after += QStringLiteral("...");
    match = collapse(text.mid(start, end - start));
}

} // namespace Edraw_Desktop
