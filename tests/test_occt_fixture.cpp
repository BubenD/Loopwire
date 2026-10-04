// A STEP ASSEMBLY FOR THE TESTS, written by Open CASCADE itself: what a CAD program exports, with names, colours and a
// surface Edraw's own reader cannot take (a sphere). Only in a build that has Open CASCADE.
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <QString>

// Bracket: a red box 0..10 each way. Panel: a blue box from (20, 0, 0) to (40, 10, 5). Ball: a green sphere of radius
// 20 round (0, 40, 0).
bool writeTestStepAssembly(const QString& path) {
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument(TCollection_ExtendedString("BinXCAF"), doc);
    const Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const Handle(XCAFDoc_ColorTool) colours = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    auto add = [&](const TopoDS_Shape& s, const char* name, const Quantity_Color& c) {
        const TDF_Label label = shapes->AddShape(s, false);
        TDataStd_Name::Set(label, TCollection_ExtendedString(name));
        colours->SetColor(label, c, XCAFDoc_ColorSurf);
    };
    add(BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), gp_Pnt(10, 10, 10)).Shape(), "Bracket", Quantity_Color(1, 0, 0, Quantity_TOC_RGB));
    add(BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), gp_Pnt(40, 10, 5)).Shape(), "Panel", Quantity_Color(0, 0, 1, Quantity_TOC_RGB));
    add(BRepPrimAPI_MakeSphere(gp_Pnt(0, 40, 0), 20).Shape(), "Ball", Quantity_Color(0, 1, 0, Quantity_TOC_RGB));
    STEPCAFControl_Writer writer;
    writer.SetColorMode(true);
    writer.SetNameMode(true);
    bool ok = writer.Transfer(doc, STEPControl_AsIs);
    ok = ok && writer.Write(path.toLocal8Bit().constData()) == IFSelect_RetDone;
    app->Close(doc);
    return ok;
}
