"""Rebuild speed.exe from delinked original objects plus the C++ of source/ and tests/ (docs/STRUCTURE.md).

    python build.py            build build/speed.exe
    python build.py --clean    re-run the delink split first

Pipeline:
  1. delink splits legacy/orig/speed.exe into one relocatable COFF object per original unity-build unit (build/obj).
  2. Every .cpp in source/ and tests/ is compiled for x86 with flags compatible with the game's MSVC 7.1 runtime.
  3. Replacements: a compiled symbol whose name equals an original symbol, or one marked NFS_REPLACE(addr, Name),
     takes over that original. The original definition is unbound (made an undefined reference; its bytes stay in place
     as `<name>$orig`), so every caller binds to the new code while all other addresses stay where they were.
  4. Calls from new code into original code bind by exact symbol name, by NFS_BIND(addr, Name), or by a unique match on
     the demangled name. Direct Win32 calls go through the original IAT.
  5. Compiled objects are moved into a trailing `.srcx` section so original sections keep their addresses.
  6. link.exe links everything at the original base. With no source objects the result is restored to a byte-identical
     copy of the original; otherwise the original import/IAT directories are carried into the new image.
"""
import argparse, bisect, glob, json, os, re, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import coffpatch  # noqa: E402
import shims  # noqa: E402
import symbols  # noqa: E402
import x86scan  # noqa: E402

VC = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC"
MSVC_BIN = glob.glob(os.path.join(VC, r"Tools\MSVC\*\bin\Hostx64\x86"))[-1]
CL = os.path.join(MSVC_BIN, "cl.exe")
LINK = os.path.join(MSVC_BIN, "link.exe")
UNDNAME = os.path.join(MSVC_BIN, "undname.exe")
ORIG = os.path.join(ROOT, "legacy", "orig", "speed.exe")
BUILD = os.path.join(ROOT, "build")
OBJ = os.path.join(BUILD, "obj")
PATCHED = os.path.join(BUILD, "obj_patched")
SRC_OBJ = os.path.join(BUILD, "src_obj")
SRC_PATCHED = os.path.join(BUILD, "src_patched")
DELINK = os.path.join(ROOT, "tools", "delink.exe")

# New code and data share one RWX initialised-data section so the linker places it after the original .data instead of
# between original sections (code-flagged sections are grouped right after .text, which would move .rdata).
SRCX = 0xE0000040  # CNT_INITIALIZED_DATA | MEM_EXECUTE | MEM_READ | MEM_WRITE
SECTION_RENAME = {".text": (".srcx", SRCX), ".rdata": (".srcx", SRCX), ".data": (".srcx", SRCX),
                  ".bss": (".srcb", None), ".CRT": (".srcx", SRCX)}

CFLAGS = ["/nologo", "/c", "/O2", "/Oy-", "/GS-", "/Gy-", "/GR-", "/EHs-c-", "/Zl", "/arch:IA32", "/fp:precise",
          "/Zc:wchar_t-", "/Zc:threadSafeInit-", "/Zc:sizedDealloc-", "/std:c++17", "/W3", "/wd4100", "/wd4201", "/wd4099",
          "/D_HAS_EXCEPTIONS=0", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX", "/D_CRT_SECURE_NO_WARNINGS",
          "/I", os.path.join(ROOT, "include"), "/I", os.path.join(ROOT, "legacy", "include"),
          "/I", os.path.join(ROOT, "external", "vulkan-headers"),
          "/I", os.path.join(ROOT, "external", "sdl3", "include")]
# The movie decoders' headers (install_deps.py fetches libvpx and opus; tools/thirdparty.py builds their DLLs): without
# them the WebM player is left out (source/engine/movies/WebmPlayer.cpp).
for _folder in (("external", "libvpx"), ("external", "opus", "include")):
    if os.path.isdir(os.path.join(ROOT, *_folder)):
        CFLAGS += ["/I", os.path.join(ROOT, *_folder)]
# /LARGEADDRESSAWARE: 4 GB of address space on 64-bit Windows instead of 2 GB (what the community "4GB patch" does).
LDFLAGS = ["/nologo", "/MACHINE:X86", "/SUBSYSTEM:WINDOWS", "/ENTRY:entry", "/NODEFAULTLIB", "/FIXED",
           "/INCREMENTAL:NO", "/BASE:0x400000", "/FILEALIGN:0x1000", "/NOCOFFGRPINFO", "/EMITVOLATILEMETADATA:NO",
           "/OPT:NOREF", "/OPT:NOICF", "/MAP", "/MERGE:.srcb=.srcx", "/LARGEADDRESSAWARE"]
