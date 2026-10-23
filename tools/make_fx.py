"""Visual effects step: remastered effect textures + effect definitions for the engine.

python make_fx.py <extracted baseq3> <fx work dir>

Textures come from the shaders cgame uses (scripts/*.shader: rocketExplosion, railCore,
smokePuff, bloodExplosion ...). Each is upscaled with edge-preserving cleanup; animmap
sequences become one horizontal flipbook atlas. Marks drawn with GL_ZERO
GL_ONE_MINUS_SRC_COLOR (darkening) become RGBA decals.

fx.json holds every effect's parameters taken from id's GPL cgame code (cg_weapons.c,
cg_effects.c) in id units and milliseconds; the Godot builder converts (32 units = 1 m).
"""
import os, sys, json
import numpy as np
import cv2
from PIL import Image

ROOT, OUT = sys.argv[1], sys.argv[2]
TEX = os.path.join(OUT, "textures")
os.makedirs(TEX, exist_ok=True)

# name: (frames, mode, source shader in scripts/*.shader, frame rate of the animmap)
# mode: add (additive, unshaded), blend (alpha), darken (multiply mark -> decal), env (shell map)
SOURCES = {
    "rocket_explosion": (["models/weaphits/rlboom/rlboom_%d" % i for i in range(1, 9)], "add", "rocketExplosion", 8),
    "grenade_explosion": (["models/weaphits/glboom/glboom_%d" % i for i in range(1, 4)], "add", "grenadeExplosion", 5),
    "bfg_explosion": (["models/weaphits/bfgboom/bfgboom_%d" % i for i in range(1, 4)], "add", "bfgExplosion", 5),
    "bullet_explosion": (["models/weaphits/bullet%d" % i for i in range(1, 4)], "add", "bulletExplosion", 5),
    "rail_explosion": (["models/weaphits/ring02_%d" % i for i in range(1, 5)], "blend", "railExplosion", 5),
    "blood_explosion": (["models/weaphits/blood20%d" % i for i in range(1, 6)], "blend", "bloodExplosion", 5),
    "plasma_explosion": (["models/weaphits/plasmaboom"], "add", "plasmaExplosion", 0),
    "smoke_puff": (["gfx/misc/smokepuff3"], "blend", "smokePuff", 0),
    "shotgun_smoke": (["gfx/misc/smokepuff2b"], "blend", "shotgunSmokePuff", 0),
    "rail_core": (["gfx/misc/railcorethin_mono"], "add", "railCore", 0),
    "rail_disc": (["gfx/misc/raildisc_mono2"], "add", "railDisc", 0),
    "lightning_bolt": (["gfx/misc/lightning3new"], "add", "lightningBoltNew", 0),
    "teleport": (["gfx/misc/teleportEffect2"], "add", "teleportEffect", 0),
    "bubble": (["sprites/bubble"], "blend", "waterBubble", 0),
    "tracer": (["gfx/misc/tracer2"], "add", "gfx/misc/tracer", 0),
    "plasma_ball": (["sprites/plasmaa"], "add", "sprites/plasma1", 0),
    "bfg_ball": (["models/weaphits/bfg01"], "add", "models/weaphits/bfg01", 0),
    "electric": (["models/weaphits/electric"], "add", "models/weaphits/electric", 0),
    "flare": (["gfx/misc/flare"], "add", "flareShader", 0),
    "splash": (["sprites/splash"], "add", "wake", 0),
    "mark_bullet": (["gfx/damage/bullet_mrk"], "darken", "gfx/damage/bullet_mrk", 0),
    "mark_burn": (["gfx/damage/burn_med_mrk"], "darken", "gfx/damage/burn_med_mrk", 0),
    "mark_hole": (["gfx/damage/hole_lg_mrk"], "darken", "gfx/damage/hole_lg_mrk", 0),
    "mark_energy": (["gfx/damage/plasma_mrk"], "blend", "gfx/damage/plasma_mrk", 0),
    "mark_blood": (["gfx/damage/blood_stain"], "blend", "bloodMark", 0),
    "shell_quad": (["textures/effects/quadmap2"], "env", "powerups/quad", 0),
    "shell_battlesuit": (["textures/effects/envmapgold2"], "env", "powerups/battleSuit", 0),
    "shell_regen": (["textures/effects/regenmap2"], "env", "powerups/regen", 0),
    "shell_invis": (["textures/effects/invismap"], "env", "powerups/invisibility", 0),
    "blood_screen": (["gfx/damage/blood_screen"], "blend", "viewBloodBlend", 0),
}


