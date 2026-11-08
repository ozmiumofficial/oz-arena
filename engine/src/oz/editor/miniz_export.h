/*
Oz: the header miniz's own CMake makes (GenerateExportHeader). miniz's four .c files are compiled straight
into the map editor (engine/quake3e/CMakeLists.txt, engine/tests/editor_core_test.py), statically: nothing
is exported.
*/
#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H
#define MINIZ_EXPORT
#endif
