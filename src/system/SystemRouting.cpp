#include "system/SystemRouting.h"
#include "system/SystemExport.h"
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace Edraw_Desktop {
namespace {
bool segmentBox(const QPointF& a,const QPointF& b,const QRectF& box) {
    const QRectF r=box.adjusted(.01,.01,-.01,-.01);
    if(std::abs(a.y()-b.y())<.001)return a.y()>r.top()&&a.y()<r.bottom()&&std::max(a.x(),b.x())>r.left()&&std::min(a.x(),b.x())<r.right();
    if(std::abs(a.x()-b.x())<.001)return a.x()>r.left()&&a.x()<r.right()&&std::max(a.y(),b.y())>r.top()&&std::min(a.y(),b.y())<r.bottom();
    return false;
}
double distance(const QPointF& a,const QPointF& b){return std::abs(a.x()-b.x())+std::abs(a.y()-b.y());}
QPointF clearPoint(QPointF point,const std::vector<QRectF>& boxes) {
    for(int round=0;round<4;++round)for(const QRectF& box:boxes)if(box.contains(point)){
        const double left=std::abs(point.x()-box.left()),right=std::abs(point.x()-box.right());
        const double top=std::abs(point.y()-box.top()),bottom=std::abs(point.y()-box.bottom());
        const double least=std::min({left,right,top,bottom});
        if(least==left)point.setX(box.left()-.1);else if(least==right)point.setX(box.right()+.1);else if(least==top)point.setY(box.top()-.1);else point.setY(box.bottom()+.1);
    }
    return point;
}
std::vector<QPointF> route(const QPointF& start,const QPointF& end,const std::vector<QRectF>& boxes,const std::vector<std::vector<QPointF>>& others) {
    if(distance(start,end)<.001)return {start,end};
    std::vector<double> xs{start.x(),end.x()},ys{start.y(),end.y()};
    for(const QRectF& box:boxes){xs.push_back(box.left());xs.push_back(box.right());ys.push_back(box.top());ys.push_back(box.bottom());}
    const auto xRange=std::minmax_element(xs.begin(),xs.end()),yRange=std::minmax_element(ys.begin(),ys.end());
    const double left=*xRange.first-25,right=*xRange.second+25,top=*yRange.first-25,bottom=*yRange.second+25;
    xs.push_back(left);xs.push_back(right);ys.push_back(top);ys.push_back(bottom);
    auto unique=[](std::vector<double>& values){std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end(),[](double a,double b){return std::abs(a-b)<.001;}),values.end());};
    unique(xs);unique(ys);
    const int nx=static_cast<int>(xs.size()),ny=static_cast<int>(ys.size());
    if(static_cast<size_t>(nx)*ny>64000)return {start,{end.x(),start.y()},end};
    auto nearest=[](const std::vector<double>& values,double value){return static_cast<int>(std::min_element(values.begin(),values.end(),[&](double a,double b){return std::abs(a-value)<std::abs(b-value);})-values.begin());};
    const int sx=nearest(xs,start.x()),sy=nearest(ys,start.y()),ex=nearest(xs,end.x()),ey=nearest(ys,end.y());
    const int count=nx*ny*3,initial=(sy*nx+sx)*3+2;
    std::vector<double> cost(count,std::numeric_limits<double>::infinity());std::vector<int> previous(count,-1);
    using State=std::pair<double,int>;std::priority_queue<State,std::vector<State>,std::greater<State>> queue;
    cost[initial]=0;queue.push({distance(start,end),initial});int finish=-1;
    while(!queue.empty()){
        const auto [estimate,state]=queue.top();queue.pop();const int cell=state/3,dir=state%3,x=cell%nx,y=cell/nx;
        const QPointF at(xs[static_cast<size_t>(x)],ys[static_cast<size_t>(y)]);
        if(estimate>cost[state]+distance(at,end)+.001)continue;
        if(x==ex&&y==ey){finish=state;break;}
        for(const auto& step:{QPoint(1,0),QPoint(-1,0),QPoint(0,1),QPoint(0,-1)}){
            const int xx=x+step.x(),yy=y+step.y();if(xx<0||xx>=nx||yy<0||yy>=ny)continue;
            const QPointF next(xs[static_cast<size_t>(xx)],ys[static_cast<size_t>(yy)]);
            bool blocked=false;for(const QRectF& box:boxes)if(segmentBox(at,next,box)){blocked=true;break;}if(blocked)continue;
            double crossings=0;
            for(const auto& path:others)for(size_t i=1;i<path.size();++i){const QPointF a=path[i-1],b=path[i];
                if(std::abs(at.y()-next.y())<.001&&std::abs(a.x()-b.x())<.001&&a.x()>std::min(at.x(),next.x())+.01&&a.x()<std::max(at.x(),next.x())-.01&&at.y()>std::min(a.y(),b.y())+.01&&at.y()<std::max(a.y(),b.y())-.01)crossings+=80;
                if(std::abs(at.x()-next.x())<.001&&std::abs(a.y()-b.y())<.001&&a.y()>std::min(at.y(),next.y())+.01&&a.y()<std::max(at.y(),next.y())-.01&&at.x()>std::min(a.x(),b.x())+.01&&at.x()<std::max(a.x(),b.x())-.01)crossings+=80;
            }
            const int direction=step.x()?0:1,index=(yy*nx+xx)*3+direction;
            const double candidate=cost[state]+distance(at,next)+(dir<2&&dir!=direction?12:0)+crossings;
            if(candidate+.001<cost[index]){cost[index]=candidate;previous[index]=state;queue.push({candidate+distance(next,end),index});}
        }
    }
    if(finish<0)return {start,{end.x(),start.y()},end};
    std::vector<QPointF> path;for(int state=finish;state>=0;state=previous[state]){const int cell=state/3;path.push_back({xs[static_cast<size_t>(cell%nx)],ys[static_cast<size_t>(cell/nx)]});}
    std::reverse(path.begin(),path.end());return path;
}
}
bool systemPathCrossesBox(const std::vector<QPointF>& path,const QRectF& box){for(size_t i=1;i<path.size();++i)if(segmentBox(path[i-1],path[i],box))return true;return false;}
std::vector<QRectF> systemRouteObstacles(const SystemDesign& design,const SysCable& cable,double margin){
    std::vector<QRectF> boxes;
    for(const SysUnit& unit:design.units){
        if(unit.kind==QLatin1String("module")||design.isWithin(cable.aUnit,unit.id)||design.isWithin(cable.bUnit,unit.id))continue;
        if((unit.id==cable.aUnit&&cable.aInside)||(unit.id==cable.bUnit&&cable.bInside))continue;
        boxes.push_back(unit.rect().adjusted(-margin,-margin,margin,margin));
    }
    return boxes;
}
std::vector<QPointF> sysCablePath(const SystemDesign& design,const SysCable& cable,double end){
    const auto base=sysCableBasePath(design,cable,end);if(base.size()<4)return base;
    const auto boxes=systemRouteObstacles(design,cable);
    std::vector<std::vector<QPointF>> others;
    for(const SysCable& other:design.cables)if(other.id!=cable.id)others.push_back(sysCableBasePath(design,other,end));
    std::vector<QPointF> goals;for(const QPointF& point:cable.waypoints)goals.push_back(clearPoint(point,boxes));goals.push_back(base[base.size()-2]);
    std::vector<QPointF> result{base.front(),base[1]};QPointF from=base[1];
    for(const QPointF& goal:goals){const auto segment=route(from,goal,boxes,others);for(size_t i=1;i<segment.size();++i)result.push_back(segment[i]);from=goal;}
    result.push_back(base.back());
    // KEEP THE PORT LEADS, and remove redundant bends only between them.
    for(size_t i=2;i+2<result.size();){const QPointF a=result[i]-result[i-1],b=result[i+1]-result[i];
        if(std::abs(a.x()*b.y()-a.y()*b.x())<.001&&a.x()*b.x()+a.y()*b.y()>=0)result.erase(result.begin()+static_cast<ptrdiff_t>(i));else ++i;}
    return result;
}
void arrangeSystemPorts(SystemDesign& design,const QStringList& movedUnits){
    for(SysCable& cable:design.cables){
        cable.aInside=cable.aUnit==cable.bUnit||design.isWithin(cable.bUnit,cable.aUnit);
        cable.bInside=cable.aUnit==cable.bUnit||design.isWithin(cable.aUnit,cable.bUnit);
    }
    QSet<QString> affected;
    for(const SysUnit& unit:design.units)for(const QString& moved:movedUnits)if(unit.id==moved||design.isWithin(unit.id,moved))affected.insert(unit.id);
    const auto moved=affected;
    for(const SysCable& cable:design.cables)if(moved.contains(cable.aUnit)||moved.contains(cable.bUnit)){affected.insert(cable.aUnit);affected.insert(cable.bUnit);}
    struct Placement { SysPort* port;QString side;double at; };
    for(SysUnit& unit:design.units){if(!affected.contains(unit.id))continue;std::vector<Placement> placements;
        for(SysPort& port:unit.ports){
            const auto connected=design.cablesAt(unit.id,port.id);if(connected.empty())continue;
            QPointF goal;int count=0;bool outside=false;
            for(const SysCable* cable:connected){const bool first=cable->aUnit==unit.id&&cable->aPort==port.id;const bool internal=first?cable->aInside:cable->bInside;
                if(outside&&internal)continue;if(!internal&&!outside){goal={};count=0;outside=true;}
                const SysUnit* other=design.unit(first?cable->bUnit:cable->aUnit);const SysPort* otherPort=other?other->port(first?cable->bPort:cable->aPort):nullptr;
                if(!cable->waypoints.empty())goal+=first?cable->waypoints.front():cable->waypoints.back();
                else if(other&&otherPort)goal+=sysPortPoint(*other,*otherPort);else continue;++count;
            }
            if(!count)continue;goal/=count;double best=std::numeric_limits<double>::infinity();Placement selected{&port,port.side,port.at};
            for(const QString& side:{QStringLiteral("left"),QStringLiteral("right"),QStringLiteral("top"),QStringLiteral("bottom")}){
                SysPort candidate=port;candidate.side=side;
                candidate.at=std::clamp(side==QLatin1String("left")||side==QLatin1String("right")?(goal.y()-unit.y)/std::max(1.0,unit.h):(goal.x()-unit.x)/std::max(1.0,unit.w),.08,.92);
                const QPointF point=sysPortPoint(unit,candidate),lead=point+sysPortOut(candidate)*(outside?30:-30);
                double penalty=distance(point,goal)+(side==port.side?0:3);
                for(const SysUnit& obstacle:design.units)if(obstacle.id!=unit.id&&obstacle.kind!=QLatin1String("module")&&!design.isWithin(unit.id,obstacle.id)&&!design.isWithin(obstacle.id,unit.id))
                    if(obstacle.rect().adjusted(-8,-8,8,8).contains(point)||obstacle.rect().adjusted(-8,-8,8,8).contains(lead))penalty+=1000;
                if(penalty<best){best=penalty;selected={&port,side,candidate.at};}
            }
            placements.push_back(selected);
        }
        for(const QString& side:{QStringLiteral("left"),QStringLiteral("right"),QStringLiteral("top"),QStringLiteral("bottom")}){
            std::vector<Placement*> ports;for(Placement& placement:placements)if(placement.side==side)ports.push_back(&placement);
            std::sort(ports.begin(),ports.end(),[](const Placement* a,const Placement* b){return a->at<b->at;});
            const double extent=side==QLatin1String("left")||side==QLatin1String("right")?unit.h:unit.w;
            const double spacing=ports.size()>1?std::min(18/std::max(1.0,extent),.84/(ports.size()-1)):0;
            double previous=.08-spacing;
            for(Placement* placement:ports){placement->at=std::max(placement->at,previous+spacing);previous=placement->at;}
            double next=.92+spacing;for(auto i=ports.rbegin();i!=ports.rend();++i){(*i)->at=std::min((*i)->at,next-spacing);next=(*i)->at;}
        }
        for(const Placement& placement:placements){placement.port->side=placement.side;placement.port->at=std::clamp(placement.at,.05,.95);}
    }
}
}
