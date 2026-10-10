#pragma once
#include "system/SystemDesign.h"
#include <QPointF>
#include <vector>
namespace Edraw_Desktop {
// PORTS FOLLOW THEIR CONNECTIONS after a unit or a cable route is moved, keeping port identities intact.
void arrangeSystemPorts(SystemDesign& design,const QStringList& movedUnits);
bool systemPathCrossesBox(const std::vector<QPointF>& path,const QRectF& box);
std::vector<QRectF> systemRouteObstacles(const SystemDesign& design,const SysCable& cable,double margin=10.0);
}
