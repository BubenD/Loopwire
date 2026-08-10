cmake_minimum_required(VERSION 3.21)

project(EdrawDesktop
    VERSION 0.1.0
    DESCRIPTION "Edraw desktop - native C++/Qt 2D CAD for wire harnesses"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt tooling: run moc/rcc/uic automatically.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core)

# --- Core library (everything except main, so tests can link it) -------------
add_library(edraw_core STATIC
    src/geometry/Vec2.h
    src/geometry/Geometry.h
    src/geometry/Geometry.cpp
    src/geometry/Snap.h
    src/geometry/Snap.cpp
    src/model/Entity.h
    src/model/Entity.cpp
    src/model/Layer.h
    src/model/Block.h
    src/model/Block.cpp
    src/model/PartSymbols.cpp
    src/model/DrawingTemplate.h
    src/model/DrawingTemplate.cpp
    src/model/Picture.cpp
    src/model/Document.h
    src/model/Document.cpp
    src/commands/UndoStack.h
    src/commands/UndoStack.cpp
    src/io/AppPaths.h
    src/io/AppPaths.cpp
    src/io/PdfText.cpp
    src/io/Dwx.h
    src/io/Dwx.cpp
    src/io/Dxf.h
    src/io/Dxf.cpp
    src/tools/ToolId.h
    src/tools/EntityTransforms.h
    src/tools/EntityTransforms.cpp
    src/tools/ModifyOps.h
    src/tools/ModifyOps.cpp
    src/tools/QuickSelect.h
    src/tools/QuickSelect.cpp
    src/harness/Harness.h
    src/harness/Harness.cpp
    src/harness/HarnessBuilder.h
    src/harness/HarnessBuilder.cpp
    src/harness/PartsLibrary.h
    src/harness/Datasheet.h
    src/harness/Datasheet.cpp
    src/harness/PartsLibrary.cpp
    src/harness/MechanicalHarness.h
    src/harness/MechModels.cpp
    src/harness/MechanicalHarness.cpp
    src/harness/Bom.h
    src/harness/Bom.cpp
    src/harness/CableSpec.cpp
    src/analysis/VoltageDrop.h
    src/analysis/Thermal.h
    src/analysis/Thermal.cpp
    src/analysis/VoltageDrop.cpp
    src/render/CanvasWidget.h
    src/render/CanvasWidget.cpp
    src/render/EntityRenderer.h
    src/render/EntityRenderer.cpp
    src/app/FontLibrary.h
    src/app/FontLibrary.cpp
    src/app/Editor.h
    src/app/Editor.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/ribbon/RibbonIcons.h
    src/app/ribbon/RibbonIcons.cpp
    src/app/ribbon/HomeTab.cpp
    src/app/ribbon/TemplateTab.cpp
    src/app/ribbon/ModifyTab.cpp
    src/app/ribbon/InsertTab.cpp
    src/app/ribbon/AnnotationTab.cpp
    src/app/ribbon/SchematicBuildTab.cpp
    src/app/ribbon/HarnessMechanicalTab.cpp
    src/app/HarnessBuilderDialog.h
    src/app/HarnessBuilderDialog.cpp
    src/app/SplashScreen.h
    src/app/SplashScreen.cpp
    src/app/PartsLibraryDialog.h
    src/app/PartsLibraryDialog.cpp
    src/app/DrawingPreview.h
    src/app/DrawingPreview.cpp
    src/app/MechanicalBuilderDialog.h
    src/app/HarnessStepsDialog.cpp
    src/app/MechModelCanvasDialog.cpp
    src/app/MechModelsDialog.cpp
    src/app/MechanicalBuilderDialog.cpp
    src/app/MechanicalValidationDialog.h
    src/app/MechanicalValidationDialog.cpp
    src/app/BlockDialog.h
    src/app/BlockDialog.cpp
    src/app/TemplateDialog.h
    src/app/TemplateDialog.cpp
    src/app/BundleDiameterDialog.h
    src/app/BundleDiameterDialog.cpp
    src/app/ThermalModelDialog.h
    src/app/ThermalModelDialog.cpp
    src/app/BomDialog.h
    src/app/BomDialog.cpp
    src/app/VoltageDropDialog.h
    src/app/CableBuilderDialog.cpp
    src/app/PartViewDialog.cpp
    src/app/PictureInsert.cpp
    src/app/UiBits.cpp
    src/app/VoltageDropDialog.cpp
)

target_include_directories(edraw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(edraw_core PUBLIC Qt6::Widgets Qt6::Gui Qt6::Core)

# --- Application executable --------------------------------------------------
# The .rc (compiled by the MSVC resource compiler) embeds the Explorer/taskbar
# icon in Edraw.exe.
set(EDRAW_WIN_RESOURCES "")
if(WIN32)
    set(EDRAW_WIN_RESOURCES resources/edraw.rc)
endif()

qt_add_executable(Edraw WIN32
    src/main.cpp
    # The stylesheet and the SVG carets it names. In the EXECUTABLE, not the library: a .qrc compiled
    # into a plain static library is not reliably registered - the linker may drop its initialiser -
    # and a stylesheet that silently is not there is a bad way to find that out.
    resources/edraw.qrc
    ${EDRAW_WIN_RESOURCES}
)
target_link_libraries(Edraw PRIVATE edraw_core)

set_target_properties(Edraw PROPERTIES
    WIN32_EXECUTABLE ON
    OUTPUT_NAME "Edraw")

# Recompile the .rc (and thus refresh the embedded icon) when edraw.ico changes.
if(WIN32)
    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/resources/edraw.rc
                 PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/edraw.ico)
endif()

# --- Scratch GUI smoke target (only when a source is pointed at it) ----------
if(EDRAW_SMOKE_SRC)
    qt_add_executable(hb_smoke ${EDRAW_SMOKE_SRC} resources/edraw.qrc)
    target_link_libraries(hb_smoke PRIVATE edraw_core)
endif()

# --- Tests (optional) --------------------------------------------------------
option(EWIRE_BUILD_TESTS "Build Edraw unit tests" OFF)
if(EWIRE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