DIRECTIVE = re.compile(r'/FAILIFMISMATCH:"nfs_(replace|bind)_(0x[0-9a-fA-F]+)=([^"]+)"')


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace", **kw)
    if r.returncode != 0:
        sys.stdout.write(r.stdout + r.stderr)
        sys.exit("command failed: " + os.path.basename(cmd[0]))
    return r.stdout


def msvc_env():
    cache = os.path.join(BUILD, "msvc_env.json")
    if os.path.exists(cache):
        return json.load(open(cache))
    out = subprocess.run(f'cmd /s /c ""{VC}\\Auxiliary\\Build\\vcvarsall.bat" x86 >nul && set"',
                         capture_output=True, text=True, shell=True).stdout
    env = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
    json.dump(env, open(cache, "w"))
    return env


SYMBOLS = os.path.join(BUILD, "symbols.json")


def add_extra_functions(s):
    """Turn the code between the IDA map's functions (legacy/config/extra_functions.txt) into functions.

    delink only recovers rel32 call/jump relocations inside functions. A call from code outside them keeps its original
    displacement and so keeps reaching the original callee even after that callee has been replaced."""
    path = os.path.join(ROOT, "legacy", "config", "extra_functions.txt")
    if not os.path.exists(path):
        return
    funcs = s["functions"]
    known = {int(f["start"], 16): int(f["size"], 16) for f in funcs}
    extra = sorted(int(l, 16) for l in open(path) if l.strip() and not l.startswith("#"))
    starts = sorted(set(known) | set(extra))
    text_end = next(int(seg["end"], 16) for seg in s["segments"] if seg["name"] == ".text")
    splits = json.load(open(os.path.join(ROOT, "legacy", "config", "splits.json")))
    range_ends = sorted(int(b, 16) for v in splits.values() for a, b in v.get("function_ranges", []))
    names = {int(n["addr"], 16): n["name"] for n in s["names"]}
    for a in extra:
        if a in known:
            continue
        i = bisect.bisect_right(starts, a)
        end = starts[i] if i < len(starts) else text_end
        j = bisect.bisect_right(range_ends, a)
        if j < len(range_ends):
            end = min(end, range_ends[j])  # never cross into the next split unit
        funcs.append({"start": "0x%X" % a, "size": "0x%X" % (end - a), "name": names.get(a, "sub_%X" % a),
                      "thunk": False, "lib": False, "static": False, "public": True, "thunk_target": None})
    funcs.sort(key=lambda f: int(f["start"], 16))


def prepare_symbols():
    """legacy/config/symbols.json with legacy/config/symbol_overrides.json applied -> build/symbols.json."""
    s = json.load(open(os.path.join(ROOT, "legacy", "config", "symbols.json")))
    add_extra_functions(s)
    overrides = {int(k, 16): v for k, v in json.load(open(os.path.join(ROOT, "legacy", "config", "symbol_overrides.json"))).items()
                 if not k.startswith("_")}
    seen = set()
    for f in s["functions"]:
        a = int(f["start"], 16)
        if a in overrides:
            f["name"] = overrides[a]
            seen.add(a)
    for n in s["names"]:
        a = int(n["addr"], 16)
        if a in overrides:
            n["name"] = overrides[a]
            seen.add(a)
    # Unnamed data (no IDA label at the address) gets a new name entry; code must already be a function start.
    data_start = min(int(seg["start"], 16) for seg in s["segments"] if seg["name"] != ".text")
    for a in sorted(set(overrides) - seen):
        if a < data_start:
            sys.exit("symbol_overrides.json: no function at %#x" % a)
        s["names"].append({"addr": "0x%X" % a, "name": overrides[a], "public": True, "weak": False, "is_func": False})
    s["names"].sort(key=lambda n: int(n["addr"], 16))
    text = json.dumps(s)
    if not os.path.exists(SYMBOLS) or open(SYMBOLS).read() != text:
        open(SYMBOLS, "w").write(text)
        return True
    return False


def split(clean, symbols_changed):
    import hashlib
    stamp = os.path.join(BUILD, "split.stamp")
    digest = hashlib.sha1(open(SYMBOLS, "rb").read() + open(os.path.join(ROOT, "legacy", "config", "splits.json"), "rb").read()).hexdigest()
    stale = not os.path.exists(stamp) or open(stamp).read() != digest
    if (clean or stale) and os.path.isdir(OBJ):
        shutil.rmtree(OBJ)
    if not os.path.exists(os.path.join(OBJ, "__pe_resources.res")):
        print("SPLIT", os.path.basename(ORIG))
        run([DELINK, "ida-split", SYMBOLS, ORIG, "--idapro", os.path.join(ROOT, "legacy", "config", "splits.json"), "-o", OBJ])
        open(stamp, "w").write(digest)


