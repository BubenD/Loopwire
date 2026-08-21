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
