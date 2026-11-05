"""Build the Oz engine: Quake3e's core with the blend's parts (ENGINE_PLAN.md P0).

python engine/build_engine.py [--debug] [--clean] [--jobs 4] [--skip-qvms]

1. Fetches the game code into thirdparty/ioq3: ioquake3 at the commit pinned in
   thirdparty.json "game_code", with game/patches/*.patch applied (already-applied ones are left alone).
2. Builds engine/quake3e with CMake and Ninja under MSVC 2022's vcvars64: OzArena.x64.exe,
   OzArena.ded.x64.exe and the renderer modules (OzArena_<renderer>_x86_64.dll). When the vulkan
   renderer's UI shaders changed, compiles them to SPIR-V first, with glslang built from source.
3. Builds the QVMs (qagame, cgame, ui) with ioquake3's CMake and its own q3lcc and q3asm.
4. Stages build/engine/: OzArena.exe, OzArena.ded.exe, the renderer modules,
   oz/pak_oz_code.pk3 with the QVMs, oz/zz-oz-cards.pk3 (tools/make_cards_pk3.py) and oz/zz-oz-ui.pk3 with engine/ui (the native UI's pages,
   styles, fonts and settings.json, under ui/) and the display font traced from the player's pak0.

Run it with build/engine/OzArena.exe; the player's baseq3 comes from their Steam install
(fs_steampath) or +set fs_basepath "<Quake 3 folder>".

On Linux (WSL2 Ubuntu or a Linux PC: python3 engine/build_engine.py) the same libraries and engine build
with gcc and Ninja into build/linux/: OzArena.x86_64, OzArena.ded.x86_64, OzArena_<renderer>_x86_64.so,
libSDL2-2.0.so.0 and libopenal.so.1 (each executable looks beside itself, $ORIGIN). Its oz/ paks are the
main build's (build/engine/oz, --paks-from), copied as they are: a pure server checks paks by checksum,
so the Linux and Windows games must carry the same files. The world packs and Ozmium's local-only CZ45
paks are left out. engine/tools/linux_deps.sh installs what the build needs (apt).
"""
import os, re, sys, json, glob, shutil, hashlib, zipfile, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(HERE, "quake3e")
LINUX = sys.platform.startswith("linux")
OUT = os.path.join(ROOT, "build", "linux" if LINUX else "engine")
MAIN_OZ = os.path.join(ROOT, "build", "engine", "oz")   # the Windows build's paks, which Linux carries as they are
PINS = json.load(open(os.path.join(HERE, "thirdparty.json")))


def for_platform(entry):
    """A thirdparty.json lib or tool with its "linux" entry applied on Linux: its cmake definitions replace
    the same-named ones, its engine_cmake entries and program replace those, its stage replaces the list."""
    over = entry.get("linux") if LINUX else None
    if not over:
        return entry
    e = dict(entry)
    if "cmake" in over:
        names = {d.split("=", 1)[0] for d in over["cmake"]}
        e["cmake"] = [d for d in entry.get("cmake", []) if d.split("=", 1)[0] not in names] + over["cmake"]
    if "engine_cmake" in over:
        e["engine_cmake"] = dict(entry.get("engine_cmake", {}), **over["engine_cmake"])
    for k in ("stage", "program"):
        if k in over:
            e[k] = over[k]
    return e
GAME = PINS["game_code"]
GAME_TREE = os.path.join(ROOT, "thirdparty", "ioq3")
VCVARS = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
NAME = "OzArena"

ap = argparse.ArgumentParser()
ap.add_argument("--debug", action="store_true", help="Debug build instead of Release")
ap.add_argument("--clean", action="store_true", help="remove build/engine first")
ap.add_argument("--jobs", type=int, default=4, help="compile jobs (4 is safe on 16 GB)")
ap.add_argument("--skip-qvms", action="store_true", help="keep the QVMs from the last build")
ap.add_argument("--libs-only", action="store_true", help="build the third-party libraries and stop")
ap.add_argument("--ui-only", action="store_true", help="only restage oz/zz-oz-ui.pk3 from engine/ui")
ap.add_argument("--paks-from", default=MAIN_OZ, help="Linux: the oz/ paks to carry (the Windows build's)")
a = ap.parse_args()
CONFIG = "Debug" if a.debug else "Release"


