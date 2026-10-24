"""Builds q3cam, the camera mod for recording trailer footage from retail Quake 3 (TRAILER_PLAN.md).

python tools/trailer/q3cam/build.py [--out work/trailer/q3cam]

1. id's own QVM toolchain from the pinned 1.32 source (tools/q3core/thirdparty/id-q3a): lburg,
   q3rcc, q3cpp and q3asm, compiled 32-bit with MSVC 2022 (the 1999 code assumes it).
2. id's 1.32 cgame and game code, copied out of thirdparty, with src/cg_q3cam.c added and the
   HOOKS below put in (thirdparty itself is never edited).
3. cgame.qvm and qagame.qvm, compiled as cgame.bat / game.bat do, packed in <out>/pak0.pk3.

Everything is built under --out (work/ is gitignored). capture_q3.py installs the pk3.
"""
import os, sys, glob, shutil, subprocess, argparse, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
ID = os.path.join(ROOT, "tools", "q3core", "thirdparty", "id-q3a")
LCC = os.path.join(ID, "lcc")

ap = argparse.ArgumentParser()
ap.add_argument("--out", default=os.path.join(ROOT, "work", "trailer", "q3cam"))
a = ap.parse_args()
OUT = os.path.abspath(a.out)
BIN = os.path.join(OUT, "tools")
SRC = os.path.join(OUT, "src")

# (file, anchor, text put after the anchor). Anchors are id's lines; CRLF is handled below.
HOOKS = [
    ("cgame/cg_local.h",
     "void CG_DrawActiveFrame( int serverTime, stereoFrame_t stereoView, qboolean demoPlayback );\n",
     "\n// cg_q3cam.c\nvoid CG_CamPlay_f( void );\nvoid CG_CamStop_f( void );\nvoid CG_CamFrame( void );\n"
     "int CG_CamView( int inwater );\nvoid CG_CamAreamask( void );\n"),
    ("cgame/cg_consolecmds.c",
     '\t{ "viewpos", CG_Viewpos_f },\n',
     '\t{ "cam_play", CG_CamPlay_f },\n\t{ "cam_stop", CG_CamStop_f },\n'),
    ("cgame/cg_view.c",
     "\t// build cg.refdef\n",
     "\tCG_CamFrame();\t\t// q3cam: advance the camera path\n"),
    ("cgame/cg_view.c",
     "\tinwater = CG_CalcViewValues();\n",
     "\tinwater = CG_CamView( inwater );\t// q3cam: the path's view replaces it\n"),
    ("cgame/cg_view.c",
     "\tmemcpy( cg.refdef.areamask, cg.snap->areamask, sizeof( cg.refdef.areamask ) );\n",
     "\tCG_CamAreamask();\t// q3cam: every area drawn while a path plays\n"),
    ("game/g_main.c",
     "\t// for tracking changes\n\tCheckCvars();\n",
     "\n\t// q3cam: with cam_broadcast 1, every entity goes to every client, so a camera path far\n"
     "\t// from the spectator still sees items, doors and lifts\n"
     "\tif ( trap_Cvar_VariableIntegerValue( \"cam_broadcast\" ) ) {\n"
     "\t\tfor ( i = 0, ent = g_entities ; i < level.num_entities ; i++, ent++ ) {\n"
     "\t\t\tif ( ent->inuse ) {\n"
     "\t\t\t\tent->r.svFlags |= SVF_BROADCAST;\n"
     "\t\t\t}\n"
     "\t\t}\n"
     "\t}\n"),
]