def original_objects():
    splits = json.load(open(os.path.join(ROOT, "legacy", "config", "splits.json")))
    return [os.path.join(OBJ, k) for k in splits]


def object_symbols(path):
    c = coffpatch.Coff(open(path, "rb").read())
    defs, undef = set(), set()
    for _, s in c.symbols():
        value, section, typ, cls, naux = struct.unpack_from("<IhHBB", s, 8)
        if cls == coffpatch.IMAGE_SYM_CLASS_EXTERNAL:
            (defs if section > 0 else undef).add(c.name_of(bytes(s[:8])))
    return defs, undef, c.section_data(".drectve").decode("latin-1", "replace")


GEN = os.path.join(BUILD, "gen")


FXC = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Windows Kits", "10", "bin",
                   "10.0.26100.0", "x64", "fxc.exe")


def compile_own_effects():
    """rebuild/assets/shaders/*.fx -> build/fx/own/*.fx.o (fx_2_0, without preshaders: the Vulkan translation runs shader
    constants as they are set). Returns the output directory."""
    out_dir = os.path.join(BUILD, "fx", "own")
    os.makedirs(out_dir, exist_ok=True)
    for src in sorted(glob.glob(os.path.join(ROOT, "assets", "shaders", "*.fx"))):
        out = os.path.join(out_dir, os.path.basename(src) + ".o")
        if os.path.exists(out) and os.path.getmtime(out) >= os.path.getmtime(src):
            continue
        print("FXC", os.path.basename(src))
        r = subprocess.run([FXC, "/nologo", "/T", "fx_2_0", "/O3", "/Op", "/Fo", out, src], capture_output=True,
                           text=True)
        if r.returncode:
            sys.exit(r.stdout + r.stderr)
    return out_dir


def generate_sources():
    """build/gen/shaders.cpp: the game's effects, the Xbox 360 ones (assets/fx) and the rebuild's own (shaders/)
    translated for the Vulkan renderer (tools/fxconv.py)."""
    import fxconv
    if not glob.glob(os.path.join(ROOT, "legacy", "fx", "*.fx.o")):
        sys.exit("legacy/fx has no *.fx.o files: python install_deps.py --fx PATH copies the Xbox 360 effects there (legacy/fx/README.md)")
    own = compile_own_effects()
    if fxconv.build(ORIG, GEN, os.path.join(BUILD, "fx", "cache"), os.path.join(ROOT, "legacy", "fx"), own):
        print("GEN shaders.cpp")


# The rebuild's own renderer, effect runtime, sun shadows, Xbox 360 effects and scenery culling need no bit-exact match
# with the original's x87 results: they get SSE2 (scalar SSE float math, and copies and compares of known sizes inlined
# as SSE moves instead of the original CRT's memcpy).
SSE2_FOLDERS = ("source/engine/rendering/vulkan/", "source/engine/rendering/materials/", "source/engine/rendering/sky/",
                "source/engine/movies/")
SSE2_FILES = {
    "source/engine/rendering/culling/SceneryCulling.cpp", "source/engine/rendering/effects/RainDrops.cpp",
    "source/engine/rendering/frame/Frame.cpp", "source/engine/rendering/frame/Mirror.cpp",
    "source/engine/rendering/frame/Misc.cpp", "source/engine/rendering/frame/Models.cpp",
    "source/engine/rendering/frame/Passes.cpp", "source/engine/rendering/frame/Resources.cpp",
    "source/engine/rendering/frame/Shader.cpp", "source/engine/rendering/lights/Lamps.cpp",
    "source/engine/rendering/lights/LocalLights.cpp", "source/engine/rendering/post/PostProcess.cpp",
    "source/engine/rendering/post/ViewModes.cpp", "source/engine/rendering/shadows/ShadowDebug.cpp",
    "source/engine/rendering/shadows/Shadows.cpp", "source/engine/rendering/shadows/SunShadows.cpp",
    "source/engine/resources/PackBridge.cpp", "source/engine/resources/PackCodec.cpp",
    "source/engine/resources/PackFiles.cpp",
}
SOURCE_ROOTS = ("source", "tests")


def source_flags(rel):
    rel = rel.replace(os.sep, "/")
    if rel.startswith(SSE2_FOLDERS) or rel in SSE2_FILES:
        return ["/arch:SSE2" if f == "/arch:IA32" else f for f in CFLAGS]
    return CFLAGS


