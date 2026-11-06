"""blender -b rocket_launcher_rebuild.blend --python save_clean.py -- <out.blend>

The work file without the Tripo reference and the high-detail copy (both rebuilt by script): the Rebuild parts and the
Final game mesh with its UVs and materials, compressed, for git. Images stay external (the bakes live in the build
folder)."""
import bpy, sys

out = sys.argv[sys.argv.index("--") + 1]
for name in ("Reference", "High", "_tmp_bool"):
    col = bpy.data.collections.get(name)
    if col:
        for o in list(col.objects):
            bpy.data.objects.remove(o)
        bpy.data.collections.remove(col)
for im in list(bpy.data.images):
    if im.packed_file:
        im.unpack(method="REMOVE")
bpy.data.orphans_purge(do_local_ids=True, do_linked_ids=True, do_recursive=True)
bpy.ops.wm.save_as_mainfile(filepath=out, compress=True, copy=True)
print("saved", out)