def run(cmd, cwd=ROOT, env=None):
    print(">", " ".join(cmd), flush=True)
    if env:   # Windows finds a program on the parent's PATH, not env's (cmake is VS's, on vcvars' PATH)
        cmd = [shutil.which(cmd[0], path=env["PATH"]) or cmd[0]] + cmd[1:]
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def msvc_env():
    """The environment vcvars64.bat sets up (cl, link, the Windows SDK, VS's CMake and Ninja).
    On Linux, this environment, once gcc, CMake and Ninja are found."""
    if LINUX:
        missing = [t for t in ("gcc", "g++", "cmake", "ninja", "git") if not shutil.which(t)]
        if missing:
            sys.exit("missing %s: run engine/tools/linux_deps.sh" % ", ".join(missing))
        return dict(os.environ)
    if not os.path.exists(VCVARS):
        sys.exit("Visual Studio 2022 (Desktop C++) isn't installed: no " + VCVARS)
    out = subprocess.run('"%s" >nul && set' % VCVARS, shell=True, capture_output=True, text=True, check=True).stdout
    env = dict(os.environ)
    for line in out.splitlines():
        k, sep, v = line.partition("=")
        if sep:
            env[k] = v
    return env


def fetch_game_code():
    fetch(GAME_TREE, GAME["url"], GAME["commit"])   # a refetch drops old patches; reapplied below
    apply_patches(GAME_TREE, GAME["patches"])


def apply_patches(tree, folder):
    """folder/*.patch (relative to the repo) onto a fetched checkout, in name order. The set applied is
    fingerprinted in the checkout's .git (oz_patches): when any patch is added, edited or removed, the
    checkout goes back to its pinned commit and every patch is applied again. Only skipping the ones
    already applied, an edited patch (lg-width's 0015, 2026-11-03) never applied over its old version."""
    patches = sorted(glob.glob(os.path.join(ROOT, folder, "*.patch")))
    digest = hashlib.sha1()
    for patch in patches:
        digest.update(os.path.basename(patch).encode() + b"\0" + open(patch, "rb").read() + b"\0")
    stamp = os.path.join(tree, ".git", "oz_patches")
    if os.path.exists(stamp) and open(stamp).read().strip() == digest.hexdigest():
        return
    print("> patches changed: %s back to its pinned commit" % os.path.relpath(tree, ROOT))
    run(["git", "checkout", "-q", "-f", "HEAD"], tree)
    run(["git", "clean", "-q", "-fd"], tree)
    for patch in patches:
        print("> patch", os.path.basename(patch))
        if LINUX:   # this checkout's CRLF patch onto Linux git's LF checkout
            r = subprocess.run(["git", "apply", "--whitespace=nowarn", "-"], cwd=tree, capture_output=True,
                               input=open(patch, "rb").read().replace(b"\r\n", b"\n"))
            r.stderr = r.stderr.decode(errors="replace")
        else:
            r = subprocess.run(["git", "apply", "--whitespace=nowarn", patch], cwd=tree, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("patch %s doesn't apply to %s:\n%s" % (os.path.basename(patch), tree, r.stderr))
    with open(stamp, "w") as f:
        f.write(digest.hexdigest() + "\n")


def fetch(d, url, commit, sparse=None):
    """Shallow checkout of one pinned commit; an existing checkout at that commit is left alone.
    With sparse (a list of paths), only those files are fetched (a partial clone)."""
    if os.path.isdir(os.path.join(d, ".git")):
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=d, capture_output=True, text=True).stdout.strip()
        if head == commit:
            return
    else:
        os.makedirs(d, exist_ok=True)
        run(["git", "init", "-q"], d)
        run(["git", "remote", "add", "origin", url], d)
    if sparse:
        run(["git", "sparse-checkout", "set", "--no-cone"] + sparse, d)
    run(["git", "fetch", "-q", "--depth", "1"] + (["--filter=blob:none"] if sparse else []) + ["origin", commit], d)
    run(["git", "checkout", "-q", "-f", "FETCH_HEAD"], d)


