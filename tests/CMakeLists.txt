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
