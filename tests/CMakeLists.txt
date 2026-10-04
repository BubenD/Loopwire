add_executable(edraw_tests
    test_geometry.cpp
    test_editor.cpp
    test_io.cpp
    test_harness.cpp
    test_voltagedrop.cpp
    test_mechbuilder.cpp
    test_thermal.cpp
    test_template.cpp
    test_datasheet.cpp
    test_picture.cpp
    test_pdftext.cpp
    test_pdfimport.cpp
    test_partfrompdf.cpp
    test_sizetable.cpp
    test_cable.cpp
    test_detailview.cpp
    test_mechmodels.cpp
    test_output.cpp
    test_connectorparts.cpp
    test_partweb.cpp
    test_libraryuse.cpp
    test_partspanel.cpp
    test_entityrefs.cpp
    test_heatshrink.cpp
    test_pagesetup.cpp
    test_mechmodelcanvas.cpp
    test_drawingspace.cpp
    test_stepsbuilder.cpp
    test_licenses.cpp
    test_odaexport.cpp
    test_mechscript.cpp
    test_mechwizard.cpp
    test_brickharness.cpp
    test_main.cpp
    # THE APPLICATION'S OWN RESOURCES, in the test binary as well. The default mechanical models - the house
    # style - are built into the executable, and a test of "a fresh install comes up with the house models"
    # cannot check that from a binary the resource was never compiled into. Listed on the target rather than
    # on the core library for the same reason the application lists it: a .qrc compiled into a plain static
    # library is not reliably registered.
    ../resources/edraw.qrc
)
# Widgets, because the step builder is a window and is tested as one.
target_link_libraries(edraw_tests PRIVATE edraw_core Qt6::Core Qt6::Gui Qt6::Widgets)
# The editor tests build deep call trees (recursive entity traversal, large
# local buffers in test cases). The Windows default 1 MB stack is not enough
# and the runner silently overflows before it can print anything. 16 MB
# matches the desktop application's headroom.
if(WIN32 AND MSVC)
    target_link_options(edraw_tests PRIVATE /STACK:16777216,16777216)
endif()

# THE QT RUNTIME BESIDE THE TEST BINARY, TOO.
#
# windeployqt runs for Edraw.exe and leaves the DLLs in the build root. Nothing put them next to
# edraw_tests.exe, so running the tests the obvious way - double-clicking them, or from their own
# directory - stopped dead at "Qt6Widgets.dll was not found" before a single test had run. They worked
# only when started from the build root, which is a thing somebody has to know and tell you; a test
# suite that cannot be run by running it is one that quietly stops being run.
#
# Same flags as the application's own deployment, and for the same licence reasons - see the long note
# beside them in the top-level CMakeLists.
if(WIN32 AND EDRAW_WINDEPLOYQT)
    add_custom_command(TARGET edraw_tests POST_BUILD
        COMMAND "${EDRAW_WINDEPLOYQT}"
                "--$<IF:$<CONFIG:Debug>,debug,release>"
                --no-translations --no-compiler-runtime
                --no-opengl-sw
                --no-system-d3d-compiler
                --no-system-dxc-compiler
                --verbose 0
                "$<TARGET_FILE:edraw_tests>"
        COMMENT "Deploying the Qt runtime beside edraw_tests.exe")
endif()

# AND THE OFFSCREEN PLATFORM PLUGIN, WHICH WINDEPLOYQT DOES NOT BRING.
#
# It copies the platform plugin the binary looks like it needs - qwindows - and the tests use neither:
# test_main forces QT_QPA_PLATFORM=offscreen so the suite runs the same on a machine with no display as
# on one with a screen. With no qoffscreen.dll to load, Qt gives up inside the QApplication constructor
# and fails fast: the process dies with 0xC0000409 having printed nothing at all, which reads like a
# crash in the first test rather than a missing file, and sends you looking in the wrong place.
#
# Taken from the imported target rather than copied from a path, so the debug build gets qoffscreend.dll
# and the release build gets qoffscreen.dll without either being spelled out here.
if(WIN32)
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
        add_custom_command(TARGET edraw_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:edraw_tests>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                    "$<TARGET_FILE_DIR:edraw_tests>/platforms"
            COMMENT "Copying the offscreen platform plugin beside edraw_tests.exe")
    else()
        message(WARNING
            "Qt6::QOffscreenIntegrationPlugin was not found, so qoffscreen.dll will not be copied beside "
            "edraw_tests.exe. The suite forces QT_QPA_PLATFORM=offscreen and will fail fast on startup "
            "with no output unless QT_QPA_PLATFORM_PLUGIN_PATH points at a Qt that has it.")
    endif()
endif()

# Open CASCADE's DLLs beside the test binary too, where the build reads STEP through it - and the fixture that writes
# a STEP assembly with it for the tests.
if(EDRAW_OCCT_DLLS)
    target_sources(edraw_tests PRIVATE test_occt_fixture.cpp)
    target_include_directories(edraw_tests PRIVATE "${OpenCASCADE_INCLUDE_DIR}")
    target_link_libraries(edraw_tests PRIVATE ${OpenCASCADE_LIBRARIES})
    add_custom_command(TARGET edraw_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EDRAW_OCCT_DLLS} "$<TARGET_FILE_DIR:edraw_tests>"
        COMMENT "Copying the Open CASCADE DLLs beside edraw_tests.exe")
endif()

add_test(NAME edraw_tests COMMAND edraw_tests)

# THE STAND-IN FOR THE ODA FILE CONVERTER.
#
# Named ODAFileConverter so it passes the manager's own name check - the manager refuses a file that
# is not called that, and a test that bypassed the check would not be testing the manager. Built into
# its own directory so the name cannot collide with anything, and so a test can point the manager at
# it by path.
#
# Deliberately free of Qt: it stands in for a program Edraw only ever talks to through argv and the
# filesystem, and giving it Qt would make the test binary's dependencies say something untrue about
# what the manager needs.
add_executable(oda_stub oda_stub.cpp)
set_target_properties(oda_stub PROPERTIES
    OUTPUT_NAME "ODAFileConverter"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/odastub"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/odastub"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/odastub"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}/odastub"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_BINARY_DIR}/odastub")
add_dependencies(edraw_tests oda_stub)
# Where it landed, so the test does not have to guess at the generator's directory layout.
target_compile_definitions(edraw_tests PRIVATE
    EDRAW_ODA_STUB_DIR="${CMAKE_CURRENT_BINARY_DIR}/odastub")
