"""Fetches what the build needs from outside this repository, and checks the rest.

    python install_deps.py                fetch what can be fetched: delink (built from source), dxc, SDL3 and the
                                          movie decoders' sources (libvpx, opus)
    python install_deps.py --check        only report what is there and what is missing
    python install_deps.py --dry-run      print what would be downloaded and run, and change nothing
    python install_deps.py --game PATH    copy the v1.3 speed.exe (the file, or the folder it is in) to legacy/orig/
    python install_deps.py --fx PATH      copy the Xbox 360 effects (the NFSMW Xbox 360 Stuff mod's folder, or the folder
                                          with the .fx.o files) to legacy/fx/
    python install_deps.py --data-tools   also pip install what the data tools (tools/, ../decompiled) use
    python install_deps.py --only a,b     just these: delink, dxc, sdl3, libvpx, opus, game, fx, python
    python install_deps.py --force        redo what is already there

delink is built with cargo (Rust, https://rustup.rs) from a pinned commit of its source. dxc and SDL3 come from their
official release zips, which must match the SHA-256 GitHub publishes for them. libvpx and opus are source releases
(BSD licences) checked against a pinned SHA-256; build.py makes vpx.dll and opus.dll of them (tools/thirdparty.py). The game and the effects are yours and cannot be
downloaded: they are copied from where you have them and checked against the SHA-256 of the files this tree was written
against. Python 3, standard library only. Windows only (the build is MSVC's).
"""
import argparse
import hashlib
import importlib.util
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.error
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
DEPS = os.path.join(ROOT, "build", "deps")
DELINK_EXE = os.path.join(ROOT, "tools", "delink.exe")
DELINK_STAMP = os.path.join(DEPS, "delink.commit")
DXC_DIR = os.path.join(ROOT, "external", "dxc")
ORIG = os.path.join(ROOT, "legacy", "orig", "speed.exe")
FX_DIR = os.path.join(ROOT, "legacy", "fx")

#: delink 0.16.4: the commit the objects in build/obj were made with.
DELINK_REPO = "dbalatoni13/delink"
DELINK_COMMIT = "f9431175e2d5cfbb15c26d96d98d0fe0c7d1e96c"
#: DirectX Shader Compiler 1.9.2609; the SHA-256 is the digest GitHub lists for the asset.
DXC_URL = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip"
DXC_SHA256 = "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1"
DXC_FILES = ("dxc.exe", "dxcompiler.dll")
#: SDL 3.4.18 (zlib licence), the game's sound and game controllers (source/core/Sdl.cpp): its headers for the build and
#: the 32-bit SDL3.dll that goes beside the exe.
SDL3_DIR = os.path.join(ROOT, "external", "sdl3")
SDL3_URL = "https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-VC.zip"
SDL3_SHA256 = "78d84602ae616cfe26b33a73b7d3b9b6b56e8a6650e53d92c9c6ca938f747acd"
SDL3_FILES = (os.path.join("include", "SDL3", "SDL.h"), os.path.join("lib", "x86", "SDL3.dll"))
#: The movie decoders' sources (docs/MOVIES.md): libvpx 1.15.2 (GitHub's tag archive) and opus 1.5.2 (Xiph's release),
#: each unpacked into its own folder of external/ without the archive's top folder.
SOURCE_DEPS = {
    "libvpx": ("https://github.com/webmproject/libvpx/archive/refs/tags/v1.15.2.tar.gz", "libvpx-1.15.2.tar.gz",
               "26fcd3db88045dee380e581862a6ef106f49b74b6396ee95c2993a260b4636aa", "configure"),
    "opus": ("https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz", "opus-1.5.2.tar.gz",
             "65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1", "opus_sources.mk"),
}
#: The original PC v1.3 speed.exe.
GAME_SHA256 = "80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c"
#: The NFSMW Xbox 360 Stuff mod's compiled effects (4.2) that assets/shaders/owned was written against.
FX_SHA256 = {
    "car.fx.o": "8f4f7ead4b758fe5d1508faf6c30fc98ac95393dc44aed953e9f0f42bdab8c4d",
    "carnormalmap.fx.o": "3ee88c716aea66e6c4043b42916ca52e5c8fbd9911320bd443ebf4c89e13b764",
    "debugPoly.fx.o": "266e11361d566e7da0180f44ee79fe33e8dd7da8359f6e95c88b83e7b6e8ad64",
    "glassreflectshader.fx.o": "353af4c0aeb1fcba5133473d2079658f81f1d5f04dbee685459b4fb6a4cb6558",
    "grass.fx.o": "15b2948441d07ced7d58fd16229b654ba48d361088089e2deb68f8450583eb26",
    "hdr.fx.o": "3bd86ef4c60a0fec44d48c1cbdc8c8be396736b83b75362e13dd0aadeb44373f",
    "particles.fx.o": "38dc9ac67749ae4ead91c054a079afbd0941470527a70c548822fecf0557e1a4",
    "shadowscreenfilter.fx.o": "f4400ced679f56a07101b8aefbd5177873696d4721509d1ae9a095a5084cc0f3",
    "shadowvolume.fx.o": "2468d6746b777cde99b267b5fbbede8518f487bcdfc24a41e53efe7502f8e057",
    "sky.fx.o": "ce9fbc32e9403cbff6fa52d81a761877fbb3e7610845d6530b5e6fbc91789a0f",
    "standard.fx.o": "a287f0ab377359487dc244d0d01c7de0114a693ce664fb842d512927475bdcd8",
    "trees.fx.o": "9c54798a18cd7d8557dee30981df63bac01a7ea8cf5f75930515705669c189c2",
    "visualtreatment.fx.o": "dcbf8fa0d6f8ad5e3f2df602fc5ba812346bec1d4b4c66ec4d5d0eddfb182bb2",
    "waterseashader.fx.o": "1de277d831aebcccac6df8d68460f8a83246898a5ef97053feffedf50ed59391",
    "world.fx.o": "8b1ebc2cefbc7becd47063b23ecab092730c03375a42cfc051eba6536dcf03ad",
    "worldbone.fx.o": "f947e965796639cc971d3487a87d6e7a163c74f4e37348ef898635f8872c5a3d",
    "worldnormalmap.fx.o": "225bf81d038e9ff530e2ce9ed14700cd5a443f374f51372615abac6194f9fc12",
    "worldreflect.fx.o": "b590d6bd15e59e848bbfc3fc4986a185f16629292d8d409cfa641b9489d8bce1",
}
#: pip package -> the module that shows it is installed; only the data tools use them.
DATA_PACKAGES = {"numpy": "numpy", "Pillow": "PIL", "scipy": "scipy", "librosa": "librosa"}

