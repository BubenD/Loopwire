add_executable(edraw_tests
    test_geometry.cpp
    test_editor.cpp
    test_io.cpp
    test_harness.cpp
    test_main.cpp
)
target_link_libraries(edraw_tests PRIVATE edraw_core Qt6::Core)

add_test(NAME edraw_tests COMMAND edraw_tests)
