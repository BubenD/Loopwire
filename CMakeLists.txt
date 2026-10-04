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
    # MainWindow.cpp and the wizard are past the default section limit of one object file.
    add_compile_options(/bigobj)
else()
    add_compile_options(-Wall -Wextra)
endif()

# OpenGL: the 3D view draws a CAD model of a million triangles on the graphics card (QOpenGLContext drawing off
# screen - no QOpenGLWidget, no Qt Quick). LGPL like the rest of qtbase.
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core PrintSupport Network OpenGL)

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
    src/model/PartSymbols.h
    src/model/PartSymbols.cpp
    src/model/DrawingTemplate.h
    src/model/DrawingTemplate.cpp
    src/model/Picture.h
    src/model/Picture.cpp
    src/model/Document.h
    src/model/Document.cpp
    src/commands/UndoStack.h
    src/commands/UndoStack.cpp
    src/io/AppPaths.h
    src/io/AppPaths.cpp
    src/io/PdfText.h
    src/io/PdfText.cpp
    src/io/PdfImport.h
    src/io/PdfImport.cpp
    src/io/Dwx.h
    src/io/Dwx.cpp
    src/io/Dxf.h
    src/io/Dxf.cpp
    src/io/OdaConverter.h
    src/io/OdaConverter.cpp
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
    src/harness/MechBuildScript.h
    src/harness/MechBuildScript.cpp
    src/harness/MechanicalHarness.h
    src/harness/MechModels.h
    src/harness/MechModels.cpp
    src/harness/MechanicalHarness.cpp
    src/harness/BrickHarness.h
    src/harness/BrickHarness.cpp
    src/harness/BrickDrc.h
    src/harness/BrickDrc.cpp
    src/harness/Bom.h
    src/harness/Bom.cpp
    src/harness/CableSpec.h
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
    src/app/Chrome.cpp
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
    src/app/ribbon/ViewTab.cpp
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
    src/app/HarnessStepsDialog.h
    src/app/HarnessStepsDialog.cpp
    src/app/MechModelCanvasDialog.h
    src/app/MechModelCanvasDialog.cpp
    src/app/MechModelsDialog.h
    src/app/MechModelsDialog.cpp
    src/app/OutputDialog.h
    src/app/OutputDialog.cpp
    src/app/ScientificCalculatorDialog.h
    src/app/ScientificCalculatorDialog.cpp
    src/app/MechWizardDialog.h
    src/app/MechWizardDialog.cpp
    src/app/BrickWizardDialog.h
    src/app/BrickWizardDialog.cpp
    src/app/OdaSetupDialog.h
    src/app/OdaSetupDialog.cpp
    src/app/PageSetupDialog.h
    src/app/PageSetupDialog.cpp
    src/app/PageSetupManagerDialog.h
    src/app/PageSetupManagerDialog.cpp
    src/model/PageSetup.h
    src/model/PageSetup.cpp
    src/model/EntityRefs.h
    src/model/EntityRefs.cpp
    src/harness/PartDataFromWeb.h
    src/harness/PartDataFromWeb.cpp
    src/app/PartWebDataDialog.h
    src/app/PartWebDataDialog.cpp
    src/app/MechanicalValidationDialog.h
    src/app/MechanicalValidationDialog.cpp
    src/app/RuleExplainer.h
    src/app/RuleExplainer.cpp
    src/app/View3DPane.h
    src/app/View3DPane.cpp
    src/geometry/Mesh3D.h
    src/geometry/Mesh3D.cpp
    src/render/Render3D.h
    src/render/Render3D.cpp
    src/render/GlRender3D.h
    src/render/GlRender3D.cpp
    src/geometry/Environment3D.h
    src/geometry/Environment3D.cpp
    src/harness/Harness3D.h
    src/harness/Harness3D.cpp
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
    src/app/CableBuilderDialog.h
    src/app/CableBuilderDialog.cpp
    src/app/PartViewDialog.h
    src/app/PartViewDialog.cpp
    src/app/PictureInsert.cpp
    src/app/UiBits.h
    src/app/UiBits.cpp
    src/app/VoltageDropDialog.cpp
)

