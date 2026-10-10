#include "app/PdfViewer.h"
#include "io/NativePdf.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QScrollArea>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {
PdfViewer::PdfViewer(QWidget* parent):QWidget(parent) {
    setObjectName(QStringLiteral("pdfViewer"));
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto* bar=new QHBoxLayout;
    auto button=[&](const char* id,const QString& text,const QString& tip,auto fn){
        auto* b=new QToolButton(this);b->setObjectName(QLatin1String(id));b->setText(text);b->setToolTip(tip);
        connect(b,&QToolButton::clicked,this,fn);bar->addWidget(b);return b;
    };
    button("pdfPrevious",QStringLiteral("<"),QStringLiteral("Previous page"),[this]{goToPage(page_);});
    pageSpin_=new QSpinBox(this);pageSpin_->setObjectName(QStringLiteral("pdfPage"));pageSpin_->setRange(1,1);bar->addWidget(pageSpin_);
    connect(pageSpin_,&QSpinBox::valueChanged,this,[this](int page){goToPage(page);});
    pageTotal_=new QLabel(QStringLiteral("/ 0"),this);bar->addWidget(pageTotal_);
    button("pdfNext",QStringLiteral(">"),QStringLiteral("Next page"),[this]{goToPage(page_+2);});
    button("pdfPreviousHit",QStringLiteral("< Hit"),QStringLiteral("Previous page with a match"),[this]{jumpHit(-1);});
    button("pdfNextHit",QStringLiteral("Hit >"),QStringLiteral("Next page with a match"),[this]{jumpHit(1);});
    button("pdfZoomOut",QStringLiteral("-"),QStringLiteral("Zoom out"),[this]{setZoom(zoom_/1.25);});
    zoomLabel_=new QLabel(this);bar->addWidget(zoomLabel_);
    button("pdfZoomIn",QStringLiteral("+"),QStringLiteral("Zoom in"),[this]{setZoom(zoom_*1.25);});
    button("pdfFit",QStringLiteral("Fit"),QStringLiteral("Fit page width"),[this]{fit_=true;requestRender();});
    button("pdfExternal",QStringLiteral("Open"),QStringLiteral("Open in the default PDF viewer"),[this]{if(!path_.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(path_));});
    bar->addStretch(1);layout->addLayout(bar);
    scroll_=new QScrollArea(this);scroll_->setAlignment(Qt::AlignHCenter|Qt::AlignTop);
    scroll_->setStyleSheet(QStringLiteral("QScrollArea{background:#34383e;}"));
    image_=new QLabel(scroll_);image_->setAlignment(Qt::AlignTop|Qt::AlignLeft);scroll_->setWidget(image_);
    scroll_->viewport()->installEventFilter(this);layout->addWidget(scroll_,1);
    status_=new QLabel(this);status_->setWordWrap(true);status_->setObjectName(QStringLiteral("pdfStatus"));layout->addWidget(status_);
    resizeTimer_=new QTimer(this);resizeTimer_->setSingleShot(true);resizeTimer_->setInterval(120);
    connect(resizeTimer_,&QTimer::timeout,this,[this]{if(fit_&&!path_.isEmpty())requestRender();});
}
PdfViewer::~PdfViewer(){++token_;if(worker_)worker_->wait();}
void PdfViewer::clear(){++token_;path_.clear();textPages_.clear();textLoaded_=false;ready_=false;pending_=false;image_->clear();count_=0;pageTotal_->setText(QStringLiteral("/ 0"));}
void PdfViewer::showFile(const QString& path,const QStringList& terms,const PnOptions& options,int firstPage){
    if(path!=path_){clear();path_=path;fit_=true;pageSize_={};}
    terms_=terms;options_=options;page_=std::max(0,firstPage-1);requestRender();
}
void PdfViewer::goToPage(int oneBased){if(path_.isEmpty()||!count_)return;const int page=std::clamp(oneBased-1,0,count_-1);if(page==page_&&ready_)return;page_=page;requestRender();}
void PdfViewer::setZoom(double zoom){fit_=false;zoom_=std::clamp(zoom,.2,8.0);requestRender();}
void PdfViewer::jumpHit(int direction){
    const PnMatcher matcher(terms_,options_);std::vector<int> hits;
    for(int i=0;i<static_cast<int>(textPages_.size());++i)if(matcher.contains(textPages_[static_cast<size_t>(i)].text))hits.push_back(i);
    if(hits.empty())return;
    int target=direction>0?hits.front():hits.back();
    if(direction>0){for(int page:hits)if(page>page_){target=page;break;}}
    else{for(auto i=hits.rbegin();i!=hits.rend();++i)if(*i<page_){target=*i;break;}}
    goToPage(target+1);
}
void PdfViewer::requestRender(){
    if(path_.isEmpty())return;
    ++token_;ready_=false;
    if(worker_){pending_=true;return;}
    pending_=false;const int token=token_,requestedPage=page_;const QString path=path_;
    const QStringList terms=terms_;const PnOptions options=options_;const bool needText=!textLoaded_;
    const double dpr=devicePixelRatioF();
    const int width=static_cast<int>((fit_?std::max(128,scroll_->viewport()->width()-24):std::max(1.0,pageSize_.isEmpty()?612.0:pageSize_.width())*zoom_)*dpr);
    status_->setText(QStringLiteral("Rendering page %1...").arg(page_+1));
    QPointer<PdfViewer> self(this);
    worker_=QThread::create([self,path,requestedPage,width,token,terms,options,needText,dpr]{
        PdfRaster raster=renderNativePdf(path,requestedPage,width);
        std::vector<pdf::TextPage> pages;
        if(needText){QFile file(path);if(file.open(QIODevice::ReadOnly))pages=pdf::readTextPages(file.readAll());}
        if(auto* app=QCoreApplication::instance())QMetaObject::invokeMethod(app,[self,raster,pages=std::move(pages),token,needText,dpr,terms,options]()mutable{
            if(!self||token!=self->token_)return;
            if(needText){self->textPages_=std::move(pages);self->textLoaded_=true;}
            if(raster.image.isNull()){self->image_->clear();self->status_->setText(raster.error);return;}
            QImage image=raster.image;
            const PnMatcher matcher(terms,options);
            QRectF first;
            if(raster.page<static_cast<int>(self->textPages_.size())){
                QPainter painter(&image);painter.setCompositionMode(QPainter::CompositionMode_Multiply);
                for(const auto& run:self->textPages_[static_cast<size_t>(raster.page)].runs){
                    const auto matches=matcher.find(run.text);
                    for(const PnMatch& match:matches){
                        QRectF box=run.bounds;
                        if(run.horizontal&&run.text.size()>0){const double step=box.width()/run.text.size();box.setLeft(box.left()+step*match.start);box.setWidth(step*(match.end-match.start));}
                        box=QRectF(box.x()*image.width(),box.y()*image.height(),box.width()*image.width(),box.height()*image.height()).adjusted(-2,-2,2,2);
                        painter.fillRect(box,match.exact?QColor(255,230,0,145):QColor(255,160,50,145));
                        if(first.isNull())first=box;
                    }
                }
            }
            QPixmap pixmap=QPixmap::fromImage(image);pixmap.setDevicePixelRatio(dpr);self->image_->setPixmap(pixmap);self->image_->resize(pixmap.size()/dpr);
            self->count_=raster.count;self->page_=raster.page;self->pageSize_=raster.pageSize;
            self->pageSpin_->blockSignals(true);self->pageSpin_->setMaximum(raster.count);self->pageSpin_->setValue(raster.page+1);self->pageSpin_->blockSignals(false);
            self->pageTotal_->setText(QStringLiteral("/ %1").arg(raster.count));self->zoom_=image.width()/dpr/raster.pageSize.width();self->zoomLabel_->setText(QStringLiteral("%1%").arg(self->zoom_*100,0,'f',0));
            self->ready_=true;self->status_->setText(QStringLiteral("Page %1 of %2").arg(raster.page+1).arg(raster.count));
            if(!first.isNull())self->scroll_->ensureVisible(static_cast<int>(first.center().x()/dpr),static_cast<int>(first.center().y()/dpr),60,60);
        },Qt::QueuedConnection);
    });
    QThread* worker=worker_;
    connect(worker,&QThread::finished,this,[this,worker]{worker_=nullptr;worker->deleteLater();if(pending_&&!path_.isEmpty())requestRender();});
    worker->start();
}
void PdfViewer::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);resizeTimer_->start();}
bool PdfViewer::eventFilter(QObject* object,QEvent* event){
    if(object==scroll_->viewport()&&event->type()==QEvent::Wheel){auto* wheel=static_cast<QWheelEvent*>(event);if(wheel->modifiers()&Qt::ControlModifier){setZoom(zoom_*std::pow(1.0015,wheel->angleDelta().y()));return true;}}
    return QWidget::eventFilter(object,event);
}
}
