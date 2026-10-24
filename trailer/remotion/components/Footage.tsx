import { AbsoluteFill, OffthreadVideo, staticFile } from "remotion";
import { ClipRef, FPS, clipStart, media } from "../lib/timeline";

/** A captured clip filling the frame, from its start (clipStart) on, at `rate`. */
export const Footage: React.FC<{ clip: ClipRef; style?: React.CSSProperties }> = ({ clip, style }) => (
  <AbsoluteFill style={style}>
    <OffthreadVideo
      src={staticFile(media(clip.src))}
      trimBefore={Math.max(0, Math.round(clipStart(clip) * FPS))}
      playbackRate={clip.rate ?? 1}
      muted
      style={{ width: "100%", height: "100%", objectFit: "cover" }}
    />
  </AbsoluteFill>
);
