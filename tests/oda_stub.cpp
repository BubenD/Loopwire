// A stand-in for the ODA File Converter, so the export pipeline can be tested on a machine that does
// not have it.
//
// WHY THIS EXISTS. OdaConverter's job is almost entirely orchestration: lay out a temporary
// directory, write a DXF into it, invoke another program with six positional arguments in an order
// that is not ours to choose, work out from the result whether a DWG appeared, and move it. Every one
// of those steps can be wrong in a way that compiles. None of them can be exercised without something
// on the other end of QProcess, and the real converter cannot be a build dependency: Edraw is not
// allowed to redistribute it (that is the whole reason for the setup dialog), so it will not be
// present on a build machine or in CI.
//
// So the test supplies the other end. This binary implements the same command-line contract the real
// converter does - and nothing else - which is exactly the surface the manager depends on.
//
// It deliberately does NOT produce a real DWG. Nothing downstream of the manager reads the bytes; the
// manager's contract is "a non-empty file with the right name appeared in the output directory", and
// that is what is checked. A test that needed a genuine DWG here would be testing ODA, not Edraw.
//
// Behaviour is steered by EDRAW_ODA_STUB_MODE, so the failure paths are reachable too:
//
//   (unset) / "ok"    convert every .dxf in the input directory
//   "no-output"       exit 0 having written nothing   - the silent failure the manager guards against
//   "exit-fail"       exit 1 having written nothing
//   "exit-fail-ok"    exit 1 but write the output anyway - some ODA versions do this
//   "hang"            sleep past any sensible timeout, to exercise the watchdog
//   "needs-display"   refuse to run when QT_QPA_PLATFORM is set, the way the real converter does when
//                     its build shipped no offscreen plugin - so the hidden-run fallback is tested

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    // The real converter takes: <input dir> <output dir> <version> <type> <recurse> <audit>
    // Anything else is a caller that has got the contract wrong, and saying so is the point.
    if (argc < 7) {
        std::fprintf(stderr, "oda_stub: expected 6 arguments, got %d\n", argc - 1);
        return 2;
    }
    const fs::path inDir  = argv[1];
    const fs::path outDir = argv[2];
    const std::string version = argv[3];
    const std::string type    = argv[4];
    const std::string recurse = argv[5];
    const std::string audit   = argv[6];

    // The arguments Edraw is expected to send. Checked rather than ignored: if the manager ever sends
    // "ACAD2018" as "ACAD 2018", or swaps type and version, the real converter would reject it with a
    // message about arguments and no clue which one. Here that becomes a named test failure.
    if (version != "ACAD2018") {
        std::fprintf(stderr, "oda_stub: unexpected version '%s'\n", version.c_str());
        return 2;
    }
    if (type != "DWG") {
        std::fprintf(stderr, "oda_stub: unexpected output type '%s'\n", type.c_str());
        return 2;
    }
    if (recurse != "0" || audit != "1") {
        std::fprintf(stderr, "oda_stub: unexpected recurse/audit '%s'/'%s'\n",
                     recurse.c_str(), audit.c_str());
        return 2;
    }

    const char* modeEnv = std::getenv("EDRAW_ODA_STUB_MODE");
    const std::string mode = modeEnv ? modeEnv : "ok";

    // WHAT THE REAL CONVERTER DOES WHEN IT CANNOT DRAW.
    //
    // Edraw runs it with QT_QPA_PLATFORM=offscreen so no window appears. That works only if ODA's
    // build shipped the offscreen plugin; when it did not, Qt does not quietly fall back to a window,
    // it refuses to start with "no Qt platform plugin could be initialized" and converts nothing.
    // Reproduced here so the retry that follows is exercised rather than assumed.
    const char* platform = std::getenv("QT_QPA_PLATFORM");

    // WHAT ENVIRONMENT DID EDRAW ACTUALLY HAND US? Written down where the test can read it.
    //
    // Whether QT_QPA_PLATFORM was set is the single thing that decides if the real converter shows a
    // modal error box instead of converting, and it is invisible from outside the process. Recording
    // it here is what lets a test assert that Edraw only asks for the offscreen platform when the
    // plugin to serve it actually exists.
    // Written to EDRAW_ODA_STUB_REPORT when the test names one, because the manager deletes its
    // temporary directory on the way out and a report left in there would be gone before it could be
    // read.
    if (const char* report = std::getenv("EDRAW_ODA_STUB_REPORT")) {
        std::ofstream pf(report);
        if (pf) pf << ((platform && *platform) ? platform : "unset");
    }

    if (mode == "needs-display" && platform && *platform) {
        std::fprintf(stderr,
                     "qt.qpa.plugin: Could not load the Qt platform plugin \"%s\"\n"
                     "This application failed to start because no Qt platform plugin could be "
                     "initialized.\n",
                     platform);
        return 1;
    }

    if (mode == "hang") {
        std::this_thread::sleep_for(std::chrono::seconds(600));
        return 0;
    }
    if (mode == "no-output") return 0;   // the dangerous one: success reported, nothing produced
    if (mode == "exit-fail") return 1;

    std::error_code ec;
    fs::create_directories(outDir, ec);

    int converted = 0;
    for (const auto& entry : fs::directory_iterator(inDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".dxf") continue;

        const fs::path out = outDir / (entry.path().stem().string() + ".dwg");
        std::ofstream f(out, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "oda_stub: could not write %s\n", out.string().c_str());
            return 1;
        }
        // An AC1032 signature and enough bytes to be non-empty. Not a DWG, and not pretending to be:
        // see the note at the top.
        f << "AC1032";
        f << std::string(512, '\0');
        ++converted;
    }

    if (mode == "exit-fail-ok") return 1;
    return converted > 0 ? 0 : 0;   // ODA reports 0 even with nothing to do
}
