// The map editor's panes (ENGINE_PLAN.md P13, engine/EDITOR.md): the 3D view alone, or four, as in
// Radiant: the 3D view, and the 2D views straight down each axis, top (X across, Y up), front (X
// across, Z up) and side (Y across, Z up). A 2D view is orthographic (the renderer's RDF_OZ_ORTHO), drawn
// as lines on a grid that follows the editor's; each keeps its own place and zoom.
//
// The tools work in the pane the pointer is over (a drag in the one it began in): use_pane() puts that
// pane's rectangle, axes and projection where ray(), project() and units_per_pixel() read them, so the
// select, create, clip and vertex tools work the same in every pane. In a 2D pane a move stays in the
// view's plane, the create tool makes a brush as deep as the selection or the last brush made, the
// clipper cuts along the view's axis, the right or middle button drags the view and the wheel zooms.

#include "ed_view.h"
#include "ed_engine.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include "../ui/oz_ui_engine.h"
}

namespace oz::ed {

static const char *PANE_NAMES[4] = {"3D", "top", "front", "side"};
static const double MIN_SCALE = 1.0 / 16, MAX_SCALE = 64;  // map units a pixel
static const int SEAM = 2;                                  // pixels between the four panes

const char *pane_name(Pane p) {
	return PANE_NAMES[int(p)];
}

bool pane_by_name(const std::string &name, Pane &p) {
	for (int i = 0; i < 4; i++) {
		if (name == PANE_NAMES[i] || (i == 0 && name == "cam")) {
			p = Pane(i);
			return true;
		}
	}
	return false;
}

void Session::lay_out(int x, int y, int w, int h) {
	for (Rect &r : panes_) {
		r = Rect();
	}
	if (layout != 4) {
		panes_[int(solo)] = Rect{x, y, w, h};
	} else {
		const int w0 = (w - SEAM) / 2, h0 = (h - SEAM) / 2;
		panes_[int(Pane::Cam)] = Rect{x, y, w0, h0};
		panes_[int(Pane::Top)] = Rect{x + w0 + SEAM, y, w - w0 - SEAM, h0};
		panes_[int(Pane::Front)] = Rect{x, y + h0 + SEAM, w0, h - h0 - SEAM};
		panes_[int(Pane::Side)] = Rect{x + w0 + SEAM, y + h0 + SEAM, w - w0 - SEAM, h - h0 - SEAM};
	}
	if (panes_[int(active_)].w <= 0) {
		active_ = layout == 4 ? Pane::Cam : solo;  // the pane the pointer was in is gone
	}
}

bool Session::pane_at(int x, int y, Pane &p) const {
	for (int i = 0; i < 4; i++) {
		const Rect &r = panes_[i];
		if (r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) {
			p = Pane(i);
			return true;
		}
	}
	return false;
}

void Session::use_pane(Pane p) {
	const Rect &r = panes_[int(p)];
	vx_ = r.x, vy_ = r.y, vw_ = std::max(1, r.w), vh_ = std::max(1, r.h);
	if (p == Pane::Cam) {
		ortho_axis_ = -1;
		tan_x_ = std::tan(cam.fov * 0.5 * 3.14159265358979323846 / 180);
		tan_y_ = tan_x_ * vh_ / vw_;
		cam.axes(fwd_, left_, up_);
		eye_ = cam.origin;
		return;
	}
	// looking down -Z, along +Y, along -X; left is up x forward, so across is +X, +X, +Y
	static const Vec3 forward[3] = {{0, 0, -1}, {0, 1, 0}, {-1, 0, 0}}, upward[3] = {{0, 1, 0}, {0, 0, 1}, {0, 0, 1}};
	static const int axis[3] = {2, 1, 0};
	const int i = int(p) - 1;
	const View2D &v = views2d[i];
	fwd_ = forward[i], up_ = upward[i], left_ = up_.cross(fwd_);
	ortho_axis_ = axis[i];
	half_w_ = v.scale * vw_ * 0.5, half_h_ = v.scale * vh_ * 0.5;
	eye_ = v.center - fwd_ * double(OZ_ED_FAR);  // its rays start far back, so the whole map is ahead
}

View2D &Session::view2d() {
	return views2d[std::max(0, int(active_) - 1)];
}

void Session::start_pan(int b) {
	drag_ = Drag::Pan;
	press_button_ = b;
	press_x_ = mx_, press_y_ = my_;
	if (ortho_axis_ >= 0) {
		pan_origin_ = view2d().center;
		return;
	}
	pan_origin_ = cam.origin;
	Hit h;
	pan_depth_ = pick(mx_, my_, h) ? std::max(64.0, (h.point - cam.origin).dot(fwd_)) : 512;
}

void Session::pan_to(int x, int y) {
	if (ortho_axis_ >= 0) {
		const double upp = view2d().scale;
		view2d().center = pan_origin_ + left_ * ((x - press_x_) * upp) + up_ * ((y - press_y_) * upp);
		use_pane(active_);
		return;
	}
	const double upp = pan_depth_ * 2 * tan_x_ / std::max(1, vw_);
	cam.origin = pan_origin_ + left_ * ((x - press_x_) * upp) + up_ * ((y - press_y_) * upp);
}

void Session::zoom_2d(int steps) {
	// the map point under the pointer stays there
	Vec3 o, d, o2;
	ray(mx_, my_, o, d);
	View2D &v = view2d();
	v.scale = std::clamp(v.scale * std::pow(0.8, steps), MIN_SCALE, MAX_SCALE);
	use_pane(active_);
	ray(mx_, my_, o2, d);
	v.center = v.center + (o - o2);
	use_pane(active_);
}

void Session::frame_2d(const Bounds &b) {
	if (b.empty()) {
		return;
	}
	if (view_w_ <= 1) {
		frame2d_later_ = b;  // no viewport yet (a map opened before the editor's first frame)
		return;
	}
	// each 2D view's place on the bounds' middle, zoomed so they fit (the size its pane has in the four)
	static const int across[3] = {0, 0, 1}, upward[3] = {1, 2, 2};
	const Vec3 size = b.size();
	const double s[3] = {size.x, size.y, size.z};
	const int w = std::max(64, layout == 4 ? panes_[1].w : view_w_ / 2), h = std::max(64, layout == 4 ? panes_[1].h : view_h_ / 2);
	for (int i = 0; i < 3; i++) {
		views2d[i].center = b.center();
		const double fit = std::max(std::max(s[across[i]], 64.0) / w, std::max(s[upward[i]], 64.0) / h) * 1.15;
		views2d[i].scale = std::clamp(fit, MIN_SCALE, MAX_SCALE);
	}
}

// A 2D pane's grid: lines on the editor's grid (doubled while closer than 6 pixels), every eighth
// brighter, the axes in their colors (X red, Y green, Z blue), covering what the pane shows.
void Session::build_grid2d(Pane p) {
	const int i = int(p) - 1;
	Overlay &g = grid2d_[i];
	g.begin();
	const View2D &v = views2d[i];
	static const int across[3] = {0, 0, 1}, upward[3] = {1, 2, 2};
	static const unsigned char axis_color[3][4] = {{219, 60, 40, 220}, {90, 200, 90, 220}, {80, 130, 255, 220}};
	static const unsigned char minor[4] = {230, 221, 210, 22}, major[4] = {230, 221, 210, 60};
	double step = grid;
	while (step / v.scale < 6) {
		step *= 2;
	}
	const double c[3] = {v.center.x, v.center.y, v.center.z};
	const int a = across[i], u = upward[i];
	const double a0 = c[a] - half_w_, a1 = c[a] + half_w_, u0 = c[u] - half_h_, u1 = c[u] + half_h_;
	auto point = [&](double pa, double pu) {
		double q[3] = {c[0], c[1], c[2]};
		q[a] = pa, q[u] = pu;
		return Vec3(q[0], q[1], q[2]);
	};
	auto shade = [&](double k, int axis_of) {
		const double cells = std::round(k / step);
		return std::fabs(k) < 1e-6 ? axis_color[axis_of] : std::fmod(std::fabs(cells), 8.0) == 0 ? major : minor;
	};
	// the lines across mark the upward axis's values, so the axis line among them is the across axis's
	for (double k = std::floor(u0 / step) * step; k <= u1; k += step) {
		g.line(point(a0, k), point(a1, k), shade(k, a), "$oz_ed_lines_xray");
	}
	for (double k = std::floor(a0 / step) * step; k <= a1; k += step) {
		g.line(point(k, u0), point(k, u1), shade(k, u), "$oz_ed_lines_xray");
	}
	g.end();
}

}  // namespace oz::ed
