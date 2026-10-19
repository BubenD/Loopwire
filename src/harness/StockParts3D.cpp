#include "harness/StockParts3D.h"
#include "model/PartSymbols.h"
#include "render/Render3D.h"
#include <algorithm>
#include <cmath>

namespace Loopwire {
namespace {
constexpr double pi=3.14159265358979323846;
struct Frame {
    Vec2 origin, x{1,0}, y{0,1};double depth=1;
    V3 map(V3 v) const {const Vec2 p=origin+x*v.x+y*v.y;return {p.x,p.y,v.z*depth};}
};
std::vector<Vec2> landmarks(const Entity& e) {
    if(e.type==EntityType::Line)return {e.a,e.b};
    if(e.type==EntityType::Polyline)return e.points;
    return {e.center};
}
Frame frameOf(const std::vector<const Entity*>& actual,PartSymbol kind) {
    const auto reference=partSymbolEntities(kind,{0,0});
    std::vector<Vec2> p,q;
    for(size_t i=0;i<std::min(reference.size(),actual.size());++i) {
        if(reference[i].type!=actual[i]->type)continue;
        const auto a=landmarks(reference[i]),b=landmarks(*actual[i]);
        for(size_t k=0;k<std::min(a.size(),b.size());++k){p.push_back(a[k]);q.push_back(b[k]);}
    }
    Frame frame;if(p.empty())return frame;
    size_t b=0,c=0;double far=0,area=0;
    for(size_t i=1;i<p.size();++i)if((p[i]-p[0]).length()>far){far=(p[i]-p[0]).length();b=i;}
    auto cross=[](Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;};
    for(size_t i=1;i<p.size();++i)if(std::abs(cross(p[b]-p[0],p[i]-p[0]))>area){area=std::abs(cross(p[b]-p[0],p[i]-p[0]));c=i;}
    if(area>1e-8) {
        const Vec2 a=p[b]-p[0],d=p[c]-p[0],u=q[b]-q[0],v=q[c]-q[0];
        const double det=cross(a,d);
        frame.x=(u*d.y-v*a.y)/det;frame.y=(v*a.x-u*d.x)/det;
        frame.origin=q[0]-frame.x*p[0].x-frame.y*p[0].y;
        frame.depth=std::sqrt(std::abs(cross(frame.x,frame.y)));
    } else {
        // The top-view ferrite consists of concentric circles: orientation is immaterial.
        const double scale=reference.front().radius>1e-9 ? actual.front()->radius/reference.front().radius : 1;
        frame.x={scale,0};frame.y={0,scale};frame.depth=scale;frame.origin=q[0]-p[0]*scale;
    }
    return frame;
}
Mesh3D ring(V3 centre,V3 axis,double length,double inner,double outer) {
    axis=axis.normalized();V3 x=cross3(axis,{0,0,1}).normalized();if(x.length()<.5)x={1,0,0};
    const V3 y=cross3(axis,x);Mesh3D mesh;constexpr int sides=40;
    for(int end=0;end<2;++end)for(int skin=0;skin<2;++skin)for(int k=0;k<sides;++k) {
        const double a=2*pi*k/sides,r=skin?outer:inner;
        mesh.v.push_back(centre+axis*((end-.5)*length)+(x*std::cos(a)+y*std::sin(a))*r);
    }
    for(int k=0;k<sides;++k){const int n=(k+1)%sides;
        for(int skin=0;skin<2;++skin){const int a=skin*sides+k,b=skin*sides+n,c=a+2*sides,d=b+2*sides;
            if(skin){mesh.addTri(a,b,c);mesh.addTri(b,d,c);}else{mesh.addTri(a,c,b);mesh.addTri(b,c,d);}}
        for(int end=0;end<2;++end){const int a=end*2*sides+k,b=end*2*sides+n,c=a+sides,d=b+sides;
            if(end){mesh.addTri(a,c,b);mesh.addTri(b,c,d);}else{mesh.addTri(a,b,c);mesh.addTri(b,d,c);}}
    }return mesh;
}
}
void appendStockParts3D(Harness3DScene& scene,const std::vector<Entity>& drawing) {
    QHash<QString,std::vector<const Entity*>> groups;QStringList order;
    for(const auto& e:drawing) {
        const auto instance=e.meta.value(kPartSymbolInstKey).toString();
        if(instance.isEmpty() || e.meta.value(kPartSymbolKey).toString().isEmpty())continue;
        if(!groups.contains(instance))order<<instance;groups[instance].push_back(&e);
    }
    for(const auto& key:order) {
        const auto& group=groups[key];const auto name=group.front()->meta.value(kPartSymbolKey).toString();
        auto kinds=mechanicalPartSymbols();const auto found=std::find_if(kinds.begin(),kinds.end(),[&](PartSymbol p){return partSymbolName(p)==name;});
        if(found==kinds.end())continue;
        const auto kind=*found;const Frame frame=frameOf(group,kind);++scene.parts;
        const QString brick=harness3DPartKey(*group.front());
        auto add=[&](Mesh3D mesh,QColor colour,int texture=Texture3D::Plastic,double alpha=1.0) {
            for(auto& v:mesh.v)v=frame.map(v);
            if(cross(frame.x,frame.y)<0)for(auto& face:mesh.f)std::swap(face[1],face[2]);
            Harness3DItem item;item.mesh=std::move(mesh);item.color=colour;item.what=name;item.brick=brick;
            item.texture=texture;item.ownLook=true;item.alpha=alpha;scene.items.push_back(std::move(item));
        };
        const QColor metal(185,190,198),dark(45,46,50),copper(190,110,48);
        auto lead=[&](V3 a,V3 b,double r=.22){add(tubeMesh({a,b},r,12,true),metal,Texture3D::Metal);};
        switch(kind) {
        case PartSymbol::ThResistor: {
            auto radius=[](double t){return 1.0+.3*std::pow((t-4.8)/4.8,2);};
            std::vector<V3> body;for(int k=0;k<=32;++k)body.push_back({-4.8+9.6*k/32,0,0});
            add(tubeMeshVar(body,radius,32,true),QColor(194,161,112),Texture3D::Matte);
            for(double x:{-1.5,-.6,.3,1.9})add(tubeMesh({{x-.13,0,0},{x+.13,0,0}},radius(x+4.8)+.025,32,true),x==1.9?QColor(190,155,65):QColor(82,54,35),Texture3D::Matte);
            lead({-9.3,0,0},{-4.8,0,0});lead({4.8,0,0},{9.3,0,0});break;
        }
        case PartSymbol::ThCapacitor:
            add(tubeMesh({{0,-5,0},{0,5,0}},4,40,true),QColor(43,58,74));
            add(tubeMesh({{0,4.9,0},{0,5,0}},3.7,40,true),metal,Texture3D::Metal);
            add(boxMesh({1.6,-4,3.1},{2.3,4,3.5}),QColor(190,193,188));
            lead({-1.75,-5,0},{-1.75,-10,0});lead({1.75,-5,0},{1.75,-9,0});break;
        case PartSymbol::ThCoil: {
            add(tubeMesh({{-4.8,0,0},{4.8,0,0}},2.3,32,true),dark);
            std::vector<V3> coil;for(int k=0;k<=280;++k){double t=k/280.0,a=14*pi*t;coil.push_back({-4.7+9.4*t,2.4*std::cos(a),2.4*std::sin(a)});}
            add(tubeMesh(coil,.22,8,true),copper,Texture3D::Metal);
            lead({-2.4,-2.6,0},{-2.4,-7.6,0});lead({2.4,-2.6,0},{2.4,-7.6,0});break;
        }
        case PartSymbol::SmtResistor:
        case PartSymbol::SmtCapacitor: {
            const double h=kind==PartSymbol::SmtResistor?.4:.7;
            add(boxMesh({-1.6,-.8,-h},{1.6,.8,h}),kind==PartSymbol::SmtResistor?dark:QColor(185,151,95));
            add(boxMesh({-1.61,-.81,-h-.01},{-1,.81,h+.01}),metal,Texture3D::Metal);
            add(boxMesh({1,-.81,-h-.01},{1.61,.81,h+.01}),metal,Texture3D::Metal);break;
        }
        case PartSymbol::SmtCoil:
            add(boxMesh({-2,-2,-1.2},{2,2,1.2}),dark);
            add(tubeMesh({{0,0,1.2},{0,0,1.3}},1.3,32,true),QColor(76,77,80));
            add(boxMesh({-2.01,-2,-1.21},{-1.1,2,-.8}),metal,Texture3D::Metal);
            add(boxMesh({1.1,-2,-1.21},{2.01,2,-.8}),metal,Texture3D::Metal);break;
        case PartSymbol::FerriteRingSide:add(ring({0,0,0},{0,1,0},5,3,6),dark,Texture3D::Matte);break;
        case PartSymbol::FerriteRingTop:add(ring({0,0,0},{0,0,1},5,3,6),dark,Texture3D::Matte);break;
        case PartSymbol::Splice:
        case PartSymbol::SpliceHeatshrink:
            add(tubeMesh({{-4,0,0},{4,0,0}},1.2,32,true),metal,Texture3D::Metal);
            lead({-10,0,0},{-4,0,0},.3);lead({4,0,0},{10,0,0},.3);
            if(kind==PartSymbol::SpliceHeatshrink) {
                std::vector<V3> path{{-8,0,0},{-7,0,0},{7,0,0},{8,0,0}};
                add(tubeMeshVar(path,[](double t){return t<1?.55+1.15*t:t>15?.55+1.15*(16-t):1.7;},32,true),QColor(36,37,40),Texture3D::Matte);
            }break;
        case PartSymbol::ThLed: {
            lead({-1.27,-4,0},{-1.27,-9,0});lead({1.27,-4,0},{1.27,-7.5,0});
            add(boxMesh({.4,-3,-.3},{1,0.4,.3}),metal,Texture3D::Metal);
            std::vector<V3> dome{{0,-3,0},{0,2,0}};for(int k=1;k<=20;++k)dome.push_back({0,2+2.5*k/20.0,0});
            add(tubeMeshVar(dome,[](double t){return t<=5?2.5:std::sqrt(std::max(0.0001,6.25-(t-5)*(t-5)));},40,true),QColor(220,45,35),Texture3D::Plastic,.78);
            add(tubeMesh({{0,-4,0},{0,-3,0}},2.9,40,true),QColor(175,35,25));break;
        }
        default:break;
        }
    }
}

void appendLacingTies3D(Harness3DScene& scene,const std::vector<Entity>& drawing) {
    for(const auto& mask:drawing) {
        if(mask.type!=EntityType::Wipeout || !mask.meta.value("lacingTie").toBool() || mask.points.size()<4)continue;
        const Vec2 centre=(mask.points[0]+mask.points[2])*.5,axis=(mask.points[1]-mask.points[0]).normalized(),across=axis.perp();
        const double half=distance(mask.points[1],mask.points[2])*.5,band=distance(mask.points[0],mask.points[1])*.5;
        const double cord=std::clamp(band/1.7*.35,.2,.65);
        std::vector<Vec2> section;
        // Intersect the actual wire, sleeve and part meshes with the tie's plane.
        // A cross-section hull encloses several wires without running through them.
        for(const auto& item:scene.items) {
            if(item.alpha<.5 || item.what==QLatin1String("Lacing tie") || !item.surfaceText.isEmpty())continue;
            for(const auto& face:item.mesh.f)for(int edge=0;edge<3;++edge) {
                const auto a=item.mesh.v[size_t(face[size_t(edge)])],b=item.mesh.v[size_t(face[size_t((edge+1)%3)])];
                const double sa=dot(Vec2{a.x,a.y}-centre,axis),sb=dot(Vec2{b.x,b.y}-centre,axis);
                if((sa>0 && sb>0)||(sa<0 && sb<0))continue;
                const double t=std::abs(sa-sb)>1e-10?sa/(sa-sb):0;
                const V3 p=a+(b-a)*t;const double u=dot(Vec2{p.x,p.y}-centre,across);
                if(std::abs(u)<=half+cord)section.push_back({u,p.z});
            }
        }
        // Legacy drawings may contain only line strokes: still form a closed loop.
        if(section.size()<3)for(int k=0;k<32;++k){double a=2*pi*k/32;section.push_back({std::max(.4,half-1.5)*std::cos(a),std::max(.4,half-1.5)*std::sin(a)});}
        std::sort(section.begin(),section.end(),[](Vec2 a,Vec2 b){return a.x==b.x?a.y<b.y:a.x<b.x;});
        auto turn=[](Vec2 a,Vec2 b,Vec2 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
        std::vector<Vec2> hull;
        for(const auto p:section){while(hull.size()>1 && turn(hull[hull.size()-2],hull.back(),p)<=1e-9)hull.pop_back();hull.push_back(p);}
        const size_t lower=hull.size();
        for(auto i=section.rbegin()+1;i!=section.rend();++i){while(hull.size()>lower && turn(hull[hull.size()-2],hull.back(),*i)<=1e-9)hull.pop_back();hull.push_back(*i);}
        if(hull.size()>1)hull.pop_back();if(hull.size()<3)continue;
        std::vector<Vec2> loop;
        for(size_t k=0;k<hull.size();++k) {
            const Vec2 before=(hull[k]-hull[(k+hull.size()-1)%hull.size()]).normalized(),after=(hull[(k+1)%hull.size()]-hull[k]).normalized();
            double a=std::atan2(-before.x,before.y),b=std::atan2(-after.x,after.y);while(b<a)b+=2*pi;
            const int steps=std::max(1,int(std::ceil((b-a)/(.15))));
            for(int j=0;j<=steps;++j){double angle=a+(b-a)*j/steps;loop.push_back(hull[k]+Vec2{std::cos(angle),std::sin(angle)}*(cord+.08));}
        }
        auto point=[&](Vec2 p,double s){const Vec2 xy=centre+axis*s+across*p.x;return V3{xy.x,xy.y,p.y};};
        std::vector<V3> path;const int turns=2;
        for(int k=0;k<=turns*int(loop.size());++k){const double t=double(k)/(turns*loop.size());path.push_back(point(loop[size_t(k)%loop.size()],band*(2*t-1)));}
        Mesh3D mesh=tubeMesh(path,cord,10,true);
        const Vec2 top=*std::max_element(loop.begin(),loop.end(),[](Vec2 a,Vec2 b){return a.y<b.y;});
        mesh.append(tubeMesh({point(top,-band),point(top+Vec2{cord,cord},0),point(top,band)},cord*1.25,10,true));
        for(double side:{-1.,1.})mesh.append(tubeMesh({point(top,0),point(top+Vec2{side*cord*2,cord*1.5},side*band*1.5),point(top+Vec2{side*cord*3,cord},side*band*2)},cord,10,true));
        Harness3DItem item;item.mesh=std::move(mesh);item.smoothContours=true;item.color=QColor(225,215,185);item.ownLook=true;
        item.what=QStringLiteral("Lacing tie");item.brick=QStringLiteral("drawing:special:")+mask.id;scene.items.push_back(std::move(item));
    }
}
std::vector<RoutedFitting3D> routedStockAndTies3D(const Harness3DScene& scene,const Route3D& route,const Route3D& original) {
    std::vector<RoutedFitting3D> result;
    if(std::none_of(route.segments.begin(),route.segments.end(),[](const auto& s){return !s.designCable.isEmpty();}))return result;
    const auto kinds=mechanicalPartSymbols();
    for(const auto& item:scene.items) {
        const bool stock=std::any_of(kinds.begin(),kinds.end(),[&](auto kind){return item.what==partSymbolName(kind);});
        const bool tie=item.what==QLatin1String("Lacing tie");
        if(!stock && !tie)continue;
        // Connected parts are already retained by the connector-node transform.
        if(stock){if(std::none_of(route.nodes.begin(),route.nodes.end(),[&](const auto& n){return n.brick==item.brick;}))result.push_back({item,{}});continue;}
        V3 lo,hi;if(!item.mesh.bounds(lo,hi))continue;
        const Vec2 centre{(lo.x+hi.x)*.5,(lo.y+hi.y)*.5};
        const RouteSegment3D* chosen=nullptr;std::vector<V3> source;double nearest=1e300;
        for(const auto& s:original.segments) {
            if(s.designCable.isEmpty() || !route.segment(s.id))continue;
            auto path=original.corners(s);for(auto& p:path)p.z=0;
            for(size_t i=1;i<path.size();++i){const V3 d=path[i]-path[i-1];const double t=std::clamp(dot3(V3{centre.x,centre.y,0}-path[i-1],d)/std::max(1e-12,dot3(d,d)),0.,1.);
                const double gap=(V3{centre.x,centre.y,0}-(path[i-1]+d*t)).length();
                if(gap<nearest){nearest=gap;chosen=&s;source=path;}}
        }
        if(!chosen || nearest>std::max(10.,std::hypot(hi.x-lo.x,hi.y-lo.y))) {result.push_back({item,{}});continue;}
        const auto destination=route.centreline(*route.segment(chosen->id));
        if(destination.size()<2)continue;
        double sourceLength=0,destLength=0;
        for(size_t i=1;i<source.size();++i)sourceLength+=(source[i]-source[i-1]).length();
        for(size_t i=1;i<destination.size();++i)destLength+=(destination[i]-destination[i-1]).length();
        Harness3DItem fitted=item;
        for(auto& vertex:fitted.mesh.v) {
            double best=1e300,along=0,acc=0,offset=0;
            for(size_t i=1;i<source.size();++i) {
                const auto delta=source[i]-source[i-1];const double length=delta.length();if(length<1e-9)continue;
                const double t=std::clamp(dot3(V3{vertex.x,vertex.y,0}-source[i-1],delta)/(length*length),0.,1.);
                const auto foot=source[i-1]+delta*t;const double gap=(V3{vertex.x,vertex.y,0}-foot).length();
                if(gap<best){best=gap;along=acc+length*t;offset=dot3(V3{vertex.x,vertex.y,0}-foot,V3{-delta.y,delta.x,0}/length);}acc+=length;
            }
            const double target=sourceLength>1e-9?along/sourceLength*destLength:0;acc=0;
            for(size_t i=1;i<destination.size();++i) {
                const auto delta=destination[i]-destination[i-1];const double length=delta.length();if(length<1e-9)continue;
                if(acc+length>=target || i+1==destination.size()) {
                    const auto tangent=delta/length;V3 across=cross3({0,0,1},tangent).normalized();if(across.length()<.5)across=cross3({1,0,0},tangent).normalized();
                    const auto up=cross3(tangent,across).normalized();
                    vertex=destination[i-1]+delta*std::clamp((target-acc)/length,0.,1.)+across*offset+up*vertex.z;break;
                }acc+=length;
            }
        }
        result.push_back({std::move(fitted),chosen->id});
    }
    return result;
}

}