target_include_directories(edraw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(edraw_core PUBLIC Qt6::Widgets Qt6::Gui Qt6::Core Qt6::PrintSupport
                                       Qt6::Network Qt6::OpenGL)

# --- Open CASCADE Technology: STEP models read whole (optional) --------------
#
# Edraw's own STEP reader takes planes, cylinders and cones; a real CAD export is mostly B-spline surfaces, and an
# assembly besides. Open CASCADE reads all of it. It is optional - without it the build still works, reading STEP
# files with Edraw's own reader - and it is used through its DLLs only, never linked statically: Open CASCADE is
# LGPL-2.1 with the Open CASCADE exception, and replaceable DLLs are what the LGPL asks for (see
# THIRD-PARTY-NOTICES.txt).
#
# THE BUILD OF IT MATTERS FOR THE LICENCE PAPERWORK. The prebuilt Windows package links FFmpeg, FreeImage, FreeType,
# TBB, jemalloc and OpenVR - none of which Edraw uses, all of which would then ship and need notices of their own.
# Build it instead from the release source with only what reading STEP needs:
#
#   cmake -S OCCT-8.0.1 -B occt-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DINSTALL_DIR=<EDRAW_OCCT_DIR>
#         -DBUILD_MODULE_FoundationClasses=OFF -DBUILD_MODULE_ModelingData=OFF -DBUILD_MODULE_ModelingAlgorithms=OFF
#         -DBUILD_MODULE_Visualization=OFF -DBUILD_MODULE_ApplicationFramework=OFF -DBUILD_MODULE_DataExchange=OFF
#         -DBUILD_MODULE_Draw=OFF -DBUILD_ADDITIONAL_TOOLKITS="TKDESTEP TKMesh TKXCAF"
#         -DUSE_TK=OFF -DUSE_FREETYPE=OFF -DUSE_FREEIMAGE=OFF -DUSE_FFMPEG=OFF -DUSE_OPENVR=OFF -DUSE_TBB=OFF
#         -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_VTK=OFF -DUSE_OPENGL=OFF -DUSE_GLES2=OFF -DUSE_D3D=OFF
#   cmake --build occt-build && cmake --install occt-build
#
# Every DLL in its bin folder is then Open CASCADE's own, and they are all copied beside Edraw.exe.
option(EDRAW_WITH_OCCT "Read STEP models through Open CASCADE Technology" ON)
set(EDRAW_OCCT_DIR "C:/Software/occt-8.0.1/lean" CACHE PATH "Where Open CASCADE is installed")
set(EDRAW_OCCT_DLLS "")
if(EDRAW_WITH_OCCT)
    find_package(OpenCASCADE CONFIG QUIET HINTS "${EDRAW_OCCT_DIR}" "${EDRAW_OCCT_DIR}/cmake")
    if(OpenCASCADE_FOUND)
        message(STATUS "Open CASCADE ${OpenCASCADE_VERSION}: STEP models read whole (${OpenCASCADE_BINARY_DIR})")
        target_sources(edraw_core PRIVATE src/geometry/StepOcct.h src/geometry/StepOcct.cpp)
        target_compile_definitions(edraw_core PUBLIC EDRAW_HAVE_OCCT)
        target_include_directories(edraw_core PRIVATE "${OpenCASCADE_INCLUDE_DIR}")
        target_link_libraries(edraw_core PRIVATE ${OpenCASCADE_LIBRARIES})
        file(GLOB EDRAW_OCCT_DLLS "${OpenCASCADE_BINARY_DIR}/*.dll")
        # A DLL beside it that is not Open CASCADE's own is one the notices do not cover.
        foreach(_dll IN LISTS EDRAW_OCCT_DLLS)
            get_filename_component(_name "${_dll}" NAME)
            if(NOT _name MATCHES "^TK")
                message(FATAL_ERROR
                    "${_name} in ${OpenCASCADE_BINARY_DIR} is not an Open CASCADE toolkit. It would ship beside "
                    "Edraw with no notice for it. Build Open CASCADE with its third-party options off (see above).")
            endif()
        endforeach()
    else()
        message(WARNING
            "Open CASCADE was not found in ${EDRAW_OCCT_DIR}. STEP models will be read by Edraw's own reader, "
            "which takes planes, cylinders and cones only. Set EDRAW_OCCT_DIR, or EDRAW_WITH_OCCT=OFF to silence this.")
    endif()
endif()

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
    OUTPUT_NAME "Edraw"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")

# Recompile the .rc (and thus refresh the embedded icon) when edraw.ico changes.
if(WIN32)
    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/resources/edraw.rc
                 PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/edraw.ico)
endif()

