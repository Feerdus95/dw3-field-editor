"""Builds the editor and packs a release: build/dw3-field-editor-<version>-win64.zip,
with every DLL it ships and the license of each, then unpacks it to a temporary
folder and checks that it starts and reads the fields (with the data folder the
editor last used, or --data DIR).

    python tools/package.py v0.1.0 [--data DIR]

The licenses come from MSYS2: each DLL's package (pacman -Qo) and its license
files (share/licenses/<name>, or the files pacman lists as licenses), into
licenses/<package>/ of the zip.
"""
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
BUILD = HERE / "build"
ROOT = "dw3-field-editor"
sys.path.insert(0, str(HERE))
from build import find_ucrt64  # noqa: E402  the same MSYS2 the build uses

MSYS2 = find_ucrt64().parents[1]
PREFIX = "mingw-w64-ucrt-x86_64-"


def pacman(*args):
    return subprocess.run([str(MSYS2 / "usr/bin/bash.exe"), "-lc", "pacman " + " ".join(args)],
                          capture_output=True, text=True).stdout


def owner(dll):
    """The MSYS2 package a shipped DLL comes from, or None"""
    for p in (MSYS2 / "ucrt64").rglob(dll.name):
        out = pacman("-Qqo", "/" + p.relative_to(MSYS2).as_posix()).strip()
        if out:
            return out.splitlines()[0]
    return None


def license_files(package):
    """A package's license texts"""
    name = package[len(PREFIX):] if package.startswith(PREFIX) else package
    found = [p for p in sorted((MSYS2 / "ucrt64/share/licenses" / name).glob("**/*")) if p.is_file()]
    if not found:
        for line in pacman("-Ql", package).splitlines():
            path = line.split(" ", 1)[1]
            if re.search(r"licen[cs]e|copying|notice", path, re.I) and not path.endswith("/"):
                p = MSYS2 / path.lstrip("/")
                if p.is_file():
                    found.append(p)
    return found


def main():
    if len(sys.argv) < 2 or not sys.argv[1].startswith("v"):
        sys.exit(__doc__)
    version = sys.argv[1]
    data = sys.argv[sys.argv.index("--data") + 1] if "--data" in sys.argv else None
    if subprocess.run([sys.executable, HERE / "build.py"]).returncode:
        sys.exit(1)

    files = {f"{ROOT}/{p.relative_to(BUILD).as_posix()}": p for p in BUILD.rglob("*")
             if p.is_file() and (p.name == "dw3fe.exe" or p.suffix == ".dll")}
    for name in ("README.md", "CHANGELOG.md", "LICENSE", "CREDITS.md"):
        files[f"{ROOT}/{name}"] = HERE / name

    # the licenses of the DLLs' packages: none may be missing
    packages = {}
    for dll in sorted(p for p in BUILD.rglob("*.dll")):
        pkg = owner(dll)
        if not pkg:
            sys.exit(f"no MSYS2 package owns {dll.name}: can't ship its license")
        packages.setdefault(pkg, []).append(dll.name)
    for pkg in sorted(packages):
        texts = license_files(pkg)
        if not texts:
            sys.exit(f"no license file in {pkg} (for {', '.join(packages[pkg])})")
        short = pkg[len(PREFIX):]
        for t in texts:
            files[f"{ROOT}/licenses/{short}/{t.name}"] = t
        print(f"license {short}: {len(texts)} file(s) for {', '.join(packages[pkg])}")

    out = BUILD / f"dw3-field-editor-{version}-win64.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for arc, src in sorted(files.items()):
            z.write(src, arc)
    print(f"packed {out} {out.stat().st_size // 1024} KB")

    # the zip on its own: it starts, finds its DLLs and reads every field
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(out) as z:
            z.extractall(tmp)
        env = dict(os.environ, PATH=os.pathsep.join([os.environ.get("SystemRoot", "C:/Windows") + "/system32",
                                                     os.environ.get("SystemRoot", "C:/Windows")]))
        cmd = [str(Path(tmp) / ROOT / "dw3fe.exe"), "--check"] + (["--data", data] if data else [])
        r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=600)
        print("check:", (r.stdout.strip().splitlines() or ["(nothing)"])[-1])
        if r.returncode:
            sys.exit(f"the packed editor failed:\n{r.stdout}{r.stderr}")


if __name__ == "__main__":
    main()
