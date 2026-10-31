#pragma once
// .map files in and out of the editor's document (ENGINE_PLAN.md P13), losslessly: every form
// q3map2, GtkRadiant, NetRadiant(-custom) and TrenchBroom write for Quake III.
//   Quake      ( p ) ( p ) ( p ) shader shift_s shift_t rotate scale_s scale_t [contents flags value]
//   brushDef   ( p ) ( p ) ( p ) ( ( a b c ) ( d e f ) ) shader [contents flags value]
//   Valve 220  ( p ) ( p ) ( p ) shader [ Ux Uy Uz off ] [ Vx Vy Vz off ] rotate scale_s scale_t [c f v]
//   patchDef2 and patchDef3 curves
// A file read and written unchanged keeps every entity, key (in order), brush, face (those that
// make no polygon too), curve and number; only comments and spacing change.

#include "ed_doc.h"

#include <functional>
#include <string>
#include <string_view>

namespace oz::ed {

struct ReadReport {
	int entities = 0, brushes = 0, patches = 0;
	int skipped = 0;    // primitives of a kind the editor doesn't know (brushDef3, terrainDef ...)
	std::string error;  // "line N: ..." when the file couldn't be read; the document is then empty
};

// Reads text into an empty document (texture sizes from doc.tex_size). No undo history; not dirty.
bool read_map(std::string_view text, Doc &doc, ReadReport &report);

struct WriteOptions {
	std::string title;              // the first comment line
	std::function<bool(Id)> only;   // set: those objects alone, under a bare worldspawn (a prefab)
};
std::string write_map(const Doc &doc, const WriteOptions &opts = {});

// Shortest text that reads back as the same double ("268", "0.015625", "-0.89442719099991586").
std::string number(double v);

}  // namespace oz::ed