# The Qt runtime is deployed beside Edraw.exe by every build.
#
# It used to be deployed by hand, once, by whoever set the machine up - and that holds only until the
# application starts using a Qt module it did not use before. Adding Qt6::PrintSupport (for printing) left
# a build that compiled, linked and then would not start: "Qt6PrintSupport.dll was not found". Nothing in
# the build said anything was wrong, because from the build's point of view nothing was. windeployqt knows
# exactly which DLLs and plugins the binary needs; it is asked after every link, and it copies only what is
# missing or out of date, so the cost is a second and the failure cannot come back.
if(WIN32)
    get_target_property(_edraw_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_edraw_qt_bin "${_edraw_qmake}" DIRECTORY)
    find_program(EDRAW_WINDEPLOYQT windeployqt HINTS "${_edraw_qt_bin}")
    if(EDRAW_WINDEPLOYQT)
        # THE THREE --no-* GRAPHICS FLAGS ARE LICENCE DECISIONS as much as size ones.
        #
        # Left to itself windeployqt copies four graphics libraries beside Edraw.exe:
        #
        #   opengl32sw.dll                  Mesa's software rasteriser. MIT - so shipping it means
        #                                   carrying Mesa's attribution.
        #   d3dcompiler_47.dll              Microsoft. Not open source; redistributable only on
        #   dxcompiler.dll, dxil.dll        Microsoft's own terms, which would have to be read and met.
        #
        # Edraw uses none of them. The canvas draws through QPainter. The 3D view draws through OpenGL
        # (src/render/GlRender3D.cpp), but through the graphics driver's own opengl32.dll; where a machine
        # has no usable OpenGL it falls back to its software renderer, so Mesa's opengl32sw.dll is still not
        # needed - and there is no QRhi or Qt Quick anywhere in the source. Not shipping them is simpler than
        # being entitled to ship them.
        #
        # All three flags are needed, and this is the trap: --no-system-d3d-compiler drops only
        # d3dcompiler_47.dll. dxcompiler.dll and dxil.dll are the newer DirectX Shader Compiler and have
        # their own flag, --no-system-dxc-compiler. Dropping one and assuming the other followed is how
        # two undeclared Microsoft binaries end up in a release.
        #
        # If Edraw ever does take up Qt Quick, or comes to need OpenGL where the driver has none, these flags
        # come off and the notices file has to grow to match.
        add_custom_command(TARGET Edraw POST_BUILD
            COMMAND "${EDRAW_WINDEPLOYQT}"
                    "--$<IF:$<CONFIG:Debug>,debug,release>"
                    --no-translations --no-compiler-runtime
                    --no-opengl-sw
                    --no-system-d3d-compiler
                    --no-system-dxc-compiler
                    --verbose 0
                    "$<TARGET_FILE:Edraw>"
            COMMENT "Deploying the Qt runtime beside Edraw.exe")
    else()
        message(WARNING
            "windeployqt was not found next to qmake (${_edraw_qt_bin}). The Qt DLLs will not be kept up "
            "to date beside Edraw.exe, so adding a Qt module can leave an executable that will not start.")
    endif()
endif()

# --- The licence paperwork, beside the executable ----------------------------
#
# THIS IS NOT HOUSEKEEPING, IT IS THE CONDITION ON WHICH EDRAW MAY SHIP.
#
# Edraw links Qt dynamically and ships Qt's DLLs. That makes Edraw a combined work under the LGPL v3,
# and the LGPL asks three things of us in return for it: say that Qt is used and that it is used under
# the LGPL (§4(a) - the About dialog does that), supply the licence texts (§4(b) - these files), and
# make Qt's own source code obtainable (the address is written into the notices document).
#
# The files are copied on every build rather than only at install time, because build\Edraw.exe IS what
# gets zipped up and sent to somebody. A copy of the program without these files beside it is a copy
# that was distributed in breach of the licence, and the way to make that impossible is to leave no
# build in which they are absent.
#
# They are ALSO compiled into the executable (see resources/edraw.qrc), so "About Edraw -> Third-party
# licences" can always show them. Belt and braces on purpose: the file on disk is what an auditor
# reads, the resource is what the user is shown, and neither can go missing without the other noticing.
set(EDRAW_LICENSE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt")
set(EDRAW_LICENSE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/licenses")

foreach(_f IN LISTS EDRAW_LICENSE_FILES)
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR
            "Missing licence document: ${_f}\n"
            "Edraw may not be built without the notices it is obliged to ship. Regenerate it with:\n"
            "    python resources/notices/gen_notices.py")
    endif()
endforeach()
foreach(_f "LGPL-3.0.txt" "GPL-3.0.txt" "LGPL-2.1.txt" "OCCT-LGPL-EXCEPTION-1.0.txt")
    if(NOT EXISTS "${EDRAW_LICENSE_DIR}/${_f}")
        message(FATAL_ERROR
            "Missing licence text: ${EDRAW_LICENSE_DIR}/${_f}\n"
            "The LGPL requires both the LGPL and the GPL texts accompany the program. Fetch them from "
            "https://www.gnu.org/licenses/ .")
    endif()
endforeach()

if(EDRAW_OCCT_DLLS)
    add_custom_command(TARGET Edraw POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EDRAW_OCCT_DLLS} "$<TARGET_FILE_DIR:Edraw>"
        COMMENT "Copying the Open CASCADE DLLs beside Edraw.exe")
