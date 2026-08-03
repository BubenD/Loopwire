// Minimal assertion-based test runner for Ewire core logic.
// Each test function returns the number of failures; main aggregates them.
#include <cstdio>

int run_geometry_tests();
int run_editor_tests();
int run_io_tests();
int run_harness_tests();
int run_voltagedrop_tests();
int run_mechbuilder_tests();
int run_thermal_tests();
int run_template_tests();
int run_block_tests();
int run_datasheet_tests();
int run_picture_tests();
int run_pdftext_tests();
int run_sizetable_tests();
int run_cable_tests();

int main() {
    // Unbuffered, so a crash mid-suite still shows how far it got.
    setvbuf(stdout, nullptr, _IONBF, 0);
    int failures = 0;
    failures += run_geometry_tests();
    failures += run_editor_tests();
    failures += run_io_tests();
    failures += run_harness_tests();
    failures += run_voltagedrop_tests();
    failures += run_mechbuilder_tests();
    failures += run_thermal_tests();
    failures += run_template_tests();
    failures += run_block_tests();
    failures += run_datasheet_tests();
    failures += run_picture_tests();
    failures += run_pdftext_tests();
    failures += run_sizetable_tests();
    failures += run_cable_tests();
    if (failures == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
