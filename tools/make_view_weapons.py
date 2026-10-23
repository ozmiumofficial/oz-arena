"""Quake 3's first-person weapon placement: every frame of tag_weapon in each
models/weapons2/<w>/<w>_hand.md3. cg_weapons.c CG_AddViewWeapon hangs the weapon on this tag,
and the hand model's frames are its only motion: frame 0 at rest, 1-6 the attack recoil (synced
to TORSO_ATTACK / TORSO_ATTACK2), 6-14 the weapon switch (TORSO_DROP then TORSO_RAISE).

    python tools/make_view_weapons.py <extracted baseq3> <out.json>

Writes {"<w>": [[ox, oy, oz, ax0x, ax0y, ax0z, ax1x, ..., ax2z], ...per frame]} in id view axes
(forward, left, up; units)."""
import json
import os
import struct
import sys

WEAPONS = ["gauntlet", "machinegun", "shotgun", "grenadel", "rocketl", "lightning", "railgun", "plasma", "bfg", "grapple"]


def hand_tags(path):
    b = open(path, "rb").read()
    num_frames, num_tags = struct.unpack_from("<2i", b, 76)
    ofs_tags = struct.unpack_from("<i", b, 96)[0]
    frames = []
    for f in range(num_frames):
        for t in range(num_tags):
            o = ofs_tags + (f * num_tags + t) * 112
            name = b[o:o + 64].split(b"\0")[0].decode("ascii", "replace")
            if name == "tag_weapon":
                frames.append([round(v, 4) for v in struct.unpack_from("<12f", b, o + 64)])
    return frames


def main():
    src, out = sys.argv[1], sys.argv[2]
    data = {}
    for w in WEAPONS:
        p = os.path.join(src, "models", "weapons2", w, w + "_hand.md3")
        if os.path.exists(p):
            data[w] = hand_tags(p)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump(data, open(out, "w"), separators=(",", ":"))
    print("view weapons: %s" % ", ".join("%s %d frames" % (k, len(v)) for k, v in data.items()))


if __name__ == "__main__":
    main()