def build_libs(env):
    """Third-party libraries built from source (thirdparty.json "libs"), in the order listed (FreeType
    before RmlUi). A lib without "cmake" is only fetched (cJSON's one file is compiled into the client).
    A lib's "patches" folder (engine/patches/<lib>) is applied after the fetch.
    Returns {staged name: built file} and the engine's CMake definitions for them ("engine_cmake").
    In both, {src} and {build} stand for the library's checkout and build folders, {thirdparty} for
    the folder holding every library, and {d} for MSVC's Debug suffix."""
    staged, defs = {}, []
    tp = os.path.join(OUT, "thirdparty")
    for name, lib in PINS.get("libs", {}).items():
        lib = for_platform(lib)
        src = os.path.join(tp, name)
        fetch(src, lib["url"], lib["commit"], lib.get("sparse"))
        if lib.get("submodules"):   # mbedTLS configures only with its framework submodule
            run(["git", "submodule", "update", "--init", "--depth", "1"], src)
        if "patches" in lib:
            apply_patches(src, lib["patches"])
        build = os.path.join(tp, name + "-build")
        fill = lambda v: v.format(src=src, build=build, thirdparty=tp, d="d" if a.debug else "").replace("\\", "/")
        if "cmake" in lib:
            cmake(src, build, env, *[fill(d) for d in lib["cmake"]])
        for built, as_name in lib.get("stage", {}).items():
            p = os.path.join(build, fill(built))
            if LINUX and "*" in p:   # a versioned .so: libopenal.so.1.24.3, libSDL2-2.0.so.0.3200.8
                found = sorted(f for f in glob.glob(p) if not os.path.islink(f))
                if not found:
                    sys.exit("%s didn't build %s" % (name, p))
                p = found[-1]
            staged[as_name] = p
        for k, v in lib.get("engine_cmake", {}).items():
            defs.append("%s=%s" % (k, fill(v)))
    return staged, defs


def cmake(src, build, env, *defs):
    if LINUX:   # static libraries linked into the renderer modules' shared objects
        defs = ("CMAKE_POSITION_INDEPENDENT_CODE=ON",) + defs
    run(["cmake", "-S", src, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + CONFIG] + ["-D" + d for d in defs], env=env)
    run(["cmake", "--build", build, "-j", str(a.jobs)], env=env)


def fsr_glsl(env):
    """AMD FSR 1's shaders for the oz renderer (Video > Upscaler): renderer2/glsl/ozfsr/*.in run through
    MSVC's preprocessor with AMD's ffx_a.h and ffx_fsr1.h (thirdparty.json "fidelityfx-fsr", fetched with
    the libs), which keeps their GLSL path and drops the rest, to build/engine/rend2_fsr/ozfsr<x>_fp.glsl.
    CMakeLists.txt embeds them with rend2's own GLSL (OZ_FSR_GLSL_DIR). Returns that folder."""
    inc = os.path.join(OUT, "thirdparty", "fidelityfx-fsr", "ffx-fsr")
    out = os.path.join(OUT, "rend2_fsr")
    os.makedirs(out, exist_ok=True)
    cl = None if LINUX else shutil.which("cl", path=env.get("PATH") or env.get("Path"))
    for src in sorted(glob.glob(os.path.join(SRC, "code", "renderer2", "glsl", "ozfsr", "*.in"))):
        name = os.path.splitext(os.path.basename(src))[0].replace("_", "")   # ozfsr_easu -> ozfsreasu
        if LINUX:   # gcc's preprocessor, without line markers, as MSVC's /EP
            cmd = ["gcc", "-E", "-P", "-x", "c", "-I", inc, src]
        else:
            cmd = [cl, "/nologo", "/EP", "/I", inc, "/Tc", src]
        r = subprocess.run(cmd, capture_output=True, text=True, env=env)
        if r.returncode:
            sys.exit("FSR shader %s: %s" % (src, r.stderr))
        text = "\n".join(l.rstrip() for l in r.stdout.splitlines() if l.strip()) + "\n"
        dst = os.path.join(out, name + "_fp.glsl")
        if not os.path.exists(dst) or open(dst, encoding="utf-8").read() != text:   # unchanged: no rebuild
            open(dst, "w", encoding="utf-8", newline="\n").write(text)
    return out


