#pragma once
#include "geometry/Mesh3D.h"
#include "geometry/Vec2.h"
#include <QHash>
#include <QSet>
#include <algorithm>
#include <array>
#include <map>

namespace Loopwire {
// Orthographic hidden-line removal. Clip silhouette and visible feature edges against the depth planes
// of opaque triangles. Export linework, never the tessellation of a curved surface.
inline std::vector<std::pair<Vec2,Vec2>> visibleOutlines3D(
    const std::vector<std::pair<const Mesh3D*,bool>>& meshes, int plane,
    const QSet<const Mesh3D*>& smoothMeshes = {}, V3 right = {}, V3 up = {},
    const std::function<bool(int)>& progress = {}) {
    int reported=-1;
    auto report=[&](int percent){
        if(!progress || percent==reported)return true;
        reported=percent;return progress(percent);
    };
    if(!report(0))return {};
    size_t totalFaces=0,gridFaces=0,finishedFaces=0;
    for(auto [mesh,opaque]:meshes)totalFaces+=mesh->f.size();
    totalFaces=std::max(size_t(1),totalFaces);
    const V3 view=plane==3 ? cross3(right,up).normalized() : plane==0 ? V3{0,0,1} : plane==1 ? V3{0,-1,0} : V3{1,0,0};
    auto project=[=](V3 p){return plane==3 ? Vec2{dot3(p,right),dot3(p,up)} : plane==0 ? Vec2{p.x,p.y} : plane==1 ? Vec2{p.x,p.z} : Vec2{p.y,p.z};};
    auto cross=[](Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;};
    struct Triangle { std::array<Vec2,3> p; std::array<double,3> z; double det, minX, maxX, minY, maxY, maxZ; };
    std::vector<Triangle> triangles;
    Vec2 lo{1e300,1e300},hi{-1e300,-1e300};
    for(auto [mesh,opaque]:meshes) for(const V3& p:mesh->v) {
        auto q=project(p);lo.x=std::min(lo.x,q.x);lo.y=std::min(lo.y,q.y);hi.x=std::max(hi.x,q.x);hi.y=std::max(hi.y,q.y);
    }
    if(meshes.empty()){report(100);return {};}
    // Independent cell dimensions keep a long, thin harness from putting all
    // its triangles into one row. Bins are read by reference, never copied.
    const double cellX=std::max(hi.x-lo.x,1e-6)/64.0,cellY=std::max(hi.y-lo.y,1e-6)/64.0;
    auto ix=[&](double x){return std::clamp(int((x-lo.x)/cellX),0,64);};
    auto iy=[&](double y){return std::clamp(int((y-lo.y)/cellY),0,64);};
    std::vector<std::vector<int>> grid(65*65);
    for(auto [mesh,opaque]:meshes) if(opaque) for(const auto& face:mesh->f) {
        if((gridFaces++%256)==0 && !report(int(20*gridFaces/totalFaces)))return {};
        Triangle t;
        for(int i=0;i<3;++i){const V3 p=mesh->v[size_t(face[i])];t.p[i]=project(p);t.z[i]=dot3(p,view);}
        t.det=cross(t.p[1]-t.p[0],t.p[2]-t.p[0]);if(std::abs(t.det)<1e-12)continue;
        t.minX=std::min({t.p[0].x,t.p[1].x,t.p[2].x});t.maxX=std::max({t.p[0].x,t.p[1].x,t.p[2].x});
        t.minY=std::min({t.p[0].y,t.p[1].y,t.p[2].y});t.maxY=std::max({t.p[0].y,t.p[1].y,t.p[2].y});
        t.maxZ=std::max({t.z[0],t.z[1],t.z[2]});
        const int index=int(triangles.size());triangles.push_back(t);
        for(int y=iy(t.minY);y<=iy(t.maxY);++y)for(int x=ix(t.minX);x<=ix(t.maxX);++x)grid[y*65+x].push_back(index);
    }
    std::vector<std::pair<Vec2,Vec2>> result;
    std::vector<unsigned> seen(triangles.size(),0);unsigned stamp=0;
    std::vector<int> candidates;
    if(!report(20))return {};
    for(auto [mesh,opaque]:meshes) {
        // Imported meshes can duplicate each triangle's vertices. Weld positions
        // for adjacency, so their internal seams are never exported as outlines.
        std::map<std::array<long long,3>,int> welded;
        std::vector<V3> points;std::vector<int> ids;
        for(V3 p:mesh->v) {
            const std::array<long long,3> key{std::llround(p.x*1e7),std::llround(p.y*1e7),std::llround(p.z*1e7)};
            auto [it,inserted]=welded.emplace(key,int(points.size()));if(inserted)points.push_back(p);ids.push_back(it->second);
        }
        std::map<std::pair<int,int>,std::vector<V3>> edges;
        for(const auto& face:mesh->f){
            const V3 normal=cross3(mesh->v[size_t(face[1])]-mesh->v[size_t(face[0])],mesh->v[size_t(face[2])]-mesh->v[size_t(face[0])]).normalized();
            for(int i=0;i<3;++i)edges[std::minmax(ids[size_t(face[i])],ids[size_t(face[(i+1)%3])])].push_back(normal);
        }
        // On a turned connector, cylinder facet seams all run in the same
        // direction. Profile rings run across it. Recognize that repeated axis
        // so shallow shoulders can be kept without exporting cylinder facets.
        struct Direction { V3 axis; double weight=0; int count=0; };
        std::map<std::array<int,3>,Direction> directions;
        double totalWeight=0;
        for(const auto& [edge,normals]:edges)if(normals.size()==2) {
            const double cosine=dot3(normals[0],normals[1]);
            if(cosine>=0.999390827 || cosine<0.8191520443)continue; // 2..35 degrees
            const V3 delta=points[size_t(edge.second)]-points[size_t(edge.first)];
            V3 axis=delta.normalized();
            if(axis.x<-1e-8 || (std::abs(axis.x)<1e-8 && (axis.y<-1e-8 || (std::abs(axis.y)<1e-8 && axis.z<0))))axis=axis*-1;
            const std::array<int,3> key{int(std::llround(axis.x*50)),int(std::llround(axis.y*50)),int(std::llround(axis.z*50))};
            auto& group=directions[key];group.axis=axis;group.weight+=dot3(delta,delta);++group.count;totalWeight+=dot3(delta,delta);
        }
        V3 turnedAxis;double largest=0;
        for(const auto& [key,group]:directions)if(group.count>=4 && group.weight>largest && group.weight>totalWeight*.2) {
            largest=group.weight;turnedAxis=group.axis;
        }
        size_t processedEdges=0;
        if(!report(20+int(75.0*(finishedFaces+mesh->f.size()*.2)/totalFaces)))return {};
        for(const auto& [edge,normals]:edges) {
            if((processedEdges++%64)==0 && !report(20+int(75.0*(finishedFaces+mesh->f.size()*(.2+.8*processedEdges/std::max(size_t(1),edges.size())))/totalFaces)))return {};
            bool silhouette=normals.size()==1;
            for(size_t i=1;i<normals.size();++i)
                silhouette |= (dot3(normals[0],view)>1e-8 && dot3(normals[i],view)<=1e-8) || (dot3(normals[i],view)>1e-8 && dot3(normals[0],view)<=1e-8);
            for(size_t i=1;i<normals.size();++i)
                silhouette |= (dot3(normals[0],view)<-1e-8 && dot3(normals[i],view)>=-1e-8) || (dot3(normals[i],view)<-1e-8 && dot3(normals[0],view)>=-1e-8);
            // Real corners and inner contours, without smooth tube facets or
            // the coplanar seams from triangulation.
            if(!smoothMeshes.contains(mesh))for(size_t i=1;i<normals.size();++i)
                silhouette |= dot3(normals[0],normals[i]) < 0.8191520443 ||
                    (turnedAxis.length()>0 && dot3(normals[0],normals[i])<0.999390827 &&
                     std::abs(dot3((points[size_t(edge.second)]-points[size_t(edge.first)]).normalized(),turnedAxis))<0.9);
            if(!silhouette)continue;
            const V3 pa=points[size_t(edge.first)],pb=points[size_t(edge.second)];
            const Vec2 a=project(pa),b=project(pb);if(distance(a,b)<1e-7)continue;
            const double za=dot3(pa,view),zb=dot3(pb,view);
            candidates.clear();if(++stamp==0){std::fill(seen.begin(),seen.end(),0);++stamp;}
            const double minX=std::min(a.x,b.x),maxX=std::max(a.x,b.x),minY=std::min(a.y,b.y),maxY=std::max(a.y,b.y);
            // Visit bins crossed by the edge, rather than the whole bounding box
            // of a long diagonal. A one-cell margin covers boundary roundoff.
            for(int y=iy(minY);y<=iy(maxY);++y) {
                double from=0,to=1;
                if(std::abs(b.y-a.y)>1e-12) {
                    double t0=(lo.y+y*cellY-a.y)/(b.y-a.y),t1=(lo.y+(y+1)*cellY-a.y)/(b.y-a.y);
                    if(t0>t1)std::swap(t0,t1);from=std::max(0.0,t0);to=std::min(1.0,t1);
                    if(to<from)continue;
                }
                const double x0=a.x+(b.x-a.x)*from,x1=a.x+(b.x-a.x)*to;
                for(int x=std::max(0,ix(std::min(x0,x1))-1);x<=std::min(64,ix(std::max(x0,x1))+1);++x)
                    for(int index:grid[y*65+x])if(seen[size_t(index)]!=stamp){seen[size_t(index)]=stamp;candidates.push_back(index);}
            }
            std::vector<std::pair<double,double>> hidden;
            for(int index:candidates) {
                const auto& t=triangles[size_t(index)];
                if(t.maxZ<=std::min(za,zb)+1e-5 || t.maxX<minX-1e-9 || t.minX>maxX+1e-9 || t.maxY<minY-1e-9 || t.minY>maxY+1e-9)continue;
                auto bary=[&](Vec2 p){const double v=cross(p-t.p[0],t.p[2]-t.p[0])/t.det,w=cross(t.p[1]-t.p[0],p-t.p[0])/t.det;return std::array<double,3>{1-v-w,v,w};};
                const auto wa=bary(a),wb=bary(b);double begin=0,end=1;
                auto clip=[&](double f0,double f1){
                    if(f0<0 && f1<0){end=-1;return;}
                    if(f0<0)begin=std::max(begin,f0/(f0-f1));
                    else if(f1<0)end=std::min(end,f0/(f0-f1));
                };
                for(int i=0;i<3;++i)clip(wa[i]+1e-10,wb[i]+1e-10);
                double da=-za-1e-5,db=-zb-1e-5;
                for(int i=0;i<3;++i){da+=wa[i]*t.z[i];db+=wb[i]*t.z[i];}
                clip(da,db);if(end>begin+1e-10)hidden.emplace_back(begin,end);
            }
            std::sort(hidden.begin(),hidden.end());double cursor=0;
            auto emitSpan=[&](double from,double to){if(to-from>1e-8)result.emplace_back(a+(b-a)*from,a+(b-a)*to);};
            for(auto [from,to]:hidden){emitSpan(cursor,from);cursor=std::max(cursor,to);}
            emitSpan(cursor,1);
        }
        finishedFaces+=mesh->f.size();
    }
    if(!report(95))return {};
    // Merge collinear fragments, including projected cap facets and duplicate
    // edges from adjoining meshes. This keeps long wire runs lightweight.
    struct Line { Vec2 direction,normal;double offset=0;std::vector<std::pair<double,double>> intervals; };
    QHash<QString,Line> grouped;
    for(auto [a,b]:result) {
        Vec2 direction=(b-a).normalized();if(direction.x<-1e-9 || (std::abs(direction.x)<1e-9 && direction.y<0))direction=direction*-1;
        const Vec2 normal=direction.perp();const double offset=dot(a,normal);
        const QString key=QString::number(std::llround(direction.x*1e8))+','+QString::number(std::llround(direction.y*1e8))+','+QString::number(std::llround(offset*1e5));
        auto& line=grouped[key];line.direction=direction;line.normal=normal;line.offset=offset;
        line.intervals.emplace_back(std::min(dot(a,direction),dot(b,direction)),std::max(dot(a,direction),dot(b,direction)));
    }
    result.clear();
    for(auto it=grouped.begin();it!=grouped.end();++it) {
        auto& line=it.value();std::sort(line.intervals.begin(),line.intervals.end());
        double begin=line.intervals.front().first,end=line.intervals.front().second;
        auto append=[&]{result.emplace_back(line.normal*line.offset+line.direction*begin,line.normal*line.offset+line.direction*end);};
        for(size_t i=1;i<line.intervals.size();++i) {
            auto [a,b]=line.intervals[i];if(a<=end+1e-5)end=std::max(end,b);else {append();begin=a;end=b;}
        }
        append();
    }
    if(!report(100))return {};
    return result;
}
} // namespace Loopwire
