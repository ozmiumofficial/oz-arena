import { AbsoluteFill, Audio, Sequence, staticFile } from "remotion";
import { CompareLine } from "./components/CompareLine";
import { EndCard } from "./components/EndCard";
import { Footage } from "./components/Footage";
import { Logo } from "./components/Logo";
import { Montage } from "./components/Montage";
import { Opener } from "./components/Opener";
import { Placeholder } from "./components/Placeholder";
import { TitleOverlay } from "./components/TitleOverlay";
import "./lib/fonts";
import { Segment, barFrame, edit, media } from "./lib/timeline";
import { color } from "./lib/theme";

function Picture({ s, start, length }: { s: Segment; start: number; length: number }) {
  switch (s.kind) {
    case "opener":
      return s.retail ? <Opener clip={s.retail} /> : null;
    case "compare":
      return s.retail && s.remaster ? <CompareLine retail={s.retail} remaster={s.remaster} dir={s.dir} widen={s.widen} map={s.map} /> : null;
    case "clip":
      return s.clip ? <Footage clip={s.clip} style={s.blur ? { filter: `blur(${s.blur}px) brightness(0.6)` } : undefined} /> : null;
    case "logo":
      return <Logo clip={s.clip} blur={s.blur} sub={s.note} />;
    case "montage":
      return <Montage clips={s.clips ?? []} beatsEach={s.beatsEach ?? 2} startFrame={start} length={length} />;
    case "placeholder":
      return <Placeholder label={s.label ?? s.id} note={s.note} />;
    case "end": {
      const bars = (b: number) => barFrame(s.from as number + b) - start;
      // DOWNLOAD NOW once the crane is up in the black sky, the credits on the outro
      return <EndCard clip={s.clip} downloadAt={bars(6)} creditsAt={bars(10)} />;
    }
    default:
      return <AbsoluteFill style={{ backgroundColor: color.black }} />;
  }
}

/** The whole trailer: the edit file's segments in order over the track, plus the announcer lines. */
export const Trailer: React.FC = () => (
  <AbsoluteFill style={{ backgroundColor: color.black }}>
    {edit.segments.map((s) => {
      const start = barFrame(s.from);
      const length = barFrame(s.to) - start;
      if (length <= 0) return null;
      const t = s.title;
      return (
        <Sequence key={s.id} from={start} durationInFrames={length} name={s.id}>
          <Picture s={s} start={start} length={length} />
          {t && (
            <TitleOverlay
              title={t}
              startFrame={t.from !== undefined ? barFrame(s.from + t.from) - start : 0}
              endFrame={t.to !== undefined ? barFrame(s.from + t.to) - start : length}
            />
          )}
        </Sequence>
      );
    })}
    {edit.overlays.map((o, i) => {
      const start = barFrame(o.from);
      const length = barFrame(o.to) - start;
      return (
        <Sequence key={`overlay-${i}`} from={start} durationInFrames={length} name={`title ${o.title.text}`}>
          <TitleOverlay title={o.title} startFrame={0} endFrame={length} />
        </Sequence>
      );
    })}
    <Audio src={staticFile(media(edit.music.file))} volume={edit.music.volume} />
    {edit.sounds.map((snd, i) => (
      <Sequence key={`sound-${i}`} from={barFrame(snd.at)} name={`sound ${snd.file}`}>
        <Audio src={staticFile(media(`sound/${snd.file}`))} volume={snd.volume ?? 1} />
      </Sequence>
    ))}
  </AbsoluteFill>
);
