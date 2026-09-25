"""Check a Windows build without running it: shantae.exe and every DLL it
reaches beside it are the expected machine type, and each function they import
from a DLL shipped beside them is one that DLL exports. This catches a DLL from
another package version or arch, which is all that can be checked of the ARM64
build on an x64 PC. Windows' own DLLs are not looked into."""
import argparse
import os
from pathlib import Path
import re
import subprocess

MACHINES = {"x64": "AMD64", "x86": "I386", "arm64": "ARM64"}


def readobj(tool, flag, path):
    return subprocess.run([tool, flag, str(path)], capture_output=True, text=True, check=True).stdout


def imports(tool, path):
    """{dll name (lowercase): [imported names]}; ordinal imports are skipped."""
    result, current = {}, None
    for line in readobj(tool, "--coff-imports", path).splitlines():
        line = line.strip()
        if line.startswith("Name: "):
            current = result.setdefault(line[6:].lower(), [])
        elif line.startswith("Symbol: ") and current is not None:
            name = re.sub(r" \(\d+\)$", "", line[8:])
            if name:
                current.append(name)
    return result


def exports(tool, path):
    return {line.strip()[6:] for line in readobj(tool, "--coff-exports", path).splitlines()
            if line.strip().startswith("Name: ")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=sorted(MACHINES))
    parser.add_argument("dir", type=Path, help="the build dir (shantae.exe and its DLLs)")
    parser.add_argument("--readobj", default=os.environ.get("SHANTAE_MSYS2", "C:/msys64") +
                        "/mingw64/bin/llvm-readobj.exe")
    args = parser.parse_args()
    shipped = {p.name.lower(): p for p in args.dir.glob("*.dll")}
    # Opened at run time rather than imported (see tools/windows/dlls.sh).
    todo = [args.dir / "shantae.exe"] + [shipped[n] for n in ("libegl.dll", "librashader.dll") if n in shipped]
    seen, problems, checked = set(), [], 0
    while todo:
        path = todo.pop()
        if path.name.lower() in seen:
            continue
        seen.add(path.name.lower())
        machine = re.search(r"Machine: IMAGE_FILE_MACHINE_(\w+)", readobj(args.readobj, "--file-headers", path))
        if not machine or machine.group(1) != MACHINES[args.arch]:
            problems.append(f"{path.name} is {machine.group(1) if machine else '?'} code, not {MACHINES[args.arch]}")
        for dll, names in imports(args.readobj, path).items():
            if dll not in shipped:
                continue
            todo.append(shipped[dll])
            available = exports(args.readobj, shipped[dll])
            missing = [n for n in names if n not in available]
            checked += len(names)
            if missing:
                problems.append(f"{path.name} imports {len(missing)} function(s) {dll} does not export, "
                                f"e.g. {', '.join(missing[:3])}")
    for problem in problems:
        print(problem)
    print(f"{'FAIL' if problems else 'PASS'} {args.arch} imports: {len(seen)} modules, "
          f"{checked} functions from the DLLs beside it")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
