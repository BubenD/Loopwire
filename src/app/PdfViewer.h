#pragma once
#include "io/PdfImport.h"
#include "system/PnMatch.h"
#include <QWidget>
#include <QSizeF>
class QLabel;
class QScrollArea;
class QSpinBox;
class QThread;
class QTimer;

namespace Edraw_Desktop {
class PdfViewer : public QWidget {
    Q_OBJECT
public:
    explicit PdfViewer(QWidget* parent=nullptr);
    ~PdfViewer() override;
    void showFile(const QString& path, const QStringList& terms={}, const PnOptions& options={}, int firstPage=1);
    void clear();
    int pageCount() const { return count_; }
    int currentPage() const { return page_+1; }
    bool ready() const { return ready_; }
    bool loading() const { return worker_ || pending_; }
    void goToPage(int oneBased);
    void jumpHit(int direction);
protected:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
private:
    void requestRender();
    void setZoom(double zoom);
    QString path_;
    QStringList terms_;
    PnOptions options_;
    std::vector<pdf::TextPage> textPages_;
    bool textLoaded_=false, fit_=true, ready_=false, pending_=false;
    int page_=0, count_=0, token_=0;
    double zoom_=1.0;
    QSizeF pageSize_;
    QLabel* image_=nullptr;
    QLabel* status_=nullptr;
    QLabel* pageTotal_=nullptr;
    QLabel* zoomLabel_=nullptr;
    QScrollArea* scroll_=nullptr;
    QSpinBox* pageSpin_=nullptr;
    QThread* worker_=nullptr;
    QTimer* resizeTimer_=nullptr;
};
}
