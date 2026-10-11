"""Builds the field editor: build/dw3fe.exe (Windows, 64-bit), ready to run
from Explorer, with Qt's DLLs and the MinGW ones they need next to it.

    python build.py            incremental
    python build.py --clean    from scratch

Needs MSYS2 (https://www.msys2.org) with its UCRT64 packages:

    pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-qt6-base
              mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja

MSYS2 is looked for at $MSYS2_DIR, then where the UCRT64 g++ on PATH is,
then at C:/msys64, where MSYS2's installer puts it unless told otherwise.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE / "build"


TOOLS = ("g++.exe", "cmake.exe", "ninja.exe", "windeployqt6.exe", "objdump.exe")


def find_ucrt64():
    """MSYS2's UCRT64 bin folder, with every tool the build runs"""
    roots = [Path(os.environ["MSYS2_DIR"])] if os.environ.get("MSYS2_DIR") else []
    gxx = shutil.which("g++")
    if gxx and Path(gxx).parent.parent.name.lower() == "ucrt64":
        roots.append(Path(gxx).parents[2])
    roots.append(Path("C:/msys64"))
    for root in roots:
        bin_dir = root / "ucrt64/bin"
        missing = [t for t in TOOLS if not (bin_dir / t).exists()]
        if not missing:
            return bin_dir
        if root == roots[0] and os.environ.get("MSYS2_DIR"):
            break
    sys.exit(f"no {', '.join(missing)} in {bin_dir}: install MSYS2 and its UCRT64 packages (README, Building from "
             "source), or set MSYS2_DIR to the folder MSYS2 is in")


def run(cmd, env):
    if subprocess.run([str(c) for c in cmd], env=env).returncode:
        sys.exit(1)


def deps(bin_dir):
    """The MinGW DLLs that the exe and Qt's DLLs and plugins import, from
    UCRT64's bin (windeployqt copies only Qt's own)"""
    def imports(path):
        out = subprocess.run([str(bin_dir / "objdump.exe"), "-p", str(path)], capture_output=True, text=True).stdout
        return re.findall(r"DLL Name: (\S+)", out)

    todo = [BUILD / "dw3fe.exe", *BUILD.rglob("*.dll")]
    seen = {p.name.lower() for p in BUILD.rglob("*.dll")}
    while todo:
        for name in imports(todo.pop()):
            src = bin_dir / name
            if name.lower() in seen or not src.exists():
                continue  # Windows' own, or there already
            seen.add(name.lower())
            shutil.copy2(src, BUILD / name)
            todo.append(BUILD / name)


def icon():
    """res/dw3fe.ico as Windows wants an exe's icon: square, at 16 to 256
    pixels. The picture is centered on a transparent square, not changed; the
    result goes to build/gen/dw3fe.ico, which CMakeLists.txt prefers. Without
    Pillow the original is used as it is."""
    src, out = HERE / "res/dw3fe.ico", BUILD / "gen/dw3fe.ico"
    if not src.exists() or (out.exists() and out.stat().st_mtime >= src.stat().st_mtime):
        return
    try:
        from PIL import Image
    except ImportError:
        print("note: no Pillow (pip install pillow): the exe gets res/dw3fe.ico as it is")
        return
    img = Image.open(src)
    img = max((img.ico.getimage(s) for s in img.ico.sizes()), key=lambda i: i.width * i.height).convert("RGBA")
    side = max(img.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(img, ((side - img.width) // 2, (side - img.height) // 2))
    out.parent.mkdir(parents=True, exist_ok=True)
    square.save(out, sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256) if s <= side])


def main():
    bin_dir = find_ucrt64()
    env = dict(os.environ, PATH=f"{bin_dir}{os.pathsep}{os.environ['PATH']}")
    if "--clean" in sys.argv and BUILD.exists():
        shutil.rmtree(BUILD)
    icon()
    if not (BUILD / "build.ninja").exists():
        run([bin_dir / "cmake.exe", "-S", HERE, "-B", BUILD, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
             f"-DCMAKE_CXX_COMPILER={bin_dir / 'g++.exe'}"], env)
    r = subprocess.run([str(bin_dir / "cmake.exe"), "--build", str(BUILD)], env=env, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode:
        if "Permission denied" in r.stdout + r.stderr:
            sys.exit("build/dw3fe.exe is open: close the editor and build again")
        sys.exit(1)
    # no network: the editor reads local files only
    run([bin_dir / "windeployqt6.exe", "--no-translations", "--no-opengl-sw", "--no-system-d3d-compiler",
         "--skip-plugin-types", "tls,networkinformation",
         "--verbose", "0", BUILD / "dw3fe.exe"], env)
    deps(bin_dir)
    print("ok", BUILD / "dw3fe.exe")


if __name__ == "__main__":
    main()
