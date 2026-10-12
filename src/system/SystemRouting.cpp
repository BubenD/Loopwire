#include "system/SystemRouting.h"
#include "system/SystemExport.h"
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace Loopwire {
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
bool systemSegmentsCross(const QPointF& a,const QPointF& b,const QPointF& c,const QPointF& d){
    const QPointF r=b-a,s=d-c;const double den=r.x()*s.y()-r.y()*s.x();
    if(std::abs(den)<1e-9)return false;
    const QPointF q=c-a;const double t=(q.x()*s.y()-q.y()*s.x())/den,u=(q.x()*r.y()-q.y()*r.x())/den;
    return t>1e-6&&t<1-1e-6&&u>1e-6&&u<1-1e-6;
}

std::vector<QPointF> systemStraightened(const std::vector<QPointF>& line){
    // A POINT WHERE THE LINE GOES ON STRAIGHT is no corner: taken out, so a crossing there is seen as one.
    std::vector<QPointF> out;
    for(const QPointF& p:line){
        if(!out.empty()&&distance(out.back(),p)<.001)continue;
        if(out.size()>=2){const QPointF a=out[out.size()-1]-out[out.size()-2],b=p-out.back();
            if(std::abs(a.x()*b.y()-a.y()*b.x())<.001){out.back()=p;continue;}}
        out.push_back(p);
    }
    return out;
}

int systemCrossings(const SystemDesign& design,const QStringList& only){
    struct Line{QString cable;std::vector<QPointF> points;};
    std::vector<Line> lines;
    for(const SysCable& cable:design.cables){
        lines.push_back({cable.id,systemStraightened(sysCablePath(design,cable))});
        for(const SysBranch& branch:cable.branches)lines.push_back({cable.id,systemStraightened(sysBranchPath(design,cable,branch))});
    }
    int count=0;
    for(size_t i=0;i<lines.size();++i)for(size_t j=i+1;j<lines.size();++j){
        if(lines[i].cable==lines[j].cable)continue;
        if(!only.isEmpty()&&!only.contains(lines[i].cable)&&!only.contains(lines[j].cable))continue;
        const auto& p=lines[i].points;const auto& q=lines[j].points;
        for(size_t a=1;a<p.size();++a)for(size_t b=1;b<q.size();++b)if(systemSegmentsCross(p[a-1],p[a],q[b-1],q[b]))++count;
    }
    return count;
}

int autoAlignSystem(SystemDesign& design,const QStringList& units,const QStringList& cables,double reach){
    int changes=0;
    auto near=[&](double a,double b){return std::abs(a-b)>1e-6&&std::abs(a-b)<reach;};
    // UNITS ON THE GRID: their corners and sizes to the nearest ten.
    for(SysUnit& unit:design.units){
        if(!units.isEmpty()&&!units.contains(unit.id))continue;
        const double x=std::round(unit.x/10.0)*10.0,y=std::round(unit.y/10.0)*10.0,w=std::max(60.0,std::round(unit.w/10.0)*10.0),h=std::max(40.0,std::round(unit.h/10.0)*10.0);
        if(x!=unit.x||y!=unit.y||w!=unit.w||h!=unit.h){unit.x=x;unit.y=y;unit.w=w;unit.h=h;++changes;}
    }
    for(SysCable& cable:design.cables){
        if(!cables.isEmpty()&&!cables.contains(cable.id))continue;
        SysUnit* ua=design.unit(cable.aUnit);SysUnit* ub=design.unit(cable.bUnit);
        SysPort* pa=ua?ua->port(cable.aPort):nullptr;SysPort* pb=ub?ub->port(cable.bPort):nullptr;
        if(!pa||!pb)continue;
        // A HARNESS ALMOST STRAIGHT ACROSS: its far port slid along its side so it is straight.
        const QPointF a=sysPortPoint(*ua,*pa),b=sysPortPoint(*ub,*pb);
        const bool acrossX=(pa->side==QLatin1String("left")||pa->side==QLatin1String("right"))&&(pb->side==QLatin1String("left")||pb->side==QLatin1String("right"));
        const bool acrossY=(pa->side==QLatin1String("top")||pa->side==QLatin1String("bottom"))&&(pb->side==QLatin1String("top")||pb->side==QLatin1String("bottom"));
        if(cable.waypoints.empty()&&acrossX&&near(a.y(),b.y())){
            const double at=(a.y()-ub->y)/std::max(1.0,ub->h);if(at>0.04&&at<0.96){pb->at=at;++changes;}
        }else if(cable.waypoints.empty()&&acrossY&&near(a.x(),b.x())){
            const double at=(a.x()-ub->x)/std::max(1.0,ub->w);if(at>0.04&&at<0.96){pb->at=at;++changes;}
        }
        // ITS DRAGGED BENDS: level with a port's lead where they nearly are, and one in line with the one before where it
        // nearly is - small steps and stubs taken out.
        if(!cable.waypoints.empty()){
            const QPointF da=sysPortOut(*pa)*(cable.aInside?-1.0:1.0),db=sysPortOut(*pb)*(cable.bInside?-1.0:1.0);
            const QPointF leadA=sysPortPoint(*ua,*pa)+da*30.0,leadB=sysPortPoint(*ub,*pb)+db*30.0;
            for(QPointF& w:cable.waypoints){
                for(const QPointF& lead:{leadA,leadB}){
                    if(near(w.x(),lead.x())){w.setX(lead.x());++changes;}
                    if(near(w.y(),lead.y())){w.setY(lead.y());++changes;}
                }
            }
            for(size_t i=1;i<cable.waypoints.size();++i){
                QPointF& w=cable.waypoints[i];const QPointF& v=cable.waypoints[i-1];
                if(near(w.x(),v.x())){w.setX(v.x());++changes;}
                if(near(w.y(),v.y())){w.setY(v.y());++changes;}
            }
        }
    }
    return changes;
}

double systemHarnessLength(const SystemDesign& design,const QStringList& only){
    double length=0;
    auto add=[&](const std::vector<QPointF>& line){for(size_t i=1;i<line.size();++i)length+=distance(line[i-1],line[i]);};
    for(const SysCable& cable:design.cables){
        if(!only.isEmpty()&&!only.contains(cable.id))continue;
        add(sysCablePath(design,cable));
        for(const SysBranch& branch:cable.branches)add(sysBranchPath(design,cable,branch));
    }
    return length;
}

int minimizeSystemCrossings(SystemDesign& design,const QStringList& cables,double* shorter){
    QSet<QString> chosen;
    for(const SysCable& cable:design.cables)if(cables.isEmpty()||cables.contains(cable.id))chosen.insert(cable.id);
    const int before=systemCrossings(design,cables);
    const double longBefore=systemHarnessLength(design,cables);
    // A CROSSING COUNTS FOR MORE THAN ANY LENGTH: fewer crossings first, then the shorter way.
    auto score=[&](int crossings,double length){return crossings*1e7+length;};
    // WHERE A HARNESS GOES from one of its ends: the port at its other end, its first bend, or - for a branch - the split.
    auto farFrom=[&](const SysCable& cable,const QString& unit,const QString& port,QPointF& far){
        auto portPoint=[&](const QString& u,const QString& p){const SysUnit* su=design.unit(u);const SysPort* sp=su?su->port(p):nullptr;
            if(!sp)return false;far=sysPortPoint(*su,*sp);return true;};
        const auto bends=sysCableWaypoints(design,cable);
        if(cable.aUnit==unit&&cable.aPort==port){if(!bends.empty()){far=bends.front();return true;}return portPoint(cable.bUnit,cable.bPort);}
        if(cable.bUnit==unit&&cable.bPort==port){if(!bends.empty()){far=bends.back();return true;}return portPoint(cable.aUnit,cable.aPort);}
        for(const SysBranch& branch:cable.branches)if(branch.unit==unit&&branch.port==port){
            const auto limb=sysBranchPath(design,cable,branch);if(limb.empty())return false;far=limb.front();return true;}
        return false;
    };
    SystemDesign best=design;int fewest=before;double shortest=longBefore;
    // TWO STARTS: the harnesses as they are routed, and re-routed - their dragged legs let go - each then reordered.
    SystemDesign rerouted=design;
    for(SysCable& cable:rerouted.cables)if(chosen.contains(cable.id))cable.waypoints.clear();
    const SystemDesign original=design;
    for(int start=0;start<2;++start){
    design=start==0?original:rerouted;
    if(start==1){const int now=systemCrossings(design,cables);const double length=systemHarnessLength(design,cables);
        if(score(now,length)<score(fewest,shortest)-0.5){fewest=now;shortest=length;best=design;}}
    for(int pass=0;pass<4;++pass){
        bool changed=false;
        for(SysUnit& unit:design.units)for(const QString& side:{QStringLiteral("left"),QStringLiteral("right"),QStringLiteral("top"),QStringLiteral("bottom")}){
            const bool across=side==QLatin1String("left")||side==QLatin1String("right");
            struct End{SysPort* port;double key;};std::vector<End> ends;
            for(SysPort& port:unit.ports){
                if(port.side!=side)continue;double sum=0;int n=0;
                for(const SysCable* cable:design.cablesAt(unit.id,port.id)){
                    if(!chosen.contains(cable->id))continue;QPointF far;
                    if(farFrom(*cable,unit.id,port.id,far)){sum+=across?far.y():far.x();++n;}
                }
                if(n)ends.push_back({&port,sum/n});
            }
            if(ends.size()<2)continue;
            // THE SAME PLACES ON THE SIDE, given out again in the order the harnesses go.
            std::vector<double> places;for(const End& end:ends)places.push_back(end.port->at);
            std::sort(places.begin(),places.end());
            std::stable_sort(ends.begin(),ends.end(),[](const End& a,const End& b){return a.key<b.key;});
            for(size_t i=0;i<ends.size();++i)if(std::abs(ends[i].port->at-places[i])>1e-9){ends[i].port->at=places[i];changed=true;}
        }
        if(!changed)break;
        const int now=systemCrossings(design,cables);const double length=systemHarnessLength(design,cables);
        if(score(now,length)<score(fewest,shortest)-0.5){fewest=now;shortest=length;best=design;}
    }
    }
    design=best;
    if(shorter)*shorter=longBefore-shortest;
    return before-fewest;
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
    std::vector<QPointF> goals;for(const QPointF& point:sysCableWaypoints(design,cable,end))goals.push_back(clearPoint(point,boxes));goals.push_back(base[base.size()-2]);
    std::vector<QPointF> result{base.front(),base[1]};QPointF from=base[1];
    for(const QPointF& goal:goals){const auto segment=route(from,goal,boxes,others);for(size_t i=1;i<segment.size();++i)result.push_back(segment[i]);from=goal;}
    result.push_back(base.back());
    // KEEP THE PORT LEADS, and remove redundant bends only between them - a point where the line goes on straight, and one
    // where it runs on and comes back the same way (a stub sticking out past a corner).
    for(size_t i=2;i+2<result.size();){const QPointF a=result[i]-result[i-1],b=result[i+1]-result[i];
        if(std::abs(a.x()*b.y()-a.y()*b.x())<.001)result.erase(result.begin()+static_cast<ptrdiff_t>(i));else ++i;}
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
