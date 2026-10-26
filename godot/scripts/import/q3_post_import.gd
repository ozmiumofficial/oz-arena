@tool
extends EditorScenePostImport
## Runs on every imported .glb. Material names from the tools carry id Tech 3's draw mode:
##   <stem>__mask (alpha cut-out), __blend (alpha blend), __add (additive, unshaded), __2s (two-sided)
## Characters get the same modes from their rig.json (below).
## Props whose Quake 3 shader has several stages (powerup chrome, energy layers, scrolling
## electricity) get a q3_stages ShaderMaterial from library/props/prop_shaders.json instead
## (tools/make_prop_shaders.py).
## Nodes named "flash" (muzzle flashes hung on tag_flash) start hidden.
## Maps (library/maps/<map>/<map>.glb, tools/build_maps.py) name their materials by Quake 3 shader;
## each gets its remastered world material from library/world/world_materials.json.


## Characters name their materials by texture stem; their draw modes come from the rig.json next
## to the .glb ("material_modes", tools/rig_extras.py), e.g. Bones' additive xray skin.
var _modes: Dictionary = {}
var _stages: Dictionary = {}

const PROP_SHADERS := "res://library/props/prop_shaders.json"
const STAGE_SHADERS := {"": "res://shaders/q3_stages.gdshader", "2s": "res://shaders/q3_stages_2s.gdshader",
	"add": "res://shaders/q3_stages_add.gdshader", "add2s": "res://shaders/q3_stages_add_2s.gdshader"}


const WORLD_MATERIALS := "res://library/world/world_materials.json"


func _post_import(scene: Node) -> Object:
	_modes = {}
	_stages = {}
	if get_source_file().begins_with("res://library/maps/"):
		_map_import(scene)
		return scene
	if get_source_file().begins_with("res://library/props/") and FileAccess.file_exists(PROP_SHADERS):
		var ps: Variant = JSON.parse_string(FileAccess.get_file_as_string(PROP_SHADERS))
		if ps is Dictionary:
			_stages = ps
	var rig_path := get_source_file().get_basename() + ".rig.json"
	if FileAccess.file_exists(rig_path):
		var rig: Variant = JSON.parse_string(FileAccess.get_file_as_string(rig_path))
		if rig is Dictionary:
			_modes = rig.get("material_modes", {})
	_walk(scene)
	return scene


func _walk(n: Node) -> void:
	var mi := n as MeshInstance3D
	if mi and mi.mesh:
		var additive_only := true
		for i in mi.mesh.get_surface_count():
			var m: Material = mi.mesh.surface_get_material(i)
			if m and _stages.has(m.resource_name):
				var sm := _stage_material(m.resource_name, _stages[m.resource_name])
				mi.mesh.surface_set_material(i, sm)
				if not _stages[m.resource_name]["add"]:
					additive_only = false
				continue
			if m is BaseMaterial3D:
				if not _apply(m as BaseMaterial3D):
					additive_only = false
			else:
				additive_only = false
		if additive_only:
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	if n is Node3D and String(n.name) == "flash":
		n.visible = false
	for c in n.get_children():
		_walk(c)


## The Quake 3 stages of one prop material as a ShaderMaterial (shaders/q3_stages.gdshaderinc).
func _stage_material(name: String, d: Dictionary) -> ShaderMaterial:
	var sm := ShaderMaterial.new()
	sm.resource_name = name
	var two_sided: bool = d.get("two_sided", false) or "__2s" in name
	sm.shader = load(STAGE_SHADERS[("add" if d["add"] else "") + ("2s" if two_sided else "")])
	var mode := Vector3i.ZERO
	var env := Vector3i.ZERO
	var lit := Vector3i.ZERO
	var wave_func := Vector3i.ZERO
	var rotate := Vector3.ZERO
	var stages: Array = d["stages"]
	for k in mini(stages.size(), 3):
		var st: Dictionary = stages[k]
		mode[k] = int(st["mode"])
		env[k] = int(st["env"])
		lit[k] = int(st.get("lit", false))
		rotate[k] = float(st["rotate"])
		var w: Array = st["wave"]
		wave_func[k] = int(w[0])
		sm.set_shader_parameter("tex%d" % k, load("res://library/props/" + String(st["tex"])))
		sm.set_shader_parameter("scale%d" % k, Vector2(st["scale"][0], st["scale"][1]))
		sm.set_shader_parameter("scroll%d" % k, Vector2(st["scroll"][0], st["scroll"][1]))
		sm.set_shader_parameter("turb%d" % k, Vector2(st["turb"][0], st["turb"][1]))
		sm.set_shader_parameter("wave%d" % k, Vector4(w[1], w[2], w[3], w[4]))
	sm.set_shader_parameter("mode", mode)
	sm.set_shader_parameter("env", env)
	sm.set_shader_parameter("lit", lit)
	sm.set_shader_parameter("rotate", rotate)
	sm.set_shader_parameter("wave_func", wave_func)
	# deformVertexes (the CTF flags wave): q3_deform.gdshaderinc, from q3shader.deform_uniforms
	var deforms: Array = d.get("deforms", [])
	for k in mini(deforms.size(), 2):
		var df: Dictionary = deforms[k]
		var da: Array = df["a"]
		var db: Array = df["b"]
		var mv: Array = df["move"]
		sm.set_shader_parameter("deform%d_a" % (k + 1), Vector4(da[0], da[1], da[2], da[3]))
		sm.set_shader_parameter("deform%d_b" % (k + 1), Vector4(db[0], db[1], db[2], db[3]))
		sm.set_shader_parameter("deform%d_move" % (k + 1), Vector3(mv[0], mv[1], mv[2]))
	return sm


