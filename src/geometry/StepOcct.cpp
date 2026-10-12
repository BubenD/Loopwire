#include "geometry/StepOcct.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_GTrsf.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
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

namespace Loopwire {

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

namespace {
// HOW FAR A LONG STEP HAS GOT - reading a file's solids, meshing them - told to the bar between two of its marks.
class OcctProgress : public Message_ProgressIndicator {
public:
    OcctProgress(const Load3DProgress& f, int lo, int hi, const char* stage) : f_(f), lo_(lo), hi_(hi), stage_(QString::fromLatin1(stage)) {}

protected:
    void Show(const Message_ProgressScope&, const bool) override {
        if (f_) f_(lo_ + static_cast<int>((hi_ - lo_) * std::clamp(GetPosition(), 0.0, 1.0)), stage_);
    }
    bool UserBreak() override { return false; }

private:
    Load3DProgress f_;
    int lo_, hi_;
    QString stage_;
};

TopoDS_Shape occtShapeOf(const OcctSolid& s) {
    const double a = std::max(0.01, s.a), b = std::max(0.01, s.b), c = std::max(0.0, s.c);
    const gp_Ax2 alongX(gp_Pnt(-a / 2.0, 0, 0), gp_Dir(1, 0, 0));
    TopoDS_Shape shape;
    switch (s.shape) {
        case 0: shape = BRepPrimAPI_MakeBox(gp_Pnt(-a / 2, -b / 2, -std::max(0.01, c) / 2), a, b, std::max(0.01, c)).Shape(); break;
        case 1: shape = BRepPrimAPI_MakeCylinder(alongX, b / 2, a).Shape(); break;
        case 2: {
            const TopoDS_Shape outer = BRepPrimAPI_MakeCylinder(alongX, b / 2, a).Shape();
            const double ri = std::clamp(c / 2, 0.0, b / 2 * 0.98);
            shape = ri > 1e-6 ? BRepAlgoAPI_Cut(outer, BRepPrimAPI_MakeCylinder(alongX, ri, a).Shape()).Shape() : outer;
            break;
        }
        case 3:
            shape = std::abs(b - c) < 1e-6 ? BRepPrimAPI_MakeCylinder(alongX, b / 2, a).Shape()
                                           : BRepPrimAPI_MakeCone(alongX, b / 2, c / 2, a).Shape();
            break;
        case 4: shape = BRepPrimAPI_MakeSphere(gp_Pnt(0, 0, 0), a / 2).Shape(); break;
        case 5: shape = BRepPrimAPI_MakeTorus(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), a / 2, b / 2).Shape(); break;
        case 6: {
            // A hexagon across X, its flats b apart, drawn out along X.
            const double r = b / 2 / std::cos(M_PI / 6);
            BRepBuilderAPI_MakePolygon hex;
            for (int k = 0; k < 6; ++k) hex.Add(gp_Pnt(-a / 2, r * std::cos(k * M_PI / 3), r * std::sin(k * M_PI / 3)));
            hex.Close();
            shape = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(hex.Wire()).Face(), gp_Vec(a, 0, 0)).Shape();
            break;
        }
        case 10: {
            // A SKETCH: its outline in its plane, given depth toward the one who drew it.
            BRepBuilderAPI_MakePolygon outline;
            int corners = 0;
            for (const auto& [pu, pv] : s.profile) {
                const V3 p = s.u * pu + s.v * pv;
                outline.Add(gp_Pnt(p.x, p.y, p.z));
                ++corners;
            }
            if (corners < 3) return {};
            outline.Close();
            if (!outline.IsDone()) return {};
            const BRepBuilderAPI_MakeFace face(outline.Wire(), true);
            if (!face.IsDone()) return {};
            const V3 d = s.w * s.a;
            shape = BRepPrimAPI_MakePrism(face.Face(), gp_Vec(d.x, d.y, d.z)).Shape();
            break;
        }
        case 11:
            // A PIN standing out of its face along X, or a socket going into it.
            shape = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(s.female ? -b : 0.0, 0, 0), gp_Dir(1, 0, 0)), a / 2, b).Shape();
            break;
        default: return {};
    }
    // ITS EDGES ROUNDED OR CUT OFF: every one that is a true edge between two faces - not a seam round a cylinder, not
    // the point of a cone. Too big for an edge, and it is left sharp rather than not made at all.
    if (s.fillet > 1e-6 || s.chamfer > 1e-6) {
        TopTools_IndexedDataMapOfShapeListOfShape faces;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, faces);
        auto real = [&](const TopoDS_Edge& e) {
            if (BRep_Tool::Degenerated(e)) return false;
            const TopTools_ListOfShape& on = faces.FindFromKey(e);
            if (on.Extent() < 2) return false;
            for (TopTools_ListOfShape::Iterator it(on); it.More(); it.Next())
                if (BRep_Tool::IsClosed(e, TopoDS::Face(it.Value()))) return false;
            return true;
        };
        try {
            if (s.fillet > 1e-6) {
                BRepFilletAPI_MakeFillet round(shape);
                for (int i = 1; i <= faces.Extent(); ++i)
                    if (real(TopoDS::Edge(faces.FindKey(i)))) round.Add(s.fillet, TopoDS::Edge(faces.FindKey(i)));
                round.Build();
                if (round.IsDone() && !round.Shape().IsNull()) shape = round.Shape();
            } else {
                BRepFilletAPI_MakeChamfer bevel(shape);
                for (int i = 1; i <= faces.Extent(); ++i)
                    if (real(TopoDS::Edge(faces.FindKey(i)))) bevel.Add(s.chamfer, TopoDS::Edge(faces.FindKey(i)));
                bevel.Build();
                if (bevel.IsDone() && !bevel.Shape().IsNull()) shape = bevel.Shape();
            }
        } catch (const Standard_Failure&) {
            // Left sharp.
        }
    }
    // WHERE IT GOES: turned, scaled - each axis its own - and put.
    gp_GTrsf g;
    g.SetValue(1, 1, s.m[0][0]);
    g.SetValue(1, 2, s.m[0][1]);
    g.SetValue(1, 3, s.m[0][2]);
    g.SetValue(1, 4, s.m[0][3]);
    g.SetValue(2, 1, s.m[1][0]);
    g.SetValue(2, 2, s.m[1][1]);
    g.SetValue(2, 3, s.m[1][2]);
    g.SetValue(2, 4, s.m[1][3]);
    g.SetValue(3, 1, s.m[2][0]);
    g.SetValue(3, 2, s.m[2][1]);
    g.SetValue(3, 3, s.m[2][2]);
    g.SetValue(3, 4, s.m[2][3]);
    return BRepBuilderAPI_GTransform(shape, g, true).Shape();
}
} // namespace

