#pragma once

#include <QDateTime>
#include <QDialog>
#include <QString>

#include <vector>

class QTableWidget;

// RECOVERY COPIES: a drawing or a system design saved to a file, written again every few minutes while it has changes
// not saved, so a crash or a power cut loses minutes, not the day. Beside its file, by the same name: a drawing's as .dbak
// (a DWX), a system design's as .sbak (a .wetsys). Nothing never saved to a file has one. A copy goes once its file is
// saved; the copies written are remembered, so the Recover window finds them wherever they are.
namespace recovery {

// THE COPY FOR A FILE: beside it, the same name with .dbak (a drawing) or .sbak (a system design); none for no file.
QString drawingFile(const QString& documentPath);
QString systemFile(const QString& designPath);
// A copy written: remembered for the Recover window. One taken away: forgotten.
void remember(const QString& copy);
void forget(const QString& copy);

struct Entry {
    QString path;
    bool system = false;   // a .sbak; else a .dbak
    QDateTime when;
    qint64 size = 0;
};
// Every copy remembered that is still there, the newest first.
std::vector<Entry> list();

}  // namespace recovery

// THE RECOVERY WINDOW (the E menu's Recover...): every copy, newest first - one chosen and Recovered, Deleted, or one
// found by Browse.
class RecoveryDialog : public QDialog {
public:
    explicit RecoveryDialog(QWidget* parent = nullptr);
    // The copy chosen when Recover was pressed; empty when closed without one.
    QString chosen() const { return chosen_; }

private:
    void fill();
    QTableWidget* table_ = nullptr;
    QString chosen_;
};
