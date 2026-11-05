#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"
#include "ed_shaders.h"
#include "ed_texmap.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" {
#include "../ui/oz_ui_engine.h"
#include "../../../quake3e/code/client/keycodes.h"
}

namespace oz::ed {

static const double PI = 3.14159265358979323846;
static const double CELL = 1024;            // a mesh per cell of the map
static const double PATCH_TOLERANCE = 4;    // units a curve may stray from its triangles

// the selection's look: a warm tint on its faces, its edges bright, dim where something hides them
static const unsigned char SEL_TINT[4] = {255, 80, 30, 52};
static const unsigned char SEL_EDGE[4] = {255, 120, 50, 255};
static const unsigned char SEL_EDGE_HIDDEN[4] = {255, 120, 50, 64};
static const unsigned char WIRE_EDGE[4] = {205, 196, 184, 255};    // a 2D view's world brushes
static const unsigned char WIRE_ENTITY[4] = {110, 170, 255, 255};  // an entity's brushes (doors, triggers)

Session &session() {
	static Session s;
	return s;
}

static void printf_engine(const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	OzE_Print(buf);
}

// ---------------------------------------------------------------- camera

void Camera::axes(Vec3 &forward, Vec3 &left, Vec3 &up) const {
	// id's AngleVectors, with left = -right
	double y = yaw * PI / 180, p = pitch * PI / 180;
	double sy = std::sin(y), cy = std::cos(y), sp = std::sin(p), cp = std::cos(p);
	forward = Vec3(cp * cy, cp * sy, -sp);
	Vec3 right(sy, -cy, 0);  // roll 0
	left = -right;
	up = Vec3(sp * cy, sp * sy, cp);
}

// ---------------------------------------------------------------- meshes

namespace {
struct Batch {
	std::vector<ozEdVertex_t> verts;
	std::vector<unsigned short> indexes;
};

ozEdVertex_t vertex(const Vec3 &p, double s, double t, const Vec3 &n, const Vec3 &tan, double w, const unsigned char *color) {
	ozEdVertex_t v{};
	v.xyz[0] = float(p.x), v.xyz[1] = float(p.y), v.xyz[2] = float(p.z);
	v.st[0] = float(s), v.st[1] = float(t);
	v.normal[0] = float(n.x), v.normal[1] = float(n.y), v.normal[2] = float(n.z);
	v.tangent[0] = float(tan.x), v.tangent[1] = float(tan.y), v.tangent[2] = float(tan.z), v.tangent[3] = float(w);
	std::memcpy(v.color, color, 4);
	return v;
}

// A tangent along s in the face, and the sign of its bitangent (rend2: B = (N x T) w).
void face_tangent(const Vec3 &n, const StAxes &st, Vec3 &tan, double &w) {
	Vec3 t, b;
	q3map::face_tangents(n, st.s, st.t, t, b);
	if (t.length() < 1e-12) {
		Vec3 u, v;
		q3map::default_texture_axes(n, u, v);
		t = u;
	}
	tan = t.normalized();
	w = n.cross(tan).dot(b) < 0 ? -1 : 1;
}

// a before b, with a little slack: a brush's two faces see an edge from each end
bool before(const Vec3 &a, const Vec3 &b) {
	const double e = 1e-6;
	if (std::fabs(a.x - b.x) > e) return a.x < b.x;
	if (std::fabs(a.y - b.y) > e) return a.y < b.y;
	return a.z < b.z - e;
}

int shader_handle(const char *name) {
	const ozEdRender_t *r = OzEd_Renderer();
	return r ? r->RegisterShader(name) : 0;
}
}  // namespace

Mesher::Key Mesher::key_of(const Doc &doc, Id id) const {
	Bounds b = bounds_of(doc, id);
	if (b.empty()) {
		return Key(0, 0, 0);
	}
	Vec3 c = b.center();
	return Key(int(std::floor(c.x / CELL)), int(std::floor(c.y / CELL)), int(std::floor(c.z / CELL)));
}

void Mesher::changed(const Doc &doc, const std::vector<Id> &ids) {
	auto drop = [&](Id id) {
		auto w = where_.find(id);
		if (w == where_.end()) {
			return;
		}
		Cell &c = cells_[w->second];
		c.ids.erase(std::remove(c.ids.begin(), c.ids.end(), id), c.ids.end());
		c.dirty = true;
		where_.erase(w);
	};
	auto place = [&](Id id) {
		const Object *o = doc.get(id);
		if (!o || o->kind == Kind::Entity || (include && !include(id))) {
			return;
		}
		Key k = key_of(doc, id);
		Cell &c = cells_[k];
		c.ids.push_back(id);
		c.dirty = true;
		where_[id] = k;
	};
	if (ids.empty()) {
		for (auto &kv : cells_) {
			kv.second.ids.clear();
			kv.second.dirty = true;
		}
		where_.clear();
		for (Id id : doc.ids()) {
			place(id);
		}
		return;
	}
	for (Id id : ids) {
		drop(id);
		place(id);
	}
}

const Mesher::Material &Mesher::material(ShaderId s) {
	auto it = materials_.find(s);
	if (it != materials_.end()) {
		return it->second;
	}
	const ozEdRender_t *r = OzEd_Renderer();
	std::string name = shader_name(s);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;  // q3map2 prefixes a face's shader
	}
	Material m;
	// the tool shaders as Radiant draws them: caulk solid, the rest see-through in their colors, one
	// layer a brush (their faces turned away aren't drawn: a map's hint brushes stacked up to a haze)
	struct Tool {
		Look look;
		const char *shader;
		unsigned char color[4];
	};
	static const Tool tools[] = {
		{Look::Caulk, "$oz_ed_flat", {176, 92, 150, 255}},  {Look::Clip, "$oz_ed_tool", {150, 150, 255, 56}},
		{Look::Trigger, "$oz_ed_tool", {255, 165, 40, 50}}, {Look::Hint, "$oz_ed_tool", {220, 200, 255, 12}},
		{Look::Nodraw, "$oz_ed_tool", {160, 160, 160, 36}}, {Look::Origin, "$oz_ed_tool", {255, 120, 40, 110}},
		{Look::Portal, "$oz_ed_tool", {255, 60, 60, 48}},   {Look::Sky, "$oz_ed_tool", {110, 160, 255, 16}},
		{Look::Fog, "$oz_ed_tool", {150, 170, 190, 40}},
	};
	const Look look = shaders_lib().look(name);
	m.shader = 0;
	for (const Tool &t : tools) {
		if (t.look == look) {
			m.shader = r ? r->RegisterShader(t.shader) : 0;
			std::memcpy(m.color, t.color, 4);
		}
	}
	if (look == Look::Normal && r) {
		m.shader = r->RegisterShader(name.c_str());
	}
	return materials_[s] = m;
}

