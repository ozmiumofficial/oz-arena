"""Full id Tech 3 shader-script parser for world materials.

parse_all(root) -> {shader name: Shader}. A Shader keeps every top-level directive and every
stage (map / clampMap / animMap, blendFunc, alphaFunc, rgbGen, tcGen, tcMod list), plus
describe() reduces it to what a PBR material needs: which image is the painted base, which
layers glow, how they move, and what kind of surface it is (surface, liquid, sky, fog, hidden).
"""
import os, re, glob

BLEND_ALIASES = {"add": ("gl_one", "gl_one"), "filter": ("gl_dst_color", "gl_zero"),
                 "blend": ("gl_src_alpha", "gl_one_minus_src_alpha")}
HIDDEN_PARMS = {"nodraw", "areaportal", "clusterportal", "donotenter", "hint", "skip", "origin"}
HIDDEN_PREFIX = ("textures/common/",)


class Stage:
    def __init__(self):
        self.map = None          # image path, "$lightmap", "$whiteimage"
        self.clamp = False
        self.anim = None         # (fps, [frames])
        self.blend = "opaque"    # opaque | add | filter | blend | mask | other
        self.alphafunc = None
        self.rgbgen = None       # ("wave", func, base, amp, phase, freq) | ("identity",) | ...
        self.tcgen = None        # "environment" | "lightmap" | None
        self.tcmods = []         # [("scroll", s, t) | ("rotate", deg) | ("scale", s, t) | ("turb", ...) | ("stretch", ...)]


