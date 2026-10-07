#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <vector>

namespace Edraw_Desktop {

// FINDING A PART NUMBER IN TEXT, as a person means it. Exact: the P/N stands on its own - "ABC-123" is found in
// "ABC-123", "(ABC-123)" and "ABC-123." but not in "XABC-1234" or "ABC-123-01" (a joiner - . / _ followed by a letter
// or digit carries the number on). Partial: anywhere, inside a longer number too - each match saying whether it was
// exact, so a partial one can be shown as such. Ignoring separators: - . / _ and spaces optional, "ABC-123" finding
// "ABC123" and "ABC 123". Several numbers at once, separated by ';'.
struct PnOptions {
    bool partial = false;
    bool caseSensitive = false;
    bool ignoreSeparators = false;
};

struct PnMatch {
    int start = 0;
    int end = 0;
    QString text;
    bool exact = true;
};

class PnMatcher {
public:
    PnMatcher(const QStringList& terms, const PnOptions& options);
    // The numbers asked for, each once - several separated by ';' or new lines.
    static QStringList splitTerms(const QString& text);
    bool valid() const { return valid_; }
    std::vector<PnMatch> find(const QString& text) const;
    bool contains(const QString& text) const { return !find(text).empty(); }
    // THE WORDS ROUND A MATCH: so many characters either side, runs of white space made one, "..." where cut.
    static void snippet(const QString& text, int start, int end, QString& before, QString& match, QString& after, int width = 45);

private:
    QRegularExpression re_;
    bool valid_ = false;
};

} // namespace Edraw_Desktop
