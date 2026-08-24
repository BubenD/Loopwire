// Minimal assertion-based test runner for Ewire core logic.
// Each test function returns the number of failures; main aggregates them.
#include "harness/MechModels.h"
#include <QApplication>

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
int run_pdfimport_tests();
int run_sizetable_tests();
int run_cable_tests();
int run_detailview_tests();
int run_mechmodels_tests();
int run_mechmodelcanvas_tests();
int run_drawingspace_tests();
int run_output_tests();
int run_pagesetup_tests();
int run_heatshrink_tests();
int run_connectorparts_tests();
int run_partweb_tests();
int run_libraryuse_tests();
int run_partspanel_tests();
int run_entityrefs_tests();
int run_stepsbuilder_tests();

int main(int argc, char** argv) {
    // A window can only be built with an application object behind it, and the step builder is a
    // window. Offscreen, so this runs the same on a machine with no display as on one with a screen -
    // set here rather than left to whoever starts the tests.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // A NAME, SO SETTINGS HAVE SOMEWHERE TO LIVE. Without an organisation and an application name QSettings
    // has no file at all - it answers AccessError and every write is dropped - so anything the program
    // REMEMBERS between sessions was untestable, and a test of it passed or failed for reasons that had
    // nothing to do with the code. Its own name, not the application's, so a test run cannot tread on what
    // somebody's real Edraw remembers.
    QApplication::setOrganizationName(QStringLiteral("Whatson"));
    QApplication::setApplicationName(QStringLiteral("Edraw tests"));
    QApplication app(argc, argv);
    // THE HOUSE MODELS ARE SHIPPED INSIDE THE BINARY, and the tests must not stand on them. Half of what
    // is checked here is what the program does when NOTHING is drawn for a kind yet - a wizard asked for a
    // boot it has no model of, a manager showing a kind with an empty drawing - and with a set of models
    // built into the executable there is no such thing as nothing unless it is asked for. So it is asked
    // for: the application starts from the house style, a test starts from an empty sheet.
    Edraw_Desktop::MechModelSet::setShippedDefaultsEnabled(false);
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
    failures += run_pdfimport_tests();
    failures += run_sizetable_tests();
    failures += run_cable_tests();
    failures += run_detailview_tests();
    failures += run_mechmodels_tests();
    failures += run_mechmodelcanvas_tests();
    failures += run_drawingspace_tests();
    failures += run_output_tests();
    failures += run_pagesetup_tests();
    failures += run_heatshrink_tests();
    failures += run_connectorparts_tests();
    failures += run_partweb_tests();
    failures += run_libraryuse_tests();
    failures += run_partspanel_tests();
    failures += run_entityrefs_tests();
    failures += run_stepsbuilder_tests();
    if (failures == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
