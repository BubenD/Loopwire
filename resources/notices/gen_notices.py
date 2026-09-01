"""Generate THIRD-PARTY-NOTICES.txt from the Qt install's SPDX SBOM.

WHY THIS IS GENERATED RATHER THAN WRITTEN BY HAND.

Edraw ships Qt's binaries beside Edraw.exe. Qt's DLLs statically contain third-party code - FreeType,
libpng, libjpeg, zlib, PCRE2, HarfBuzz, the Unicode tables, the Public Suffix List - whose licences
require their copyright notices to travel with the binary. A hand-written list of those components is
a list that goes stale the first time somebody adds a Qt module, and nothing in the build would say so.

Qt 6.8 ships a machine-readable SBOM describing exactly what is inside each module, with the copyright
text and licence of every bundled component. This reads it, so the notice file is a fact about the Qt
that is installed rather than a memory of the Qt that once was.

    python resources/notices/gen_notices.py            # uses the Qt in CMAKE_PREFIX_PATH / the default
    python resources/notices/gen_notices.py --check     # fail if the committed file is out of date

The result is COMMITTED. The build copies it beside the executable and never needs Python; this script
is run by whoever changes the Qt version, and --check is there so a stale file is a visible failure
rather than a silent one.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys
import textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
HEADER = os.path.join(HERE, "notices-header.txt")
OUT = os.path.join(REPO, "THIRD-PARTY-NOTICES.txt")

DEFAULT_QT = r"C:\Qt\6.8.3\msvc2022_64"

WIDTH = 98

# The Qt modules Edraw actually links and deploys. Everything credited below is credited because it is
# inside one of these, or inside a plugin windeployqt copies for them.
SHIPPED_QT_MODULES = [
    "Qt Core", "Qt GUI", "Qt Widgets", "Qt Network", "Qt PrintSupport", "Qt SVG",
]

# The Qt REPOSITORIES those modules come from, and therefore the only SBOMs worth reading. A Qt install
# describes everything it shipped, including repositories Edraw does not link: qtdeclarative (QML/Quick)
# and qttools (Qt Help, Designer, Linguist) between them account for Catch2, litehtml, Gumbo, XSVG and
# the JavaScriptCore assembler. Crediting those would not be generous, it would be false - it would tell
# the reader that code is inside a binary they were handed when it is not.
#
# Adding a Qt module means adding its repository here.
SHIPPED_QT_REPOS = ("qtbase", "qtsvg")

# NOT distributed, so not credited - and each exclusion is a claim that has to stay true:
#
#   Qt's build tools (moc, rcc, uic, qmake, windeployqt and friends) are GPL-3.0 with the Qt GPL
#   Exception 1.0. The exception is what lets them build proprietary software; it holds because we run
#   them and do not ship them. If Edraw ever ships a Qt tool, that tool needs its own notice and this
#   list is wrong.
#
#   Qt Test, Qt SQL and Qt D-Bus are not linked, so their bundled components (Catch2, valgrind headers,
#   SQLite, libdbus headers) are not conveyed.
#
#   The Linux and X11 attributions in the SBOM describe code that is not compiled into a Windows build.
SKIP_TOOL_PACKAGES = {
    "syncqt", "moc", "rcc", "uic", "qmake", "qlalr", "qvkgen", "qtpaths", "tracegen",
    "tracepointgen", "cmake_automoc_parser", "windeployqt", "macdeployqt", "androiddeployqt",
    "androidtestrunner", "qdbuscpp2xml", "qdbusxml2cpp", "qtwaylandscanner", "qsb", "balsam",
    "qmltyperegistrar", "qmlcachegen", "qmllint", "qmlformat", "qmlimportscanner", "lupdate",
    "lrelease", "lconvert", "qdoc", "qhelpgenerator", "qtattributionsscanner", "qtdiag",
    "qtplugininfo", "designer", "linguist", "assistant", "pixeltool", "qdbusviewer",
}

# Prefixes on SBOM package names that mark a component as belonging to a module we do not ship.
SKIP_MODULE_PREFIXES = (
    "Test_", "TestInternals", "TestPrivate",
    "Sql_", "QSQLite", "QPSQL", "QMYSQL", "QODBC", "QOCI", "QMimer",
    "DBus_", "Platform_", "Bootstrap_",
    "Widgets_Attribution_qtwidgets",  # no such thing today; guards a rename
)

# Components the SBOM attributes to a module we ship but that are Unix-only. Kept explicit and small,
# because the cost of wrongly dropping one is an uncredited component and the cost of wrongly keeping
# one is a harmless extra paragraph.
SKIP_UNIX_ONLY = {
    "Gui_Attribution_xserverhelper",
    "Gui_Attribution_opengl-es2-headers",
    "Gui_Attribution_vulkan-xml-spec",
    "Gui_Attribution_vulkanmemoryallocator",
}


# ==================================================================================================
# THE GPL AUDIT
#
# Edraw is proprietary. It may link code that is available under the LGPL, because the LGPL permits a
# proprietary combined work; it may NOT link code that is only available under the GPL, because the GPL
# would require Edraw's own source to be published. The distance between those two is one line in a
# CMakeLists, and nothing about adding that line looks like a licensing decision at the time.
#
# So it is checked rather than remembered. `--audit-gpl` answers one question: is anything Edraw links or
# ships available ONLY under the GPL?
#
# Two traps this is built around:
#
#   1. Not every Qt module is LGPL. Qt Charts, Qt Data Visualization, Qt Quick 3D and the Qt Virtual
#      Keyboard are offered as GPL-or-commercial with no LGPL option at all. `find_package(Qt6 ...
#      COMPONENTS Charts)` is all it takes, and the build would succeed.
#
#   2. A module whose Qt repository is not in SHIPPED_QT_REPOS is a module whose bundled components go
#      uncredited. Linking one is therefore also a failure, even if the module itself is LGPL.
# ==================================================================================================

# Which Qt repository each module's code lives in - so a linked module can be traced to the SBOM that
# describes it. Only modules plausible for this application; an unknown module is reported, not assumed.
QT_MODULE_REPO = {
    "Core": "qtbase", "Gui": "qtbase", "Widgets": "qtbase", "Network": "qtbase",
    "PrintSupport": "qtbase", "Concurrent": "qtbase", "Xml": "qtbase", "Sql": "qtbase",
    "Test": "qtbase", "DBus": "qtbase", "OpenGL": "qtbase", "OpenGLWidgets": "qtbase",
    "Svg": "qtsvg", "SvgWidgets": "qtsvg",
    "Qml": "qtdeclarative", "Quick": "qtdeclarative", "QuickWidgets": "qtdeclarative",
    "QuickControls2": "qtdeclarative",
    "Charts": "qtcharts", "DataVisualization": "qtdatavis3d", "Quick3D": "qtquick3d",
    "VirtualKeyboard": "qtvirtualkeyboard",
    "Pdf": "qtwebengine", "PdfWidgets": "qtwebengine",
    "WebEngineCore": "qtwebengine", "WebEngineWidgets": "qtwebengine",
    "SerialPort": "qtserialport", "Multimedia": "qtmultimedia",
}

# Modules known to have NO LGPL option. A backstop for the case where the module is named in the build
# but not installed, so there is no SBOM to read and the SBOM check would find nothing to complain about.
QT_MODULES_WITHOUT_LGPL = {
    "Charts", "DataVisualization", "Quick3D", "VirtualKeyboard",
}


def gpl_variants_only(license_expr: str) -> bool:
    """True if EVERY alternative in an SPDX 'A OR B' expression is a GPL variant.

    "FTL OR GPL-2.0-only" is fine - FreeType can be taken under the FTL. "GPL-3.0-only" alone is not.
    The LGPL is deliberately NOT counted as a GPL variant here: it is the licence that makes a
    proprietary combined work possible, and is the whole basis on which Edraw ships Qt.
    """
    options = [o.strip().strip("()") for o in license_expr.split(" OR ")]
    if not options:
        return False
    for opt in options:
        if "LGPL" in opt:
            return False                       # an LGPL option exists: not a GPL-only component
        if "Qt-GPL-exception" in opt:
            return False                       # the exception is what permits proprietary use
        if "GPL" not in opt:
            return False                       # some non-GPL alternative exists
    return True


def linked_qt_modules(repo_root: str) -> list[str]:
    """The Qt modules the build asks for, read out of CMakeLists.txt."""
    path = os.path.join(repo_root, "CMakeLists.txt")
    try:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
    except OSError:
        return []
    mods: list[str] = []
    # find_package(Qt6 REQUIRED COMPONENTS Widgets Gui ...)
    for m in re.finditer(r"find_package\s*\(\s*Qt6[^)]*?COMPONENTS([^)]*)\)", text,
                         re.IGNORECASE | re.DOTALL):
        for tok in m.group(1).split():
            if tok.upper() in ("REQUIRED", "QUIET", "OPTIONAL_COMPONENTS", "CONFIG"):
                continue
            mods.append(tok)
    # Qt6::Widgets in target_link_libraries, which is how a module can arrive without find_package
    for m in re.finditer(r"Qt6::(\w+)", text):
        mods.append(m.group(1))
    # qmake and other imported tool targets are not modules.
    drop = {"qmake", "rcc", "moc", "uic", "windeployqt"}
    return sorted({m for m in mods if m not in drop})


def audit_gpl(sbom_dir: str, qt_version: str) -> int:
    """Report anything Edraw links or ships that is available only under the GPL. Returns a failure count."""
    problems: list[str] = []
    notes: list[str] = []

    # ---- 1. The modules the build asks for -------------------------------------------------------
    modules = linked_qt_modules(REPO)
    print(f"Qt modules linked by the build ({len(modules)}): {', '.join(modules) or '(none found)'}")
    for mod in modules:
        if mod in QT_MODULES_WITHOUT_LGPL:
            problems.append(
                f"Qt{mod} has NO LGPL option - it is GPL-or-commercial only. Linking it obliges ETS "
                f"either to publish Edraw's source or to hold a commercial Qt licence.")
            continue
        repo = QT_MODULE_REPO.get(mod)
        if repo is None:
            problems.append(
                f"Qt{mod} is not a module this audit knows. Add it to QT_MODULE_REPO in "
                f"{os.path.relpath(__file__, REPO)} with the Qt repository it comes from, and check its "
                f"licence in that repository's SBOM before shipping.")
        elif repo not in SHIPPED_QT_REPOS:
            problems.append(
                f"Qt{mod} comes from the Qt repository '{repo}', which is not in SHIPPED_QT_REPOS. Its "
                f"bundled third-party components are therefore NOT credited in THIRD-PARTY-NOTICES.txt. "
                f"Add '{repo}' to SHIPPED_QT_REPOS and regenerate.")

    # ---- 2. Everything inside the repositories that are shipped ----------------------------------
    #
    # A missing SBOM is not a violation, so it must not fail the build - this runs on every build, and a
    # Qt too old to publish an SBOM would otherwise make Edraw unbuildable. It IS worth shouting about,
    # because it means part of this audit did not happen.
    all_sboms = sorted(glob.glob(os.path.join(sbom_dir, "*.spdx.json")))
    if not all_sboms:
        print(f"\nWARNING: no SPDX SBOM in {sbom_dir}.\n"
              f"         The component-level half of the GPL audit did not run. The module-level check "
              f"above did.\n"
              f"         Qt 6.8 and later publish an SBOM; check the licence of anything Edraw links by "
              f"hand until then.", file=sys.stderr)
        if problems:
            print(f"\nGPL EXPOSURE - {len(problems)} problem(s):", file=sys.stderr)
            for p in problems:
                print(f"\n  * {p}", file=sys.stderr)
        return len(problems)

    files = [f for f in all_sboms
             if os.path.basename(f).split("-")[0] in SHIPPED_QT_REPOS]
    checked = 0
    for path in files:
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
        for pkg in doc.get("packages", []):
            name = pkg.get("name") or ""
            lic = pkg.get("licenseConcluded") or pkg.get("licenseDeclared") or ""
            if not lic or lic == "NOASSERTION":
                continue
            checked += 1

            is_tool = name in SKIP_TOOL_PACKAGES
            not_shipped = (is_tool or name in SKIP_UNIX_ONLY
                           or name.startswith(SKIP_MODULE_PREFIXES))

            if not gpl_variants_only(lic):
                continue

            if not_shipped:
                # A GPL-only thing we do not convey. Recorded so the claim stays visible: these are
                # only harmless for as long as they really are not shipped.
                notes.append(f"{name} ({lic}) - GPL-only but not distributed"
                             f"{' (build tool, Qt GPL Exception applies to its use)' if is_tool else ''}")
            else:
                problems.append(
                    f"{name} is available only under {lic}, and is inside something Edraw ships. "
                    f"Either it must not be shipped, or Edraw's source must be published.")

    print(f"Components examined in {', '.join(SHIPPED_QT_REPOS)} (Qt {qt_version}): {checked}")
    if notes:
        print(f"\nGPL-only but NOT distributed ({len(notes)}) - fine only while that stays true:")
        for n in sorted(set(notes)):
            print(f"  - {n}")

    if problems:
        print(f"\nGPL EXPOSURE - {len(problems)} problem(s):", file=sys.stderr)
        for p in problems:
            print(f"\n  * {p}", file=sys.stderr)
        print("", file=sys.stderr)
    else:
        print("\nNo GPL exposure: nothing Edraw links or ships is GPL-only.")
    return len(problems)


def wrap(text: str, indent: str = "  ", hang: str = "") -> str:
    """Wrap to WIDTH. `hang` indents continuation lines, so a long copyright line that runs over still
    reads as one entry rather than as two."""
    out = []
    for para in text.split("\n"):
        para = para.rstrip()
        if not para:
            out.append("")
            continue
        out.extend(textwrap.wrap(para, width=WIDTH, initial_indent=indent,
                                 subsequent_indent=indent + hang, break_long_words=False,
                                 break_on_hyphens=False))
    return "\n".join(out)


def field(comment: str, key: str) -> str:
    """Pull 'Name:' / 'Description:' / 'License:' out of the SBOM's human-readable comment block."""
    for line in (comment or "").splitlines():
        line = line.strip()
        if line.startswith(key + ":"):
            return line[len(key) + 1:].strip()
    return ""


