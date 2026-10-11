#pragma once

#include <QDateTime>
#include <QDialog>
#include <QString>

#include <vector>

class QTableWidget;

// RECOVERY COPIES: what was open, written every few minutes while it has changes not saved, so a crash or a power cut
// loses minutes, not the day. A drawing goes to a .dbak (a DWX), the system design to a .sbak (a .wetsys). They live in
// one folder of their own, never beside the user's files; a drawing's copy goes once that drawing is saved.
namespace recovery {

// The folder: EDRAW_RECOVERY_DIR where it is set (the tests), else Edraw's own folder under the user's application data.
QString folder();
// THE FILE FOR ONE THING: named after its file (or "Untitled" and its tab), with a few letters of its full path so two
// drawings of the same name in different folders do not take each other's copy.
QString drawingFile(const QString& documentPath, int tab);
QString systemFile(const QString& documentPath, int tab);

struct Entry {
    QString path;
    bool system = false;   // a .sbak; else a .dbak
    QDateTime when;
    qint64 size = 0;
};
// Every copy there, the newest first. Copies older than two weeks are taken away as they are listed.
std::vector<Entry> list();

}  // namespace recovery

// THE RECOVERY WINDOW (the E menu's Recover...): every copy, newest first - one chosen and Recovered, or Deleted.
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