// The 2D views' lines: each brush's edges once (a face's polygon goes one way round, its neighbor's
// the other), each curve's tessellated rows and columns.
void Mesher::build_wire(const Doc &doc, Cell &cell) {
	std::vector<ozEdVertex_t> lv;
	auto edge = [&](const Vec3 &a, const Vec3 &b, const unsigned char c[4]) {
		static const Vec3 n(0, 0, 1);
		if (lv.size() + 2 <= 65534) {
			lv.push_back(vertex(a, 0, 0, n, Vec3(1, 0, 0), 1, c));
			lv.push_back(vertex(b, 0, 0, n, Vec3(1, 0, 0), 1, c));
		}
	};
	for (Id id : cell.ids) {
		const Object *o = doc.get(id);
		if (!o) {
			continue;
		}
		const unsigned char *c = wire_color ? wire_color : o->owner && o->owner != doc.worldspawn() ? WIRE_ENTITY : WIRE_EDGE;
		if (o->kind == Kind::Brush) {
			for (const q3map::Winding &w : doc.windings(id)) {
				for (size_t k = 0; k < w.size(); k++) {
					const Vec3 &a = w[k], &b = w[(k + 1) % w.size()];
					if (before(a, b)) {
						edge(a, b, c);
					}
				}
			}
		} else if (o->kind == Kind::Patch) {
			const q3map::PatchMesh m = patch_mesh(o->patch, PATCH_TOLERANCE);
			for (int r = 0; r < m.height; r++) {
				for (int k = 0; k < m.width; k++) {
					const Vec3 &a = m.verts[size_t(r * m.width + k)].p;
					if (k + 1 < m.width) {
						edge(a, m.verts[size_t(r * m.width + k + 1)].p, c);
					}
					if (r + 1 < m.height) {
						edge(a, m.verts[size_t((r + 1) * m.width + k)].p, c);
					}
				}
			}
		}
	}
	const ozEdRender_t *r = OzEd_Renderer();
	if (r && !cell.lines) {
		cell.lines = r->CreateMesh(1);
	}
	if (r && cell.lines) {
		std::vector<unsigned short> li(lv.size());
		for (size_t k = 0; k < li.size(); k++) {
			li[k] = (unsigned short)k;
		}
		const ozEdRange_t range{shader_handle("$oz_ed_lines_xray"), 0, int(li.size())};
		r->UploadMesh(cell.lines, lv.data(), int(lv.size()), li.data(), int(li.size()), &range, li.empty() ? 0 : 1);
	}
	cell.triangles = 0;
	cell.dirty = false;
}