## Returns true when the material is additive (casts no shadow).
func _apply(m: BaseMaterial3D) -> bool:
	var nm := m.resource_name
	var character := _modes.has(nm)   # a character texture: its painted albedo is the color
	if character:
		var md: Dictionary = _modes[nm]
		nm += ("__" + String(md["blend"]) if md["blend"] != "opaque" else "") + ("__2s" if md.get("two_sided") else "")
	if "__2s" in nm:
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	if "__add" in nm:
		# energy layers: the glow texture is the color, added on top, no lighting
		if m.emission_texture and not character:
			m.albedo_texture = m.emission_texture
		# reflection-only glass (health spheres) stays faint so the item inside reads through
		m.albedo_color = Color(0.45, 0.45, 0.45) if "_envadd" in nm else Color(1, 1, 1)
		m.emission_enabled = false
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
		m.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
		m.disable_fog = true   # fog on an additive quad adds the haze color over the whole square
		return true
	if "__mask" in nm:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		m.alpha_scissor_threshold = 0.5
	elif "__blend" in nm:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	return false


# ---------------------------------------------------------------- maps

## The importer's name suffixes (nodes/use_name_suffixes) strip "_alpha" and "_vcol" from material
## names and turn on transparency or vertex color instead. build_maps.py writes every map material
## opaque with no vertex color, so those flags say which suffix to put back. Without this,
## textures/base_wall/atech1_alpha arrived as "atech1", missed, and the Proving Grounds' column was gray.
static func _shader_name(mat: Material) -> String:
	if mat == null:
		return ""
	var name := mat.resource_name
	var bm := mat as BaseMaterial3D
	if bm:
		if bm.vertex_color_use_as_albedo:
			name += "_vcol"
		if bm.transparency != BaseMaterial3D.TRANSPARENCY_DISABLED:
			name += "_alpha"
	return name


const GLOW_SHADERS := ["world_add", "world_sprite_add", "world_mul"]   ## light effects: they add or tint, never block


## A translucent chunk's glows (light beams, flares, glowing grates) go to a sibling "<name>_fx" out of the
## lightmap bake: Godot's lightmapper ignores cast_shadow, so the beams around q3dm12's lamps boxed their
## light in and left the BFG room black (2026-10-25). Lit like the movers, by the bake's probes and the
## lamps. The chunk's other surfaces (Rustgrad's cliffs and ferns) stay baked: they bounce the light.
static func _split_glows(m: MeshInstance3D, shaders: PackedStringArray, wm: Dictionary) -> void:
	var src := m.mesh as ArrayMesh
	if src == null:
		return
	var glow: Array[int] = []
	for i in src.get_surface_count():
		if String(wm.get(shaders[i], {}).get("shader", "")) in GLOW_SHADERS:
			glow.append(i)
	if glow.is_empty():
		return
	if glow.size() == src.get_surface_count():
		m.gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC
		return
	var keep := ArrayMesh.new()
	var fx := ArrayMesh.new()
	keep.lightmap_size_hint = src.lightmap_size_hint
	for i in src.get_surface_count():
		var to := fx if i in glow else keep
		to.add_surface_from_arrays(src.surface_get_primitive_type(i), src.surface_get_arrays(i))
		to.surface_set_material(to.get_surface_count() - 1, src.surface_get_material(i))
	m.mesh = keep
	var f := MeshInstance3D.new()
	f.name = String(m.name) + "_fx"
	f.mesh = fx
	f.gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC
	f.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	f.layers = m.layers
	m.add_sibling(f)
	f.owner = m.owner
	f.transform = m.transform


## Shader names -> world materials. world_* chunks are static (baked by LightmapGI, UV2 from the
## importer's light_baking setting); model_* nodes are movers, lit dynamically; sprite_* nodes are
## camera-facing quads, left out of the bake.
func _map_import(scene: Node) -> void:
	var wm: Dictionary = {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(WORLD_MATERIALS))
	if parsed is Dictionary:
		wm = parsed.get("materials", {})
	var fallback := StandardMaterial3D.new()
	fallback.resource_name = "missing"
	fallback.albedo_color = Color(0.45, 0.43, 0.4)
	fallback.roughness = 0.8
	var cache := {}
	for mi in scene.find_children("*", "MeshInstance3D", true, false):
		var m := mi as MeshInstance3D
		var see_through := true
		var shaders := PackedStringArray()
		for i in m.mesh.get_surface_count():
			var name := _shader_name(m.mesh.surface_get_material(i))
			shaders.append(name)
			if not cache.has(name):
				var e: Dictionary = wm.get(name, {})
				var res := String(e.get("res", ""))
				cache[name] = load(res) if res != "" and ResourceLoader.exists(res) else fallback
			m.mesh.surface_set_material(i, cache[name])
			var kind := String(wm.get(name, {}).get("kind", ""))
			if kind != "liquid":
				see_through = false
		# world_..._ns: translucent surfaces (surfaceparm trans, build_maps.py), which q3map doesn't let block light
		if see_through or String(m.name).ends_with("_ns"):
			m.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		if String(m.name).ends_with("_ns"):
			_split_glows(m, shaders, wm)
		if String(m.name).begins_with("model_"):
			m.gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC
		elif String(m.name).begins_with("sprite_"):
			# camera-facing flares and flames: not baked, no shadows (world_sprite.gdshaderinc)
			m.gi_mode = GeometryInstance3D.GI_MODE_DISABLED
			m.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
