#include "ed_mapfile.h"
#include "ed_texmap.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace oz::ed {

// ---------------------------------------------------------------- numbers

std::string number(double v) {
	if (v == 0) {
		return "0";  // -0 too
	}
	char buf[64];
	auto r = std::to_chars(buf, buf + sizeof buf, v);
	return std::string(buf, r.ptr);
}

// ---------------------------------------------------------------- reading

namespace {

// Quoted strings, the bracket characters as tokens, // comments skipped.
struct Tokens {
	std::string_view s;
	size_t i = 0;
	int line = 1;
	std::string error;

	std::string tok;
	bool quoted = false;
	bool newline = false;  // a line break came before the token

	bool have_peek = false;
	std::string ptok;
	bool pquoted = false, pnewline = false;
	int pline = 0;

	explicit Tokens(std::string_view text) : s(text) {}

	void fail(const std::string &what) {
		if (error.empty()) {
			error = "line " + std::to_string(line) + ": " + what;
		}
	}

	bool more() {
		if (have_peek) {
			return true;
		}
		bool nl = false;
		skip_space(nl);
		return i < s.size();
	}

	void skip_space(bool &nl) {
		while (i < s.size()) {
			char c = s[i];
			if (c == '\n') {
				line++, i++, nl = true;
			} else if (c == ' ' || c == '\t' || c == '\r') {
				i++;
			} else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
				while (i < s.size() && s[i] != '\n') {
					i++;
				}
			} else {
				break;
			}
		}
	}

	void read(std::string &out, bool &q, bool &nl) {
		nl = false;
		skip_space(nl);
		q = false;
		out.clear();
		if (i >= s.size()) {
			return;
		}
		char c = s[i];
		if (c == '"') {
			size_t j = s.find('"', i + 1);
			if (j == std::string_view::npos) {
				j = s.size();
			}
			out.assign(s.substr(i + 1, j - i - 1));
			for (char k : out) {
				line += k == '\n';
			}
			i = j + 1;
			q = true;
			return;
		}
		if (c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']') {
			i++;
			out.assign(1, c);
			return;
		}
		size_t start = i;
		while (i < s.size()) {
			char k = s[i];
			if (k == ' ' || k == '\t' || k == '\r' || k == '\n' || k == '(' || k == ')' || k == '{' || k == '}' ||
					k == '[' || k == ']' || k == '"') {
				break;
			}
			i++;
		}
		out.assign(s.substr(start, i - start));
	}

	const std::string &next() {
		if (have_peek) {
			have_peek = false;
			tok.swap(ptok);
			quoted = pquoted, newline = pnewline;
			return tok;
		}
		read(tok, quoted, newline);
		return tok;
	}

	const std::string &peek() {
		if (!have_peek) {
			int l = line;
			read(ptok, pquoted, pnewline);
			pline = l;
			have_peek = true;
		}
		return ptok;
	}

	void expect(const char *t) {
		const std::string &got = next();
		if (got != t) {
			fail(std::string("expected ") + t + ", found " + (got.empty() ? "the end of the file" : got));
		}
	}

	double num() {
		const std::string &t = next();
		char *end = nullptr;
		double v = std::strtod(t.c_str(), &end);
		if (t.empty() || end != t.c_str() + t.size()) {
			fail("expected a number, found " + (t.empty() ? std::string("the end of the file") : t));
			return 0;
		}
		return v;
	}

	Vec3 vec3_rest() {
		Vec3 v;
		v.x = num(), v.y = num(), v.z = num();
		expect(")");
		return v;
	}

	// whether a number follows on the same line (a face's optional contents, flags and value)
	bool number_on_line() {
		const std::string &t = peek();
		if (pnewline || t.empty() || pquoted) {
			return false;
		}
		char c = t[0];
		return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
	}

	void skip_block() {
		// past the matching } of a block whose { was read
		int depth = 1;
		while (more() && depth > 0) {
			const std::string &t = next();
			if (!quoted) {
				depth += t == "{" ? 1 : t == "}" ? -1 : 0;
			}
		}
	}
};