def collect(sbom_dir: str) -> list[dict]:
    all_files = sorted(glob.glob(os.path.join(sbom_dir, "*.spdx.json")))
    if not all_files:
        sys.exit(f"No SPDX JSON found in {sbom_dir}\n"
                 f"Point --qt at a Qt 6.8+ install (the one whose DLLs you ship).")

    files = [f for f in all_files
             if os.path.basename(f).split("-")[0] in SHIPPED_QT_REPOS]
    missing = [r for r in SHIPPED_QT_REPOS
               if not any(os.path.basename(f).startswith(r + "-") for f in all_files)]
    if missing:
        sys.exit(f"No SBOM for shipped Qt repository/-ies {', '.join(missing)} in {sbom_dir}.\n"
                 f"Refusing to write a notice file that would silently omit them.")

    seen: dict[tuple, dict] = {}
    for path in files:
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
        for pkg in doc.get("packages", []):
            name = pkg.get("name") or ""
            lic = pkg.get("licenseConcluded") or pkg.get("licenseDeclared") or ""

            # Qt's own code is covered by the LGPL section of the header, not by a per-component entry.
            if "Qt-Commercial" in lic:
                continue
            if name in SKIP_TOOL_PACKAGES or name in SKIP_UNIX_ONLY:
                continue
            if name.startswith(SKIP_MODULE_PREFIXES):
                continue
            if lic in ("", "NOASSERTION"):
                continue  # toolchain/system stubs (MSVC, ODBC, PostgreSQL): nothing conveyed by us

            copyright_text = (pkg.get("copyrightText") or "").strip()
            if copyright_text in ("NOASSERTION", "NONE"):
                copyright_text = ""

            pretty = field(pkg.get("comment", ""), "Name") or name
            # BundledFreetype and FreetypePrivate are the same code seen twice; one credit is right.
            key = (pretty.lower(), lic, copyright_text[:200])
            if key in seen:
                continue
            seen[key] = {
                "name": pretty,
                "license": lic,
                "license_pretty": field(pkg.get("comment", ""), "License") or lic,
                "copyright": copyright_text,
                "homepage": (pkg.get("homepage") or "").strip(),
                # "unknown" is what the SBOM says when a vendored snapshot carries no version. Printing
                # it adds nothing; the copyright notice is what the licence actually asks for.
                "version": "" if (pkg.get("versionInfo") or "").strip().lower() in ("unknown", "noassertion")
                           else (pkg.get("versionInfo") or "").strip(),
                "description": field(pkg.get("comment", ""), "Description"),
            }
    return sorted(seen.values(), key=lambda p: p["name"].lower())


