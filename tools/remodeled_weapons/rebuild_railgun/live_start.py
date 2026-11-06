"""Start the live railgun session: Blender's window with the BlenderMCP add-on's server on port 9877 (9876 is the
rocket launcher session's), the Tripo reference as a ghost, the viewport ready.

    blender <work>/railgun_rebuild.blend --python live_start.py      (or study/ref.blend the first time)

bl.py sends scripts to the server; each runs on Blender's main thread with this folder importable.
"""
import bpy, os, sys

PORT = 9877
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)


def _port_everywhere():
    for sc in bpy.data.scenes:
        if hasattr(sc, "blendermcp_port"):
            sc.blendermcp_port = PORT


_port_everywhere()
_tries = [0]


def _start():
    _tries[0] += 1
    try:
        _port_everywhere()
        server = getattr(bpy.types, "blendermcp_server", None)
        if server is not None and server.running:
            if server.port != PORT:
                print("[live_start] a server runs on", server.port, "- leaving it")
            else:
                print("[live_start] server on", PORT)
            return None
        if server is not None:
            server.port = PORT
        bpy.ops.blendermcp.start_server()
        server = getattr(bpy.types, "blendermcp_server", None)
        print("[live_start] started:", server and server.running, server and server.port)
        return None if (server and server.running) else 2.0
    except Exception as e:
        print("[live_start] retry:", e)
        return 2.0 if _tries[0] < 15 else None


def _ghost():
    ref = bpy.data.objects.get("REF_tripo")
    if ref is None:
        return
    # the viewport color only (Solid shading, Material colors): its textured node tree stays for comparisons
    if ref.active_material:
        ref.active_material.diffuse_color = (0.55, 0.60, 0.70, 0.22)
    ref.hide_select = True
    # drawn 2 mm inside its surface (viewport only), so it shows where the rebuild falls short instead of
    # fighting the rebuild's faces for the same depth
    m = ref.modifiers.get("ghost_inset") or ref.modifiers.new("ghost_inset", "DISPLACE")
    m.direction = "NORMAL"; m.mid_level = 0.0; m.strength = -0.0020
    m.show_render = False; m.show_viewport = True


def _viewport():
    for win in bpy.context.window_manager.windows:
        for area in win.screen.areas:
            if area.type == "VIEW_3D":
                sp = area.spaces.active
                sh = sp.shading
                sh.type = "SOLID"
                sh.color_type = "MATERIAL"
                sh.light = "STUDIO"
                sh.show_cavity = True
                sh.cavity_type = "BOTH"
                sh.show_backface_culling = False
                sp.overlay.show_floor = False
                sp.overlay.show_axis_x = sp.overlay.show_axis_y = False
                sp.clip_start = 0.001
                sp.clip_end = 50
    return None


_ghost()
bpy.app.timers.register(_start, first_interval=0.3, persistent=True)
bpy.app.timers.register(_viewport, first_interval=1.0)