def msvc_env():
    """The environment vcvars32.bat sets up (x86 tools)."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    vs = subprocess.run([vswhere, "-latest", "-products", "*", "-property", "installationPath"],
                        capture_output=True, text=True).stdout.strip()
    bat = os.path.join(vs, "VC", "Auxiliary", "Build", "vcvars32.bat")
    if not os.path.isfile(bat):
        sys.exit("q3cam: MSVC's vcvars32.bat not found (Visual Studio 2022 C++ tools)")
    out = subprocess.run(f'"{bat}" >nul && set', capture_output=True, text=True, shell=True).stdout
    env = {k.upper(): v for k, v in os.environ.items()}
    for line in out.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            env[k.upper()] = v   # set prints "Path"; os.environ's keys are upper case
    return env


ENV = None


def run(cmd, cwd=None):
    # CreateProcess looks programs up on this process's PATH, not the env passed, so find cl here
    exe = shutil.which(cmd[0], path=ENV["PATH"]) or cmd[0]
    r = subprocess.run([exe] + cmd[1:], cwd=cwd, env=ENV, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"q3cam: failed: {' '.join(cmd)}\n{r.stdout}{r.stderr}")
    return r


def cl_exe(name, sources, incs, defines=()):
    """Compiles C sources into BIN/<name>.exe."""
    obj = os.path.join(BIN, "obj", name)
    os.makedirs(obj, exist_ok=True)
    cmd = ["cl", "/nologo", "/O2", "/MT", "/w", "/DWIN32", "/D_CRT_SECURE_NO_WARNINGS",
           f"/Fo{obj}\\", f"/Fe{os.path.join(BIN, name + '.exe')}"]
    cmd += [f"/I{i}" for i in incs] + [f"/D{d}" for d in defines] + sources
    run(cmd)


def build_tools():
    os.makedirs(BIN, exist_ok=True)
    src = os.path.join(LCC, "src")
    cl_exe("lburg", [os.path.join(LCC, "lburg", f) for f in ("lburg.c", "gram.c")], [os.path.join(LCC, "lburg")])
    gen = []
    for md in ("dagcheck", "alpha", "mips", "sparc", "x86", "x86linux"):
        c = os.path.join(BIN, md + ".c")
        run([os.path.join(BIN, "lburg.exe"), os.path.join(src, md + ".md"), c])
        gen.append(c)
    rcc = ["alloc", "bind", "dag", "decl", "enode", "error", "expr", "event", "init", "inits", "input",
           "lex", "list", "main", "output", "prof", "profio", "simp", "stmt", "string", "sym", "trace",
           "tree", "types", "null", "symbolic", "gen", "bytecode", "stab"]
    cl_exe("q3rcc", [os.path.join(src, f + ".c") for f in rcc] + gen, [src])
    cpp = os.path.join(LCC, "cpp")
    cl_exe("q3cpp", [os.path.join(cpp, f + ".c") for f in
                     ("cpp", "lex", "nlist", "tokens", "macro", "eval", "include", "hideset", "getopt", "unix")], [cpp],
           # names MSVC now has as intrinsics
           defines=("outp=cpp_outp", "memmove=cpp_memmove"))
    asm = os.path.join(ID, "q3asm")
    cl_exe("q3asm", [os.path.join(asm, "q3asm.c"), os.path.join(asm, "cmdlib.c"), "user32.lib"], [asm])  # cmdlib's _printf


def copy_sources():
    if os.path.isdir(SRC):
        shutil.rmtree(SRC)
    for part in ("cgame", "game", "ui"):
        shutil.copytree(os.path.join(ID, "code", part), os.path.join(SRC, part))
    # ui_shared.h includes "../../ui/menudef.h", id's top-level ui folder
    os.makedirs(os.path.join(OUT, "ui"), exist_ok=True)
    shutil.copy(os.path.join(ID, "ui", "menudef.h"), os.path.join(OUT, "ui", "menudef.h"))
    shutil.copy(os.path.join(HERE, "src", "cg_q3cam.c"), os.path.join(SRC, "cgame", "cg_q3cam.c"))
    for rel, anchor, text in HOOKS:
        p = os.path.join(SRC, rel)
        s = open(p, "rb").read().decode("latin-1")
        crlf = "\r\n" in s
        s = s.replace("\r\n", "\n")
        if s.count(anchor) != 1:
            sys.exit(f"q3cam: hook anchor not found once in {rel}: {anchor.strip()}")
        s = s.replace(anchor, anchor + text)
        if crlf:
            s = s.replace("\n", "\r\n")
        open(p, "wb").write(s.encode("latin-1"))


def qvm(name, part, defines, files):
    """Compiles <part>'s files to bytecode and assembles <name>.qvm, as cgame.bat and game.bat do."""
    vm = os.path.join(OUT, "vm_" + part)
    os.makedirs(vm, exist_ok=True)
    asms = []
    for f in files:
        c = f if os.path.isabs(f) else os.path.join(SRC, part, f + ".c")
        base = os.path.splitext(os.path.basename(c))[0]
        i_file = os.path.join(vm, base + ".i")
        a_file = os.path.join(vm, base + ".asm")
        # lcc's driver (etc/win32.c) passes q3cpp these defines, then the .bat file's own
        defs = ["-D__LCC__", "-D__STDC__=1", "-Dwin32", "-D_WIN32", "-D_M_IX86", "-DQ3_VM"] + [f"-D{d}" for d in defines]
        # output from stdout: q3cpp's creat(name, 0666) trips the modern CRT's mode check
        r = run([os.path.join(BIN, "q3cpp.exe")] + defs +
                [f"-I{os.path.join(SRC, 'cgame')}", f"-I{os.path.join(SRC, 'game')}", f"-I{os.path.join(SRC, 'ui')}", c])
        with open(i_file, "w", newline="\n") as fh:
            fh.write(r.stdout)
        run([os.path.join(BIN, "q3rcc.exe"), "-target=bytecode", "-g", i_file, a_file])
        asms.append(a_file)
    # the list names files relative to vm/: q3asm's parser splits "C:\..." at the colon
    syscalls = ("cg" if part == "cgame" else "g") + "_syscalls"
    shutil.copy(os.path.join(SRC, part, syscalls + ".asm"), os.path.join(vm, syscalls + ".asm"))
    names = [os.path.splitext(os.path.basename(x))[0] for x in asms]
    lst = os.path.join(vm, name + ".q3asm")
    with open(lst, "w") as fh:
        fh.write(f"-o ../{name}\n")
        for n in [names[0], syscalls] + names[1:]:
            fh.write(n + "\n")
    run([os.path.join(BIN, "q3asm.exe"), "-f", name], cwd=vm)
    path = os.path.join(OUT, name + ".qvm")
    if not os.path.isfile(path):
        sys.exit(f"q3cam: q3asm wrote no {name}.qvm")
    return path


