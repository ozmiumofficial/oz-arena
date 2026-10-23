extends Node
## Debug: lists the material of every surface in a map GLB whose name matches a filter.
##   godot --headless --path . res://tests/probe_materials.tscn -- map=q3dm17 filter=teleporter
func _ready() -> void:
	var map := "q3dm17"
	var filt := "teleporter"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("map="): map = a.substr(4)
		if a.begins_with("filter="): filt = a.substr(7)
	var n := (load("res://library/maps/%s/%s.glb" % [map, map]) as PackedScene).instantiate()
	for mi in n.find_children("*", "MeshInstance3D", true, false):
		for i in mi.mesh.get_surface_count():
			var m: Material = mi.mesh.surface_get_material(i)
			var nm := m.resource_name if m else "null"
			var path := m.resource_path if m else ""
			if filt in nm or filt in path:
				var sh := ""
				if m is ShaderMaterial:
					sh = (m as ShaderMaterial).shader.resource_path
				print("%s surface %d: name=%s path=%s shader=%s class=%s" % [mi.name, i, nm, path, sh, m.get_class() if m else ""])
	n.free()
	get_tree().quit()