class Shader:
    def __init__(self, name):
        self.name = name
        self.parms = set()       # surfaceparm values
        self.cull = "back"
        self.surfacelight = 0.0
        self.lightimage = None
        self.backsplash = (0.05, 24.0)   # q3map_backsplash <percent> <distance>, as a fraction (shaders.c defaults)
        self.lightsubdivide = 0          # q3map_lightsubdivide; 0 = q3map's default
        self.editorimage = None
        self.skyparms = None     # (farbox, cloudheight, nearbox)
        self.fogparms = None     # ((r, g, b), distance)
        self.deforms = []        # raw deformVertexes argument lists
        self.sort = None
        self.stages = []

    def describe(self, root):
        """-> dict for the material builder."""
        kind = "surface"
        if self.parms & HIDDEN_PARMS or self.name.startswith(HIDDEN_PREFIX):
            kind = "hidden"
        if "sky" in self.parms or self.skyparms:
            kind = "sky"
        elif self.parms & {"water", "slime", "lava"}:
            kind = "liquid"
        elif "fog" in self.parms or self.fogparms:
            kind = "fog"
        layers = [s for s in self.stages if s.map and not s.map.startswith("$")]
        # the painted texture almost always shares the shader's name; prefer it over effect
        # layers drawn beneath it (scrolling holograms, reflections)
        base = next((s for s in layers if _norm(s.map) == self.name and s.tcgen != "environment"), None)
        if base is None:
            base = next((s for s in layers if s.tcgen != "environment" and s.blend != "add"), None)
        if base is None:
            base = next((s for s in layers if s.tcgen != "environment"), None)
        if base is None and layers:
            base = layers[0]
        # an opaque first stage makes the surface opaque, whatever is added over it: chrome_metal is
        # an environment-mapped chrome with an additive detail layer named like the shader, which
        # made the wall additive and see-through (q3tourney4's console recess, 2026-10-23)
        if (base is not None and base.blend == "add" and layers and layers[0] is not base
                and layers[0].blend == "opaque" and not layers[0].alphafunc):
            base = layers[0]
        glows =[s for s in layers if s is not base and s.blend == "add" and s.tcgen != "environment"]
        env = any(s.tcgen == "environment" for s in self.stages)
        if base is None and kind == "surface" and self.editorimage:
            base_img, base_stage = self.editorimage, None
        else:
            base_img, base_stage = (base.map if base else None), base
        blend = "opaque"
        if base_stage is not None:
            if base_stage.alphafunc:
                blend = "mask"
            elif base_stage.blend == "blend" and base_stage is layers[0]:
                blend = "blend"
            elif base_stage.blend == "add":
                blend = "add"
        anim = base_stage.anim if base_stage is not None else None
        # an opaque layer drawn first and a paint layer blended over it: the first layer shows
        # where the paint is clear (console: a reflection; diamond2c_ow: energy under a grate)
        under = (layers[0] if base_stage is not None and base_stage is not layers[0] and base_stage.blend == "blend"
                 and (layers[0].tcgen == "environment" or layers[0].blend == "opaque") else None)
        # a reflection added first and a paint layer cut by alphaFunc over it (glass_frame): glass
        # in the holes
        glass = (layers[0] if base_stage is not None and base_stage is not layers[0] and base_stage.alphafunc
                 and layers[0].tcgen == "environment" and layers[0].blend == "add" else None)
        # glow layers drawn before a blended paint layer (teslacoil: arcs inside a cable frame)
        glow_under = (base_stage is not None and base_stage.blend == "blend"
                      and any(layers.index(g) < layers.index(base_stage) for g in glows))
        # added stages the $lightmap filter multiplies afterwards are lit paint, not light
        # (chrome_metal's detail layer: red under console03's light, not a white glow)
        lm = next((i for i, s in enumerate(self.stages) if s.map == "$lightmap" and s.blend == "filter"), None)
        glow_lit = (bool(glows) and base_stage is not None and base_stage.blend == "opaque" and lm is not None
                    and all(self.stages.index(g) < lm for g in glows))
        return dict(
            name=self.name, kind=kind, blend=blend, two_sided=self.cull in ("none", "disable", "twosided"),
            base=_norm(base_img), env=env, anim=anim,
            base_env=base_stage is not None and base_stage.tcgen == "environment",
            under=dict(map=_norm(under.map), tcmods=under.tcmods, env=under.tcgen == "environment") if under else None,
            glow_under=glow_under,
            glow_lit=glow_lit,
            glass=dict(map=_norm(glass.map), tcmods=glass.tcmods,
                       lit=any(s.map == "$lightmap" and s.blend == "filter" for s in self.stages)) if glass else None,
            # a $lightmap stage multiplying what's drawn (glass01: a reflection darkened by the light)
            lit_add=blend == "add" and any(s.map == "$lightmap" and s.blend == "filter" for s in self.stages),
            tcmods=base_stage.tcmods if base_stage is not None else [],
            glows=[dict(map=_norm(g.map), anim=g.anim, tcmods=g.tcmods, rgbgen=g.rgbgen) for g in glows],
            surfacelight=self.surfacelight, lightimage=_norm(self.lightimage),
            parms=sorted(self.parms), deforms=self.deforms, skyparms=self.skyparms, fogparms=self.fogparms,
            editorimage=_norm(self.editorimage),
            sky_layers=[dict(map=_norm(s.map), tcmods=s.tcmods, blend=s.blend) for s in layers] if kind == "sky" else [],
        )


WAVEFORMS = {"sin": 1, "triangle": 2, "square": 3, "sawtooth": 4, "inversesawtooth": 5}


def deform_uniforms(deforms):
    """deformVertexes wave / move / bulge as godot/shaders/q3_deform.gdshaderinc's uniforms:
    [(a, b, move), ...], two at most, in shader order. deformVertexes normal only shimmers the
    lighting and is left out."""
    out = []
    for d in deforms or []:
        if not d or len(out) == 2:
            continue
        try:
            if d[0] == "wave" and len(d) >= 7:
                div = float(d[1])
                out.append(([1, 1.0 / div if div else 100.0, float(d[3]), float(d[4])],
                            [float(d[5]), float(d[6]), WAVEFORMS.get(d[2].lower(), 1), 0], [0, 0, 0]))
            elif d[0] == "move" and len(d) >= 9:
                out.append(([2, 0, float(d[5]), float(d[6])],
                            [float(d[7]), float(d[8]), WAVEFORMS.get(d[4].lower(), 1), 0],
                            [float(d[1]), float(d[2]), float(d[3])]))
            elif d[0] == "bulge" and len(d) >= 4:
                out.append(([3, float(d[1]), float(d[2]), float(d[3])], [0, 0, 0, 0], [0, 0, 0]))
        except ValueError:
            continue
    return out


def _norm(p):
    return os.path.splitext(p.replace("\\", "/").lower())[0] if p else None


