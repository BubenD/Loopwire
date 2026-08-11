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
    test_mechmodels.cpp
    test_mechmodelcanvas.cpp
    test_stepsbuilder.cpp
    test_main.cpp
)
# Widgets, because the step builder is a window and is tested as one.
target_link_libraries(edraw_tests PRIVATE edraw_core Qt6::Core Qt6::Gui Qt6::Widgets)

add_test(NAME edraw_tests COMMAND edraw_tests)