def load(path):
    for ext in (".tga", ".jpg", ".png"):
        f = os.path.join(ROOT, path + ext)
        if os.path.isfile(f):
            return np.array(Image.open(f).convert("RGBA")).astype(np.float32) / 255.0
    return None


def remaster(img, scale):
    """Upscale RGBA with Lanczos, then smooth the interpolation steps while keeping edges."""
    h, w = img.shape[:2]
    W, H = w * scale, h * scale
    up = np.clip(cv2.resize(img, (W, H), interpolation=cv2.INTER_LANCZOS4), 0, 1)
    rgb8 = (up[..., :3] * 255).astype(np.uint8)
    for _ in range(2):
        rgb8 = cv2.bilateralFilter(rgb8, 7, 22, 5)
    rgb = rgb8.astype(np.float32) / 255.0
    rgb = np.clip(rgb + 0.4 * (rgb - cv2.GaussianBlur(rgb, (0, 0), 2.0)), 0, 1)
    a = cv2.GaussianBlur(up[..., 3], (0, 0), 0.6)
    return np.dstack([rgb, a])


report = {}
for name, (frames, mode, shader, fps) in SOURCES.items():
    imgs = [load(p) for p in frames]
    imgs = [i for i in imgs if i is not None]
    if not imgs:
        print("missing", name)
        continue
    long_edge = max(imgs[0].shape[:2])
    scale = max(1, min(8, 512 // long_edge))
    outs = []
    for im in imgs:
        r = remaster(im, scale)
        if mode == "darken":
            # dst *= (1 - src): darkening amount = source brightness -> soot decal
            amt = r[..., :3].mean(-1)
            r = np.dstack([np.full(amt.shape + (3,), 0.04, np.float32) * np.array([1.0, 0.85, 0.75], np.float32), amt])
        elif mode in ("add", "env"):
            r[..., 3] = 1.0  # additive: color is the contribution, black adds nothing
        outs.append(r)
    atlas = np.concatenate(outs, axis=1)
    Image.fromarray((np.clip(atlas, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(os.path.join(TEX, name + ".png"))
    fh, fw = outs[0].shape[:2]
    report[name] = dict(file="textures/%s.png" % name, frames=len(outs), frame_size=[fw, fh], mode=mode,
                        source_shader=shader, animmap_fps=fps)
    print("%-18s %-6s %d frame(s) %dx%d" % (name, mode, len(outs), fw, fh))

# Effect parameters from id's cgame code (id units, ms). Colors are 0..1 RGB.
FX = {
    "_note": "Values from Quake III Arena GPL source: cg_weapons.c (CG_RegisterWeapon, CG_MissileHitWall, "
             "CG_RocketTrail, CG_PlasmaTrail, CG_RailTrail, CG_*EjectBrass, CG_LightningBolt) and cg_effects.c "
             "(CG_MakeExplosion, CG_Bleed, CG_GibPlayer, CG_SpawnEffect, CG_SmokePuff, CG_BubbleTrail). "
             "cvar defaults used: cg_railTrailTime 400, cg_brassTime 2500, cg_tracerLength 100. "
             "MUZZLE_FLASH_TIME 20 ms in the original; the remaster stretches the flash light fade to 60 ms.",
    "explosions": {
        "rocket": dict(sprite="rocket_explosion", model="weaphits/boom01", duration=1000, radius=64,
                       light=300, light_color=[1, 0.75, 0], mark="mark_burn", mark_radius=64, smoke=True, sparks=True),
        "grenade": dict(sprite="grenade_explosion", model="weaphits/boom01", duration=600, radius=64,
                        light=300, light_color=[1, 1, 0], mark="mark_burn", mark_radius=64, smoke=True, sparks=True),
        "bfg": dict(sprite="bfg_explosion", model="weaphits/boom01", duration=600, radius=32,
                    light=300, light_color=[0.2, 1, 0.2], mark="mark_burn", mark_radius=32, smoke=False, sparks=True),
        "plasma": dict(sprite="plasma_explosion", model="weaphits/ring02", duration=600, radius=16,
                       light=0, light_color=[0.6, 0.6, 1], mark="mark_energy", mark_radius=16, mark_fade=True),
        "rail": dict(sprite="rail_explosion", model="weaphits/ring02", duration=600, radius=24, light=0,
                     light_color=[1, 0.5, 0], mark="mark_energy", mark_radius=24, mark_fade=True, tint_player=True),
        "bullet": dict(sprite="bullet_explosion", model="weaphits/bullet", duration=600, radius=8,
                       light=0, light_color=[1, 1, 0], mark="mark_bullet", mark_radius=8, sparks=True),
        "shotgun_pellet": dict(sprite="bullet_explosion", model="weaphits/bullet", duration=600, radius=4,
                               light=0, light_color=[1, 1, 0], mark="mark_bullet", mark_radius=4, sparks=True),
        "lightning": dict(sprite=None, model=None, duration=0, radius=12, light=0, light_color=[0.6, 0.6, 1],
                          mark="mark_hole", mark_radius=12),
    },
    "muzzle_flash": {  # flashDlightColor; light radius 300 + rand(0..31)
        "gauntlet": [0.6, 0.6, 1.0], "machinegun": [1, 1, 0], "shotgun": [1, 1, 0], "grenadel": [1, 0.7, 0],
        "rocketl": [1, 0.75, 0], "lightning": [0.6, 0.6, 1.0], "railgun": [1, 0.5, 0], "plasma": [0.6, 0.6, 1.0],
        "bfg": [1, 0.7, 1], "grapple": [0.6, 0.6, 1.0], "_light_radius": 300, "_flash_ms": 20,
    },
    "projectiles": {
        "rocket": dict(model="ammo/rocket/rocket", light=200, light_color=[1, 0.75, 0], trail="smoke",
                       trail_step=50, trail_radius=64, trail_life=2000, trail_alpha=0.33),
        "grenade": dict(model="ammo/grenade1", light=0, trail="smoke", trail_step=50, trail_radius=32,
                        trail_life=700, trail_alpha=0.33),
        "plasma": dict(sprite="plasma_ball", radius=16, light=0, trail="plasma_sparks", trail_step=50, trail_life=600),
        "bfg": dict(model="weaphits/bfg", sprite="bfg_ball", light=0),
    },
    "rail_trail": dict(core="rail_core", disc="rail_disc", core_life=400, radius=4, spacing=5, rotation_step_deg=10,
                       disc_radius=1.1, disc_life=600, disc_speed=6, core_start=20),
    "lightning_beam": dict(sprite="lightning_bolt", impact_model="weaphits/crackle", impact_back=16,
                           scroll=[-5, -7.2], wave_hz=[7.1, 8.1]),
    "smoke": dict(rocket_trail=dict(sprite="smoke_puff", alpha=0.33),
                  shotgun=dict(sprite="shotgun_smoke", radius=32, alpha=0.33, life=900, rise=8)),
    "brass": dict(machinegun=dict(model="weapons2/shells/m_shell", vel=[0, [-50, -10], [100, 150]], life=[2500, 3125],
                                  bounce=0.4, offset=[8, -4, 24]),
                  shotgun=dict(model="weapons2/shells/s_shell", count=2, vel=[[60, 120], 40, [100, 150]],
                               life=[7500, 10000], bounce=0.3, offset=[8, 0, 24])),
    "blood": dict(sprite="blood_explosion", duration=500, radius=24, mark="mark_blood"),
    "gibs": dict(models=["gibs/skull", "gibs/abdomen", "gibs/arm", "gibs/chest", "gibs/fist", "gibs/foot",
                         "gibs/forearm", "gibs/intestine", "gibs/leg", "gibs/leg"],
                 velocity_xy=250, velocity_z=[0, 500], life=[5000, 8000], bounce=0.6),
    "teleport": dict(model="misc/telep", sprite="teleport", duration=500, z_offset=-24, scroll=[0, 2], scale=[1, 4]),
    "bubbles": dict(sprite="bubble", radius=3, life=[1000, 1250], speed_z=6, jitter=5),
    "tracer": dict(sprite="tracer", length=100, width=1, chance=0.4),
    "powerup_shells": {
        "quad": dict(map="shell_quad", grow=3, grow_weapon=0.5, rotate=30, scroll=[1, 0.1]),
        "battlesuit": dict(map="shell_battlesuit", grow=1, grow_weapon=0.5, rotate=333, scroll=[0.3, 0.3]),
        "regen": dict(map="shell_regen", grow=3, grow_weapon=0.5, rotate=30, scroll=[1, 0.1]),
        "invis": dict(map="shell_invis", grow=0, grow_weapon=0, rotate=0, scroll=[0, 0]),
    },
}
json.dump(dict(textures=report, effects=FX), open(os.path.join(OUT, "fx.json"), "w"), indent=1)
print("%d effect textures, fx.json written" % len(report))