def compile_sources(env):
    os.makedirs(SRC_OBJ, exist_ok=True)
    headers = []
    for folder in ("include", os.path.join("legacy", "include")) + SOURCE_ROOTS:
        headers += glob.glob(os.path.join(ROOT, folder, "**", "*.h"), recursive=True)
        headers += glob.glob(os.path.join(ROOT, folder, "**", "*.inc"), recursive=True)
    newest_header = max([os.path.getmtime(h) for h in headers] or [0])
    objs = []
    sources = []
    for folder in SOURCE_ROOTS:
        sources += sorted(glob.glob(os.path.join(ROOT, folder, "**", "*.cpp"), recursive=True))
    sources += sorted(glob.glob(os.path.join(GEN, "*.cpp")))
    for src in sources:
        rel = os.path.relpath(src, ROOT) if not src.startswith(GEN) else os.path.join("gen", os.path.basename(src))
        obj = os.path.join(SRC_OBJ, rel.replace(os.sep, "__")[:-4] + ".obj")
        if not os.path.exists(obj) or os.path.getmtime(obj) < max(os.path.getmtime(src), newest_header):
            print("CC", rel)
            run([CL] + source_flags(rel) + ["/Fo" + obj, src], env=env)
            coffpatch.rename_sections_file(obj, SECTION_RENAME)
        objs.append(obj)
    return objs


def import_thunks(thunks, env):
    """Direct (non-dllimport) API calls from new code jump through the original exe's IAT slots."""
    asm = os.path.join(BUILD, "import_thunks.asm")
    obj = os.path.join(SRC_OBJ, "import_thunks.obj")
    with open(asm, "w") as f:
        f.write(".386\n.model flat\n")
        for slot in sorted(set(thunks.values())):
            f.write("extern %s:dword\n" % slot)
        f.write(".code\n")
        for name, slot in sorted(thunks.items()):
            f.write("public %s\n%s:\n    jmp dword ptr [%s]\n" % (name, name, slot))
        f.write("end\n")
    run([os.path.join(MSVC_BIN, "ml.exe"), "/nologo", "/c", "/coff", "/Fo" + obj, asm], env=env)
    coffpatch.rename_sections_file(obj, SECTION_RENAME)
    return obj


def dynamic_imports(apis, env):
    """APIs the original exe never imported: each `__imp_` slot starts out pointing at a stub that looks the export up
    once (source/core/DynamicImports.cpp), stores it in the slot and jumps to it with the caller's arguments intact."""
    asm = os.path.join(BUILD, "dynamic_imports.asm")
    obj = os.path.join(SRC_OBJ, "dynamic_imports.obj")
    lines = [".386", ".model flat", "extern _nfs_resolve_import@8:near", ".data"]
    for i, (slot, export) in enumerate(sorted(apis.items())):
        lines += ["public %s" % slot, "%s dd nfs_resolver_%d" % (slot, i), "nfs_api_name_%d db \"%s\", 0" % (i, export)]
    lines.append(".code")
    for i, (slot, export) in enumerate(sorted(apis.items())):
        lines += ["nfs_resolver_%d:" % i, "    push ecx", "    push edx", "    push offset %s" % slot,
                  "    push offset nfs_api_name_%d" % i, "    call _nfs_resolve_import@8", "    pop edx", "    pop ecx",
                  "    jmp eax"]
    lines.append("end")
    open(asm, "w").write("\n".join(lines) + "\n")
    run([os.path.join(MSVC_BIN, "ml.exe"), "/nologo", "/c", "/coff", "/Fo" + obj, asm], env=env)
    coffpatch.rename_sections_file(obj, SECTION_RENAME)
    print("DYNAMIC %s" % ", ".join(sorted(apis.values())))
    return obj


