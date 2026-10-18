#include "app/ribbon/RibbonIcons.h"

#include "model/PartSymbols.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>

#include <cmath>

namespace Loopwire {

// Shared CAD pictograms: a 24-unit grid, consistent strokes, and an accent for the action.
// Four raster pixels per logical pixel keep the ribbon sharp at Windows display scaling.
QIcon makeCadActionIcon(const QString& key, const QColor& ink) {
    QPixmap pm(96,96);pm.setDevicePixelRatio(4);pm.fill(Qt::transparent);
    QPainter p(&pm);p.setRenderHint(QPainter::Antialiasing,true);
    const QColor accent=ink.lightness()>145?QColor("#72b8f4"):QColor("#176ac1");
    QColor wash=accent;wash.setAlpha(42);
    auto stroke=[&](const QColor& c,double width=1.65,Qt::PenStyle style=Qt::SolidLine){p.setPen(QPen(c,width,style,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);};
    auto line=[&](double x,double y,double xx,double yy){p.drawLine(QPointF(x,y),QPointF(xx,yy));};
    auto arrow=[&](QPointF a,QPointF b,double head=3.0){p.drawLine(a,b);const double angle=std::atan2(b.y()-a.y(),b.x()-a.x());
        for(double side:{-.55,.55})p.drawLine(b,b-QPointF(std::cos(angle+side)*head,std::sin(angle+side)*head));};
    auto box=[&](double x,double y,double w,double h,bool filled=false){p.setBrush(filled?QBrush(wash):QBrush(Qt::NoBrush));p.drawRoundedRect(QRectF(x,y,w,h),.6,.6);p.setBrush(Qt::NoBrush);};
    stroke(ink);
    if(key=="move"){
        stroke(accent);arrow({12,12},{12,3});arrow({12,12},{21,12});arrow({12,12},{12,21});arrow({12,12},{3,12});
        stroke(ink);p.setBrush(ink);p.drawEllipse(QPointF(12,12),1.4,1.4);
    }else if(key=="rotate"){
        box(8,9,7,7,true);stroke(accent);QPainterPath a;a.arcMoveTo(3,3,18,18,40);a.arcTo(3,3,18,18,40,270);p.drawPath(a);arrow({16.5,20},{19,18},2.8);
    }else if(key=="mirror"){
        stroke(ink,1.2,Qt::DashLine);line(12,2.5,12,21.5);stroke(ink);p.drawPolygon(QPolygonF{{3,6},{9,11},{3,18}});
        stroke(accent);p.setBrush(wash);p.drawPolygon(QPolygonF{{21,6},{15,11},{21,18}});
    }else if(key=="scale"){
        box(3.5,13.5,7,7);stroke(accent);box(3.5,3.5,17,17);arrow({10,14},{17,7});
    }else if(key=="offset"){
        p.drawPolyline(QPolygonF{{4,4},{4,20},{20,20}});stroke(accent);p.drawPolyline(QPolygonF{{9,4},{9,15},{20,15}});arrow({13,8},{18,8},2.5);
    }else if(key=="trim"){
        line(14,3,14,21);line(3,12,14,12);stroke(accent,1.5,Qt::DashLine);line(16,12,22,12);stroke(accent);line(17,4,21,8);line(21,4,17,8);
    }else if(key=="extend"){
        line(20,3,20,21);line(3,12,9,12);stroke(accent);arrow({9,12},{19,12});
    }else if(key=="fillet"||key=="chamfer"){
        stroke(ink,1.1,Qt::DashLine);line(4,4,4,11);line(4,4,11,4);stroke(ink);line(4,20,4,12);line(12,4,20,4);
        stroke(accent,2);QPainterPath corner;corner.moveTo(4,12);if(key=="fillet")corner.quadTo(4,4,12,4);else corner.lineTo(12,4);p.drawPath(corner);
    }else if(key=="copy"){
        box(4,3,11,14);stroke(accent);box(9,8,11,14,true);line(12,12,17,12);line(12,16,17,16);
    }else if(key=="cut"){
        p.drawEllipse(QPointF(5,17),3,3);p.drawEllipse(QPointF(18,17),3,3);stroke(accent);line(7,15,19,3);line(16,15,4,3);stroke(ink);p.drawEllipse(QPointF(11.5,10.5),1,1);
    }else if(key=="match"){
        stroke(accent,2.4);line(19,3,11,11);stroke(ink);p.drawPolygon(QPolygonF{{10,10},{14,14},{10,18},{6,14}});
        stroke(accent);p.setBrush(wash);QPainterPath brush;brush.moveTo(6,14);brush.quadTo(2,16,3,21);brush.quadTo(9,22,10,18);brush.closeSubpath();p.drawPath(brush);
    }else if(key=="array"){
        for(int r=0;r<3;++r)for(int c=0;c<3;++c){stroke(r==2&&c==0?ink:accent,1.4);box(3+c*7,3+r*7,4,4,r==2&&c==0);}
    }else if(key=="arraypolar"){
        stroke(ink,1,Qt::DashLine);p.drawEllipse(QPointF(12,12),7.5,7.5);stroke(accent);for(int i=0;i<6;++i){double a=i*M_PI/3;p.setBrush(wash);p.drawEllipse(QPointF(12+8*std::cos(a),12+8*std::sin(a)),2,2);}stroke(ink);p.drawEllipse(QPointF(12,12),1,1);
    }else if(key=="arraypath"){
        QPainterPath path;path.moveTo(3,20);path.cubicTo(3,4,21,20,21,4);p.drawPath(path);stroke(accent);for(QPointF at:{QPointF(3,20),QPointF(8,12),QPointF(16,12),QPointF(21,4)})box(at.x()-2,at.y()-2,4,4,true);
    }else if(key=="delete"){
        line(4,6,20,6);line(9,3,15,3);p.drawPolyline(QPolygonF{{6,6},{7,21},{17,21},{18,6}});stroke(accent);line(10,10,10,17);line(14,10,14,17);
    }else if(key=="explode"){
        box(8,8,8,8);stroke(accent);arrow({7,7},{3,3});arrow({17,7},{21,3});arrow({7,17},{3,21});arrow({17,17},{21,21});
    }else if(key=="qselect"){
        stroke(ink,1.2,Qt::DashLine);box(3,3,15,15);stroke(accent);p.setBrush(wash);p.drawPolygon(QPolygonF{{10,9},{21,15},{16,16},{14,21}});
    }else if(key=="purge"){
        stroke(ink);line(18,3,11,12);stroke(accent);p.setBrush(wash);p.drawPolygon(QPolygonF{{9,11},{15,15},{11,21},{3,18}});line(7,17,6,19);line(10,17,9,20);stroke(ink);line(17,20,21,20);
    }else if(key=="align"){
        stroke(accent);line(4,2,4,22);stroke(ink);box(7,4,13,6,true);box(7,14,8,6);
    }else if(key=="break"||key=="breakcurve"){
        line(2,12,7,12);line(17,12,22,12);stroke(accent);
        for(double x:{9.0,15.0}){QPainterPath mark;mark.moveTo(x+2,4);if(key=="breakcurve"){mark.cubicTo(x-3,8,x+3,16,x-2,20);}else mark.lineTo(x-2,20);p.drawPath(mark);}
    }else if(key=="pair"||key=="pairroute"||key=="harness"||key=="harnessroute"){
        const bool routed=key.endsWith("route"),bundle=key.startsWith("harness");
        for(int k=0;k<(bundle?3:2);++k){stroke(k==0?ink:accent,1.55);double phase=k*(bundle?2*M_PI/3:M_PI);
            QPainterPath wire;for(int i=0;i<=96;++i){
                const double t=i/96.0;
                const double baseline=bundle?(k-1)*3.2:(k==0?-3.2:3.2);
                const double envelope=std::sin(M_PI*t);
                const double offset=baseline*(1-envelope)+3.2*std::sin(4*M_PI*t+phase)*envelope;
                QPointF center(2+20*t,12),normal(0,1);
                if(routed){
                    const double distance=t*(22+2.5*M_PI);
                    if(distance<=13)center=QPointF(2+distance,17);
                    else if(distance<=13+2.5*M_PI){
                        const double angle=M_PI/2-(distance-13)/5;
                        normal=QPointF(std::cos(angle),std::sin(angle));center=QPointF(15,12)+normal*5;
                    }else{center=QPointF(20,12-(distance-13-2.5*M_PI));normal=QPointF(1,0);}
                }
                const QPointF at=center+normal*(routed?.65:1.0)*offset;
                if(i==0)wire.moveTo(at);else wire.lineTo(at);
            }p.drawPath(wire);
        }
    }else if(key=="fanout"||key=="wires"){
        for(int k=0;k<3;++k){stroke(k==1?ink:accent,1.55);double out=5+k*7,center=9+k*3;QPainterPath wire;wire.moveTo(2,key=="fanout"?center:out);
            if(key=="wires")wire.cubicTo(6,out,6,center,9,center);else wire.lineTo(8,center);
            wire.cubicTo(15,center,15,out,20,out);wire.lineTo(22,out);p.drawPath(wire);}
    }else if(key=="autoroute"){
        box(2,3,5,6);box(17,16,5,6);stroke(accent,1.9);QPainterPath route;route.moveTo(7,6);route.lineTo(14,6);route.quadTo(19.5,6,19.5,12);route.lineTo(19.5,16);p.drawPath(route);arrow({10,6},{14,6},2.4);
    }else if(key.startsWith("heatshrink")){
        line(2,15,22,15);stroke(accent);p.setBrush(wash);
        if(key=="heatshrink1")p.drawRect(QRectF(4,10,16,10));
        else if(key=="heatshrink2"){p.drawPolygon(QPolygonF{{3,14},{6,10},{9,10},{9,20},{6,20},{3,16}});p.drawPolygon(QPolygonF{{15,10},{18,10},{21,14},{21,16},{18,20},{15,20}});}
        else if(key=="heatshrink3")p.drawPolygon(QPolygonF{{4,10},{14,10},{20,14},{20,16},{14,20},{4,20}});
        else if(key=="heatshrink4"){p.drawRect(QRectF(4,10,16,10));stroke(ink,0.9);for(int i=0;i<4;++i){line(4+i*4,10,8+i*4,20);line(8+i*4,10,4+i*4,20);}stroke(accent);}
        else p.drawPolygon(QPolygonF{{4,14},{8,10},{16,10},{20,14},{20,16},{16,20},{8,20},{4,16}});
        stroke(ink,1.25);for(int i=0;i<2;++i){QPainterPath wave;wave.moveTo(9+i*6,7);wave.cubicTo(6+i*6,5,12+i*6,4,9+i*6,2);p.drawPath(wave);}
    }else if(key=="lacingtie"){
        stroke(ink,1.1);line(2,10,22,10);line(2,14,22,14);
        stroke(accent,1.6);box(10,7,4,11,true);
        QPainterPath knot;knot.moveTo(12,7);knot.cubicTo(3,3,7,1,12,6);
        knot.cubicTo(17,1,21,3,12,7);p.drawPath(knot);
        line(12,6,8,2);line(12,6,16,2);
    }else if(key=="label"||key=="labelclear"){
        line(2,12,22,12);if(key=="labelclear"){stroke(accent,1.1);box(3,5,18,14,true);}stroke(ink);p.setBrush(ink.lightness()>145?QColor("#263545"):QColor("#eff5fc"));p.drawRoundedRect(QRectF(6,7,12,10),1,1);stroke(accent,1.4);line(9,10,15,10);line(9,14,13,14);
    }else if(key=="axisangle"){
        stroke(ink,1.1,Qt::DashLine);line(3,12,21,12);line(12,3,12,21);stroke(accent,1.8);QPainterPath sweep;sweep.arcMoveTo(5,5,14,14,80);sweep.arcTo(5,5,14,14,80,-235);p.drawPath(sweep);arrow({6,17},{5.5,15},2.5);stroke(ink);p.drawEllipse(QPointF(12,12),1.5,1.5);
    }else if(key=="parts"){
        line(2,12,6,12);line(18,12,22,12);stroke(accent);box(6,7,12,10,true);stroke(ink,1.25);line(9,8,9,16);line(13,8,13,16);line(16,8,16,16);
    }else if(key=="text"||key=="mtext"){
        stroke(accent,1.8);p.drawPolyline(QPolygonF{{4,20},{10,4},{16,20}});line(6.5,14,13.5,14);
        if(key=="mtext"){stroke(ink,1.3);line(17,7,22,7);line(18,12,22,12);line(19,17,22,17);}
    }else if(key=="netlabel"){
        line(2,12,5,12);stroke(accent);p.setBrush(wash);p.drawPolygon(QPolygonF{{5,7},{17,7},{22,12},{17,17},{5,17}});stroke(ink,1.3);line(8,10,15,10);line(8,14,13,14);
    }else if(key=="volumearrow"){
        stroke(accent);p.setBrush(wash);p.drawPolygon(QPolygonF{{3,9},{13,9},{13,4},{22,12},{13,20},{13,15},{3,15}});
    }else if(key=="centermark"){
        p.drawEllipse(QPointF(12,12),6,6);stroke(accent,1.4);line(12,2,12,9);line(12,15,12,22);line(2,12,9,12);line(15,12,22,12);p.drawEllipse(QPointF(12,12),.7,.7);
    }else if(key=="centerline"){
        line(3,5,21,5);line(3,19,21,19);stroke(accent,1.5,Qt::DashDotLine);line(2,12,22,12);
    }else if(key=="detailview"){
        p.drawEllipse(QPointF(6,6),3.5,3.5);line(6,5,6,7);line(5,6,7,6);stroke(accent);p.drawEllipse(QPointF(15,15),7,7);line(15,12,15,18);line(12,15,18,15);
    }else if(key=="leader"||key=="balloon"){
        stroke(accent);arrow({12,12},{3,21});line(12,12,19,12);stroke(ink);
        if(key=="leader"){line(12,4,21,4);line(12,8,18,8);}else{p.drawEllipse(QPointF(16,6),4.5,4.5);line(15,5,16,4);line(16,4,16,8);}
    }else if(key=="arrowonly"){
        stroke(accent,1.8);arrow({20,19},{4,5},3.4);
    }else if(key.startsWith("notemark")){
        stroke(accent);p.setBrush(wash);const QString shape=key.mid(9);
        if(shape=="square")p.drawRect(QRectF(5,5,14,14));
        else if(shape=="hexagon")p.drawPolygon(QPolygonF{{3,12},{7.5,4.5},{16.5,4.5},{21,12},{16.5,19.5},{7.5,19.5}});
        else if(shape=="diamond")p.drawPolygon(QPolygonF{{12,2},{22,12},{12,22},{2,12}});
        else if(shape=="circle")p.drawEllipse(QPointF(12,12),9,9);
        else p.drawPolygon(QPolygonF{{12,3},{22,20},{2,20}});
        stroke(ink,1.4);const double top=shape.isEmpty()||shape=="triangle"?11:9;line(11,top+1,12.5,top);line(12.5,top,12.5,top+7);
    }else if(key=="note"){
        // A NOTE: a sheet of notepaper, its corner turned down, with lines of writing on it.
        p.drawPolygon(QPolygonF{{4,3},{15,3},{20,8},{20,21},{4,21}});
        stroke(accent);p.setBrush(wash);p.drawPolygon(QPolygonF{{15,3},{15,8},{20,8}});
        stroke(ink,1.3);line(7,10,13,10);line(7,14,17,14);line(7,18,15,18);
    }else if(key=="drawingdetails"){
        // THE DRAWING'S DETAILS: a sheet with its border, and the title block in the corner where the number, the
        // revision and the title are written.
        box(2.5,3.5,19,17);stroke(ink,1.0);box(4,5,16,14);
        stroke(accent);p.setBrush(wash);p.drawRect(QRectF(11,13,9,6));p.setBrush(Qt::NoBrush);
        stroke(accent,1.1);line(11,16,20,16);line(15,13,15,16);
    }else if(key=="revision"){
        box(3,3,18,18);line(3,8,21,8);stroke(accent);p.setBrush(wash);p.drawPolygon(QPolygonF{{12,10},{17,18},{7,18}});
    }else if(key=="table"){
        // A TABLE: a grid of cells, three by three, its heading row shaded.
        box(3,4,18,16);stroke(accent);p.fillRect(QRectF(3.8,4.8,16.4,4.4),wash);line(3,9.3,21,9.3);
        stroke(ink,1.2);line(9,4,9,20);line(15,4,15,20);line(3,14.6,21,14.6);
    }else if(key.startsWith("dimension")){
        const int mode=key.mid(9).toInt();
        if(mode==0){line(4,8,4,21);line(20,8,20,21);stroke(accent,1.4);arrow({12,12},{4,12},2.7);arrow({12,12},{20,12},2.7);line(8,4,16,4);}
        else if(mode==1){line(3,16,8,21);line(16,3,21,8);stroke(accent,1.4);arrow({12,12},{5,19},2.5);arrow({12,12},{19,5},2.5);}
        else if(mode==2||mode==3){
            if(mode==2){line(4,20,4,3);line(4,20,21,20);}stroke(accent);QPainterPath arc;arc.moveTo(4,7);arc.quadTo(17,7,17,20);p.drawPath(arc);line(2,7,6,7);line(17,18,17,22);
        }else if(mode==4||mode==5||mode==7){
            p.drawEllipse(QPointF(12,12),8,8);stroke(accent,1.4);
            if(mode==7){p.drawPolyline(QPolygonF{{10,14},{13,11},{14,15},{20,8}});arrow({17,11},{20,8},2.8);}
            else{arrow({12,12},{19,5},2.8);if(mode==5)arrow({12,12},{5,19},2.8);else{p.setBrush(accent);p.drawEllipse(QPointF(12,12),1.1,1.1);}}
        }else{stroke(ink,1.2);line(3,21,21,21);stroke(accent);p.drawPolyline(QPolygonF{{7,18},{7,9},{13,5},{21,5}});p.setBrush(accent);p.drawEllipse(QPointF(7,18),1.5,1.5);}
    }else if(key=="connector"){
        box(3,3,11,18,true);stroke(accent);for(int y:{7,12,17}){line(10,y,21,y);p.drawEllipse(QPointF(7,y),1,1);}
    }else if(key=="wire"){
        stroke(accent,1.9);p.drawPolyline(QPolygonF{{3,18},{9,18},{9,6},{21,6}});stroke(ink,1.4);p.setBrush(ink);p.drawEllipse(QPointF(3,18),1.5,1.5);p.drawEllipse(QPointF(21,6),1.5,1.5);
    }else if(key=="splice"){
        line(3,12,21,12);line(12,3,12,21);stroke(accent);p.setBrush(accent);p.drawEllipse(QPointF(12,12),3,3);
    }else if(key=="termination"){
        line(2,12,13,12);stroke(accent,2);line(12,7,22,17);line(12,17,22,7);
    }else if(key=="contact"){
        stroke(ink,1.3);line(4,8,6,8);line(5.5,7,5.5,16);stroke(accent);line(8,6,13,12);line(8,18,13,12);line(13,12,22,12);
    }else if(key=="shield"){
        line(2,8,22,8);line(2,17,22,17);stroke(accent,1.1);p.save();p.setClipRect(QRectF(6,8,12,9));
        for(int x=-4;x<24;x+=4){line(x,8,x+9,17);line(x,17,x+9,8);}p.restore();stroke(ink,1.5);line(6,7,6,18);line(18,7,18,18);
    }else if(key=="schematictwist"){
        p.drawPolyline(QPolygonF{{2,7},{6,7},{18,17},{22,17}});stroke(accent);p.drawPolyline(QPolygonF{{2,17},{6,17},{18,7},{22,7}});
    }else if(key=="alignrouting"){
        stroke(ink);box(2,3,4,7);box(18,15,4,7);stroke(accent,1.7);p.drawPolyline(QPolygonF{{6,6},{13,6},{13,18},{18,18}});stroke(ink,1.1,Qt::DashLine);line(6,6,18,18);
    }else if(key=="crossswitch"){
        line(12,2,12,6);line(12,14,12,22);stroke(accent,1.8);QPainterPath hop;hop.moveTo(2,12);hop.lineTo(8,12);hop.cubicTo(8,6,16,6,16,12);hop.lineTo(22,12);p.drawPath(hop);
    }else if(key=="alignmarks"){
        stroke(ink,1.2);line(2,7,22,7);line(2,17,22,17);stroke(accent,1.2,Qt::DashLine);line(12,2,12,22);stroke(accent);box(9,4,6,6,true);box(9,14,6,6,true);
    }else if(key=="resistor"||key=="resistorzigzag"||key=="capacitor"||key=="coil"||key=="ferrite"){
        line(2,12,6,12);line(18,12,22,12);stroke(accent);
        if(key=="resistor")box(6,8,12,8,true);
        else if(key=="resistorzigzag")p.drawPolyline(QPolygonF{{6,12},{8,8},{10,16},{12,8},{14,16},{16,8},{18,12}});
        else if(key=="capacitor"){line(6,12,9,12);line(15,12,18,12);line(9,5,9,19);line(15,5,15,19);}
        else{QPainterPath coil;coil.moveTo(6,12);for(int i=0;i<3;++i)coil.cubicTo(6+i*4,5,10+i*4,5,10+i*4,12);p.drawPath(coil);if(key=="ferrite"){stroke(ink,1.2);line(6,16,18,16);line(6,19,18,19);}}
    }else return {};
    p.end();return QIcon(pm);
}

// Draw a simple monochrome glyph for a tool so the ribbon reads like Loopwire's
// icon grid without external assets.
QIcon makeToolIcon(ToolId id, const QColor& ink, int variant) {
    QString action;
    switch(id) {
    case ToolId::Move: action=QStringLiteral("move");break;
    case ToolId::Rotate: action=QStringLiteral("rotate");break;
    case ToolId::Mirror: action=QStringLiteral("mirror");break;
    case ToolId::Scale: action=QStringLiteral("scale");break;
    case ToolId::Offset: action=QStringLiteral("offset");break;
    case ToolId::Trim: action=QStringLiteral("trim");break;
    case ToolId::Extend: action=QStringLiteral("extend");break;
    case ToolId::Fillet: action=QStringLiteral("fillet");break;
    case ToolId::Chamfer: action=QStringLiteral("chamfer");break;
    case ToolId::LengthBreak: action=QStringLiteral("break");break;
    case ToolId::LacingTie: action=QStringLiteral("lacingtie");break;
    case ToolId::TwistedPair: action=QStringLiteral("pair");break;
    case ToolId::TwistedRoute: action=QStringLiteral("pairroute");break;
    case ToolId::TwistedHarness: action=QStringLiteral("harness");break;
    case ToolId::TwistedHarnessRoute: action=QStringLiteral("harnessroute");break;
    case ToolId::AutoRoute: action=QStringLiteral("autoroute");break;
    case ToolId::Fanout: action=QStringLiteral("fanout");break;
    case ToolId::WiresRun: action=QStringLiteral("wires");break;
    case ToolId::AxisAngle: action=QStringLiteral("axisangle");break;
    case ToolId::Text:action=QStringLiteral("text");break;
    case ToolId::MText:action=QStringLiteral("mtext");break;
    case ToolId::Dimension:action=QStringLiteral("dimension0");break;
    case ToolId::NetLabel:action=QStringLiteral("netlabel");break;
    case ToolId::VolumeArrow:action=QStringLiteral("volumearrow");break;
    case ToolId::CenterMark:action=QStringLiteral("centermark");break;
    case ToolId::CenterLine:action=QStringLiteral("centerline");break;
    case ToolId::DetailView:action=QStringLiteral("detailview");break;
    case ToolId::TextLeader:action=QStringLiteral("leader");break;
    case ToolId::Balloon:action=QStringLiteral("balloon");break;
    case ToolId::Arrow:action=QStringLiteral("arrowonly");break;
    case ToolId::Note:action=QStringLiteral("note");break;
    case ToolId::Revision:action=QStringLiteral("revision");break;
    case ToolId::Table:action=QStringLiteral("table");break;
    case ToolId::Connector:action=QStringLiteral("connector");break;
    case ToolId::Wire:action=QStringLiteral("wire");break;
    case ToolId::Splice:action=QStringLiteral("splice");break;
    case ToolId::NotConnected:action=QStringLiteral("termination");break;
    case ToolId::ConnectionPin:action=QStringLiteral("contact");break;
    case ToolId::HeatshrinkRoute:action=QStringLiteral("heatshrink%1").arg(variant);break;
    default:break;
    }
    if(!action.isEmpty())return makeCadActionIcon(action,ink);
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (id) {
    case ToolId::Select: {
        QPolygonF a({{6, 4}, {6, 18}, {10, 14}, {13, 20}, {15, 19}, {12, 13}, {17, 13}});
        p.setBrush(ink);
        p.drawPolygon(a);
        break;
    }
    case ToolId::Line: p.drawLine(5, 18, 19, 6); break;
    case ToolId::DoubleLine: p.drawLine(4, 16, 17, 5); p.drawLine(7, 19, 20, 8); break;
    case ToolId::Polyline: { QPolygonF a({{4, 16}, {9, 9}, {14, 14}, {20, 6}}); p.drawPolyline(a); break; }
    case ToolId::DoublePolyline: {
        QPolygonF a({{4, 14}, {9, 7}, {14, 12}, {20, 4}});
        QPolygonF b({{4, 19}, {9, 12}, {14, 17}, {20, 9}});
        p.drawPolyline(a); p.drawPolyline(b); break;
    }
    case ToolId::Rect: p.drawRect(5, 7, 14, 10); break;
    case ToolId::Polygon: {
        QPolygonF h; for (int i = 0; i < 6; ++i) { double an = M_PI / 6 + i * M_PI / 3; h << QPointF(12 + 8 * std::cos(an), 12 + 8 * std::sin(an)); } p.drawPolygon(h); break;
    }
    case ToolId::Circle: p.drawEllipse(QPointF(12, 12), 7, 7); break;
    case ToolId::Arc: { QPainterPath pa; pa.moveTo(5, 17); pa.arcTo(5, 5, 16, 16, 180, -120); p.drawPath(pa); break; }
    case ToolId::Ellipse: p.drawEllipse(QPointF(12, 12), 8, 5); break;
    case ToolId::Spline: { QPainterPath pa; pa.moveTo(4, 16); pa.cubicTo(8, 2, 16, 22, 20, 8); p.drawPath(pa); break; }
    case ToolId::Hatch: {
        p.drawRect(4, 5, 16, 14);
        // diagonal hatch lines clipped to the box
        p.save();
        p.setClipRect(4, 5, 16, 14);
        for (int o = -14; o <= 16; o += 4) p.drawLine(4 + o, 19, 4 + o + 14, 5);
        p.restore();
        break;
    }
    case ToolId::Wipeout: {
        p.drawRect(4, 5, 16, 14);
        p.drawLine(4, 19, 20, 5); // crossed-out (masked) box
        p.drawLine(4, 5, 20, 19);
        break;
    }
    }
    p.end();
    return QIcon(pm);
}

QIcon makePartSymbolIcon(PartSymbol s, const QColor& ink) {
    switch(s){
    case PartSymbol::SchResistor:return makeCadActionIcon(QStringLiteral("resistor"),ink);
    case PartSymbol::SchResistorZigzag:return makeCadActionIcon(QStringLiteral("resistorzigzag"),ink);
    case PartSymbol::SchCapacitor:return makeCadActionIcon(QStringLiteral("capacitor"),ink);
    case PartSymbol::SchCoil:return makeCadActionIcon(QStringLiteral("coil"),ink);
    case PartSymbol::SchFerrite:return makeCadActionIcon(QStringLiteral("ferrite"),ink);
    default:break;
    }
    // Drawn from the part's own drawing rather than by hand: the icon is then the part, scaled to fit
    // the button, and a menu entry cannot come to show something other than what it places.
    const std::vector<Entity> ents = partSymbolEntities(s, Vec2{0.0, 0.0});
    Bounds b;
    for (const Entity& e : ents) b.expand(e.bounds());
    QPixmap pm(112, 112);pm.setDevicePixelRatio(4);
    pm.fill(Qt::transparent);
    if (!b.valid) return QIcon(pm);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const double w = std::max(1e-6, b.max.x - b.min.x), h = std::max(1e-6, b.max.y - b.min.y);
    const double k = std::min(24.0 / w, 24.0 / h);
    // A symbol with its leads is long and shallow - a resistor is 18 by 3 - and scaled to fit, evenly, it
    // comes out as a 2-pixel-tall squiggle on a line: the box reads as two dashes and the zigzag reads as
    // the same two dashes. So the HEIGHT is exaggerated, up to two and a half times, for a drawing flatter
    // than three to one. An icon is a pictogram of the thing, not a scale drawing of it; the drawing on the
    // sheet is untouched, and anything squarer than 2:1 is left exactly as it is.
    const double flat = w / h;
    const double ky = k * std::min(3.5, std::max(1.0, flat / 2.0));
    auto to = [&](const Vec2& v) {
        return QPointF(14.0 + (v.x - (b.min.x + b.max.x) / 2.0) * k,
                       14.0 - (v.y - (b.min.y + b.max.y) / 2.0) * ky);
    };
    for (const Entity& e : ents) {
        if (e.meta.value(QStringLiteral("lineType")).toString() == QLatin1String("dashed")) {
            QPen dash = pen;
            dash.setStyle(Qt::DashLine);
            p.setPen(dash);
        } else {
            p.setPen(pen);
        }
        switch (e.type) {
        case EntityType::Line:
            p.drawLine(to(e.a), to(e.b));
            break;
        case EntityType::Circle:
            // Round things stay round: the stretch is for the flat symbols, and a stretched circle in an
            // icon reads as a mistake rather than as emphasis.
            p.drawEllipse(to(e.center), e.radius * k, e.radius * k);
            break;
        case EntityType::Arc: {
            const double r = e.radius * k;
            const QPointF c = to(e.center);
            const QRectF box(c.x() - r, c.y() - r, 2 * r, 2 * r);
            const double from = e.startAngle * 180.0 / M_PI;
            double span = (e.endAngle - e.startAngle) * 180.0 / M_PI;
            while (span <= 0.0) span += 360.0;
            p.drawArc(box, static_cast<int>(from * 16), static_cast<int>(span * 16));
            break;
        }
        case EntityType::Polyline: {
            // A POLYLINE. Left out, and so drawn as nothing: the zigzag resistor came out as a bare pair of
            // leads with a gap between them, and the axial resistor's waisted body had been missing from its
            // icon since it was drawn. An icon made from the part's own drawing has to be able to draw all
            // of it, or it quietly shows something else.
            if (e.points.size() < 2) break;
            QPolygonF poly;
            for (const Vec2& v : e.points) poly << to(v);
            p.drawPolyline(poly);
            break;
        }
        case EntityType::Rect:
        case EntityType::Polygon: {
            // Both are their corners, and both close.
            if (e.points.size() < 3) break;
            QPolygonF poly;
            for (const Vec2& v : e.points) poly << to(v);
            p.drawPolygon(poly);
            break;
        }
        default:
            break;
        }
    }
    p.end();
    return QIcon(pm);
}

} // namespace Loopwire
