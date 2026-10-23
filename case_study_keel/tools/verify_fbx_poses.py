import bpy, sys, os, json, math
from mathutils import Vector
argv = sys.argv[sys.argv.index("--") + 1:]
FBX, OUTDIR, CLIPS = argv
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
bpy.ops.import_scene.fbx(filepath=FBX)
sc = bpy.context.scene
print("fps", sc.render.fps, "range", sc.frame_start, sc.frame_end)
for o in bpy.data.objects:
    sk = o.data.shape_keys if o.type == "MESH" else None
    ad = (sk.animation_data if sk else o.animation_data)
    print(o.name, o.type, "parent=", o.parent.name if o.parent else None,
          "shapekeys=", len(sk.key_blocks) if sk else 0,
          "action=", ad.action.name if ad and ad.action else None,
          "frame_range=", tuple(ad.action.frame_range) if ad and ad.action else None)

clips = json.load(open(CLIPS))["clips"]
meshes = [o for o in bpy.data.objects if o.type == "MESH"]

def bbox():
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in meshes:
        e = o.evaluated_get(dg)
        m = e.to_mesh()
        pts += [e.matrix_world @ v.co for v in m.vertices]
        e.to_mesh_clear()
    mn = Vector(map(min, zip(*pts))); mx = Vector(map(max, zip(*pts)))
    return mn, mx

# camera + light, render a pose per selected clip
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
sc.collection.objects.link(cam); sc.camera = cam
sc.render.engine = "BLENDER_WORKBENCH"
sc.display.shading.color_type = "TEXTURE"
sc.display.shading.light = "STUDIO"
sc.render.resolution_x, sc.render.resolution_y = 360, 480
sc.render.film_transparent = False
pick = ["LEGS_IDLE", "LEGS_RUN", "TORSO_ATTACK", "TORSO_GESTURE", "LEGS_JUMP", "BOTH_DEATH1"]
for c in clips:
    if c["name"] not in pick:
        continue
    f = c["start_frame"] + (c["end_frame"] - c["start_frame"]) * (0.95 if c["name"].startswith("BOTH") else 0.4)
    sc.frame_set(int(f))
    mn, mx = bbox()
    print("%-14s frame %4d  bbox min %s max %s  height %.3f m" % (c["name"], int(f), tuple(round(x, 3) for x in mn), tuple(round(x, 3) for x in mx), mx.z - mn.z))
    # view from the model's front (Blender -Y forward), a bit to the side
    cam.location = Vector((1.6, -3.2, 1.2))
    cam.rotation_euler = (math.radians(82), 0, math.radians(27))
    sc.render.filepath = os.path.join(OUTDIR, "%s.png" % c["name"])
    bpy.ops.render.render(write_still=True)