def resolve(orig_objs, src_objs, index, env):
    orig_defs = {}
    for o in orig_objs:
        for name in object_symbols(o)[0]:
            orig_defs[name] = o
    iat = {}
    for name in object_symbols(os.path.join(OBJ, "unassigned_imports.obj"))[0]:
        iat[name[6:] if name.startswith("__imp_") else name] = name

    src_defs, src_undef, directives, aliased = set(), set(), [], set()
    for o in src_objs:
        d, u, drectve = object_symbols(o)
        src_defs |= d
        src_undef |= u
        directives += [(m.group(1), int(m.group(2), 16), m.group(3)) for m in DIRECTIVE.finditer(drectve)]
        aliased |= set(re.findall(r'/ALTERNATENAME:"?([^=" ]+)=', drectve, re.I))
    src_undef -= src_defs | aliased
    short = index.demangle(sorted(src_defs | src_undef))

    replaced, alternates, errors, thunks, dynamic = {}, {}, [], {}, {}

    def original_at(addr, what):
        name = index.by_addr.get(addr)
        if name is None or name not in orig_defs:
            errors.append("%s: no original symbol at %#x" % (what, addr))
            return None
        return name

    for name in src_defs:
        if name in orig_defs:
            replaced[name] = name
    bound = set()
    for kind, addr, hint in directives:
        orig = original_at(addr, "NFS_%s(%#x, %s)" % (kind.upper(), addr, hint))
        if orig is None:
            continue
        pool = src_defs if kind == "replace" else src_undef
        cands = [n for n in pool if short[n] == hint]
        if len(cands) != 1:
            errors.append("NFS_%s(%#x, %s): %d matching %s symbols %s" % (
                kind.upper(), addr, hint, len(cands), "defined" if kind == "replace" else "referenced", cands[:4]))
            continue
        if kind == "replace":
            replaced[orig] = cands[0]
            if cands[0] != orig:
                alternates[orig] = cands[0]
        else:
            alternates[cands[0]] = orig
            bound.add(cands[0])

    for name in sorted(src_undef - bound):
        if name in orig_defs:
            continue
        m = re.match(r"__imp__(\w+?)(@\d+)?$", name)
        if m:
            if m.group(1) in iat:
                alternates[name] = iat[m.group(1)]
            else:
                dynamic[name] = m.group(1)
            continue
        m = re.match(r"_(\w+?)(@\d+)$", name)
        if m and m.group(1) in iat:
            thunks[name] = iat[m.group(1)]
            continue
        if name.startswith("_") and name[1:] in orig_defs:
            alternates[name] = name[1:]
            continue
        cands = [n for n in index.by_short.get(short[name], []) if n in orig_defs]
        if len(cands) == 1:
            alternates[name] = cands[0]
        else:
            errors.append("%s (%s): %s" % (name, short[name], "%d original candidates %s" % (len(cands), cands[:4])
                                           if cands else "no original symbol with this name"))
    if errors:
        sys.exit("unresolved:\n  " + "\n  ".join(errors))
    if thunks:
        src_objs = src_objs + [import_thunks(thunks, env)]
    if dynamic:
        src_objs = src_objs + [dynamic_imports(dynamic, env)]

    by_obj = {}
    for orig, new in replaced.items():
        by_obj.setdefault(orig_defs[orig], []).append(orig)
    uses = {n for n in src_undef if n in orig_defs} | {v for k, v in alternates.items() if k in src_undef}
    return by_obj, alternates, src_objs, replaced, uses


def full_signatures(names):
    cache_path = os.path.join(BUILD, "signatures.json")
    cache = json.load(open(cache_path)) if os.path.exists(cache_path) else {}
    missing = [n for n in names if n.startswith("?") and n not in cache]
    if missing:
        cache.update(symbols.undname_batch(UNDNAME, missing, "0"))
        json.dump(cache, open(cache_path, "w"))
    return {n: cache.get(n, n) for n in names}


def check_register_conventions(replaced, uses, index, env):
    """Warn about original functions that take arguments in registers (see tools/x86scan.py)."""
    s = json.load(open(SYMBOLS))
    sizes = {int(f["start"], 16): int(f["size"], 16) for f in s["functions"]}
    names = sorted({n for n in set(replaced) | set(uses) if index.kind.get(n) == "func"})
    sigs = full_signatures(sorted(set(names) | {replaced[n] for n in names if n in replaced}))
    cache_path = os.path.join(BUILD, "reginputs.json")
    cache = json.load(open(cache_path)) if os.path.exists(cache_path) else {}
    disasm = x86scan.Disasm(BUILD, ORIG, os.path.join(MSVC_BIN, "dumpbin.exe"), env)
    for n in names:
        addr = index.addr_of.get(n)
        if addr not in sizes:
            continue
        # A replacement's own signature tells the convention when the original name is not MSVC-mangled.
        thiscall = "__thiscall" in sigs[n] or (n in replaced and "__thiscall" in sigs.get(replaced[n], ""))
        key = "%#x:%d" % (addr, thiscall)
        if key not in cache:
            cache[key] = x86scan.register_inputs(disasm.instructions(addr, sizes[addr]), thiscall)
        if cache[key]:
            role = "replaced" if n in replaced else "called from new code"
            print("WARN %s (%s, %#x) reads %s on entry: possible register arguments" % (
                index.short.get(n, n), role, addr, "/".join(cache[key])))
    json.dump(cache, open(cache_path, "w"))


