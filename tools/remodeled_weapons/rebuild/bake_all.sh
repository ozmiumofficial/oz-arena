#!/bin/bash
# helpers -> masks -> pbr -> pack, for the rebuilt rocket launcher (run from v3-rebuild)
set -e
B="/c/Program Files/Blender Foundation/Blender 5.2/blender.exe"
T=/c/Users/Tony/Documents/Q3Remaster-experimental/tools/remodeled_weapons/rebuild
D="C:/Users/Tony/Documents/Q3Remaster/build/rocket-launcher-pbr/v3-rebuild/$1"
export PYTHONPATH=/c/Users/Tony/Documents/Q3Remaster/build/railgun-pbr/pylib
mkdir -p "$1"
"$B" -b rocket_launcher_rebuild.blend --python $T/rl_bake.py -- "$D" helpers 4096 > "$1/helpers.log" 2>&1
grep -q "bake done helpers" "$1/helpers.log" || { echo "helpers bake failed"; tail -20 "$1/helpers.log"; exit 1; }
python $T/rl_post.py "$D"
"$B" -b rocket_launcher_rebuild.blend --python $T/rl_bake.py -- "$D" pbr 4096 > "$1/pbr.log" 2>&1
grep -q "bake done pbr" "$1/pbr.log" || { echo "pbr bake failed"; tail -20 "$1/pbr.log"; exit 1; }
python $T/rl_post.py "$D" pack "C:/Users/Tony/Documents/Q3Remaster/build/rocket-launcher-pbr/v3-rebuild/textures"
echo ALL DONE