def vk_ui_shaders(env):
    """The vulkan renderer's UI shaders (renderervk/shaders/oz_ui/*.vert, *.frag) as SPIR-V arrays in
    shaders/spirv/oz_ui_shader_data.c, in Quake3e's bin2hex.c format (renderervk/tr_oz_ui.c includes
    it as vk.c includes shader_data.c). The file is committed with a hash of its sources: only a
    change to them fetches and builds glslang (thirdparty.json "tools"; no Vulkan SDK needed)."""
    shaders = os.path.join(SRC, "code", "renderervk", "shaders")
    srcs = sorted(glob.glob(os.path.join(shaders, "oz_ui", "*.vert")) + glob.glob(os.path.join(shaders, "oz_ui", "*.frag")))
    out = os.path.join(shaders, "spirv", "oz_ui_shader_data.c")
    tool = for_platform(PINS["tools"]["glslang"])
    h = hashlib.sha1()
    for s in srcs:
        h.update(os.path.basename(s).encode())
        h.update(open(s, "rb").read().replace(b"\r\n", b"\n"))
    stamp = "// sources %s, glslang %s" % (h.hexdigest(), tool["release"].split()[0])
    if os.path.exists(out) and stamp in open(out, encoding="utf-8").read():
        return
    src = os.path.join(OUT, "thirdparty", "glslang")
    build = src + "-build"
    fetch(src, tool["url"], tool["commit"])
    cmake(src, build, env, *tool["cmake"])
    spv = os.path.join(build, "oz_ui.spv")
    text = ["// Oz: the native UI's shaders for the vulkan renderer (tr_oz_ui.c), compiled from shaders/oz_ui",
            "// by engine/build_engine.py; don't edit", stamp, ""]
    for s in srcs:
        stage = os.path.splitext(s)[1][1:]
        run([os.path.join(build, tool["program"]), "-V", "-S", stage, "-o", spv, s], env=env)
        data = open(spv, "rb").read()
        body = "".join("0x%.2X" % b + ("" if i + 1 == len(data) else ", " if (i + 1) % 16 else ",\n\t") for i, b in enumerate(data))
        name = "%s_%s_spv" % (os.path.splitext(os.path.basename(s))[0], stage)
        text.append("const unsigned char %s[%d] = {\n\t%s\n};\n" % (name, len(data), body))
    os.remove(spv)
    open(out, "w", encoding="utf-8", newline="\n").write("\n".join(text))
    print("vulkan UI shaders:", out)


def stage_linux(engine_build, libs):
    """build/linux/: the executables, the renderer modules and the two libraries they load, and the main
    build's oz/ paks as they are (see the docstring), with the launcher and the LICENSE."""
    os.makedirs(os.path.join(OUT, "oz"), exist_ok=True)
    for as_name, built in libs.items():
        shutil.copy2(built, os.path.join(OUT, as_name))
    for exe in (NAME + ".x86_64", NAME + ".ded.x86_64"):
        shutil.copy2(os.path.join(engine_build, exe), os.path.join(OUT, exe))
    for f in glob.glob(os.path.join(engine_build, NAME + "_*_x86_64.so")):
        shutil.copy2(f, OUT)
    shutil.copy2(os.path.join(HERE, "installer", "linux", "oz-arena-launcher"), os.path.join(OUT, "oz-arena-launcher"))
    shutil.copy2(os.path.join(ROOT, "LICENSE"), os.path.join(OUT, "LICENSE"))
    shutil.copy2(os.path.join(HERE, "installer", "linux", "oz-arena.png"), os.path.join(OUT, "oz-arena.png"))   # the menu entry's icon
    for f in ("oz-arena-launcher", NAME + ".x86_64", NAME + ".ded.x86_64"):
        os.chmod(os.path.join(OUT, f), 0o755)
    paks = sorted(glob.glob(os.path.join(a.paks_from, "*.pk3")))
    if not any(os.path.basename(p) == "pak_oz_code.pk3" for p in paks):
        sys.exit("no pak_oz_code.pk3 in %s: build the Windows game first (python engine/build_engine.py on Windows)" % a.paks_from)
    for p in paks:
        n = os.path.basename(p)
        if re.match(r"pak_oz_world\d*\.pk3$", n) or n.startswith("pak_oz_zz_cz"):   # released apart / local only
            continue
        shutil.copy2(p, os.path.join(OUT, "oz", n))
    print("staged", OUT, "(paks from %s)" % a.paks_from)
    for f in sorted(os.listdir(OUT)) + ["oz/" + f for f in sorted(os.listdir(os.path.join(OUT, "oz")))]:
        p = os.path.join(OUT, f)
        if os.path.isfile(p):
            print("  %-36s %9d" % (f, os.path.getsize(p)))