std::shared_ptr<Object> read_patch(Tokens &tk, bool def3) {
	auto o = std::make_shared<Object>();
	o->kind = Kind::Patch;
	Patch &p = o->patch;
	p.def3 = def3;
	tk.expect("{");
	p.shader = shader_id(tk.next());
	tk.expect("(");
	int n = def3 ? 7 : 5;
	for (int k = 0; k < n; k++) {
		p.info[k] = tk.num();
	}
	tk.expect(")");
	int w = int(p.info[0]), h = int(p.info[1]);
	if (w < 1 || h < 1 || w > 1024 || h > 1024) {
		tk.fail("a curve's size is " + std::to_string(w) + " x " + std::to_string(h));
		return nullptr;
	}
	p.width = w, p.height = h;
	p.ctrl.assign(size_t(w) * h * 5, 0.0);
	tk.expect("(");
	// q3map's ParsePatch: columns of `height` points
	for (int col = 0; col < w && tk.error.empty(); col++) {
		tk.expect("(");
		for (int row = 0; row < h && tk.error.empty(); row++) {
			tk.expect("(");
			for (int k = 0; k < 5; k++) {
				p.ctrl[(size_t(row) * w + col) * 5 + k] = tk.num();
			}
			tk.expect(")");
		}
		tk.expect(")");
	}
	tk.expect(")");
	tk.expect("}");
	tk.expect("}");
	return o;
}

std::shared_ptr<Object> read_brush(Tokens &tk) {
	auto o = std::make_shared<Object>();
	o->kind = Kind::Brush;
	bool primit = false;
	if (tk.peek() == "brushDef") {
		tk.next();
		tk.expect("{");
		primit = true;
	}
	while (tk.more() && tk.error.empty()) {
		const std::string &t = tk.next();
		if (t == "}") {
			break;
		}
		if (t != "(") {
			tk.fail("a brush face starts with (, found " + t);
			return nullptr;
		}
		Face f;
		f.pts[0] = tk.vec3_rest();
		tk.expect("(");
		f.pts[1] = tk.vec3_rest();
		tk.expect("(");
		f.pts[2] = tk.vec3_rest();
		f.pts_valid = true;
		f.plane = plane_from_points(f.pts[0], f.pts[1], f.pts[2]);
		if (primit) {
			f.form = TexForm::Primitives;
			tk.expect("(");
			tk.expect("(");
			f.raw[0] = tk.num(), f.raw[1] = tk.num(), f.raw[2] = tk.num();
			tk.expect(")");
			tk.expect("(");
			f.raw[3] = tk.num(), f.raw[4] = tk.num(), f.raw[5] = tk.num();
			tk.expect(")");
			tk.expect(")");
			f.shader = shader_id(tk.next());
		} else {
			f.shader = shader_id(tk.next());
			if (tk.peek() == "[") {
				f.form = TexForm::Valve;
				tk.next();
				for (int k = 0; k < 4; k++) {
					f.raw[k] = tk.num();
				}
				tk.expect("]");
				tk.expect("[");
				for (int k = 4; k < 8; k++) {
					f.raw[k] = tk.num();
				}
				tk.expect("]");
				for (int k = 8; k < 11; k++) {
					f.raw[k] = tk.num();
				}
			} else {
				f.form = TexForm::Quake;
				for (int k = 0; k < 5; k++) {
					f.raw[k] = tk.num();
				}
			}
		}
		if (tk.number_on_line()) {
			f.has_flags = true;
			f.contents = int32_t(tk.num());
			f.flags = int32_t(tk.num());
			f.value = int32_t(tk.num());
		}
		o->brush.faces.push_back(f);
	}
	if (primit) {
		tk.expect("}");
	}
	return o;
}

}  // namespace

static bool is_origin_shader(ShaderId s) {
	return shader_name(s) == "common/origin" || shader_name(s) == "textures/common/origin";
}

// Doc::model_origin for an entity still being read: its brushes aren't in the document yet.
static Vec3 origin_of(const Entity &e, const std::vector<std::shared_ptr<Object>> &prims) {
	std::string cn = e.classname();
	if (cn == "worldspawn" || cn == "func_group" || cn == "group_info") {
		return Vec3();
	}
	for (const auto &o : prims) {
		if (o->kind != Kind::Brush) {
			continue;
		}
		bool origin = false;
		for (const Face &f : o->brush.faces) {
			origin |= is_origin_shader(f.shader);
		}
		if (!origin) {
			continue;
		}
		std::vector<Plane> planes;
		for (const Face &f : o->brush.faces) {
			planes.push_back(f.plane);
		}
		Vec3 lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
		for (const q3map::Winding &w : q3map::brush_windings(planes)) {
			for (const Vec3 &p : w) {
				lo = Vec3(std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z));
				hi = Vec3(std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z));
			}
		}
		if (lo.x <= hi.x) {
			return Vec3(std::trunc((lo.x + hi.x) * 0.5), std::trunc((lo.y + hi.y) * 0.5), std::trunc((lo.z + hi.z) * 0.5));
		}
	}
	Vec3 v;
	const std::string *org = e.find("origin");
	if (org && std::sscanf(org->c_str(), "%lf %lf %lf", &v.x, &v.y, &v.z) == 3) {
		return v;
	}
	return Vec3();
}

