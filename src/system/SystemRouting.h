#pragma once
#include "system/SystemDesign.h"
#include <QPointF>
#include <vector>
namespace Edraw_Desktop {
// PORTS FOLLOW THEIR CONNECTIONS after a unit or a cable route is moved, keeping port identities intact.
void arrangeSystemPorts(SystemDesign& design,const QStringList& movedUnits);
bool systemPathCrossesBox(const std::vector<QPointF>& path,const QRectF& box);
std::vector<QRectF> systemRouteObstacles(const SystemDesign& design,const SysCable& cable,double margin=10.0);
// CROSSINGS: how many times the harnesses' lines (their branches too) cross one another - those of the harnesses named, or
// of all of them where none is.
int systemCrossings(const SystemDesign& design,const QStringList& cables={});
// HOW LONG THE HARNESSES RUN on the diagram, branches too - those named, or all.
double systemHarnessLength(const SystemDesign& design,const QStringList& cables={});
// FEWER CROSSINGS: the ports at the harnesses' ends put in order along each side - in the order of where each one's harness
// goes - and kept only where that crosses less, or (crossing no more) runs shorter: a harness that went the long way round
// to keep from crossing straightened too. How many crossings went; `shorter`, if given, how much shorter they run.
int minimizeSystemCrossings(SystemDesign& design,const QStringList& cables={},double* shorter=nullptr);
// Whether two segments cross (not merely touch at an end).
bool systemSegmentsCross(const QPointF& a,const QPointF& b,const QPointF& c,const QPointF& d);
}