bool occtCutSolid(const OcctSolid& solid, const std::vector<OcctSolid>& cuts, Mesh3D& out, QString* why) {
    try {
        TopoDS_Shape shape = occtShapeOf(solid);
        if (shape.IsNull()) return false;
        for (const OcctSolid& c : cuts) {
            const TopoDS_Shape hole = occtShapeOf(c);
            if (hole.IsNull()) continue;
            BRepAlgoAPI_Cut cut(shape, hole);
            if (cut.IsDone() && !cut.Shape().IsNull()) shape = cut.Shape();
        }
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        if (box.IsVoid()) {
            out = Mesh3D();
            return true;   // cut away whole
        }
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        const double span = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
        BRepMesh_IncrementalMesh mesher(shape, std::clamp(span * 0.002, 0.002, 1.0), false, 0.3, true);
        out = Mesh3D();
        appendTriangles(shape, out);
        return true;
    } catch (const Standard_Failure& e) {
        if (why) *why = QStringLiteral("a cut could not be made: %1").arg(QString::fromLatin1(e.what()));
    } catch (...) {
        if (why) *why = QStringLiteral("a cut could not be made");
    }
    return false;
}

bool readStepOcct(const QString& path, std::vector<EnvPart3D>& parts, QString* how, const Load3DProgress& progress) {
    auto say = [&](int pct, const char* stage) {
        if (progress) progress(pct, QString::fromLatin1(stage));
    };
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
        say(5, "Reading the STEP file");
        if (reader.ReadFile(QFileInfo(path).absoluteFilePath().toLocal8Bit().constData()) != IFSelect_RetDone) {
            if (how) *how = QStringLiteral("%1 is not a STEP file Open CASCADE can read").arg(file);
            app->Close(doc);
            return false;
        }
        say(30, "Building the solids");
        Handle(OcctProgress) building = new OcctProgress(progress, 30, 50, "Building the solids");
        if (!reader.Transfer(doc, building->Start())) {
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
        say(50, "Meshing the surfaces");
        Handle(OcctProgress) meshing = new OcctProgress(progress, 50, 80, "Meshing the surfaces");
        IMeshTools_Parameters meshWith;
        meshWith.Deflection = deflection;
        meshWith.Angle = 0.35;
        meshWith.InParallel = true;
        BRepMesh_IncrementalMesh mesher(all, meshWith, meshing->Start());
        say(80, "Collecting the solids");
        int seen = 0;
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
            // On towards 95 with each solid - how many there are is not known until the last.
            ++seen;
            if (progress) progress(80 + static_cast<int>(15.0 * (1.0 - 1.0 / (1.0 + seen / 20.0))),
                                   QStringLiteral("Collecting the solids: %1").arg(seen));
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

} // namespace Loopwire