dry_run = False


class Failed(Exception):
    pass


def say(text):
    print(text, flush=True)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def run(command, cwd=None):
    say("  $ " + " ".join(command))
    if not dry_run and subprocess.run(command, cwd=cwd).returncode:
        raise Failed("command failed: " + os.path.basename(command[0]))


def download(url, dest, expected=None):
    """Downloads url to dest through a .part file. With expected, the SHA-256 must match or nothing is kept."""
    if expected and os.path.isfile(dest) and sha256(dest) == expected:
        say("  already downloaded: " + os.path.basename(dest))
        return
    say("  downloading " + url)
    if dry_run:
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    part = dest + ".part"
    digest = hashlib.sha256()
    request = urllib.request.Request(url, headers={"User-Agent": "nfsmw-rebuild-install-deps"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response, open(part, "wb") as out:
            total = int(response.headers.get("Content-Length") or 0)
            done = tenths = 0
            for block in iter(lambda: response.read(1 << 20), b""):
                out.write(block)
                digest.update(block)
                done += len(block)
                if total and done * 10 // total > tenths:
                    tenths = done * 10 // total
                    say("    %d%%" % (tenths * 10))
    except (urllib.error.URLError, OSError) as error:
        if os.path.exists(part):
            os.remove(part)
        raise Failed("download failed: %s (%s)" % (url, error))
    if expected and digest.hexdigest() != expected:
        os.remove(part)
        raise Failed("the SHA-256 of %s is %s, not the %s this script expects: nothing was installed" % (
            url, digest.hexdigest(), expected))
    os.replace(part, dest)


def extract(archive, dest, strip=0):
    """Unpacks a zip into dest without its first `strip` folder levels. Paths that would leave dest are refused."""
    root = os.path.abspath(dest)
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            parts = info.filename.split("/")[strip:]
            if not parts or not parts[-1]:
                continue
            target = os.path.abspath(os.path.join(root, *parts))
            if os.path.commonpath([root, target]) != root:
                raise Failed("%s holds a path outside its folder: %s" % (archive, info.filename))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with z.open(info) as source, open(target, "wb") as out:
                shutil.copyfileobj(source, out)


def find_cargo():
    installed = os.path.join(os.path.expanduser("~"), ".cargo", "bin", "cargo.exe")
    return shutil.which("cargo") or (installed if os.path.isfile(installed) else None)


def delink_status():
    if not os.path.isfile(DELINK_EXE):
        return False, "missing (python install_deps.py builds it)"
    try:
        stamp = open(DELINK_STAMP).read().strip()
    except OSError:
        stamp = ""
    return True, ("built from commit " + stamp[:7]) if stamp == DELINK_COMMIT else "present, its version is not recorded"


def install_delink(force):
    ok, note = delink_status()
    if ok and not force:
        say("delink: " + note)
        return
    say("delink: building commit %s of %s" % (DELINK_COMMIT[:7], DELINK_REPO))
    cargo = find_cargo()
    if not cargo and not dry_run:
        raise Failed("delink is built with cargo: install Rust from https://rustup.rs (the MSVC toolchain), then run this again")
    source = os.path.join(DEPS, "delink-" + DELINK_COMMIT[:7])
    if force or not os.path.isfile(os.path.join(source, "Cargo.toml")):
        archive = source + ".zip"
        download("https://github.com/%s/archive/%s.zip" % (DELINK_REPO, DELINK_COMMIT), archive)
        if not dry_run:
            extract(archive, source, strip=1)
    say("  cargo runs the build scripts of delink's crates (Cargo.lock pins them)")
    run([cargo or "cargo", "build", "--release", "--locked", "-p", "delink-cli"], cwd=source)
    if not dry_run:
        os.makedirs(os.path.dirname(DELINK_EXE), exist_ok=True)
        shutil.copyfile(os.path.join(source, "target", "release", "delink.exe"), DELINK_EXE)
        with open(DELINK_STAMP, "w") as f:
            f.write(DELINK_COMMIT + "\n")
        say("  tools/delink.exe written")


def dxc_status():
    missing = [n for n in DXC_FILES if not os.path.isfile(os.path.join(DXC_DIR, n))]
    return not missing, ("missing " + ", ".join(missing)) if missing else "present"


def install_dxc(force):
    ok, note = dxc_status()
    if ok and not force:
        say("dxc: " + note)
        return
    say("dxc: DirectX Shader Compiler 1.9.2609")
    archive = os.path.join(DEPS, os.path.basename(DXC_URL))
    download(DXC_URL, archive, DXC_SHA256)
    if dry_run:
        return
    os.makedirs(DXC_DIR, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        for wanted in DXC_FILES:
            member = next((n for n in names if ("/" + n.lower()).endswith("/bin/x64/" + wanted)), None)
            if member is None:
                raise Failed("bin/x64/%s is not in %s" % (wanted, os.path.basename(archive)))
            with z.open(member) as source, open(os.path.join(DXC_DIR, wanted), "wb") as out:
                shutil.copyfileobj(source, out)
        for member in names:  # the licence files at the top of the zip stay with the binaries
            if "/" not in member and member.upper().startswith(("LICENSE", "LICENCE")):
                with z.open(member) as source, open(os.path.join(DXC_DIR, member), "wb") as out:
                    shutil.copyfileobj(source, out)
    say("  external/dxc written")


def sdl3_status():
    missing = [n for n in SDL3_FILES if not os.path.isfile(os.path.join(SDL3_DIR, n))]
    return not missing, ("missing " + ", ".join(missing)) if missing else "present"


def install_sdl3(force):
    ok, note = sdl3_status()
    if ok and not force:
        say("sdl3: " + note)
        return
    say("sdl3: SDL 3.4.18")
    archive = os.path.join(DEPS, os.path.basename(SDL3_URL))
    download(SDL3_URL, archive, SDL3_SHA256)
    if dry_run:
        return
    root = os.path.abspath(SDL3_DIR)
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            parts = info.filename.split("/")[1:]
            if not parts or not parts[-1]:
                continue
            wanted = parts[0] == "include" or parts[:2] == ["lib", "x86"] or (len(parts) == 1 and parts[0] in (
                "LICENSE.txt", "README.md"))
            if not wanted:
                continue
            target = os.path.abspath(os.path.join(root, *parts))
            if os.path.commonpath([root, target]) != root:
                raise Failed("%s holds a path outside its folder: %s" % (archive, info.filename))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with z.open(info) as source, open(target, "wb") as out:
                shutil.copyfileobj(source, out)
    say("  external/sdl3 written")


def source_status(name):
    url, archive, digest, marker = SOURCE_DEPS[name]
    folder = os.path.join(ROOT, "external", name)
    stamp = os.path.join(folder, ".archive")
    if not os.path.isfile(os.path.join(folder, marker)):
        return False, "missing (python install_deps.py --only %s)" % name
    have = open(stamp).read().strip() if os.path.isfile(stamp) else ""
    return (True, archive) if have == digest else (False, "not the pinned %s" % archive)


def install_source(name, force):
    """A source release unpacked into external/<name> (its top folder dropped), checked against its SHA-256."""
    ok, note = source_status(name)
    if ok and not force:
        say("%s: %s" % (name, note))
        return
    url, archive_name, digest, _ = SOURCE_DEPS[name]
    say("%s: %s" % (name, archive_name))
    archive = os.path.join(DEPS, archive_name)
    download(url, archive, digest)
    if dry_run:
        return
    folder = os.path.abspath(os.path.join(ROOT, "external", name))
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    with tarfile.open(archive) as t:
        for member in t.getmembers():
            parts = member.name.split("/")[1:]
            if not parts or not parts[-1] or not (member.isfile() or member.isdir()):
                continue
            target = os.path.abspath(os.path.join(folder, *parts))
            if os.path.commonpath([folder, target]) != folder:
                raise Failed("%s holds a path outside its folder: %s" % (archive, member.name))
            if member.isdir():
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with t.extractfile(member) as source, open(target, "wb") as out:
                shutil.copyfileobj(source, out)
    with open(os.path.join(folder, ".archive"), "w") as f:
        f.write(digest + "\n")
    say("  external/%s written" % name)


def game_status():
    if not os.path.isfile(ORIG):
        return False, "missing (--game PATH copies it)"
    return (True, "the v1.3 exe") if sha256(ORIG) == GAME_SHA256 else (False, "not the v1.3 exe (SHA-256 differs)")


def install_game(path, force):
    ok, note = game_status()
    if ok and not force:
        say("game: legacy/orig/speed.exe is " + note)
        return
    source = os.path.join(path, "speed.exe") if os.path.isdir(path) else path
    if not os.path.isfile(source):
        raise Failed("there is no speed.exe at " + path)
    digest = sha256(source)
    if digest != GAME_SHA256:
        raise Failed("%s is not the original v1.3 speed.exe (SHA-256 %s, expected %s). A game folder's speed.exe may "
                     "already be a rebuilt or patched one: use the untouched original." % (source, digest, GAME_SHA256))
    say("game: copying %s to legacy/orig/speed.exe" % source)
    if not dry_run:
        os.makedirs(os.path.dirname(ORIG), exist_ok=True)
        shutil.copyfile(source, ORIG)


def fx_status():
    present = [n for n in FX_SHA256 if os.path.isfile(os.path.join(FX_DIR, n))]
    different = [n for n in present if sha256(os.path.join(FX_DIR, n)) != FX_SHA256[n]]
    note = "%d of %d" % (len(present) - len(different), len(FX_SHA256))
    if different:
        note += "; different from the version assets/shaders/owned was written against: " + ", ".join(different)
    return len(present) == len(FX_SHA256) and not different, note + ("" if present else " (--fx PATH copies them)")


def fx_folder(path):
    for candidate in (path, os.path.join(path, "ModData", "Files", "FX", "COMPILED"), os.path.join(path, "FX", "COMPILED"),
                      os.path.join(path, "COMPILED")):
        if os.path.isfile(os.path.join(candidate, "world.fx.o")):
            return candidate
    for folder, _, names in os.walk(path):
        if "world.fx.o" in names:
            return folder
    return None


def install_fx(path, force):
    ok, note = fx_status()
    if ok and not force:
        say("fx: assets/fx has " + note)
        return
    folder = fx_folder(path)
    if folder is None:
        raise Failed("no world.fx.o under " + path)
    missing = [n for n in FX_SHA256 if not os.path.isfile(os.path.join(folder, n))]
    different = [n for n in FX_SHA256 if n not in missing and sha256(os.path.join(folder, n)) != FX_SHA256[n]]
    if missing or different:
        raise Failed("the effects in %s are not the ones assets/shaders/owned was written against, those of version 4.2 of the mod "
                     "(missing: %s; different: %s)" % (folder, ", ".join(missing) or "none", ", ".join(different) or "none"))
    say("fx: copying %d effects from %s" % (len(FX_SHA256), folder))
    if not dry_run:
        os.makedirs(FX_DIR, exist_ok=True)
        for name in FX_SHA256:
            shutil.copyfile(os.path.join(folder, name), os.path.join(FX_DIR, name))


def python_status():
    have = [p for p, module in DATA_PACKAGES.items() if importlib.util.find_spec(module)]
    return len(have) == len(DATA_PACKAGES), ", ".join("%s %s" % (p, "yes" if p in have else "no") for p in DATA_PACKAGES)


def install_python(force):
    say("python: pip install " + " ".join(DATA_PACKAGES))
    run([sys.executable, "-m", "pip", "install", *DATA_PACKAGES])


def toolchain():
    """(name, ok, note) for what build.py needs from Visual Studio and the Windows SDK, looked up by build.py itself."""
    sys.path.insert(0, ROOT)
    try:
        import build
        tools = (("cl.exe", build.CL), ("link.exe", build.LINK), ("undname.exe", build.UNDNAME),
                 ("dumpbin.exe", os.path.join(build.MSVC_BIN, "dumpbin.exe")), ("fxc.exe (Windows SDK)", build.FXC))
    except (IndexError, ImportError, OSError, AttributeError):
        return [("Visual Studio x86 tools", False, "not found where build.py looks (its VC path)")]
    return [(name, os.path.isfile(path), path) for name, path in tools]


def report():
    """Prints what is there. Returns whether everything the build needs is."""
    cargo = find_cargo()
    optional_python = python_status()
    rows = toolchain() + [
        ("cargo (only to build delink)", bool(cargo) or os.path.isfile(DELINK_EXE), cargo or "not found (https://rustup.rs)"),
        ("delink", *delink_status()),
        ("dxc", *dxc_status()),
        ("SDL3", *sdl3_status()),
        ("legacy/orig/speed.exe", *game_status()),
        ("Xbox 360 effects", *fx_status()),
        ("symbol map and splits", all(os.path.isfile(os.path.join(ROOT, "legacy", "config", n)) for n in ("symbols.json", "splits.json")),
         "legacy/config/symbols.json, legacy/config/splits.json"),
    ]
    say("")
    for name, ok, note in rows:
        say("%-30s %-8s %s" % (name, "ok" if ok else "MISSING", note))
    for name in SOURCE_DEPS:
        ok, note = source_status(name)
        say("%-30s %-8s %s" % ("%s (movies, optional)" % name, "ok" if ok else "-", note))
    say("%-30s %-8s %s" % ("data tools (optional)", "ok" if optional_python[0] else "-", optional_python[1]))
    say("%-30s %-8s %s" % ("ffmpeg (optional)", "ok" if shutil.which("ffmpeg") else "-", shutil.which("ffmpeg") or "not on PATH"))
    return all(ok for _, ok, _ in rows)


def main():
    global dry_run
    parser = argparse.ArgumentParser(description="Fetches what the build needs from outside this repository.")
    parser.add_argument("--check", action="store_true", help="only report what is there and what is missing")
    parser.add_argument("--dry-run", action="store_true", help="print what would be downloaded and run, change nothing")
    parser.add_argument("--force", action="store_true", help="redo what is already there")
    parser.add_argument("--game", metavar="PATH", help="the original v1.3 speed.exe, or the folder it is in")
    parser.add_argument("--fx", metavar="PATH", help="the NFSMW Xbox 360 Stuff mod's folder, or the folder with the .fx.o files")
    parser.add_argument("--data-tools", action="store_true", help="also pip install numpy, Pillow, scipy and librosa")
    parser.add_argument("--only", metavar="A,B", help="just these: delink, dxc, sdl3, libvpx, opus, game, fx, python")
    args = parser.parse_args()
    dry_run = args.dry_run
    if os.name != "nt":
        sys.exit("The build is MSVC's, so this is for Windows.")
    if args.check:
        sys.exit(0 if report() else 1)
    def need(option):
        raise Failed("this needs " + option)

    steps = {
        "delink": lambda: install_delink(args.force),
        "dxc": lambda: install_dxc(args.force),
        "sdl3": lambda: install_sdl3(args.force),
        "libvpx": lambda: install_source("libvpx", args.force),
        "opus": lambda: install_source("opus", args.force),
        "game": lambda: install_game(args.game, args.force) if args.game else need("--game PATH"),
        "fx": lambda: install_fx(args.fx, args.force) if args.fx else need("--fx PATH"),
        "python": lambda: install_python(args.force),
    }
    if args.only:
        chosen = [n.strip() for n in args.only.split(",") if n.strip()]
        unknown = [n for n in chosen if n not in steps]
        if unknown:
            sys.exit("unknown item(s): " + ", ".join(unknown))
    else:
        chosen = ["delink", "dxc", "sdl3", "libvpx", "opus"] + (["game"] if args.game else []) + (["fx"] if args.fx else []) + (
            ["python"] if args.data_tools else [])
    failed = False
    for name in chosen:
        try:
            steps[name]()
        except Failed as error:
            say("%s: %s" % (name, error))
            failed = True
    complete = report()
    if not complete and not dry_run:
        say("\nStill missing: --game PATH and --fx PATH copy the original exe and the mod's effects from where you have them.")
    sys.exit(1 if failed or (not complete and not dry_run) else 0)


if __name__ == "__main__":
    main()