bool read_map(std::string_view text, Doc &doc, ReadReport &report) {
	report = ReadReport{};
	Tokens tk(text);
	struct Pending {
		std::shared_ptr<Object> entity;
		std::vector<std::shared_ptr<Object>> prims;
	};
	std::vector<Pending> ents;
	bool valve = false;
	while (tk.more() && tk.error.empty()) {
		if (tk.next() != "{") {
			tk.fail("expected { to start an entity, found " + tk.tok);
			break;
		}
		Pending pe;
		pe.entity = std::make_shared<Object>();
		pe.entity->kind = Kind::Entity;
		while (tk.error.empty()) {
			if (!tk.more()) {
				tk.fail("the file ends inside an entity");
				break;
			}
			std::string t = tk.next();
			if (t == "}" && !tk.quoted) {
				break;
			}
			if (t == "{" && !tk.quoted) {
				const std::string &kind = tk.peek();
				std::shared_ptr<Object> p;
				if (kind == "patchDef2" || kind == "patchDef3") {
					bool def3 = kind == "patchDef3";
					tk.next();
					p = read_patch(tk, def3);
				} else if (kind == "(" || kind == "brushDef") {
					p = read_brush(tk);
				} else {
					tk.next();  // brushDef3, terrainDef ...: not Quake III's
					tk.expect("{");
					tk.skip_block();
					tk.expect("}");
					report.skipped++;
				}
				if (p) {
					for (const Face &f : p->brush.faces) {
						valve |= f.form == TexForm::Valve;
					}
					pe.prims.push_back(std::move(p));
				}
			} else if (tk.quoted) {
				std::string v = tk.next();
				pe.entity->entity.keys.emplace_back(std::move(t), std::move(v));
			} else {
				tk.fail("unexpected " + t);
			}
		}
		if (tk.error.empty()) {
			ents.push_back(std::move(pe));
		}
	}
	if (!tk.error.empty()) {
		// past a token out of place the rest is read out of step: nothing of it can be trusted
		report.error = tk.error;
		return false;
	}
	for (const Pending &pe : ents) {
		const std::string *mv = pe.entity->entity.find("mapversion");
		valve |= mv && *mv == "220";
	}
	doc.plain_form = valve ? TexForm::Valve : TexForm::Quake;
	for (Pending &pe : ents) {
		Vec3 origin = origin_of(pe.entity->entity, pe.prims);
		Id eid = doc.add(pe.entity);
		report.entities++;
		for (auto &p : pe.prims) {
			p->owner = eid;
			for (Face &f : p->brush.faces) {
				doc.derive_st(f, origin);
			}
			doc.add(p);
			(p->kind == Kind::Brush ? report.brushes : report.patches)++;
		}
	}
	doc.clear_history();
	doc.dirty = false;
	return true;
}

// ---------------------------------------------------------------- writing

