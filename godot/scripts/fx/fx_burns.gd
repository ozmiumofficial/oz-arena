class_name Q3Burns
extends Node3D
## The remaster's lightning gun burns (Quake 3 left none): where the beam touches the map it
## scorches a line that follows the beam as it sweeps, glowing white-hot, cooling through orange,
## then fading away. Each shooter's beam draws one stroke: consecutive hit points on the same
## surface stretch the current segment, a turn or a jump starts the next one. Segments are
## decals, so a line wraps over steps and curves the way marks do.
##   var burns := Q3Burns.create(self)
##   burns.touch(shooter, at, normal)   # every frame the beam hits the world (Godot meters, axes)
##   burns.lift(shooter)                # the beam stopped or left the wall

const WIDTH := 5.0            ## units
const DEPTH := 10.0           ## units the decal reaches into and out of the surface
const MAX_SEGMENT := 48.0     ## units before a stroke starts a new segment
const MAX_TURN := 0.97        ## cosine: a sharper turn starts a new segment
const MAX_JUMP := 96.0        ## units between hits that still join into one stroke
const GLOW_TIME := 1.2        ## seconds a segment glows after the beam last touched it
const LIFE := 4.0             ## seconds until it has faded out
const MAX_SEGMENTS := 120     ## oldest go first (decals share the clustered-element budget)
const GLOW := Color(1.0, 0.55, 0.2)

static var _albedo: ImageTexture
static var _emission: ImageTexture

var _strokes: Dictionary = {}          ## shooter -> {"seg": Decal, "a": Vector3, "b": Vector3, "n": Vector3}
var _segments: Array[Dictionary] = []  ## {"decal", "t"} oldest first; t = seconds since last touched


static func create(parent: Node) -> Q3Burns:
	var b := Q3Burns.new()
	b.name = "Burns"
	parent.add_child(b)
	return b


## The line's textures, made once: across the decal's z a soft line, hot white at its core in the
## emission map; the albedo is a dark scorch.
static func _textures() -> void:
	if _albedo:
		return
	var w := 8
	var h := 32
	var a := Image.create(w, h, false, Image.FORMAT_RGBA8)
	var e := Image.create(w, h, false, Image.FORMAT_RGBA8)
	for y in h:
		var v := absf((float(y) + 0.5) / float(h) * 2.0 - 1.0)   # 0 at the middle, 1 at the edges
		var line := exp(-v * v * 5.0) * (1.0 - smoothstep(0.75, 1.0, v))
		var core := exp(-v * v * 30.0)
		for x in w:
			a.set_pixel(x, y, Color(0.05, 0.04, 0.035, line * 0.85))
			e.set_pixel(x, y, Color(1.0, 0.7 + 0.3 * core, 0.35 + 0.65 * core, line))
	_albedo = ImageTexture.create_from_image(a)
	_emission = ImageTexture.create_from_image(e)


func touch(shooter: int, at: Vector3, normal: Vector3) -> void:
	if not Settings.get_value("graphics/decals"):
		return
	var n := normal.normalized()
	var s: Dictionary = _strokes.get(shooter, {})
	if not s.is_empty() and is_instance_valid(s["seg"]):
		var a: Vector3 = s["a"]
		var b: Vector3 = s["b"]
		var same_surface := (s["n"] as Vector3).dot(n) > 0.99
		var jump := at.distance_to(b) / Q3.UNIT
		if same_surface and jump <= MAX_JUMP:
			var dir_old := (b - a).normalized()
			var dir_new := (at - a).normalized()
			if at.distance_to(a) / Q3.UNIT <= MAX_SEGMENT and (b.is_equal_approx(a) or dir_old.dot(dir_new) >= MAX_TURN):
				s["b"] = at   # stretch the segment to the new point
			else:
				s = _start(b, at, n)   # a new segment from where the last one ended
			_place(s)
			_strokes[shooter] = s
			return
	s = _start(at, at, n)
	_place(s)
	_strokes[shooter] = s


func lift(shooter: int) -> void:
	_strokes.erase(shooter)


## Every burn line gone at once (a replay jumped).
func clear() -> void:
	for seg in _segments:
		if is_instance_valid(seg["decal"]):
			(seg["decal"] as Decal).queue_free()
	_segments.clear()
	_strokes.clear()


func _start(a: Vector3, b: Vector3, n: Vector3) -> Dictionary:
	_textures()
	var d := Decal.new()
	d.texture_albedo = _albedo
	d.texture_emission = _emission
	d.emission_energy = 4.0
	d.modulate = GLOW
	d.normal_fade = 0.5
	d.upper_fade = 0.3
	d.lower_fade = 0.3
	add_child(d)
	_segments.append({"decal": d, "t": 0.0})
	while _segments.size() > MAX_SEGMENTS:
		var old: Dictionary = _segments.pop_front()
		if is_instance_valid(old["decal"]):
			(old["decal"] as Decal).queue_free()
	return {"seg": d, "a": a, "b": b, "n": n}


## The segment's decal from a to b on the surface with normal n: +Y out of the surface, +X along
## the line, and half a width past each end so joined segments overlap into one line.
func _place(s: Dictionary) -> void:
	var d: Decal = s["seg"]
	var a: Vector3 = s["a"]
	var b: Vector3 = s["b"]
	var n: Vector3 = s["n"]
	var along := (b - a) - n * (b - a).dot(n)
	var length := along.length()
	var x := along / length if length > 0.0001 else n.cross(Vector3.UP if absf(n.y) < 0.9 else Vector3.RIGHT).normalized()
	var z := x.cross(n).normalized()
	var w := WIDTH * Q3.UNIT
	d.size = Vector3(length + w, DEPTH * Q3.UNIT, w)
	d.global_transform = Transform3D(Basis(x, n, z), (a + b) * 0.5)
	for seg in _segments:
		if seg["decal"] == d:
			seg["t"] = 0.0   # touched now: hot again
			break


func _process(delta: float) -> void:
	var gain := float(Settings.get_value("graphics/effect_intensity"))
	var keep: Array[Dictionary] = []
	for seg in _segments:
		var d: Decal = seg["decal"]
		if not is_instance_valid(d):
			continue
		var t: float = seg["t"] + delta
		seg["t"] = t
		if t >= LIFE:
			d.queue_free()
			continue
		keep.append(seg)
		var hot := 1.0 - clampf(t / GLOW_TIME, 0.0, 1.0)   # 1 white-hot .. 0 cold
		d.emission_energy = 4.0 * hot * hot * gain
		d.modulate = Color(1.0, 0.55 + 0.45 * hot, 0.2 + 0.6 * hot, 1.0 - smoothstep(GLOW_TIME, LIFE, t))
	_segments = keep
