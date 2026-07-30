// Minimal assertion-based test runner for Ewire core logic.
// Each test function returns the number of failures; main aggregates them.
#include <cstdio>

int run_geometry_tests();
int run_editor_tests();
int run_io_tests();
int run_harness_tests();

int main() {
    // Unbuffered, so a crash mid-suite still shows how far it got.
    setvbuf(stdout, nullptr, _IONBF, 0);
    int failures = 0;
    failures += run_geometry_tests();
    failures += run_editor_tests();
    failures += run_io_tests();
    failures += run_harness_tests();
    if (failures == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
