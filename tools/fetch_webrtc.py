"""Downloads godot-webrtc-native (MIT, github.com/godotengine/webrtc-native) into godot/addons/webrtc_native.

python tools/fetch_webrtc.py [--version 1.2.2-stable]

Online matches use WebRTC data channels, which Godot only provides through this extension on desktop
(libdatachannel inside). The binaries are not in the repo (godot/addons/webrtc_native/lib is ignored);
this fetches the release zip and keeps the Windows and Linux x86-64 libraries (debug and release), the
.gdextension and the licenses. Needs the `gh` CLI or falls back to a plain download.
"""
import argparse, os, subprocess, sys, tempfile, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(os.path.dirname(HERE), "godot")
KEEP = ("LICENSE", "webrtc_native.gdextension", "windows.template_debug.x86_64", "windows.template_release.x86_64",
        "linux.template_debug.x86_64", "linux.template_release.x86_64")

ap = argparse.ArgumentParser()
ap.add_argument("--version", default="1.2.2-stable")
a = ap.parse_args()

if os.path.exists(os.path.join(DEST, "addons", "webrtc_native", "lib", "libwebrtc_native.windows.template_debug.x86_64.dll")):
    print("webrtc_native is already there")
    sys.exit(0)

with tempfile.TemporaryDirectory() as tmp:
    zpath = os.path.join(tmp, "webrtc.zip")
    url = f"https://github.com/godotengine/webrtc-native/releases/download/{a.version}/godot-extension-webrtc_native.zip"
    print("downloading", url)
    urllib.request.urlretrieve(url, zpath)
    with zipfile.ZipFile(zpath) as z:
        for i in z.infolist():
            if not i.is_dir() and any(k in i.filename for k in KEEP):
                z.extract(i, DEST)
                print(" ", i.filename)
print("done; open the project once so Godot registers the extension")