def stage(engine_build, qvm_build, libs):
    os.makedirs(os.path.join(OUT, "oz"), exist_ok=True)
    for as_name, built in libs.items():
        shutil.copy2(built, os.path.join(OUT, as_name))
    shutil.copy2(os.path.join(engine_build, NAME + ".x64.exe"), os.path.join(OUT, NAME + ".exe"))
    shutil.copy2(os.path.join(engine_build, NAME + ".ded.x64.exe"), os.path.join(OUT, NAME + ".ded.exe"))
    for f in glob.glob(os.path.join(engine_build, NAME + "_*_x86_64.dll")):
        shutil.copy2(f, OUT)
    stage_ui()
    stage_editor_tools()
    vm = os.path.join(qvm_build, CONFIG, "baseq3", "vm")
    pk3 = os.path.join(OUT, "oz", "pak_oz_code.pk3")
    with zipfile.ZipFile(pk3, "w", zipfile.ZIP_DEFLATED) as z:
        for name in ("qagame", "cgame", "ui"):
            z.write(os.path.join(vm, name + ".qvm"), "vm/%s.qvm" % name)
        # the game code's own files (its shaders), at their paths under game/assets
        assets = os.path.join(ROOT, "game", "assets")
        for d, _, files in os.walk(assets):
            for f in files:
                z.write(os.path.join(d, f), os.path.relpath(os.path.join(d, f), assets).replace("\\", "/"))
        z.writestr("default.cfg", default_cfg())
    print("staged", OUT)
    for f in sorted(os.listdir(OUT)) + ["oz/" + f for f in sorted(os.listdir(os.path.join(OUT, "oz")))]:
        p = os.path.join(OUT, f)
        if os.path.isfile(p):
            print("  %-36s %9d" % (f, os.path.getsize(p)))


def default_cfg():
    """Oz's game's default.cfg, run in place of id's (pak0's): the binds and the engine settings' defaults
    from engine/ui/settings.json, so a first run plays as the Settings page shows it. The player's own
    oz/q3config.cfg runs after it; the install's baseq3 one never does (common.c, Com_RunsGameConfig)."""
    settings = json.load(open(os.path.join(HERE, "ui", "settings.json"), encoding="utf-8"))["settings"]
    lines = ["// Oz Arena's defaults, made by engine/build_engine.py from engine/ui/settings.json", "unbindall",
             'bind "ESCAPE" "togglemenu"']   # as id's: the page has no entry for it
    for s in settings:
        if not s.get("wired") or s["tab"] == "profile":   # the profile's name and model are the player's
            continue
        if s["type"] == "bind":
            lines += ['bind "%s" "%s"' % (key, s["command"]) for key in s["default"] if key]
        elif s.get("cvar") and not s["cvar"].startswith("oz_") and "cvar_default" in s:
            lines.append('seta %s "%s"' % (s["cvar"], s["cvar_default"]))
    # no Settings entry: trilinear, as the look test draws (id's default jumps between mip levels in bands)
    lines.append('seta r_textureMode "GL_LINEAR_MIPMAP_LINEAR"')
    return "\n".join(lines) + "\n"


# NetRadiant-custom's map compilers (GPL-2.0) and the DLLs they load, for the map editor (P13): its
# imports (q3map2 -convert) and compiles (q3map2, mbspc). From its Windows release, unpacked into
# build/tools/netradiant-custom-<date>/app (as P1's relight uses it).
EDITOR_TOOLS = ["q3map2.exe", "mbspc.exe", "libassimp-6.dll", "libbz2-1.dll", "libgcc_s_seh-1.dll", "libglib-2.0-0.dll",
                "libiconv-2.dll", "libintl-8.dll", "libjpeg-8.dll", "libminizip-1.dll", "libpcre2-8-0.dll", "libpng16-16.dll",
                "libstdc++-6.dll", "libwinpthread-1.dll", "libxml2-16.dll", "zlib1.dll"]