void Mesher::build(const Doc &doc, Cell &cell) {
	if (wire) {
		build_wire(doc, cell);
		return;
	}
	std::map<int, Batch> batches;  // shader handle -> geometry
	Batch tint;
	std::vector<ozEdVertex_t> edges;  // pairs
	auto edge = [&](const Vec3 &a, const Vec3 &b) {
		static const Vec3 n(0, 0, 1);
		edges.push_back(vertex(a, 0, 0, n, Vec3(1, 0, 0), 1, SEL_EDGE));
		edges.push_back(vertex(b, 0, 0, n, Vec3(1, 0, 0), 1, SEL_EDGE));
	};
	auto add_tint = [&](const Batch &from, size_t first_vert, size_t first_index) {
		if (!overlay || tint.verts.size() + (from.verts.size() - first_vert) > 65535) {
			return;
		}
		unsigned short base = (unsigned short)tint.verts.size();
		for (size_t k = first_vert; k < from.verts.size(); k++) {
			ozEdVertex_t v = from.verts[k];
			std::memcpy(v.color, SEL_TINT, 4);
			tint.verts.push_back(v);
		}
		for (size_t k = first_index; k < from.indexes.size(); k++) {
			tint.indexes.push_back((unsigned short)(from.indexes[k] - first_vert + base));
		}
	};
	Batch scratch;  // an object drawn by its tint and edges alone
	for (Id id : cell.ids) {
		const Object *o = doc.get(id);
		if (!o) {
			continue;
		}
		const bool mats = !materials || materials(id);
		if (o->kind == Kind::Brush) {
			const auto &ws = doc.windings(id);
			for (size_t i = 0; i < o->brush.faces.size() && i < ws.size(); i++) {
				const Face &f = o->brush.faces[i];
				const q3map::Winding &w = ws[i];
				if (w.size() < 3) {
					continue;
				}
				const Material &mat = material(f.shader);
				scratch = Batch();
				Batch &b = mats ? batches[mat.shader] : scratch;
				if (b.verts.size() + w.size() > 65535) {
					continue;  // a cell this dense would need a split; it's never been seen
				}
				Vec3 tan;
				double sign;
				face_tangent(f.plane.n, f.st, tan, sign);
				const size_t fv = b.verts.size(), fi = b.indexes.size();
				unsigned short base = (unsigned short)b.verts.size();
				for (const Vec3 &p : w) {
					b.verts.push_back(vertex(p, f.st.s.dot(p) + f.st.s0, f.st.t.dot(p) + f.st.t0, f.plane.n, tan, sign, mat.color));
				}
				// id's renderers take a triangle clockwise from its front as facing the camera (GL_Cull
				// culls GL_FRONT): the kernel's polygons go the other way
				for (size_t k = 2; k < w.size(); k++) {
					b.indexes.push_back(base);
					b.indexes.push_back((unsigned short)(base + k));
					b.indexes.push_back((unsigned short)(base + k - 1));
				}
				add_tint(b, fv, fi);
				if (overlay) {
					for (size_t k = 0; k < w.size(); k++) {
						const Vec3 &a = w[k], &c = w[(k + 1) % w.size()];
						if (before(a, c)) {
							edge(a, c);
						}
					}
				}
			}
		} else if (o->kind == Kind::Patch) {
			const Patch &pt = o->patch;
			q3map::PatchMesh m = patch_mesh(pt, PATCH_TOLERANCE);
			const Material &mat = material(pt.shader);
			scratch = Batch();
			Batch &b = mats ? batches[mat.shader] : scratch;
			if (m.verts.empty() || b.verts.size() + m.verts.size() > 65535) {
				continue;
			}
			const size_t fv = b.verts.size(), fi = b.indexes.size();
			unsigned short base = (unsigned short)b.verts.size();
			for (size_t k = 0; k < m.verts.size(); k++) {
				const Vec3 &n = k < m.normals.size() ? m.normals[k] : Vec3(0, 0, 1);
				Vec3 u, v;
				q3map::default_texture_axes(n, u, v);
				b.verts.push_back(vertex(m.verts[k].p, m.verts[k].s, m.verts[k].t, n, (u - n * u.dot(n)).normalized(), 1, mat.color));
			}
			for (int r = 0; r + 1 < m.height; r++) {
				for (int c = 0; c + 1 < m.width; c++) {
					unsigned short a = (unsigned short)(base + r * m.width + c), bb = (unsigned short)(a + 1);
					unsigned short d = (unsigned short)(a + m.width), e = (unsigned short)(d + 1);
					// du x dv faces out (the kernel's facing)
					b.indexes.insert(b.indexes.end(), {a, d, bb, bb, d, e});
				}
			}
			add_tint(b, fv, fi);
			if (overlay) {
				// the curve's border and its control net
				for (int c = 0; c + 1 < m.width; c++) {
					edge(m.verts[c].p, m.verts[c + 1].p);
					edge(m.verts[(m.height - 1) * m.width + c].p, m.verts[(m.height - 1) * m.width + c + 1].p);
				}
				for (int r = 0; r + 1 < m.height; r++) {
					edge(m.verts[r * m.width].p, m.verts[(r + 1) * m.width].p);
					edge(m.verts[r * m.width + m.width - 1].p, m.verts[(r + 1) * m.width + m.width - 1].p);
				}
				auto cp = [&](int r, int c) {
					const double *x = &pt.ctrl[(size_t(r) * pt.width + c) * 5];
					return Vec3(x[0], x[1], x[2]);
				};
				for (int r = 0; r < pt.height; r++) {
					for (int c = 0; c < pt.width; c++) {
						if (c + 1 < pt.width) {
							edge(cp(r, c), cp(r, c + 1));
						}
						if (r + 1 < pt.height) {
							edge(cp(r, c), cp(r + 1, c));
						}
					}
				}
			}
		}
	}
	if (overlay && !tint.indexes.empty()) {
		batches[shader_handle("$oz_ed_tint")] = std::move(tint);
	}

	// pack the batches into meshes of up to 65535 vertexes
	const ozEdRender_t *r = OzEd_Renderer();
	cell.triangles = 0;
	size_t used = 0;
	std::vector<ozEdVertex_t> verts;
	std::vector<unsigned short> indexes;
	std::vector<ozEdRange_t> ranges;
	auto emit = [&]() {
		if (indexes.empty() || !r) {
			return;
		}
		int h;
		if (used < cell.meshes.size()) {
			h = cell.meshes[used];
		} else if (!spare_.empty()) {
			h = spare_.back();
			spare_.pop_back();
			cell.meshes.push_back(h);
		} else {
			h = r->CreateMesh(0);
			if (!h) {
				return;
			}
			cell.meshes.push_back(h);
		}
		used++;
		r->UploadMesh(h, verts.data(), int(verts.size()), indexes.data(), int(indexes.size()), ranges.data(), int(ranges.size()));
		for (const ozEdRange_t &rg : ranges) {
			cell.triangles += rg.numIndexes / 3;
		}
		verts.clear(), indexes.clear(), ranges.clear();
	};
	for (auto &kv : batches) {
		Batch &b = kv.second;
		if (b.indexes.empty()) {
			continue;
		}
		if (verts.size() + b.verts.size() > 65535) {
			emit();
		}
		unsigned short base = (unsigned short)verts.size();
		ranges.push_back(ozEdRange_t{kv.first, int(indexes.size()), int(b.indexes.size())});
		verts.insert(verts.end(), b.verts.begin(), b.verts.end());
		for (unsigned short ix : b.indexes) {
			indexes.push_back((unsigned short)(ix + base));
		}
	}
	emit();
	// meshes the cell no longer needs wait for the next build
	while (cell.meshes.size() > used) {
		int h = cell.meshes.back();
		cell.meshes.pop_back();
		if (r) {
			r->UploadMesh(h, nullptr, 0, nullptr, 0, nullptr, 0);
		}
		spare_.push_back(h);
	}

	// the overlay's edges: the visible ones bright, a dim copy over everything
	if (r && overlay) {
		if (!cell.lines) {
			cell.lines = r->CreateMesh(1);
		}
		const size_t n = std::min(edges.size(), size_t(32766)) & ~size_t(1);  // both copies in one mesh
		std::vector<ozEdVertex_t> lv(edges.begin(), edges.begin() + n);
		for (size_t k = 0; k < n; k++) {
			ozEdVertex_t v = edges[k];
			std::memcpy(v.color, SEL_EDGE_HIDDEN, 4);
			lv.push_back(v);
		}
		std::vector<unsigned short> li(lv.size());
		for (size_t k = 0; k < li.size(); k++) {
			li[k] = (unsigned short)k;
		}
		ozEdRange_t lr[2] = {{shader_handle("$oz_ed_lines"), 0, int(n)}, {shader_handle("$oz_ed_lines_xray"), int(n), int(n)}};
		if (cell.lines) {
			r->UploadMesh(cell.lines, lv.data(), int(lv.size()), li.data(), int(li.size()), lr, n ? 2 : 0);
		}
	}
	cell.dirty = false;
}

void Mesher::flush(const Doc &doc) {
	for (auto &kv : cells_) {
		if (kv.second.dirty) {
			build(doc, kv.second);
		}
	}
}

int Mesher::triangles() const {
	int n = 0;
	for (const auto &kv : cells_) {
		n += kv.second.triangles;
	}
	return n;
}

void Mesher::draw(const float origin[3], const float axis[3][3]) const {
	for (const auto &kv : cells_) {
		for (int h : kv.second.meshes) {
			OzEd_AddModel(h, origin, axis, 0, nullptr);
		}
		if (kv.second.lines && !kv.second.ids.empty()) {
			OzEd_AddModel(kv.second.lines, origin, axis, 0, nullptr);
		}
	}
}

void Mesher::reset() {
	cells_.clear();
	where_.clear();
	materials_.clear();
	spare_.clear();
}

int Mesher::meshes() const {
	int n = 0;
	for (const auto &kv : cells_) {
		n += int(kv.second.meshes.size());
	}
	return n;
}

// ---------------------------------------------------------------- overlays

void Overlay::begin() {
	for (auto &kv : tris_) {
		kv.second.v.clear(), kv.second.i.clear();
	}
	for (auto &kv : lines_) {
		kv.second.v.clear(), kv.second.i.clear();
	}
}

unsigned short Overlay::vert(Part &p, const Vec3 &at, const unsigned char c[4]) {
	static const Vec3 n(0, 0, 1);
	p.v.push_back(vertex(at, 0, 0, n, Vec3(1, 0, 0), 1, c));
	return (unsigned short)(p.v.size() - 1);
}

void Overlay::line(const Vec3 &a, const Vec3 &b, const unsigned char c[4], const char *look) {
	Part &p = lines_[look];
	if (p.v.size() + 2 > 65535) {
		return;
	}
	p.i.push_back(vert(p, a, c));
	p.i.push_back(vert(p, b, c));
}

void Overlay::tri(const Vec3 &a, const Vec3 &b, const Vec3 &c, const unsigned char col[4], const char *look) {
	Part &p = tris_[look];
	if (p.v.size() + 3 > 65535) {
		return;
	}
	unsigned short i0 = vert(p, a, col), i1 = vert(p, b, col), i2 = vert(p, c, col);
	p.i.insert(p.i.end(), {i0, i1, i2});
}

