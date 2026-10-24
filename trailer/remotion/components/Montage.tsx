import { AbsoluteFill, Sequence, interpolate, useCurrentFrame } from "remotion";
import { ClipRef, beatFramesFrom } from "../lib/timeline";
import { Footage } from "./Footage";

/**
 * Fast cuts on the beat: one clip per `beatsEach` beats, in order, looping the list. `startFrame`
 * is the segment's place in the trailer, so the cuts land on the track's real beats.
 */
export const Montage: React.FC<{ clips: ClipRef[]; beatsEach: number; startFrame: number; length: number }> = ({ clips, beatsEach, startFrame, length }) => {
  const frame = useCurrentFrame();
  const beats = beatFramesFrom(startFrame).map((f) => f - startFrame);
  const cuts = [0, ...beats.filter((b, i) => i % beatsEach === 0 && b > 0 && b < length), length];
  const uniq = cuts.filter((c, i) => i === 0 || c > cuts[i - 1]);
  return (
    <AbsoluteFill style={{ backgroundColor: "black" }}>
      {uniq.slice(0, -1).map((at, i) => {
        const len = uniq[i + 1] - at;
        const flash = interpolate(frame - at, [0, 3], [0.35, 0], { extrapolateLeft: "clamp", extrapolateRight: "clamp" });
        return (
          <Sequence key={i} from={at} durationInFrames={len}>
            <Footage clip={clips[i % clips.length]} />
            <AbsoluteFill style={{ backgroundColor: `rgba(255,240,225,${flash})` }} />
          </Sequence>
        );
      })}
    </AbsoluteFill>
  );
};