def render(components: list[dict], qt_version: str) -> str:
    with open(HEADER, encoding="utf-8") as fh:
        header = fh.read()
    header = header.replace("@QT_VERSION@", qt_version)
    header = header.replace("@QT_MODULES@", ", ".join(SHIPPED_QT_MODULES))

    lines = [header.rstrip(), ""]
    lines.append("=" * WIDTH)
    lines.append("SCHEDULE OF THIRD-PARTY COMPONENTS WITHIN QT")
    lines.append("=" * WIDTH)
    lines.append("")
    lines.append(wrap(
        f"The following components are contained within the Qt {qt_version} libraries listed above and "
        f"are therefore distributed with Edraw. Each is reproduced here with the copyright notices its "
        f"licence requires be retained. This schedule is generated from the SPDX SBOM published with "
        f"the Qt installation, and is deliberately inclusive: a component listed here may not be active "
        f"in every build configuration.", indent=""))
    lines.append("")
    lines.append(f"{len(components)} components.")
    lines.append("")

    for i, c in enumerate(components, 1):
        title = c["name"]
        if c["version"]:
            title += f" {c['version']}"
        lines.append("-" * WIDTH)
        lines.append(f"{i}. {title}")
        lines.append("-" * WIDTH)
        if c["description"]:
            lines.append(wrap(c["description"]))
        lines.append("")
        lines.append(f"  Licence: {c['license_pretty']}")
        lines.append(f"  SPDX:    {c['license']}")
        if c["homepage"]:
            lines.append(f"  Home:    {c['homepage']}")
        if c["copyright"]:
            lines.append("")
            lines.append(wrap(c["copyright"], hang="    "))
        lines.append("")

    lines.append("=" * WIDTH)
    lines.append("END OF THIRD-PARTY NOTICES")
    lines.append("=" * WIDTH)
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--qt", default=os.environ.get("EDRAW_QT_DIR", DEFAULT_QT),
                    help="Qt install whose DLLs are shipped (default: %(default)s)")
    ap.add_argument("--check", action="store_true",
                    help="do not write; exit non-zero if the committed file is out of date")
    ap.add_argument("--audit-gpl", action="store_true",
                    help="do not write; exit non-zero if anything linked or shipped is GPL-only")
    args = ap.parse_args()

    sbom_dir = os.path.join(args.qt, "sbom")
    qt_version = os.path.basename(os.path.dirname(os.path.abspath(args.qt))) or "6"

    if args.audit_gpl:
        return 1 if audit_gpl(sbom_dir, qt_version) else 0

    components = collect(sbom_dir)
    text = render(components, qt_version)

    if args.check:
        if not os.path.exists(OUT):
            print(f"MISSING: {OUT}", file=sys.stderr)
            return 1
        with open(OUT, encoding="utf-8", newline="") as fh:
            current = fh.read()
        if current.replace("\r\n", "\n") != text:
            print(f"OUT OF DATE: {OUT}\n"
                  f"Run: python resources/notices/gen_notices.py", file=sys.stderr)
            return 1
        print(f"up to date ({len(components)} components, Qt {qt_version})")
        return 0

    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print(f"wrote {os.path.relpath(OUT, REPO)}: {len(components)} components, Qt {qt_version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