void Overlay::quad(const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, const unsigned char col[4], const char *look) {
	Part &p = tris_[look];
	if (p.v.size() + 4 > 65535) {
		return;
	}
	unsigned short i0 = vert(p, a, col), i1 = vert(p, b, col), i2 = vert(p, c, col), i3 = vert(p, d, col);
	p.i.insert(p.i.end(), {i0, i1, i2, i0, i2, i3});
}

static void box_corners(const Vec3 &lo, const Vec3 &hi, Vec3 c[8]) {
	for (int i = 0; i < 8; i++) {
		c[i] = Vec3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
	}
}

void Overlay::box_edges(const Vec3 &lo, const Vec3 &hi, const unsigned char c[4], const char *look) {
	Part &p = lines_[look];
	if (p.v.size() + 8 > 65535) {
		return;
	}
	Vec3 k[8];
	box_corners(lo, hi, k);
	unsigned short base = (unsigned short)p.v.size();
	for (const Vec3 &v : k) {
		vert(p, v, c);
	}
	static const int e[24] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 1, 3, 4, 6, 5, 7, 0, 4, 1, 5, 2, 6, 3, 7};
	for (int i : e) {
		p.i.push_back((unsigned short)(base + i));
	}
}

void Overlay::box_faces(const Vec3 &lo, const Vec3 &hi, const unsigned char c[4], const char *look) {
	Part &p = tris_[look];
	if (p.v.size() + 8 > 65535) {
		return;
	}
	Vec3 k[8];
	box_corners(lo, hi, k);
	unsigned short base = (unsigned short)p.v.size();
	for (const Vec3 &v : k) {
		vert(p, v, c);
	}
	// -X, +X, -Y, +Y, -Z, +Z, each counter-clockwise from outside, drawn clockwise (as the mesher's)
	static const int q[24] = {0, 4, 6, 2, 1, 3, 7, 5, 0, 1, 5, 4, 2, 6, 7, 3, 0, 2, 3, 1, 4, 5, 7, 6};
	for (int f = 0; f < 6; f++) {
		const int *v = &q[f * 4];
		p.i.insert(p.i.end(), {(unsigned short)(base + v[0]), (unsigned short)(base + v[2]), (unsigned short)(base + v[1]),
				(unsigned short)(base + v[0]), (unsigned short)(base + v[3]), (unsigned short)(base + v[2])});
	}
}

void Overlay::polygon(const Winding &w, const unsigned char c[4], const char *look) {
	Part &p = tris_[look];
	if (w.size() < 3 || p.v.size() + w.size() > 65535) {
		return;
	}
	unsigned short base = (unsigned short)p.v.size();
	for (const Vec3 &v : w) {
		vert(p, v, c);
	}
	for (size_t k = 2; k < w.size(); k++) {
		p.i.insert(p.i.end(), {base, (unsigned short)(base + k), (unsigned short)(base + k - 1)});  // clockwise from the front, as the mesher's
	}
}

void Overlay::outline(const Winding &w, const unsigned char c[4], const char *look) {
	for (size_t k = 0; k < w.size(); k++) {
		line(w[k], w[(k + 1) % w.size()], c, look);
	}
}

void Overlay::end() {
	const ozEdRender_t *r = OzEd_Renderer();
	if (!r) {
		return;
	}
	auto upload = [&](std::map<std::string, Part> &parts, int &mesh, bool lines, bool &used, uint64_t &hash) {
		std::vector<ozEdVertex_t> v;
		std::vector<unsigned short> idx;
		std::vector<ozEdRange_t> ranges;
		for (auto &kv : parts) {
			Part &p = kv.second;
			if (p.i.empty() || v.size() + p.v.size() > 65535) {
				continue;
			}
			unsigned short base = (unsigned short)v.size();
			ranges.push_back(ozEdRange_t{r->RegisterShader(kv.first.c_str()), int(idx.size()), int(p.i.size())});
			v.insert(v.end(), p.v.begin(), p.v.end());
			for (unsigned short i : p.i) {
				idx.push_back((unsigned short)(i + base));
			}
		}
		used = !ranges.empty();
		if (!used && !mesh) {
			return;
		}
		// FNV-1a over what would go up: the same as last time stays as it is
		uint64_t h = 1469598103934665603ull;
		auto mix = [&h](const void *p, size_t n) {
			const unsigned char *b = (const unsigned char *)p;
			for (size_t k = 0; k < n; k++) {
				h = (h ^ b[k]) * 1099511628211ull;
			}
		};
		mix(v.data(), v.size() * sizeof(ozEdVertex_t));
		mix(idx.data(), idx.size() * sizeof(unsigned short));
		mix(ranges.data(), ranges.size() * sizeof(ozEdRange_t));
		if (mesh && h == hash) {
			return;
		}
		hash = h;
		if (!mesh) {
			mesh = r->CreateMesh(lines ? 1 : 0);
		}
		if (mesh) {
			r->UploadMesh(mesh, v.data(), int(v.size()), idx.data(), int(idx.size()), ranges.data(), int(ranges.size()));
		}
	};
	upload(tris_, tri_mesh_, false, tri_used_, tri_hash_);
	upload(lines_, line_mesh_, true, line_used_, line_hash_);
}

void Overlay::draw(const float origin[3], const float axis[3][3]) const {
	if (tri_used_ && tri_mesh_) {
		OzEd_AddModel(tri_mesh_, origin, axis, 0, nullptr);
	}
	if (line_used_ && line_mesh_) {
		OzEd_AddModel(line_mesh_, origin, axis, 0, nullptr);
	}
}

void Overlay::reset() {
	tri_mesh_ = line_mesh_ = 0;
	tri_used_ = line_used_ = false;
	tri_hash_ = line_hash_ = 0;
}

// ---------------------------------------------------------------- the grid

// The 3D view's ground grid: lines on the editor's grid, every eighth brighter, the axes in red and
// green. The fine lines cover the ground near the middle and the bright ones go on further (fine
// lines far off run together into a haze), so the lines stay few.
static int build_grid(int existing, double grid) {
	const ozEdRender_t *r = OzEd_Renderer();
	if (!r) {
		return 0;
	}
	int h = existing ? existing : r->CreateMesh(1);
	if (!h) {
		return 0;
	}
	std::vector<ozEdVertex_t> verts;
	std::vector<unsigned short> indexes;
	const double step = grid, extent = std::min(8192.0, step * 8 * 256), near = std::min(extent, step * 128);
	auto line = [&](float x0, float y0, float x1, float y1, unsigned char r8, unsigned char g8, unsigned char b8, unsigned char a8) {
		ozEdVertex_t v{};
		v.color[0] = r8, v.color[1] = g8, v.color[2] = b8, v.color[3] = a8;
		v.xyz[0] = x0, v.xyz[1] = y0;
		indexes.push_back((unsigned short)verts.size());
		verts.push_back(v);
		v.xyz[0] = x1, v.xyz[1] = y1;
		indexes.push_back((unsigned short)verts.size());
		verts.push_back(v);
	};
	const int cells = int(std::lround(extent / step));
	for (int c = -cells; c <= cells; c++) {
		const bool major = c % 8 == 0;
		const float i = float(c * step), e = float(major ? extent : near);
		if (!major && std::fabs(i) > near) {
			continue;
		}
		if (c == 0) {
			line(-e, 0, e, 0, 219, 60, 40, 200);  // X: red
			line(0, -e, 0, e, 90, 200, 90, 200);  // Y: green
			continue;
		}
		line(-e, i, e, i, 230, 221, 210, major ? 70 : 28);
		line(i, -e, i, e, 230, 221, 210, major ? 70 : 28);
	}
	ozEdRange_t range{r->RegisterShader("$oz_ed_lines"), 0, int(indexes.size())};
	r->UploadMesh(h, verts.data(), int(verts.size()), indexes.data(), int(indexes.size()), &range, 1);
	return h;
}

