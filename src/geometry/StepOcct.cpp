#include "geometry/StepOcct.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>

#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace Edraw_Desktop {

namespace {

QString labelName(const TDF_Label& label) {
    Handle(TDataStd_Name) name;
    if (label.IsNull() || !label.FindAttribute(TDataStd_Name::GetID(), name)) return QString();
    const TCollection_ExtendedString& s = name->Get();
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(s.ToExtString()), s.Length()).trimmed();
}

// THE TRIANGLES OF A SHAPE'S FACES, where its location puts them, each wound the way its face faces.
void appendTriangles(const TopoDS_Shape& shape, Mesh3D& m) {
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull() || tri->NbTriangles() == 0) continue;
        const gp_Trsf trsf = loc.Transformation();
        const bool flip = face.Orientation() == TopAbs_REVERSED;
        const int base = static_cast<int>(m.v.size());
        for (int i = 1; i <= tri->NbNodes(); ++i) {
            const gp_Pnt p = tri->Node(i).Transformed(trsf);
            m.addVertex({p.X(), p.Y(), p.Z()});
        }
        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            int a = 0, b = 0, c = 0;
            tri->Triangle(i).Get(a, b, c);
            if (flip) std::swap(b, c);
            m.addTri(base + a - 1, base + b - 1, base + c - 1);
        }
    }
}

} // namespace

bool readStepOcct(const QString& path, std::vector<EnvPart3D>& parts, QString* how) {
    // ONE AT A TIME: the document application is shared, and a part's model may be read while an environment loads.
    static std::mutex one;
    const std::lock_guard<std::mutex> hold(one);
    QElapsedTimer clock;
    clock.start();
    const QString file = QFileInfo(path).fileName();
    try {
        // Open CASCADE's messages are for its own console; a GUI has none.
        Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_Printer));
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        Handle(TDocStd_Document) doc;
        app->NewDocument(TCollection_ExtendedString("BinXCAF"), doc);
        // IN MILLIMETRES, whatever the file was written in - the drawing's unit.
        XCAFDoc_DocumentTool::SetLengthUnit(doc, 0.001);
        STEPCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        if (reader.ReadFile(QFileInfo(path).absoluteFilePath().toLocal8Bit().constData()) != IFSelect_RetDone) {
            if (how) *how = QStringLiteral("%1 is not a STEP file Open CASCADE can read").arg(file);
            app->Close(doc);
            return false;
        }
        if (!reader.Transfer(doc)) {
            if (how) *how = QStringLiteral("nothing in %1 could be turned into solids").arg(file);
            app->Close(doc);
            return false;
        }
        // TESSELLATED ONCE, the whole of it, finely for its size: a thousandth or so of its span.
        const Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        NCollection_Sequence<TDF_Label> roots;
        shapes->GetFreeShapes(roots);
        TopoDS_Compound all;
        BRep_Builder builder;
        builder.MakeCompound(all);
        for (int i = 1; i <= roots.Length(); ++i) builder.Add(all, XCAFDoc_ShapeTool::GetShape(roots.Value(i)));
        Bnd_Box box;
        BRepBndLib::Add(all, box);
        if (box.IsVoid()) {
            if (how) *how = QStringLiteral("%1 holds no shapes").arg(file);
            app->Close(doc);
            return false;
        }
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        const double span = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
        const double deflection = std::clamp(span * 0.0008, 0.005, 5.0);
        BRepMesh_IncrementalMesh mesher(all, deflection, false, 0.35, true);
        // EVERY SOLID WHERE THE ASSEMBLY PUTS IT, with its name and colour.
        size_t tris = 0;
        for (XCAFPrs_DocumentExplorer ex(doc, XCAFPrs_DocumentExplorerFlags_OnlyLeafNodes); ex.More(); ex.Next()) {
            const XCAFPrs_DocumentNode& node = ex.Current();
            if (!node.Style.IsVisible()) continue;
            TopoDS_Shape shape;
            if (!XCAFDoc_ShapeTool::GetShape(node.RefLabel, shape) || shape.IsNull()) continue;
            shape.Location(node.Location, false);
            EnvPart3D part;
            appendTriangles(shape, part.mesh);
            if (part.mesh.empty()) continue;
            part.name = labelName(node.Label);
            if (part.name.isEmpty()) part.name = labelName(node.RefLabel);
            if (node.Style.IsSetColorSurf()) {
                const Quantity_Color& c = node.Style.GetColorSurf();
                part.color = QColor::fromRgbF(static_cast<float>(c.Red()), static_cast<float>(c.Green()),
                                              static_cast<float>(c.Blue()));
            }
            tris += part.mesh.f.size();
            parts.push_back(std::move(part));
        }
        app->Close(doc);
        if (parts.empty()) {
            if (how) *how = QStringLiteral("%1 has no faces to show").arg(file);
            return false;
        }
        if (how)
            *how = QStringLiteral("%1: %2 solid(s), %3 triangles, read in %4 s")
                       .arg(file)
                       .arg(parts.size())
                       .arg(tris)
                       .arg(clock.elapsed() / 1000.0, 0, 'f', 1);
        return true;
    } catch (const Standard_Failure& e) {
        if (how) *how = QStringLiteral("%1 could not be read: %2").arg(file, QString::fromLatin1(e.what()));
    } catch (...) {
        if (how) *how = QStringLiteral("%1 could not be read").arg(file);
    }
    parts.clear();
    return false;
}

} // namespace Edraw_Desktop
