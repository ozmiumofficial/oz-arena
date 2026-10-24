"""The trailer track's beat map (TRAILER_PLAN.md, "The editor"): tempo, beats, bar starts, sections,
loudness per bar and the drop, so the edit's cuts land on the music.

python tools/trailer/beats.py [<audio file>] [--out work/trailer/music/beats.json]

Default audio: the one file in work/trailer/music. Writes beats.json (times in seconds) and
beats.png, the loudness per bar with the sections and the drop marked, for choosing where the
trailer's acts sit. The drop is the bar where the loudness jumps most against the bars before it.
"""
import os, sys, glob, json, argparse, subprocess, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import footage

MUSIC = os.path.join(ROOT, "work", "trailer", "music")

ap = argparse.ArgumentParser()
ap.add_argument("audio", nargs="?")
ap.add_argument("--out", default=os.path.join(MUSIC, "beats.json"))
ap.add_argument("--sections", type=int, default=9, help="how many sections to split the track into")
a = ap.parse_args()


def load(path):
    """Mono 22.05 kHz samples, decoded by ffmpeg (librosa's own mp3 support varies)."""
    import librosa
    ff = footage.find_ffmpeg()
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "track.wav")
        subprocess.run([ff, "-y", "-loglevel", "error", "-i", path, "-ac", "1", "-ar", "22050", wav], check=True)
        y, sr = librosa.load(wav, sr=22050)
    return y, sr


def main():
    import librosa
    audio = a.audio or next((f for f in sorted(glob.glob(os.path.join(MUSIC, "*")))
                             if os.path.splitext(f)[1].lower() in (".mp3", ".wav", ".ogg", ".flac", ".m4a")), None)
    if not audio:
        sys.exit("beats: no audio file (pass one, or put it in work/trailer/music)")
    y, sr = load(audio)
    hop = 512
    onset = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    tempo, beat_frames = librosa.beat.beat_track(onset_envelope=onset, sr=sr, hop_length=hop)
    tempo = float(np.atleast_1d(tempo)[0])
    beats = librosa.frames_to_time(beat_frames, sr=sr, hop_length=hop)

    # bar starts: the phase of every fourth beat with the strongest onsets
    strength = onset[beat_frames]
    phase = int(np.argmax([strength[p::4].mean() for p in range(4)]))
    bars = beats[phase::4]

    # loudness per bar (RMS in dB)
    rms = librosa.feature.rms(y=y, hop_length=hop)[0]
    t_rms = librosa.frames_to_time(np.arange(len(rms)), sr=sr, hop_length=hop)
    bar_db = []
    for i, t0 in enumerate(bars):
        t1 = bars[i + 1] if i + 1 < len(bars) else t0 + 4 * 60.0 / tempo
        m = (t_rms >= t0) & (t_rms < t1)
        bar_db.append(float(librosa.amplitude_to_db(np.array([rms[m].mean() if m.any() else 0.0]), ref=np.max(rms))[0]))
    bar_db = np.array(bar_db)

    # the drop: the bar where the next 4 bars are loudest against the 4 before (after the intro)
    jump = np.full(len(bars), -np.inf)
    for i in range(4, len(bars) - 4):
        if bars[i] > 15.0:
            jump[i] = bar_db[i:i + 4].mean() - bar_db[i - 4:i].mean()
    drop = int(np.argmax(jump))

    # sections: agglomerative segmentation of beat-synced timbre and harmony, snapped to bars
    chroma = librosa.feature.chroma_cqt(y=y, sr=sr, hop_length=hop)
    mfcc = librosa.feature.mfcc(y=y, sr=sr, n_mfcc=13, hop_length=hop)
    feats = librosa.util.sync(np.vstack([librosa.util.normalize(chroma), librosa.util.normalize(mfcc)]), beat_frames)
    bounds = librosa.segment.agglomerative(feats, a.sections)
    b_times = sorted({float(bars[np.argmin(np.abs(bars - beats[min(b, len(beats) - 1)]))]) for b in bounds} | {float(bars[drop])})
    duration = float(len(y) / sr)
    sections = []
    for i, t0 in enumerate(b_times):
        t1 = b_times[i + 1] if i + 1 < len(b_times) else duration
        m = (bars >= t0) & (bars < t1)
        sections.append({"start": round(t0, 3), "end": round(t1, 3), "bars": int(m.sum()),
                         "loudness_db": round(float(bar_db[m].mean()) if m.any() else -60.0, 1)})

    out = {"audio": os.path.basename(audio), "duration": round(duration, 3), "tempo": round(tempo, 2),
           "beats": [round(float(t), 3) for t in beats], "bars": [round(float(t), 3) for t in bars],
           "bar_loudness_db": [round(float(v), 1) for v in bar_db],
           "drop": round(float(bars[drop]), 3), "sections": sections}
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    json.dump(out, open(a.out, "w"), indent=1)

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(16, 4), dpi=100)
    ax.bar(bars, bar_db + 60, width=np.diff(np.append(bars, duration)) * 0.9, align="edge", bottom=-60, color="#b3170a")
    for s in sections:
        ax.axvline(s["start"], color="#888", lw=1)
        ax.text(s["start"] + 0.5, 1, "%.1f" % s["start"], color="#444", fontsize=8)
    ax.axvline(out["drop"], color="#ff4a1c", lw=2)
    ax.text(out["drop"] + 0.5, -5, "drop %.2f s" % out["drop"], color="#ff4a1c", fontsize=10)
    ax.set_xlim(0, duration)
    ax.set_xlabel("seconds")
    ax.set_ylabel("loudness per bar (dB)")
    ax.set_title("%s: %.1f BPM, %d bars" % (out["audio"], tempo, len(bars)))
    fig.savefig(os.path.splitext(a.out)[0] + ".png", bbox_inches="tight")
    print("beats: %.2f BPM, %d beats, %d bars, drop at %.2f s, %d sections -> %s"
          % (tempo, len(beats), len(bars), out["drop"], len(sections), a.out))
    for s in sections:
        print("  %6.2f-%6.2f s  %2d bars  %5.1f dB" % (s["start"], s["end"], s["bars"], s["loudness_db"]))


main()