// ---------------------------------------------------------------- the session

Session::Session() {
	// in the lit preview the compile draws what it has: the editor only what changed since
	world_.include = [this](Id id) { return !sel.count(id) && visible(id) && (!lit_ || changed_since(id)); };
	chosen_.include = [this](Id id) { return sel.count(id) != 0; };
	chosen_.materials = [this](Id id) { return !lit_ || changed_since(id); };
	chosen_.overlay = true;
	// the 2D views draw every brush's lines, the selection's after them in its color (moving with a drag)
	wire_.include = [this](Id id) { return !sel.count(id) && visible(id); };
	wire_.wire = true;
	wire_sel_.include = [this](Id id) { return sel.count(id) != 0; };
	wire_sel_.wire = true;
	wire_sel_.wire_color = SEL_EDGE;
	attach();
	new_map();
}

void Session::attach() {
	doc.tex_size = [](ShaderId s, int &w, int &h) { shaders_lib().size(shader_name(s), w, h); };
	doc.on_change = [this](const std::vector<Id> &ids) { changed(ids); };
	doc.get_selection = [this]() { return selected(); };
	doc.set_selection = [this](const std::vector<Id> &ids) {
		if (tool == Tool::Faces) {
			return;  // the faces tool keeps its faces (those still there)
		}
		corners.clear();
		std::vector<Id> there;
		for (Id id : ids) {
			if (doc.get(id)) {
				there.push_back(id);
			}
		}
		select(there, SelMode::Set);
	};
}

void Session::changed(const std::vector<Id> &ids) {
	if (ids.empty()) {
		pending_all_ = true;
	} else {
		pending_.insert(pending_.end(), ids.begin(), ids.end());
	}
	ents_dirty_ = true;
}

void Session::say(const std::string &text, bool error) {
	printf_engine("%smap editor: %s\n", error ? "^3" : "", text.c_str());
	if (notify) {
		notify(text, error);
	}
}

void Session::new_map() {
	doc = Doc();
	attach();
	doc.plain_form = TexForm::Valve;  // TrenchBroom's "Quake 3 (Valve)", which NetRadiant-custom and q3map2 read too
	auto world = std::make_shared<Object>();
	world->kind = Kind::Entity;
	world->entity.set("classname", "worldspawn");
	Id w = doc.add(world);
	// a room that plays at once: floor, walls, ceiling, a light and a spawn point
	const ShaderId floor = shader_id("base_floor/clang_floor"), wall = shader_id("gothic_block/blocks15"),
		ceil = shader_id("base_wall/concrete");
	doc.add(box_brush(doc, Vec3(-512, -512, -16), Vec3(512, 512, 0), floor, w));
	doc.add(box_brush(doc, Vec3(-512, -512, 256), Vec3(512, 512, 272), ceil, w));
	doc.add(box_brush(doc, Vec3(-528, -512, -16), Vec3(-512, 512, 272), wall, w));
	doc.add(box_brush(doc, Vec3(512, -512, -16), Vec3(528, 512, 272), wall, w));
	doc.add(box_brush(doc, Vec3(-528, -528, -16), Vec3(528, -512, 272), wall, w));
	doc.add(box_brush(doc, Vec3(-528, 512, -16), Vec3(528, 528, 272), wall, w));
	auto light = std::make_shared<Object>();
	light->kind = Kind::Entity;
	light->entity.keys = {{"classname", "light"}, {"origin", "0 0 192"}, {"light", "600"}};
	doc.add(light);
	auto spawn = std::make_shared<Object>();
	spawn->kind = Kind::Entity;
	spawn->entity.keys = {{"classname", "info_player_deathmatch"}, {"origin", "-256 0 24"}, {"angle", "0"}};
	doc.add(spawn);
	doc.clear_history();
	doc.dirty = false;
	path.clear();
	name = "untitled";
	hidden.clear();
	locked.clear();
	show_rev++;
	cam = Camera();
	cam.origin = Vec3(-480, -400, 200);
	cam.yaw = 40, cam.pitch = 18;
	frame_2d(map_bounds());
	set_tool(Tool::Select);
	sel.clear();
	corners.clear();
	faces.clear();
	picked_ok_ = false;
	leak_.clear();
	leak_rev_++;
	pending_all_ = true;
	ents_dirty_ = true;
}

bool Session::open(const std::string &file, std::string &error) {
	char *text = nullptr;
	int len = OzEd_ReadOSFile(file.c_str(), &text);
	if (len < 0) {
		error = "can't read " + file;
		return false;
	}
	Doc d;
	d.tex_size = doc.tex_size;
	ReadReport rep;
	bool ok = read_map(std::string_view(text, size_t(len)), d, rep);
	OzEd_Free(text);
	if (!ok) {
		error = rep.error;
		return false;
	}
	doc = std::move(d);
	attach();
	hidden.clear();
	locked.clear();
	show_rev++;
	path = file;
	size_t slash = file.find_last_of("/\\");
	name = file.substr(slash == std::string::npos ? 0 : slash + 1);
	if (name.size() > 4 && name.compare(name.size() - 4, 4, ".map") == 0) {
		name.resize(name.size() - 4);
	}
	set_tool(Tool::Select);
	sel.clear();
	corners.clear();
	faces.clear();
	picked_ok_ = false;
	leak_.clear();
	leak_rev_++;
	pending_all_ = true;
	ents_dirty_ = true;
	frame_all();
	printf_engine("map editor: opened %s (%d entities, %d brushes, %d curves)\n", file.c_str(), rep.entities, rep.brushes,
			rep.patches);
	return true;
}

bool Session::save(const std::string &file, std::string &error) {
	write_defi_beside(file);  // a Defrag course's map info beside it
	WriteOptions wo;
	wo.title = name + ", written by the Oz Arena map editor";
	std::string text = write_map(doc, wo);
	if (!OzEd_WriteOSFile(file.c_str(), text.data(), int(text.size()))) {
		error = "can't write " + file;
		return false;
	}
	path = file;
	doc.dirty = false;
	autosave_clear();  // saved: nothing to keep aside
	printf_engine("map editor: saved %s\n", file.c_str());
	return true;
}

