// Minimal assertion-based test runner for Ewire core logic.
// Each test function returns the number of failures; main aggregates them.
#include "harness/MechModels.h"
#include <QApplication>

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

namespace {
// A RUN THAT CRASHES SAYS WHERE: the exception and the calls that led to it, function by function (and file and line
// where the build has its symbols), before the process goes - so an intermittent crash can be found from its log.
LONG WINAPI sayCrash(EXCEPTION_POINTERS* info) {
    std::fprintf(stdout, "\nCRASH: exception 0x%08lX at %p\n", info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress);
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 40; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0)
            break;
        char buffer[sizeof(SYMBOL_INFO) + 512] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 offset = 0;
        const char* name = SymFromAddr(process, frame.AddrPC.Offset, &offset, symbol) ? symbol->Name : "?";
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD column = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &column, &line))
            std::fprintf(stdout, "  #%d %s  %s:%lu\n", depth, name, line.FileName, line.LineNumber);
        else
            std::fprintf(stdout, "  #%d %s\n", depth, name);
    }
    std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace
#endif

int run_geometry_tests();
int run_latency_tests();
int run_editor_tests();
int run_io_tests();
int run_harness_tests();
int run_harnessbuilder_tests();
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
int run_systemdesign_tests();

int main(int argc, char** argv) {
    // UNBUFFERED: a run that dies leaves its log up to the line it died after, not a page short of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _WIN32
    SetUnhandledExceptionFilter(sayCrash);
#endif
    // A window can only be built with an application object behind it, and the step builder is a
    // window. Offscreen, so this runs the same on a machine with no display as on one with a screen -
    // set here rather than left to whoever starts the tests.
    qputenv("QT_QPA_PLATFORM", qEnvironmentVariableIsEmpty("LOOPWIRE_TEST_PLATFORM")
                                 ? QByteArray("offscreen") : qgetenv("LOOPWIRE_TEST_PLATFORM"));
#ifdef _WIN32
    // THE SYSTEM'S FONTS FOR THE OFFSCREEN PLATFORM. It looks for fonts beside Qt, where there are none, and then
    // draws every letter as an empty box - a PDF written in a test then has no text in it to search.
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
        const QByteArray windows = qgetenv("SystemRoot");
        qputenv("QT_QPA_FONTDIR", (windows.isEmpty() ? QByteArray("C:\\Windows") : windows) + "\\Fonts");
    }
#endif
    // A NAME, SO SETTINGS HAVE SOMEWHERE TO LIVE. Without an organisation and an application name QSettings
    // has no file at all - it answers AccessError and every write is dropped - so anything the program
    // REMEMBERS between sessions was untestable, and a test of it passed or failed for reasons that had
    // nothing to do with the code. Its own name, not the application's, so a test run cannot tread on what
    // somebody's real Loopwire remembers.
    QApplication::setOrganizationName(QStringLiteral("Whatson"));
    QApplication::setApplicationName(QStringLiteral("Loopwire tests"));
    QApplication app(argc, argv);
    // THE HOUSE MODELS ARE SHIPPED INSIDE THE BINARY, and the tests must not stand on them. Half of what
    // is checked here is what the program does when NOTHING is drawn for a kind yet - a wizard asked for a
    // boot it has no model of, a manager showing a kind with an empty drawing - and with a set of models
    // built into the executable there is no such thing as nothing unless it is asked for. So it is asked
    // for: the application starts from the house style, a test starts from an empty sheet.
    Loopwire::MechModelSet::setShippedDefaultsEnabled(false);
    // Unbuffered, so a crash mid-suite still shows how far it got.
    setvbuf(stdout, nullptr, _IONBF, 0);
    // SOME SUITES ONLY, by name (LOOPWIRE_TEST_ONLY=brickharness,io): a failure looked into without the whole run.
    const QList<QByteArray> onlySuites = qgetenv("LOOPWIRE_TEST_ONLY").split(',');
    auto only = [&](const char* name) { return qgetenv("LOOPWIRE_TEST_ONLY").isEmpty() || onlySuites.contains(QByteArray(name)); };
    int failures = 0;
    // Performance measurements are opt-in, so normal correctness runs stay short.
    if (onlySuites.contains(QByteArray("latency"))) failures += run_latency_tests();
    if (only("geometry")) failures += run_geometry_tests();
    if (only("editor")) failures += run_editor_tests();
    if (only("io")) failures += run_io_tests();
    if (only("harness")) failures += run_harness_tests();
    if (only("harnessbuilder")) failures += run_harnessbuilder_tests();
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
    if (only("systemdesign")) failures += run_systemdesign_tests();
    if (failures == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
