"""Build and install GhidraBoy for a Ghidra install without Gradle.

GhidraBoy's release builds stop at Ghidra 11.4.2 and its Gradle/Kotlin setup
does not run on JDK 25. The shipped code is plain Java plus a SLEIGH spec, so
we compile it straight against the target Ghidra's jars.

The source is a GhidraBoy checkout in GhidraBoy/ with the Ghidra 12 loader API
port applied:

    git clone https://github.com/Gekkio/GhidraBoy
    git -C GhidraBoy checkout 42032f9d97e9e502dc10a14743bdb3d1b9388588
    git -C GhidraBoy apply ../tools/ghidraboy-ghidra12.patch

Usage: python tools/build_ghidraboy.py [--ghidra DIR] [--no-install]
--ghidra defaults to $GHIDRA_INSTALL_DIR.
"""
import argparse
import datetime
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "GhidraBoy"
DEFAULT_GHIDRA = os.environ.get("GHIDRA_INSTALL_DIR")


def ghidra_props(ghidra: Path) -> dict:
    props = {}
    for line in (ghidra / "Ghidra" / "application.properties").read_text().splitlines():
        if "=" in line and not line.startswith("#"):
            k, v = line.split("=", 1)
            props[k.strip()] = v.strip()
    return props


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ghidra", type=Path, default=DEFAULT_GHIDRA)
    ap.add_argument("--no-install", action="store_true")
    args = ap.parse_args()
    if args.ghidra is None:
        ap.error("pass --ghidra or set GHIDRA_INSTALL_DIR")

    ghidra = Path(args.ghidra).resolve()
    props = ghidra_props(ghidra)
    version = props["application.version"]
    release = props.get("application.release.name", "PUBLIC")

    jars = sorted(p for sub in ("Framework", "Features")
                  for p in (ghidra / "Ghidra" / sub).rglob("*.jar"))
    classpath = os.pathsep.join(str(j) for j in jars)

    # Build in a temp dir so nothing is left behind in the project.
    build = Path(tempfile.mkdtemp(prefix="ghidraboy_"))
    classes = build / "classes"
    classes.mkdir(parents=True)
    ext = build / "GhidraBoy"
    (ext / "lib").mkdir(parents=True)

    sources = sorted((SRC / "src" / "main" / "java").rglob("*.java"))
    argfile = build / "javac.args"
    argfile.write_text("\n".join(
        ["--release", "21", "-nowarn", "-encoding", "UTF-8",
         "-cp", f'"{classpath}"', "-d", f'"{classes}"']
        + [f'"{s}"' for s in sources]).replace("\\", "/"))
    run(["javac", f"@{argfile}"])
    run(["jar", "cf", ext / "lib" / "GhidraBoy.jar", "-C", classes, "."])

    shutil.copytree(SRC / "data", ext / "data")
    slaspec = ext / "data" / "languages" / "sm83.slaspec"
    cp_arg = build / "sleigh.args"
    cp_arg.write_text(f'-cp "{classpath}"'.replace("\\", "/"))
    run(["java", f"@{cp_arg}", "ghidra.pcodeCPort.slgh_compile.SleighCompile",
         "-l", "-n", "-t", "-e", "-c", "-f", slaspec])

    for f in ("README.markdown", "LICENSE", "Module.manifest"):
        shutil.copy2(SRC / f, ext / f)
    (ext / "extension.properties").write_text(
        "name=GhidraBoy\n"
        "description=Support for Sharp SM83 / Game Boy\n"
        "author=Gekkio\n"
        f"createdOn={datetime.date.today()}\n"
        f"version={version}\n")

    if args.no_install:
        print("built:", ext)
        return

    appdata = Path(os.environ["APPDATA"])
    dest = appdata / "ghidra" / f"ghidra_{version}_{release}" / "Extensions" / "GhidraBoy"
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(ext, dest)
    shutil.rmtree(build, ignore_errors=True)
    print("installed:", dest)


if __name__ == "__main__":
    sys.exit(main())