def entry_shims(replaced, alternates, src_objs, env):
    """Route original callers of replaced functions through register-preserving shims (see tools/shims.py)."""
    funcs = set()
    for o in src_objs:
        funcs |= coffpatch.Coff(open(o, "rb").read()).defined_functions()
    targets = {orig: new for orig, new in replaced.items() if new in funcs}
    if not targets:
        return src_objs
    impl = {new: new + "$impl" for orig, new in targets.items() if orig == new}
    sigs = full_signatures(sorted(set(targets.values())))
    asm_lines = [".386", ".model flat", ".code"]
    labels = {}
    for i, (orig, new) in enumerate(sorted(targets.items())):
        spec = shims.spec_from_signature(sigs[new]) if new.startswith("?") else shims.spec_from_c_name(new)
        if spec is None:
            sys.exit("shim: cannot read the calling convention of %s (%s)" % (new, sigs[new]))
        shim, target = "nfs_shim_%d" % i, "nfs_impl_%d" % i
        labels[shim], labels[target] = orig, impl.get(new, new)
        asm_lines[2:2] = ["extern %s:near" % target, "public %s" % shim]
        asm_lines += shims.shim_asm(shim, target, spec)
        alternates.pop(orig, None)
    asm_lines.append("end")
    asm = os.path.join(BUILD, "entry_shims.asm")
    obj = os.path.join(SRC_OBJ, "entry_shims.obj")
    open(asm, "w").write("\n".join(asm_lines) + "\n")
    run([os.path.join(MSVC_BIN, "ml.exe"), "/nologo", "/c", "/coff", "/Fo" + obj, asm], env=env)
    c = coffpatch.Coff(open(obj, "rb").read())
    c.rename_symbols(labels)
    c.rename_sections(SECTION_RENAME)
    open(obj, "wb").write(c.serialize())
    os.makedirs(SRC_PATCHED, exist_ok=True)
    out = []
    for o in src_objs:
        dst = os.path.join(SRC_PATCHED, os.path.basename(o))
        coffpatch.rename_symbols_file(o, dst, impl)
        out.append(dst)
    print("SHIM %d replaced functions entered through register-preserving shims" % len(targets))
    return out + [obj]


def link(orig_objs, src_objs, index, env):
    by_obj, alternates, src_objs, replaced, uses = resolve(orig_objs, src_objs, index, env)
    with open(os.path.join(BUILD, "uses.json"), "w") as f:
        json.dump({"0x%x" % index.addr_of[n]: n for n in sorted(uses) if index.addr_of.get(n)}, f, indent=0)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "porting", "callconv_check.py")])
    check_register_conventions(replaced, uses, index, env)
    src_objs = entry_shims(replaced, alternates, src_objs, env)
    os.makedirs(PATCHED, exist_ok=True)
    functions = set()
    for o, names in by_obj.items():
        functions |= coffpatch.Coff(open(o, "rb").read()).defined_functions() & set(names)
    final_objs = []
    interior = 0
    for o in orig_objs:
        c = coffpatch.Coff(open(o, "rb").read())
        if o in by_obj:
            for n in c.unbind(by_obj[o]):
                if not n.startswith(("__real@", "__xmm@", "__mask@", "??_C@")):
                    print("REPLACE %-40s -> %s" % (n, replaced[n]))
        kept = c.keep_interior(functions)
        interior += kept
        if o in by_obj or kept:
            dst = os.path.join(PATCHED, os.path.basename(o))
            open(dst, "wb").write(c.serialize())
            final_objs.append(dst)
        else:
            final_objs.append(o)
    print("INTERIOR %d references inside replaced functions (switch cases and tables) kept on the original code" %
          interior)
    rsp = os.path.join(BUILD, "link.rsp")
    with open(rsp, "w") as f:
        for o in final_objs + src_objs + [os.path.join(OBJ, "__except_list.obj"), os.path.join(OBJ, "__pe_resources.res")]:
            f.write('"%s"\n' % o)
        for k, v in sorted(alternates.items()):
            f.write('"/ALTERNATENAME:%s=%s"\n' % (k, v))
    out = os.path.join(BUILD, "speed_linked.exe")
    print("LINK %d original + %d source objects, %d replaced, %d bindings" % (
        len(final_objs), len(src_objs), len(replaced), len(alternates)))
    run([LINK] + LDFLAGS + ["/OUT:" + out, "@" + rsp], env=env)
    with open(os.path.join(BUILD, "replaced.json"), "w") as f:
        json.dump({k: {"address": "0x%x" % index.addr_of.get(k, 0), "by": v} for k, v in sorted(replaced.items())}, f, indent=1)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "porting", "raw_addresses.py")])
    write_progress(index, replaced)
    return out, bool(src_objs)