static std::string q3map2_exe() {
	const char *set = OzE_CvarString("ed_q3map2");
	if (set && set[0]) {
		return set;
	}
	return std::string(OzEd_EngineDir()) + "/tools/q3map2.exe";
}

bool Session::import_bsp(const std::string &map, std::string &error) {
	std::string exe = q3map2_exe();
	if (!OzEd_FileExists(exe.c_str())) {
		error = "q3map2 isn't there (" + exe + ")";
		return false;
	}
	std::string baseq3 = OzEd_BaseQ3();
	if (baseq3.empty()) {
		error = "the game's baseq3 folder wasn't found";
		return false;
	}
	void *bsp = nullptr;
	int len = OzE_ReadFile(("maps/" + map + ".bsp").c_str(), &bsp);
	if (len <= 0) {
		error = "no maps/" + map + ".bsp in the game's files";
		return false;
	}
	std::string dir = std::string(OzEd_HomePath()) + "/oz/editor/import";
	std::string src = dir + "/maps/" + map + ".bsp";
	bool wrote = OzEd_WriteOSFile(src.c_str(), bsp, len) != 0;
	OzE_FreeFile(bsp);
	if (!wrote) {
		error = "can't write " + src;
		return false;
	}
	// only id's paks: a map pack's broken shader script stops q3map2
	std::string args = "-game quake3 -fs_basepath \"" + baseq3.substr(0, baseq3.find_last_of("/\\")) + "\"";
	int count = 0;
	char **paks = OzEd_ListOSFiles(baseq3.c_str(), "pk3", &count);
	for (int i = 0; i < count; i++) {
		const char *n = paks[i];
		bool id = std::strlen(n) == 8 && (n[0] == 'p' || n[0] == 'P') && n[3] >= '0' && n[3] <= '8';
		if (!id) {
			args += " -fs_forbiddenpath \"" + std::string(n) + "\"";
		}
	}
	OzEd_FreeList(paks);
	// brush primitives count in texture repeats: no image sizes needed, which q3map2 can't always find
	args += " -convert -format map_bp \"" + src + "\"";
	std::string log = dir + "/" + map + "_convert.log";
	int code = OzEd_Run(exe.c_str(), args.c_str(), dir.c_str(), log.c_str());
	std::string out = dir + "/maps/" + map + "_converted.map";
	if (code != 0 || !OzEd_FileExists(out.c_str())) {
		error = "q3map2 couldn't convert " + map + " (" + log + ")";
		return false;
	}
	if (!open(out, error)) {
		return false;
	}
	path.clear();  // a new map until saved
	name = map;
	doc.dirty = true;
	// the camera at the intermission point, facing its target, else at the first spawn
	const Object *start = nullptr;
	for (const char *cn : {"info_player_intermission", "info_player_deathmatch", "info_player_start"}) {
		for (Id id : doc.ids_of(Kind::Entity)) {
			if (doc.get(id)->entity.classname() == cn) {
				start = doc.get(id);
				break;
			}
		}
		if (start) {
			break;
		}
	}
	if (start) {
		Vec3 o;
		if (parse_vec(start->entity.get("origin"), o)) {
			cam.origin = o;
			cam.pitch = 0;
			cam.yaw = std::atof(start->entity.get("angle", "0").c_str());
			std::string target = start->entity.get("target");
			for (Id id : doc.ids_of(Kind::Entity)) {
				const Entity &e = doc.get(id)->entity;
				Vec3 t;
				if (!target.empty() && e.get("targetname") == target && parse_vec(e.get("origin"), t)) {
					Vec3 d = t - o;
					cam.yaw = std::atan2(d.y, d.x) * 180 / PI;
					cam.pitch = -std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)) * 180 / PI;
				}
			}
			if (start->entity.classname() != "info_player_intermission") {
				cam.origin.z += 26 + 9;  // the player's eye
			}
		}
	}
	return true;
}

static void look_at(Camera &cam, const Bounds &b, bool keep_direction) {
	if (b.empty()) {
		return;
	}
	Vec3 c = b.center();
	double r = std::max(b.size().length() * 0.5, 96.0);  // a spawn point isn't framed from inside its box
	if (!keep_direction) {
		cam.yaw = 45, cam.pitch = 35;
	}
	Vec3 f, l, u;
	cam.axes(f, l, u);
	cam.origin = c - f * (r / std::tan(cam.fov * 0.5 * PI / 180) * 1.3);
}

Bounds Session::map_bounds() const {
	// where players stand (spawns, items), when the map has them: a map's sky shell can reach far out
	Bounds b;
	int points = 0;
	for (Id id : doc.ids_of(Kind::Entity)) {
		const Entity &e = doc.get(id)->entity;
		const std::string cn = e.classname();
		Vec3 o;
		bool where = cn.compare(0, 12, "info_player_") == 0 || cn.compare(0, 5, "item_") == 0 || cn.compare(0, 7, "weapon_") == 0 ||
				cn.compare(0, 5, "ammo_") == 0 || cn.compare(0, 5, "team_") == 0;
		if (where && parse_vec(e.get("origin"), o)) {
			b.add(o - Vec3(64, 64, 64));
			b.add(o + Vec3(64, 64, 64));
			points++;
		}
	}
	if (points < 2) {
		b = bounds_of(doc, doc.ids());
	}
	return b;
}

void Session::frame_all() {
	const Bounds b = map_bounds();
	look_at(cam, b, false);
	frame_2d(b);
}

void Session::frame_selection() {
	if (sel.empty()) {
		frame_all();
		return;
	}
	look_at(cam, selection_bounds(), true);
	frame_2d(selection_bounds());
}

