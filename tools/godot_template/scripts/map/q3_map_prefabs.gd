class_name Q3MapPrefabs
extends RefCounted
## Prefabs for the map editor (MAP_EDITOR_PLAN.md E4, OZMIUM's PrefabData): pieces of map saved by
## name with a thumbnail, to stamp into any map. A prefab file is a map file (Q3MapDoc's format)
## holding just those objects, with info.prefab set; entity links and groups inside it survive.

static var dir := "user://map_editor/prefabs"     ## tests point this somewhere else
const EXTENSION := ".q3prefab.json"


static func path_of(name: String) -> String:
	return dir.path_join(name + EXTENSION)


static func thumb_path(name: String) -> String:
	return dir.path_join(name + ".png")


## Saved prefabs by name: [{name, path, thumb}].
static func list() -> Array:
	var out := []
	var d := DirAccess.open(dir)
	if d == null:
		return out
	for f in d.get_files():
		if f.ends_with(EXTENSION):
			var n := f.trim_suffix(EXTENSION)
			out.append({"name": n, "path": dir.path_join(f), "thumb": thumb_path(n)})
	out.sort_custom(func(a, b): return a["name"] < b["name"])
	return out


static func exists(name: String) -> bool:
	return FileAccess.file_exists(path_of(name))


## Saves objects (taken from doc, with the groups they use) as a prefab.
static func save(name: String, doc: Q3MapDoc, ids: Array) -> Error:
	var tmp := Q3MapDoc.new()
	tmp.info = {"name": name, "author": "", "description": "", "prefab": true}
	for id in ids:
		var o := doc.get_obj(id)
		if o.is_empty():
			continue
		tmp.objects[id] = o.duplicate(true)
		var g := int(o.get("group", 0))
		if g != 0 and doc.groups.has(g):
			tmp.groups[g] = doc.groups[g].duplicate()
	if tmp.objects.is_empty():
		return ERR_INVALID_PARAMETER
	tmp.next_id = 1
	for id in tmp.objects:
		tmp.next_id = maxi(tmp.next_id, id + 1)
	DirAccess.make_dir_recursive_absolute(dir)
	return tmp.save(path_of(name))


## The prefab's objects, or [] when it can't be read.
static func load_objects(name: String) -> Array:
	var tmp := Q3MapDoc.new()
	if tmp.load_file(path_of(name)) != "":
		return []
	var ids := tmp.objects.keys()
	ids.sort()
	return ids.map(func(i): return tmp.objects[i])


static func remove(name: String) -> void:
	for p in [path_of(name), thumb_path(name)]:
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(p))


## The saved thumbnail, or null.
static func thumbnail(name: String) -> Texture2D:
	var p := thumb_path(name)
	if not FileAccess.file_exists(p):
		return null
	var img := Image.load_from_file(ProjectSettings.globalize_path(p))
	return ImageTexture.create_from_image(img) if img and not img.is_empty() else null