def write_progress(index, replaced):
    """build/progress.tsv: ported vs. original function count and code bytes per unit."""
    s = json.load(open(SYMBOLS))
    splits = json.load(open(os.path.join(ROOT, "legacy", "config", "splits.json")))
    ranges = sorted((int(a, 16), int(b, 16), k.rsplit("/", 1)[-1][:-4]) for k, v in splits.items() for a, b in v["function_ranges"])
    ported = {index.addr_of[n] for n in replaced if n in index.addr_of}
    stats = {}
    for f in s["functions"]:
        a, size = int(f["start"], 16), int(f["size"], 16)
        unit = next((u for lo, hi, u in ranges if lo <= a < hi), "?")
        st = stats.setdefault(unit, [0, 0, 0, 0])
        st[0] += 1
        st[1] += size
        if a in ported:
            st[2] += 1
            st[3] += size
    total = [sum(v[i] for v in stats.values()) for i in range(4)]
    with open(os.path.join(BUILD, "progress.tsv"), "w") as f:
        f.write("unit\tfunctions\tbytes\tported_functions\tported_bytes\n")
        for unit, v in sorted(stats.items(), key=lambda kv: next((lo for lo, hi, u in ranges if u == kv[0]), 0)):
            f.write("%s\t%d\t%d\t%d\t%d\n" % (unit, *v))
        f.write("TOTAL\t%d\t%d\t%d\t%d\n" % tuple(total))
    print("PROGRESS %d/%d functions, %d/%d bytes (%.3f%%) from source" % (total[2], total[0], total[3], total[1], 100.0 * total[3] / total[1]))


def pe_sections(b):
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    n = struct.unpack_from("<H", b, pe + 6)[0]
    opt = struct.unpack_from("<H", b, pe + 20)[0]
    secs = []
    for i in range(n):
        o = pe + 24 + opt + 40 * i
        vs, va, rs, ra = struct.unpack_from("<IIII", b, o + 8)
        secs.append((b[o:o + 8].rstrip(b"\0").decode(errors="replace"), va, vs, ra, rs))
    return pe, secs


# DirectX's DLLs, which the rebuild no longer loads (docs/VULKAN.md, "No DirectX"): their import descriptors leave the
# table, and each of their IAT slots holds the address of the rebuild's own function of the same name
# (source/utils/D3DXMath.cpp, Input.cpp, Sound.cpp), so the original code's calls through the slots land there. The image
# has a fixed base, so the addresses stay valid. Imports by ordinal are named here.
DROPPED_DLLS = ("d3d9.dll", "d3dx9_26.dll", "dinput8.dll", "dsound.dll")
ORDINAL_NAMES = {("dsound.dll", 1): "DirectSoundCreate", ("dsound.dll", 6): "DirectSoundCaptureCreate"}


def map_functions(map_path):
    """{undecorated name: address} of the stdcall C functions in the link map (_Name@N)."""
    out = {}
    for line in open(map_path, errors="replace"):
        parts = line.split()
        if len(parts) >= 3 and re.match(r"^_[A-Za-z0-9_]+@\d+$", parts[1]):
            out.setdefault(parts[1][1:parts[1].index("@")], int(parts[2], 16))
    return out


def drop_imports(b, pe, secs, linked):
    def offset(rva):
        for name, va, vsize, raw, rsize in secs:
            if va <= rva < va + max(vsize, rsize):
                return rva - va + raw
        sys.exit("rva %#x is in no section" % rva)

    def cstring(rva):
        o = offset(rva)
        return bytes(b[o:b.index(b"\0", o)]).decode("latin-1")

    functions = map_functions(os.path.splitext(linked)[0] + ".map")
    dd = pe + 24 + 96
    table_rva, _ = struct.unpack_from("<II", b, dd + 8)
    table = offset(table_rva)
    descriptors = []
    while True:
        d = bytes(b[table + 20 * len(descriptors):table + 20 * (len(descriptors) + 1)])
        if d == bytes(20):
            break
        descriptors.append(d)
    kept = []
    for d in descriptors:
        lookup, _, _, name_rva, iat = struct.unpack("<5I", d)
        if cstring(name_rva).lower() not in DROPPED_DLLS:
            kept.append(d)
            continue
        names = lookup or iat
        for k in range(1 << 16):
            entry = struct.unpack_from("<I", b, offset(names) + 4 * k)[0]
            if not entry:
                break
            if entry & 0x80000000:
                function = ORDINAL_NAMES.get((cstring(name_rva).lower(), entry & 0xFFFF))
                if not function:
                    sys.exit("%s ordinal %d has no name in ORDINAL_NAMES" % (cstring(name_rva), entry & 0xFFFF))
            else:
                function = cstring(entry + 2)
            if function not in functions:
                sys.exit("%s!%s has no replacement (source/core)" % (cstring(name_rva), function))
            struct.pack_into("<I", b, offset(iat) + 4 * k, functions[function])
        print("IMPORTS %s dropped, %d slots point at the rebuild's functions" % (cstring(name_rva), k))
    b[table:table + 20 * (len(descriptors) + 1)] = b"".join(kept) + bytes(20 * (len(descriptors) + 1 - len(kept)))
    struct.pack_into("<I", b, dd + 12, 20 * (len(kept) + 1))