namespace {

struct Out {
	std::string s;
	void raw(const char *t) {
		s += t;
	}
	void raw(const std::string &t) {
		s += t;
	}
	void num(double v) {
		s += number(v);
	}
	void vec(const Vec3 &v) {
		s += "( ";
		num(v.x), s += ' ', num(v.y), s += ' ', num(v.z);
		s += " )";
	}
	void pair(const std::string &k, const std::string &v) {
		// keys and values can't hold quotes (q3map's tokenizer ends at one)
		s += '"';
		for (char c : k) {
			s += c == '"' ? '\'' : c;
		}
		s += "\" \"";
		for (char c : v) {
			s += c == '"' ? '\'' : (c == '\n' || c == '\r') ? ' ' : c;
		}
		s += "\"\n";
	}
};

void write_brush(Out &out, const Doc &doc, Id id, const Object &o, const Vec3 &origin) {
	const std::vector<q3map::Winding> &ws = doc.windings(id);
	const std::vector<Face> &faces = o.brush.faces;
	// brushDef when its faces came that way, or when the file's plain form can't hold a mapping
	std::vector<Face> conv(faces);
	bool bp = false;
	for (const Face &f : faces) {
		bp |= f.raw_valid && f.form == TexForm::Primitives;
	}
	if (!bp) {
		for (size_t i = 0; i < conv.size() && !bp; i++) {
			Face &f = conv[i];
			if (!f.raw_valid || f.form != doc.plain_form) {
				doc.derive_raw(f, doc.plain_form, origin, i < ws.size() ? ws[i] : q3map::Winding{});
				bp |= f.form != doc.plain_form;
			}
		}
	}
	if (bp) {
		for (size_t i = 0; i < conv.size(); i++) {
			if (!(faces[i].raw_valid && faces[i].form == TexForm::Primitives)) {
				conv[i] = faces[i];
				doc.derive_raw(conv[i], TexForm::Primitives, origin, i < ws.size() ? ws[i] : q3map::Winding{});
			}
		}
	}
	out.raw(bp ? "{\nbrushDef\n{\n" : "{\n");
	for (size_t i = 0; i < conv.size(); i++) {
		const Face &f = conv[i];
		Vec3 pts[3];
		doc.plane_points(f, i < ws.size() ? ws[i] : q3map::Winding{}, pts);
		out.vec(pts[0]), out.raw(" "), out.vec(pts[1]), out.raw(" "), out.vec(pts[2]), out.raw(" ");
		const double *r = f.raw;
		if (bp) {
			out.raw("( ( "), out.num(r[0]), out.raw(" "), out.num(r[1]), out.raw(" "), out.num(r[2]);
			out.raw(" ) ( "), out.num(r[3]), out.raw(" "), out.num(r[4]), out.raw(" "), out.num(r[5]), out.raw(" ) ) ");
			out.raw(shader_name(f.shader));
		} else if (f.form == TexForm::Valve) {
			out.raw(shader_name(f.shader));
			out.raw(" [ "), out.num(r[0]), out.raw(" "), out.num(r[1]), out.raw(" "), out.num(r[2]), out.raw(" "), out.num(r[3]);
			out.raw(" ] [ "), out.num(r[4]), out.raw(" "), out.num(r[5]), out.raw(" "), out.num(r[6]), out.raw(" "), out.num(r[7]);
			out.raw(" ] "), out.num(r[8]), out.raw(" "), out.num(r[9]), out.raw(" "), out.num(r[10]);
		} else {
			out.raw(shader_name(f.shader));
			for (int k = 0; k < 5; k++) {
				out.raw(" "), out.num(r[k]);
			}
		}
		if (f.has_flags) {
			out.raw(" " + std::to_string(f.contents) + " " + std::to_string(f.flags) + " " + std::to_string(f.value));
		}
		out.raw("\n");
	}
	out.raw(bp ? "}\n}\n" : "}\n");
}

void write_patch(Out &out, const Object &o) {
	const Patch &p = o.patch;
	out.raw("{\n");
	out.raw(p.def3 ? "patchDef3\n{\n" : "patchDef2\n{\n");
	out.raw(shader_name(p.shader) + "\n( ");
	for (int k = 0; k < (p.def3 ? 7 : 5); k++) {
		out.num(p.info[k]), out.raw(" ");
	}
	out.raw(")\n(\n");
	for (int col = 0; col < p.width; col++) {
		out.raw("( ");
		for (int row = 0; row < p.height; row++) {
			const double *c = &p.ctrl[(size_t(row) * p.width + col) * 5];
			out.raw("( ");
			for (int k = 0; k < 5; k++) {
				out.num(c[k]), out.raw(" ");
			}
			out.raw(") ");
		}
		out.raw(")\n");
	}
	out.raw(")\n}\n}\n");
}

}  // namespace

std::string write_map(const Doc &doc, const WriteOptions &opts) {
	Out out;
	out.s.reserve(1 << 20);
	if (!opts.title.empty()) {
		out.raw("// " + opts.title + "\n");
	}
	std::vector<Id> ents = doc.ids_of(Kind::Entity);
	// worldspawn first: q3map takes the first entity as the world
	Id world = doc.worldspawn();
	if (world) {
		for (size_t i = 0; i < ents.size(); i++) {
			if (ents[i] == world) {
				ents.erase(ents.begin() + i);
				ents.insert(ents.begin(), world);
				break;
			}
		}
	}
	int n = 0;
	for (Id eid : ents) {
		if (opts.only && eid != world && !opts.only(eid)) {
			continue;
		}
		const Object *e = doc.get(eid);
		out.raw("// entity " + std::to_string(n++) + "\n{\n");
		if (opts.only && eid == world) {
			out.pair("classname", "worldspawn");  // a prefab's world holds its brushes, not the map's keys
		} else {
			for (const auto &kv : e->entity.keys) {
				out.pair(kv.first, kv.second);
			}
		}
		Vec3 origin = doc.model_origin(eid);
		int b = 0;
		for (Id pid : doc.owned(eid)) {
			if (opts.only && !opts.only(pid)) {
				continue;
			}
			const Object *o = doc.get(pid);
			out.raw("// brush " + std::to_string(b++) + "\n");
			if (o->kind == Kind::Brush) {
				write_brush(out, doc, pid, *o, origin);
			} else {
				write_patch(out, *o);
			}
		}
		out.raw("}\n");
	}
	return std::move(out.s);
}

}  // namespace oz::ed
