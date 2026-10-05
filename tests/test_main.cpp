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
int run_partfrompdf_tests();
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
int run_licenses_tests();
int run_odaexport_tests();
int run_mechscript_tests();
int run_mechwizard_tests();
int run_brickharness_tests();

int main(int argc, char** argv) {
    // UNBUFFERED: a run that dies leaves its log up to the line it died after, not a page short of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
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
    // SOME SUITES ONLY, by name (EDRAW_TEST_ONLY=brickharness,io): a failure looked into without the whole run.
    const QList<QByteArray> onlySuites = qgetenv("EDRAW_TEST_ONLY").split(',');
    auto only = [&](const char* name) { return qgetenv("EDRAW_TEST_ONLY").isEmpty() || onlySuites.contains(QByteArray(name)); };
    int failures = 0;
    if (only("geometry")) failures += run_geometry_tests();
    if (only("editor")) failures += run_editor_tests();
    if (only("io")) failures += run_io_tests();
    if (only("harness")) failures += run_harness_tests();
    if (only("voltagedrop")) failures += run_voltagedrop_tests();
    if (only("mechbuilder")) failures += run_mechbuilder_tests();
    if (only("thermal")) failures += run_thermal_tests();
    if (only("template")) failures += run_template_tests();
    if (only("block")) failures += run_block_tests();
    if (only("datasheet")) failures += run_datasheet_tests();
    if (only("picture")) failures += run_picture_tests();
    if (only("pdftext")) failures += run_pdftext_tests();
    if (only("pdfimport")) failures += run_pdfimport_tests();
    if (only("partfrompdf")) failures += run_partfrompdf_tests();
    if (only("sizetable")) failures += run_sizetable_tests();
    if (only("cable")) failures += run_cable_tests();
    if (only("detailview")) failures += run_detailview_tests();
    if (only("mechmodels")) failures += run_mechmodels_tests();
    if (only("mechmodelcanvas")) failures += run_mechmodelcanvas_tests();
    if (only("drawingspace")) failures += run_drawingspace_tests();
    if (only("output")) failures += run_output_tests();
    if (only("pagesetup")) failures += run_pagesetup_tests();
    if (only("heatshrink")) failures += run_heatshrink_tests();
    if (only("connectorparts")) failures += run_connectorparts_tests();
    if (only("partweb")) failures += run_partweb_tests();
    if (only("libraryuse")) failures += run_libraryuse_tests();
    if (only("partspanel")) failures += run_partspanel_tests();
    if (only("entityrefs")) failures += run_entityrefs_tests();
    if (only("stepsbuilder")) failures += run_stepsbuilder_tests();
    if (only("licenses")) failures += run_licenses_tests();
    if (only("odaexport")) failures += run_odaexport_tests();
    if (only("mechscript")) failures += run_mechscript_tests();
    if (only("mechwizard")) failures += run_mechwizard_tests();
    if (only("brickharness")) failures += run_brickharness_tests();
    if (failures == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