void Session::frame(int x, int y, int w, int h) {
	const ozEdRender_t *r = OzEd_Renderer();
	if (!r || w <= 0 || h <= 0) {
		return;
	}
	if (generation_ != OzE_RendererGeneration()) {
		// a renderer start: every handle from before is gone
		generation_ = OzE_RendererGeneration();
		world_.reset();
		chosen_.reset();
		wire_.reset();
		wire_sel_.reset();
		for (Overlay *o : {&ents_, &ents_sel_, &hover_lines_, &preview_, &faces_lines_, &links_, &place_preview_, &leak_lines_, &sun_lines_}) {
			o->reset();
		}
		for (Overlay &o : pane_handles_) {
			o.reset();
		}
		for (Overlay &o : grid2d_) {
			o.reset();
		}
		leak_built_ = ~0ull;
		faces_doc_rev_ = ~0ull;
		links_doc_ = ~0ull;
		model_handles_.clear();  // models register again with the new renderer
		grid_mesh_ = 0;
		pending_all_ = true;
		ents_dirty_ = true;
		hover_rev_ = ~0ull;
	}
	if (!grid_mesh_ || grid_mesh_step_ != grid) {
		grid_mesh_ = build_grid(grid_mesh_, grid);
		grid_mesh_step_ = grid;
	}
	load_defs();

	// the camera
	int now = OzE_Milliseconds();
	double dt = last_ms_ ? std::min((now - last_ms_) * 0.001, 0.1) : 0;
	last_ms_ = now;
	if (looking_) {
		Vec3 f, l, u;
		cam.axes(f, l, u);
		Vec3 move;
		if (OzE_KeyDown('w')) move = move + f;
		if (OzE_KeyDown('s')) move = move - f;
		if (OzE_KeyDown('a')) move = move + l;
		if (OzE_KeyDown('d')) move = move - l;
		if (OzE_KeyDown('e')) move = move + Vec3(0, 0, 1);
		if (OzE_KeyDown('q')) move = move - Vec3(0, 0, 1);
		double speed = 640 * (OzE_KeyDown(K_SHIFT) ? 3 : 1) * (OzE_KeyDown(K_CTRL) ? 0.25 : 1);
		if (move.length() > 0) {
			cam.origin = cam.origin + move.normalized() * (speed * dt);
		}
	}
	view_w_ = w, view_h_ = h;
	lay_out(x, y, w, h);
	if (!frame2d_later_.empty()) {
		const Bounds b = frame2d_later_;
		frame2d_later_ = Bounds();
		frame_2d(b);
	}
	use_pane(active_);  // the tools' view: the pane the pointer works in
	const bool flat = layout == 4 || solo != Pane::Cam;  // a 2D pane shows

	// the lit preview, on or off: every mesh again
	if (previewing() != lit_) {
		lit_ = previewing();
		pending_all_ = true;
		ents_dirty_ = true;
	}

	// what changed
	if (pending_all_ || !pending_.empty()) {
		// the selection keeps only what's still there
		for (auto it = sel.begin(); it != sel.end();) {
			it = doc.get(*it) ? std::next(it) : sel.erase(it);
		}
		for (auto it = corners.begin(); it != corners.end();) {
			it = sel.count(it->first) ? std::next(it) : corners.erase(it);
		}
		for (auto it = faces.begin(); it != faces.end();) {
			const Object *o = doc.get(it->first);
			it = o && o->kind == Kind::Brush && it->second < int(o->brush.faces.size()) ? std::next(it) : faces.erase(it);
		}
	}
	if (pending_all_) {
		world_.changed(doc, {});
		chosen_.changed(doc, {});
		wire_.changed(doc, {});
		wire_sel_.changed(doc, {});
		pending_all_ = false;
		pending_.clear();
	} else if (!pending_.empty()) {
		std::sort(pending_.begin(), pending_.end());
		pending_.erase(std::unique(pending_.begin(), pending_.end()), pending_.end());
		world_.changed(doc, pending_);
		chosen_.changed(doc, pending_);
		wire_.changed(doc, pending_);
		wire_sel_.changed(doc, pending_);
		pending_.clear();
	}
	world_.flush(doc);
	chosen_.flush(doc);
	if (flat) {
		wire_.flush(doc);
		wire_sel_.flush(doc);
	}

	// what the pointer is over
	if (!looking_ && drag_ == Drag::None && over_) {
		hover_handle_ = handle_at(mx_, my_);
		Hit hit;
		hover_ok_ = hover_handle_.kind == Handle::None && pick(mx_, my_, hit);
		hover_ = hit;
	} else if (!over_ && drag_ == Drag::None) {
		hover_ok_ = false;
		hover_handle_ = Handle();
	}
	if (ents_dirty_) {
		build_entities();
		ents_dirty_ = false;
	}
	build_hover();
	build_preview();
	build_faces();
	build_links();
	build_place_preview();
	compile_poll();
	autosave_tick();
	build_leak();
	build_sun_lines();

	// draw each pane, then back to the active one's view for the tools
	static const float seam[4] = {0.16f, 0.15f, 0.14f, 1.0f};
	if (layout == 4) {
		OzEd_Fill(x, y, w, h, seam);
	}
	for (Pane p : {Pane::Cam, Pane::Top, Pane::Front, Pane::Side}) {
		if (panes_[int(p)].w > 0 && panes_[int(p)].h > 0) {
			draw_pane(p, now);
		}
	}
	use_pane(active_);

	// over the view: the box select's rectangle
	if (drag_ == Drag::Box) {
		int x0 = std::min(press_x_, mx_), x1 = std::max(press_x_, mx_), y0 = std::min(press_y_, my_), y1 = std::max(press_y_, my_);
		static const float fill[4] = {1.0f, 0.42f, 0.18f, 0.10f}, edge[4] = {1.0f, 0.47f, 0.2f, 0.9f};
		OzEd_Fill(x0, y0, x1 - x0, y1 - y0, fill);
		OzEd_Fill(x0, y0, x1 - x0, 1, edge);
		OzEd_Fill(x0, y1 - 1, x1 - x0, 1, edge);
		OzEd_Fill(x0, y0, 1, y1 - y0, edge);
		OzEd_Fill(x1 - 1, y0, 1, y1 - y0, edge);
	}
}