def stage_editor_tools():
    apps = sorted(glob.glob(os.path.join(ROOT, "build", "tools", "netradiant-custom-*", "app")))
    if not apps:
        print("> no build/tools/netradiant-custom-*/app: the map editor can't import or compile maps")
        return
    dest = os.path.join(OUT, "tools")
    os.makedirs(dest, exist_ok=True)
    for f in EDITOR_TOOLS:
        shutil.copy2(os.path.join(apps[-1], f), dest)
    for f in ("LICENSE", "README.md"):
        p = os.path.join(apps[-1], "docs", f)
        if os.path.exists(p):
            shutil.copy2(p, os.path.join(dest, "netradiant-custom-" + f))


def stage_ui():
    """engine/ui -> oz/zz-oz-ui.pk3 under ui/ (the native UI reads ui/settings.json, ui/pages/*.rml, ...),
    with the fetched libraries' "ui" files (the fonts). The display font is id's art, so the engine
    traces it from the player's pak0 as the UI starts (oz_ui_font.cpp). The engine mounts this pak
    under every mod and on pure servers, for the native UI only."""
    ui = os.path.join(HERE, "ui")
    os.makedirs(os.path.join(OUT, "oz"), exist_ok=True)
    with zipfile.ZipFile(os.path.join(OUT, "oz", "zz-oz-ui.pk3"), "w", zipfile.ZIP_DEFLATED) as z:
        for d, _, files in os.walk(ui):
            for f in sorted(files):
                p = os.path.join(d, f)
                z.write(p, "ui/" + os.path.relpath(p, ui).replace("\\", "/"))
        for name, lib in PINS.get("libs", {}).items():
            for src, dest in lib.get("ui", {}).items():
                p = os.path.join(OUT, "thirdparty", name, src)
                if not os.path.exists(p):
                    sys.exit("%s isn't fetched: run build_engine.py without --ui-only first" % p)
                z.write(p, "ui/" + dest)
    # the player cards' art (P12): the Godot build's library/cards, derived from id's art, so made here and never committed
    subprocess.call([sys.executable, os.path.join(ROOT, "tools", "make_cards_pk3.py")])


if a.ui_only:
    if LINUX:
        sys.exit("--ui-only is the Windows build's: Linux carries the main build's zz-oz-ui.pk3")
    stage_ui()
    sys.exit(0)
if a.clean and os.path.isdir(OUT):
    shutil.rmtree(OUT)
env = msvc_env()
libs, lib_defs = build_libs(env)
if a.libs_only:
    sys.exit(0)
engine_build = os.path.join(OUT, "cmake")
qvm_build = os.path.join(OUT, "qvm")
vk_ui_shaders(env)
fsr_dir = fsr_glsl(env)
cmake(SRC, engine_build, env, "CNAME=" + NAME, "DNAME=" + NAME + ".ded", "USE_SDL=ON", "USE_VULKAN=ON", "USE_RENDERER_DLOPEN=ON",
      "OZ_FSR_GLSL_DIR=" + fsr_dir.replace("\\", "/"), *lib_defs)
if LINUX:   # the QVMs and every pak are the main build's (see the docstring)
    stage_linux(engine_build, libs)
    sys.exit(0)
if not a.skip_qvms:
    fetch_game_code()
    # The QVM build doesn't track headers: a .c compiled against an older struct layout kept its old
    # .asm (P3's centity_t and localEntity_t fields corrupted the cgame at random). A header newer than
    # the last cgame.qvm rebuilds them all.
    built = os.path.join(qvm_build, CONFIG, "baseq3", "vm", "cgame.qvm")
    headers = glob.glob(os.path.join(GAME_TREE, "code", "**", "*.h"), recursive=True)
    if os.path.exists(built) and max(os.path.getmtime(h) for h in headers) > os.path.getmtime(built):
        print("> a game header changed: rebuilding the QVMs")
        shutil.rmtree(qvm_build)
    cmake(GAME_TREE, qvm_build, env, "BUILD_SERVER=OFF", "BUILD_CLIENT=OFF", "BUILD_RENDERER_GL1=OFF",
          "BUILD_RENDERER_GL2=OFF", "BUILD_GAME_LIBRARIES=OFF", "BUILD_GAME_QVMS=ON")
stage(engine_build, qvm_build, libs)