def _num(x, d=0.0):
    try:
        return float(x)
    except (TypeError, ValueError):
        return d


def _parse_stage(text):
    st = Stage()
    for line in text.split("\n"):
        w = line.split()
        if not w:
            continue
        k = w[0]
        if k in ("map", "clampmap") and len(w) > 1:
            st.map, st.clamp = w[1], k == "clampmap"
        elif k == "animmap" and len(w) > 2:
            st.anim = (_num(w[1], 1.0), [_norm(f) for f in w[2:]])
            st.map = w[2]
        elif k == "blendfunc" and len(w) > 1:
            pair = tuple(w[1:3]) if len(w) > 2 else BLEND_ALIASES.get(w[1], (w[1], ""))
            if pair == ("gl_one", "gl_one"):
                st.blend = "add"
            elif pair in (("gl_dst_color", "gl_zero"), ("gl_zero", "gl_src_color")):
                st.blend = "filter"
            elif pair == ("gl_src_alpha", "gl_one_minus_src_alpha"):
                st.blend = "blend"
            elif pair == ("gl_one", "gl_zero"):
                st.blend = "opaque"
            else:
                st.blend = "other"
        elif k == "alphafunc" and len(w) > 1:
            st.alphafunc = w[1]
        elif k == "rgbgen" and len(w) > 1:
            st.rgbgen = (w[1],) + tuple(_num(x) if i else x for i, x in enumerate(w[2:]))
        elif k == "tcgen" and len(w) > 1:
            st.tcgen = w[1]
        elif k == "tcmod" and len(w) > 1:
            st.tcmods.append((w[1],) + tuple(_num(x) if re.fullmatch(r"-?[\d.]+", x) else x for x in w[2:]))
    return st


def parse_all(root):
    shaders = {}
    for f in sorted(glob.glob(os.path.join(root, "scripts", "*.shader"))):
        text = re.sub(r"//[^\n]*", "", open(f, encoding="latin-1").read().lower())
        i, n = 0, len(text)
        while i < n:
            m = re.compile(r"([^\s{}]+)\s*\{").search(text, i)
            if not m:
                break
            depth, j = 0, m.end() - 1
            while j < n:
                if text[j] == "{":
                    depth += 1
                elif text[j] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            body = text[m.end():j]
            sh = Shader(_norm(m.group(1)))
            sh.source = os.path.basename(f)
            for st_text in re.findall(r"\{([^{}]*)\}", body):
                sh.stages.append(_parse_stage(st_text))
            for line in re.sub(r"\{[^{}]*\}", "\n", body).split("\n"):
                w = line.replace("(", " ").replace(")", " ").split()
                if not w:
                    continue
                k = w[0]
                if k == "surfaceparm" and len(w) > 1:
                    sh.parms.add(w[1])
                elif k == "cull" and len(w) > 1:
                    sh.cull = w[1]
                elif k in ("q3map_surfacelight", "q3map_surfaceLight".lower()) and len(w) > 1:
                    sh.surfacelight = _num(w[1])
                elif k == "q3map_lightimage" and len(w) > 1:
                    sh.lightimage = w[1]
                elif k == "q3map_backsplash" and len(w) > 2:
                    sh.backsplash = (_num(w[1]) * 0.01, _num(w[2]))
                elif k == "q3map_lightsubdivide" and len(w) > 1:
                    sh.lightsubdivide = int(_num(w[1]))
                elif k == "qer_editorimage" and len(w) > 1:
                    sh.editorimage = w[1]
                elif k == "skyparms" and len(w) > 3:
                    sh.skyparms = (None if w[1] == "-" else _norm(w[1]), w[2], None if w[3] == "-" else _norm(w[3]))
                elif k == "fogparms" and len(w) > 4:
                    sh.fogparms = ((_num(w[1]), _num(w[2]), _num(w[3])), _num(w[4]))
                elif k == "deformvertexes" and len(w) > 1:
                    sh.deforms.append(w[1:])
                elif k == "sort" and len(w) > 1:
                    sh.sort = w[1]
            shaders.setdefault(sh.name, sh)  # first definition wins, like the engine
            i = j + 1
    return shaders