def finalize(linked, modified):
    final = os.path.join(BUILD, "speed.exe")
    if not modified:
        run([DELINK, "ida-restore-pe", ORIG, linked, "-o", final])
        same = open(final, "rb").read() == open(ORIG, "rb").read()
        print("OUT build/speed.exe (byte-identical to original: %s)" % same)
        return
    b = bytearray(open(linked, "rb").read())
    o = open(ORIG, "rb").read()
    pe_b, secs_b = pe_sections(b)
    pe_o, secs_o = pe_sections(o)
    for name in (".text", ".rdata", ".data"):
        sb = next(s for s in secs_b if s[0] == name)
        so = next(s for s in secs_o if s[0] == name)
        if sb[1] != so[1] or sb[2] < so[2]:
            sys.exit("original section %s moved or shrank (%#x/%#x vs %#x/%#x)" % (name, sb[1], sb[2], so[1], so[2]))
    dd_b, dd_o = pe_b + 24 + 96, pe_o + 24 + 96
    for idx in (1, 12):  # import directory, IAT
        b[dd_b + 8 * idx:dd_b + 8 * idx + 8] = o[dd_o + 8 * idx:dd_o + 8 * idx + 8]
    imp_rva, imp_size = struct.unpack_from("<II", o, dd_o + 8)
    rdata_o = next(s for s in secs_o if s[0] == ".rdata")
    rdata_b = next(s for s in secs_b if s[0] == ".rdata")
    off = imp_rva - rdata_o[1]
    if b[rdata_b[3] + off:rdata_b[3] + off + imp_size] != o[rdata_o[3] + off:rdata_o[3] + off + imp_size]:
        sys.exit("import descriptors differ from the original")
    drop_imports(b, pe_b, secs_b, linked)
    struct.pack_into("<HH", b, pe_b + 24 + 48, 4, 0)  # subsystem version 4.0, as shipped
    open(final, "wb").write(b)
    print("OUT build/speed.exe")
    for s in secs_b:
        print("  %-8s va=%#08x vsize=%#08x" % (s[0], s[1] + 0x400000, s[2]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true")
    ap.add_argument("--baseline", action="store_true", help="link only the original objects (must be byte-identical)")
    ap.add_argument("--no-data", action="store_true", help="the code only: no packs made from assets/ (docs/PACKS.md)")
    args = ap.parse_args()
    needed = [ORIG, DELINK] + ([] if args.baseline else [os.path.join(ROOT, "external", "dxc", "dxc.exe")])
    missing = [os.path.relpath(p, ROOT) for p in needed if not os.path.isfile(p)]
    if missing:
        sys.exit("missing: %s\npython install_deps.py --check says what is there; python install_deps.py fetches delink and dxc"
                 % ", ".join(missing))
    os.makedirs(BUILD, exist_ok=True)
    env = msvc_env()
    split(args.clean, prepare_symbols())
    index = symbols.SymbolIndex(SYMBOLS, os.path.join(BUILD, "names.json"), UNDNAME)
    if not args.baseline:
        generate_sources()
    src_objs = [] if args.baseline else compile_sources(env)
    if not args.baseline:
        import thirdparty
        for line in thirdparty.build(env):
            if not line.endswith("up to date"):
                print("DLL", line)
    linked, modified = link(original_objects(), src_objs, index, env)
    finalize(linked, modified)
    if not args.baseline and not args.no_data and os.path.isdir(os.path.join(ROOT, "assets", "vehicles")):
        # The data: the packs whose sources changed since they were made (tools/assetc).
        subprocess.run([sys.executable, "-m", "assetc.vehicles"], cwd=os.path.join(ROOT, "tools"), check=True)
        subprocess.run([sys.executable, "-m", "assetc.world", "--if-changed"], cwd=os.path.join(ROOT, "tools"),
                       check=True)


if __name__ == "__main__":
    main()