endif()

add_custom_command(TARGET Edraw POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${EDRAW_LICENSE_FILES} "${CMAKE_BINARY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${EDRAW_LICENSE_DIR}" "${CMAKE_BINARY_DIR}/licenses"
    COMMENT "Copying the third-party notices and licence texts beside Edraw.exe")

# Regenerating the notices from the Qt SBOM, and auditing for GPL exposure. Neither is part of the
# normal build - the generated file is committed, so a build needs no Python - but:
#
#   cmake --build build --target edraw_notices        refresh the notices after a Qt upgrade
#   cmake --build build --target edraw_notices_check  fail if somebody upgraded Qt and forgot
#   cmake --build build --target edraw_gpl_audit      fail if anything linked or shipped is GPL-only
#
# The audit is the one to run when changing what Edraw links. Not every Qt module is available under the
# LGPL - Qt Charts, Qt Data Visualization, Qt Quick 3D and the Qt Virtual Keyboard are GPL-or-commercial
# only - and adding one would oblige ETS either to publish Edraw's source or to buy a commercial licence.
# Nothing about adding a find_package component looks like that decision at the time, so it is checked.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    # Derived here rather than borrowed from the windeployqt block above, which only runs on Windows:
    # the notices describe the Qt that is installed, and that is worth knowing on any platform.
    get_target_property(_edraw_qmake_n Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_edraw_qt_bin_n "${_edraw_qmake_n}" DIRECTORY)
    get_filename_component(_edraw_qt_root "${_edraw_qt_bin_n}" DIRECTORY)
    set(_edraw_gen "${CMAKE_CURRENT_SOURCE_DIR}/resources/notices/gen_notices.py")
    add_custom_target(edraw_notices
        COMMAND "${Python3_EXECUTABLE}" "${_edraw_gen}" --qt "${_edraw_qt_root}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Regenerating THIRD-PARTY-NOTICES.txt from the Qt SBOM")
    add_custom_target(edraw_notices_check
        COMMAND "${Python3_EXECUTABLE}" "${_edraw_gen}" --qt "${_edraw_qt_root}" --check
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking THIRD-PARTY-NOTICES.txt against the installed Qt")
    add_custom_target(edraw_gpl_audit
        COMMAND "${Python3_EXECUTABLE}" "${_edraw_gen}" --qt "${_edraw_qt_root}" --audit-gpl
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Auditing for GPL exposure in what Edraw links and ships")

    # THE AUDIT RUNS ON EVERY BUILD, and a GPL-only dependency FAILS the build.
    #
    # A check somebody has to remember to run is a check that gets run once, on the day it is written.
    # The thing being guarded against here - a Qt module that turns out to be GPL-or-commercial - arrives
    # as one word added to a find_package line, by someone who wanted a graph and had no reason to think
    # they were making a licensing decision. The build is the only place that is certain to be watching.
    #
    # It costs a fraction of a second: five JSON files read, no network, no compilation. A missing SBOM
    # warns rather than fails, so this cannot make Edraw unbuildable on a Qt that predates SBOMs.
    #
    # Python is optional for building Edraw, so a machine without it simply does not get the check - which
    # is why the audit is also a target that can be run deliberately, and why the rule is written down in
    # the README rather than living only here.
    add_dependencies(Edraw edraw_gpl_audit)
else()
    message(WARNING
        "Python 3 was not found, so the GPL audit will not run on this machine. Edraw is proprietary and "
        "must not link GPL-only code; not every Qt module is available under the LGPL. Run "
        "'python resources/notices/gen_notices.py --audit-gpl' on a machine that has Python before "
        "shipping, and see the Licensing section of README.md.")
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
