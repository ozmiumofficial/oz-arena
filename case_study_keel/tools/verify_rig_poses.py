import bpy, sys, os, math
from mathutils import Vector
OUTDIR = sys.argv[sys.argv.index("--") + 1]
os.makedirs(OUTDIR, exist_ok=True)
sc = bpy.context.scene
arm = bpy.data.objects["Keel"]
mesh = bpy.data.objects["Keel_mesh"]
ad = arm.animation_data
ad.action = None
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
sc.collection.objects.link(cam); sc.camera = cam
cam.data.lens = 50
sc.render.engine = "BLENDER_WORKBENCH"
sc.display.shading.color_type = "TEXTURE"
sc.render.resolution_x, sc.render.resolution_y = 400, 520


def pose(legs, torso, t_legs, t_torso):
    for tr in list(ad.nla_tracks):
        ad.nla_tracks.remove(tr)
    for name, tt in ((legs, t_legs), (torso, t_torso)):
        if not name:
            continue
        act = bpy.data.actions[name]
        tr = ad.nla_tracks.new()
        st = tr.strips.new(name, 1, act)
        st.blend_type = "REPLACE"
        st.influence = 1.0
        st.use_animated_influence = False
        # show the action frame tt at scene frame 1000 by offsetting the strip
        st.frame_start_ui = 1000 - (tt - 1)
    sc.frame_set(1000)


shots = [("rest", "LEGS_IDLE", "TORSO_STAND", 1, 1),
         ("run_attack", "LEGS_RUN", "TORSO_ATTACK", 14, 8),
         ("walk_gesture", "LEGS_WALK", "TORSO_GESTURE", 10, 60),
         ("jump_raise", "LEGS_JUMP", "TORSO_RAISE", 15, 6),
         ("crouch_attack2", "LEGS_IDLECR", "TORSO_ATTACK2", 1, 6),
         ("death2", "BOTH_DEATH2", None, 110, 0)]
for name, legs, torso, tl, tt in shots:
    pose(legs, torso, tl, tt)
    cam.location = Vector((2.3, -4.2, 1.4))
    cam.rotation_euler = (math.radians(84), 0, math.radians(29))
    sc.render.filepath = os.path.join(OUTDIR, name + ".png")
    bpy.ops.render.render(write_still=True)
print("done")