def main():
    global ENV
    ENV = msvc_env()
    build_tools()
    copy_sources()
    bg = ["../game/bg_slidemove", "../game/bg_pmove", "../game/bg_lib", "../game/bg_misc",
          "../game/q_math", "../game/q_shared"]
    cg = ["cg_main", "cg_consolecmds", "cg_draw", "cg_drawtools", "cg_effects", "cg_ents", "cg_event",
          "cg_info", "cg_localents", "cg_marks", "cg_players", "cg_playerstate", "cg_predict",
          "cg_scoreboard", "cg_servercmds", "cg_snapshot", "cg_view", "cg_weapons", "cg_q3cam"]
    cg_files = cg + [os.path.join(SRC, "cgame", b + ".c") for b in bg]
    g = ["g_main", "bg_misc", "bg_lib", "bg_pmove", "bg_slidemove", "q_math", "q_shared",
         "ai_dmnet", "ai_dmq3", "ai_team", "ai_main", "ai_chat", "ai_cmd", "ai_vcmd",
         "g_active", "g_arenas", "g_bot", "g_client", "g_cmds", "g_combat", "g_items", "g_mem",
         "g_misc", "g_missile", "g_mover", "g_session", "g_spawn", "g_svcmds", "g_target", "g_team",
         "g_trigger", "g_utils", "g_weapon"]
    cg_qvm = qvm("cgame", "cgame", ["CGAME"], [os.path.normpath(f) for f in cg_files])
    g_qvm = qvm("qagame", "game", [], g)   # game.bat defines nothing more
    pk3 = os.path.join(OUT, "pak0.pk3")
    with zipfile.ZipFile(pk3, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(cg_qvm, "vm/cgame.qvm")
        z.write(g_qvm, "vm/qagame.qvm")
    print(f"q3cam: {pk3} (cgame {os.path.getsize(cg_qvm)} bytes, qagame {os.path.getsize(g_qvm)} bytes)")


main()