void Session::draw_pane(Pane p, int now) {
	use_pane(p);
	const Rect &r = panes_[int(p)];
	static const float back[4] = {0.012f, 0.010f, 0.011f, 1.0f}, back2d[4] = {0.030f, 0.027f, 0.026f, 1.0f};
	OzEd_Fill(r.x, r.y, r.w, r.h, p == Pane::Cam ? back : back2d);
	OzEd_ClearScene();
	static const float zero[3] = {0, 0, 0};
	static const float ident[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	float moved[3] = {0, 0, 0};
	if (drag_ == Drag::Move) {
		moved[0] = float(delta_.x), moved[1] = float(delta_.y), moved[2] = float(delta_.z);
	}
	Overlay &handles = pane_handles_[int(p)];
	build_handles(handles);  // sized for this pane
	if (p == Pane::Cam) {
		world_.draw(zero, ident);
		chosen_.draw(moved, ident);
		if (grid_mesh_) {
			OzEd_AddModel(grid_mesh_, zero, ident, 0, nullptr);
		}
	} else {
		// lines on the pane's grid, the grid first, the selection last (they all draw through each other,
		// one look, in the order they're added)
		build_grid2d(p);
		grid2d_[int(p) - 1].draw(zero, ident);
		wire_.draw(zero, ident);
		wire_sel_.draw(moved, ident);
	}
	draw_models(moved, now);
	ents_.draw(zero, ident);
	ents_sel_.draw(moved, ident);
	faces_lines_.draw(zero, ident);
	links_.draw(zero, ident);
	place_preview_.draw(zero, ident);
	leak_lines_.draw(zero, ident);
	sun_lines_.draw(zero, ident);
	hover_lines_.draw(zero, ident);
	preview_.draw(zero, ident);
	handles.draw(zero, ident);
	float org[3] = {float(eye_.x), float(eye_.y), float(eye_.z)};
	float axis[3][3] = {{float(fwd_.x), float(fwd_.y), float(fwd_.z)}, {float(left_.x), float(left_.y), float(left_.z)},
		{float(up_.x), float(up_.y), float(up_.z)}};
	if (p == Pane::Cam) {
		const double fy = std::atan(tan_y_) * 2 * 180 / PI;
		OzEd_RenderScene(r.x, r.y, r.w, r.h, org, axis, float(cam.fov), float(fy), now, 0);
	} else {
		OzEd_RenderScene(r.x, r.y, r.w, r.h, org, axis, float(half_w_ * 2), float(half_h_ * 2), now, 1);
	}
}

void Session::look(int dx, int dy) {
	if (!looking_) {
		return;
	}
	cam.yaw -= dx * 0.12;
	cam.pitch = std::clamp(cam.pitch + dy * 0.12, -89.0, 89.0);
}

// ---------------------------------------------------------------- the view's geometry

void Session::ray(double sx, double sy, Vec3 &o, Vec3 &d) const {
	// the renderer's projection: x across the view right, y down
	double nx = ((sx - vx_) / vw_) * 2 - 1, ny = ((sy - vy_) / vh_) * 2 - 1;
	if (ortho_axis_ >= 0) {
		// a 2D view: every ray straight ahead, from its spot on the plane far back
		o = eye_ - left_ * (nx * half_w_) - up_ * (ny * half_h_);
		d = fwd_;
		return;
	}
	o = cam.origin;
	d = (fwd_ - left_ * (nx * tan_x_) - up_ * (ny * tan_y_)).normalized();
}

bool Session::project(const Vec3 &p, double &sx, double &sy) const {
	if (ortho_axis_ >= 0) {
		const Vec3 v = p - eye_;
		sx = vx_ + vw_ * 0.5 * (1 - v.dot(left_) / half_w_);
		sy = vy_ + vh_ * 0.5 * (1 - v.dot(up_) / half_h_);
		return v.dot(fwd_) > 0;
	}
	Vec3 v = p - cam.origin;
	double df = v.dot(fwd_);
	if (df < 0.5) {
		return false;
	}
	sx = vx_ + vw_ * 0.5 * (1 - v.dot(left_) / df / tan_x_);
	sy = vy_ + vh_ * 0.5 * (1 - v.dot(up_) / df / tan_y_);
	return true;
}

double Session::units_per_pixel(const Vec3 &at) const {
	if (ortho_axis_ >= 0) {
		return half_w_ * 2 / std::max(1, vw_);
	}
	double depth = std::max(1.0, (at - cam.origin).dot(fwd_));
	return depth * 2 * tan_x_ / std::max(1, vw_);
}

bool Session::plane_point(double sx, double sy, const Vec3 &at, const Vec3 &n, Vec3 &out) const {
	Vec3 o, d;
	ray(sx, sy, o, d);
	double denom = d.dot(n);
	if (std::fabs(denom) < 1e-6) {
		return false;
	}
	double t = (at - o).dot(n) / denom;
	if (t <= 0 || t > (ortho_axis_ >= 0 ? 4.0 * OZ_ED_FAR : 65536.0)) {
		return false;
	}
	out = o + d * t;
	return true;
}

// ---------------------------------------------------------------- reports

std::string Session::render_stats() const {
	char buf[160];
	int fighters = 0;
	for (const EntModel &m : models_) {
		fighters += !m.fighter.empty();
	}
	std::snprintf(buf, sizeof buf, "%d meshes, %d triangles, %d models, %d fighters", world_.meshes() + chosen_.meshes(), world_.triangles() + chosen_.triangles(),
		int(models_.size()) - fighters, fighters);
	return buf;
}

bool Session::renderer_ok() const {
	return OzEd_Renderer() != nullptr;
}

static std::string num(double v) {
	char buf[32];
	if (std::fabs(v - std::round(v)) < 1e-6) {
		std::snprintf(buf, sizeof buf, "%.0f", v);
	} else {
		std::snprintf(buf, sizeof buf, "%.2f", v);
	}
	return buf;
}

std::string Session::status() const {
	char buf[320];
	int brushes = 0, patches = 0, entities = 0;
	for (Id id : doc.ids()) {
		switch (doc.get(id)->kind) {
		case Kind::Brush: brushes++; break;
		case Kind::Patch: patches++; break;
		case Kind::Entity: entities++; break;
		}
	}
	std::string picked;
	if (tool == Tool::Faces && !faces.empty()) {
		picked = "Picked: " + std::to_string(faces.size()) + (faces.size() == 1 ? " face" : " faces") + " \xc2\xb7 ";
	} else if (!sel.empty()) {
		int b = 0, p = 0, e = 0;
		for (Id id : sel) {
			const Object *o = doc.get(id);
			if (o) {
				(o->kind == Kind::Brush ? b : o->kind == Kind::Patch ? p : e)++;
			}
		}
		Bounds bb = selection_bounds();
		std::string what;
		auto add = [&](int n, const char *one, const char *many) {
			if (n) {
				what += (what.empty() ? "" : ", ") + std::to_string(n) + " " + (n == 1 ? one : many);
			}
		};
		add(b, "brush", "brushes");
		add(p, "curve", "curves");
		add(e, "entity", "entities");
		if (!what.empty()) {  // (a selection of what was just deleted is pruned at the next frame)
			picked = "Selected: " + what;
			if (!bb.empty()) {
				Vec3 s = bb.size();
				picked += " (" + num(s.x) + " x " + num(s.y) + " x " + num(s.z) + ")";
			}
			picked += " \xc2\xb7 ";
		}
	}
	const std::string comp = compile_status();
	if (!comp.empty()) {
		picked = comp + " \xc2\xb7 " + picked;
	}
	if (lit_) {
		const int n = changes_since_compile();
		picked = "Lit preview of the last compile" + (n ? ", " + std::to_string(n) + (n == 1 ? " change since" : " changes since") : std::string()) +
			" \xc2\xb7 " + picked;
	}
	std::string shown;
	if (!hidden.empty()) {
		shown += std::to_string(hidden.size()) + " hidden \xc2\xb7 ";
	}
	if (!locked.empty()) {
		shown += std::to_string(locked.size()) + " locked \xc2\xb7 ";
	}
	if (filters) {
		int n = 0;
		for (unsigned f = filters; f; f &= f - 1) {
			n++;
		}
		shown += std::to_string(n) + (n == 1 ? " filter \xc2\xb7 " : " filters \xc2\xb7 ");
	}
	std::snprintf(buf, sizeof buf, "%s%d brushes \xc2\xb7 %d curves \xc2\xb7 %d entities \xc2\xb7 %sgrid %s \xc2\xb7 texture lock %s",
			picked.c_str(), brushes, patches, entities, shown.c_str(), num(grid).c_str(), texture_lock ? "on" : "off");
	return buf;
}

}  // namespace oz::ed
