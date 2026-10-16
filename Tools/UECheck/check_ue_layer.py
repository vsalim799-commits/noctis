#!/usr/bin/env python3
"""Engine-free checks of the Unreal layer of Noctis (no Unreal Engine needed).

  python3 Tools/UECheck/check_ue_layer.py                 # syntax/type check of Source/Noctis + Source/NoctisEditor
  python3 Tools/UECheck/check_ue_layer.py --shipping      # same, WITH_EDITOR=0 / no automation tests / no debug draw
  python3 Tools/UECheck/check_ue_layer.py --core          # NoctisCore sources as UBT compiles them (NoPCH, UE macros)
  python3 Tools/UECheck/check_ue_layer.py --snapshot      # build + run the standalone mirror of the automation tests
  python3 Tools/UECheck/check_ue_layer.py --all           # everything above
  python3 Tools/UECheck/check_ue_layer.py Runner Widgets  # only translation units whose path contains these strings

What the syntax check is: every .cpp of the game and editor modules is compiled with `clang++ -fsyntax-only` (C++20,
-fno-exceptions -fno-rtti -Wall -Wextra -Wshadow-all -Wundef -Werror, like UBT's shadow/undefined-identifier errors)
against the REAL NoctisCore headers and a hand-written stand-in of the Unreal API (Tools/UECheck/stub, UE 5.4/5.5
signatures). UHT is emulated by rewriting GENERATED_BODY() into Super/ThisClass typedefs; *.generated.h are empty.
It catches: drift between the UE layer and the core API, C++ errors in the UE code, printf-style misuse in UE_LOG /
FString::Printf (FString passed by value), shadowing, and calls that do not match the UE signatures as the stand-in
declares them. It does NOT prove that the code compiles with the real engine: the stand-in is written from the
documentation, so a wrong assumption about a UE signature is wrong in both places. Only UBT can settle those.

Outputs go to build/uecheck/ (ignored by git). Needs clang++ (>= 14) and, for --snapshot, a CMake build of the core
(build/libnoctis_core.a, `cmake --build build --target noctis_core`).
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
WORK = os.path.join(REPO, "build", "uecheck")
MIRROR = os.path.join(WORK, "mirror")
STUBINC = os.path.join(WORK, "stubinc")
STUB = os.path.join(HERE, "stub")
COREINC = os.path.join(HERE, "coreinc")
CORE_PUBLIC = os.path.join(REPO, "Source", "NoctisCore", "Public")
CORE_PRIVATE = os.path.join(REPO, "Source", "NoctisCore", "Private")
CXX = os.environ.get("CXX_CLANG", "clang++")

WARNINGS = ["-Wall", "-Wextra", "-Wshadow-all", "-Wundef", "-Wno-unused-parameter", "-Wno-unused-private-field",
            "-Wno-missing-field-initializers", "-Werror"]


# ----------------------------------------------------------------------------------------------- UHT emulation

def _balanced(text: str, start: int) -> int:
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError("unbalanced parentheses")


_DECL = re.compile(r"\s*(class|struct)\s+(?:\w+_API\s+)?(\w+)\s*(?:final\s*)?(?::\s*public\s+([\w:]+))?")
_REFLECT = re.compile(r"\b(UCLASS|USTRUCT|UINTERFACE)\s*\(|\bGENERATED_BODY\s*\(\s*\)|\bGENERATED_UCLASS_BODY\s*\(\s*\)")


def rewrite_generated_body(text: str) -> str:
    """UCLASS(...) class X : public Y { GENERATED_BODY() -> NOCTIS_STUB_GENERATED_CLASS(X, Y) (Super, ThisClass, StaticClass)."""
    out, pos, pending = [], 0, []
    while True:
        m = _REFLECT.search(text, pos)
        if not m:
            out.append(text[pos:])
            return "".join(out)
        out.append(text[pos:m.start()])
        if m.group(1):
            end = _balanced(text, text.index("(", m.start()))
            decl = _DECL.match(text, end)
            if decl:
                pending.append((m.group(1), decl.group(2), decl.group(3)))
            out.append(text[m.start():end])
            pos = end
        else:
            kind, name, base = pending.pop(0) if pending else ("USTRUCT", "Unknown", None)
            out.append(f"NOCTIS_STUB_GENERATED_STRUCT({name})" if kind == "USTRUCT" else f"NOCTIS_STUB_GENERATED_CLASS({name}, {base or 'UObject'})")
            pos = m.end()


def build_mirror() -> None:
    if os.path.isdir(MIRROR):
        shutil.rmtree(MIRROR)
    for module in ("Noctis", "NoctisEditor"):
        src = os.path.join(REPO, "Source", module)
        for root, _, files in os.walk(src):
            for name in files:
                if not name.endswith((".h", ".cpp")):
                    continue
                source = os.path.join(root, name)
                dest = os.path.join(MIRROR, module, os.path.relpath(source, src))
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(source, encoding="utf-8") as handle:
                    text = handle.read()
                with open(dest, "w", encoding="utf-8") as handle:
                    handle.write(rewrite_generated_body(text))
    dest = os.path.join(MIRROR, "NoctisCore", "Private", "NoctisCoreModule.cpp")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.copy(os.path.join(CORE_PRIVATE, "NoctisCoreModule.cpp"), dest)


def include_dirs() -> list:
    return [os.path.join(MIRROR, "Noctis", "Public"), os.path.join(MIRROR, "Noctis", "Private"),
            os.path.join(MIRROR, "NoctisEditor", "Private"), CORE_PUBLIC]


def build_stub_includes() -> None:
    """One forwarding header per engine include the UE layer uses (all map to stub/ue_all.h)."""
    if os.path.isdir(STUBINC):
        shutil.rmtree(STUBINC)
    os.makedirs(STUBINC)
    includes = set()
    for root, _, files in os.walk(MIRROR):
        for name in files:
            with open(os.path.join(root, name), encoding="utf-8") as handle:
                includes.update(re.findall(r'#include\s+"([^"]+)"', handle.read()))
    with open(os.path.join(CORE_PUBLIC, "Noctis", "Core", "Platform.h"), encoding="utf-8") as handle:
        includes.update(re.findall(r'#include\s+"([^"]+)"', handle.read()))
    for inc in sorted(includes):
        if inc.startswith("Noctis/") or any(os.path.isfile(os.path.join(d, inc)) for d in include_dirs()):
            continue
        path = os.path.join(STUBINC, inc)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("#pragma once\n" if inc.endswith(".generated.h") else '#pragma once\n#include "ue_all.h"\n')


# ----------------------------------------------------------------------------------------------- checks

def ue_flags(shipping: bool) -> list:
    flags = [CXX, "-std=c++20", "-fsyntax-only", "-fno-exceptions", "-fno-rtti", "-ferror-limit=50"] + WARNINGS
    flags += ["-DNOCTISCORE_API=", "-DNOCTIS_API=", "-DNOCTISEDITOR_API="]
    if shipping:
        flags.append("-DNOCTIS_STUB_SHIPPING=1")
    for d in include_dirs():
        flags += ["-I", d]
    return flags + ["-isystem", STUBINC, "-isystem", STUB]


def core_flags() -> list:
    # NoctisCore.Build.cs: PCHUsage = NoPCHs, so a core TU sees CoreTypes.h + AssertionMacros.h only (Platform.h).
    return ([CXX, "-std=c++20", "-fsyntax-only", "-fno-exceptions", "-fno-rtti", "-ferror-limit=50"] + WARNINGS +
            ["-DNOCTISCORE_API=", "-DNOCTIS_UNREAL=1", "-I", COREINC, "-I", CORE_PUBLIC, "-I", CORE_PRIVATE])


def run_all(label: str, commands: list, jobs: int) -> int:
    failed = 0

    def run(item):
        name, cmd = item
        result = subprocess.run(cmd, capture_output=True, text=True)
        return name, result.returncode, result.stderr

    with cf.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        for name, code, err in pool.map(run, commands):
            if code == 0:
                print(f"  OK    {name}")
            else:
                failed += 1
                lines = [line for line in err.splitlines() if not line.startswith("In file included")]
                print(f"  FAIL  {name}\n" + "\n".join("        " + line for line in lines[:60]))
    print(f"{label} : {len(commands) - failed}/{len(commands)} unités de compilation OK")
    return failed


def check_ue_layer(filters: list, shipping: bool, jobs: int) -> int:
    build_mirror()
    build_stub_includes()
    sources = []
    for root, _, files in os.walk(MIRROR):
        for name in files:
            if name.endswith(".cpp"):
                sources.append(os.path.join(root, name))
    sources = sorted(s for s in sources if (not filters or any(f in s for f in filters)) and
                     not (shipping and os.sep + "NoctisEditor" + os.sep in s))
    commands = [(os.path.relpath(s, MIRROR), ue_flags(shipping) + [s]) for s in sources]
    return run_all("Couche UE" + (" (configuration Shipping)" if shipping else ""), commands, jobs)


def check_core(jobs: int) -> int:
    sources = []
    for root, _, files in os.walk(CORE_PRIVATE):
        for name in files:
            if name.endswith(".cpp") and name != "NoctisCoreModule.cpp":
                sources.append(os.path.join(root, name))
    commands = [(os.path.relpath(s, REPO), core_flags() + [s]) for s in sorted(sources)]
    return run_all("NoctisCore en mode UBT", commands, jobs)


def check_snapshot(data_dir: str) -> int:
    library = os.path.join(REPO, "build", "libnoctis_core.a")
    if not os.path.isfile(library):
        print(f"{library} absent : cmake --build build --target noctis_core")
        return 1
    exe = os.path.join(WORK, "snapshot_checks")
    os.makedirs(WORK, exist_ok=True)
    cmd = ["g++", "-std=c++20", "-O2", "-DNOCTIS_STANDALONE=1", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Wshadow",
           "-Wno-unused-parameter", "-Werror", "-I", CORE_PUBLIC, "-I", os.path.join(REPO, "Source", "Noctis", "Private"),
           os.path.join(HERE, "snapshot_checks_main.cpp"), library, "-lpthread", "-o", exe]
    print("Compilation du miroir des tests d'automatisation...")
    if subprocess.run(cmd).returncode != 0:
        return 1
    return subprocess.run([exe, data_dir]).returncode


def main(argv) -> int:
    parser = argparse.ArgumentParser(prog="check_ue_layer.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("filters", nargs="*", help="ne vérifier que les fichiers dont le chemin contient ces chaînes")
    parser.add_argument("--shipping", action="store_true", help="configuration sans éditeur ni tests (WITH_EDITOR=0...)")
    parser.add_argument("--core", action="store_true", help="compile le coeur comme UBT (NoPCH, macros UE)")
    parser.add_argument("--snapshot", action="store_true", help="miroir autonome des tests d'automatisation (déterminisme)")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--data", default=os.path.join(REPO, "Data"))
    parser.add_argument("-j", "--jobs", type=int, default=2)
    args = parser.parse_args(argv)
    if shutil.which(CXX) is None:
        print(f"{CXX} introuvable (installer clang ou définir CXX_CLANG).")
        return 2
    failed = 0
    if args.all or not (args.core or args.snapshot) or args.shipping:
        failed += check_ue_layer(args.filters, args.shipping, args.jobs)
    if args.all:
        failed += check_ue_layer(args.filters, True, args.jobs)
    if args.all or args.core:
        failed += check_core(args.jobs)
    if args.all or args.snapshot:
        failed += 1 if check_snapshot(args.data) != 0 else 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
