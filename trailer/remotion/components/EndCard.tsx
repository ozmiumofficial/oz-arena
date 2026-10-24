import { AbsoluteFill, Easing, interpolate, useCurrentFrame, useVideoConfig } from "remotion";
import { ClipRef } from "../lib/timeline";
import { color, font } from "../lib/theme";
import { Footage } from "./Footage";

// No links: a video can't be clicked or copied from (Ozmium, 2026-10-23); they go in the description.

/**
 * The close: the final crane rising into the black sky, then the logo and DOWNLOAD NOW over it,
 * then the credit card (disclaimer, music) at `creditsAt` frames in.
 */
export const EndCard: React.FC<{ clip?: ClipRef; downloadAt: number; creditsAt: number }> = ({ clip, downloadAt, creditsAt }) => {
  const frame = useCurrentFrame();
  const { durationInFrames } = useVideoConfig();
  const dl = interpolate(frame, [downloadAt, downloadAt + 14], [0, 1], { extrapolateLeft: "clamp", extrapolateRight: "clamp", easing: Easing.out(Easing.cubic) });
  const credits = interpolate(frame, [creditsAt, creditsAt + 20], [0, 1], { extrapolateLeft: "clamp", extrapolateRight: "clamp" });
  const fadeOut = interpolate(frame, [durationInFrames - 45, durationInFrames], [1, 0], { extrapolateLeft: "clamp" });
  const dim = interpolate(frame, [creditsAt, creditsAt + 20], [1, 0.25], { extrapolateLeft: "clamp", extrapolateRight: "clamp" });
  return (
    <AbsoluteFill style={{ backgroundColor: color.black, opacity: fadeOut }}>
      {clip && (
        <AbsoluteFill style={{ opacity: dim }}>
          <Footage clip={clip} />
        </AbsoluteFill>
      )}
      <AbsoluteFill style={{ background: "radial-gradient(ellipse at center, rgba(0,0,0,0.8) 0%, rgba(0,0,0,0.35) 60%, transparent 85%)", opacity: dl }} />
      <AbsoluteFill style={{ justifyContent: "center", alignItems: "center", textAlign: "center", opacity: dl * (1 - credits) }}>
        <div style={{ fontFamily: font.head, fontWeight: font.headWeight, fontSize: 104, color: color.inkHot, textShadow: `0 0 34px ${color.red}` }}>QUAKE III ARENA</div>
        <div style={{ fontFamily: font.body, fontWeight: 700, fontSize: 48, color: color.glow, letterSpacing: "0.26em" }}>REMASTERED</div>
        <div style={{ marginTop: 56, fontFamily: font.head, fontWeight: 700, fontSize: 112, lineHeight: 1.05, letterSpacing: "0.08em", color: color.inkHot,
          padding: "14px 54px", border: `4px solid ${color.glow}`, boxShadow: `0 0 34px ${color.red}aa`, transform: `scale(${1.15 - 0.15 * dl})` }}>
          DOWNLOAD NOW
        </div>
        <div style={{ marginTop: 26, fontFamily: font.body, fontWeight: 500, fontSize: 28, color: color.ink }}>*Requires Quake III Arena</div>
      </AbsoluteFill>
      <AbsoluteFill style={{ justifyContent: "center", alignItems: "center", textAlign: "center", opacity: credits, fontFamily: font.body }}>
        <div style={{ fontSize: 30, fontWeight: 500, color: color.ink, maxWidth: 1300, lineHeight: 1.5 }}>
          Fan project, not affiliated with or endorsed by id Software, ZeniMax or Microsoft.
        </div>
        <div style={{ fontSize: 28, fontWeight: 500, color: color.dim, marginTop: 30 }}>
          Music: "Data Destruction" by Skyth x Boom Kitty
        </div>
      </AbsoluteFill>
    </AbsoluteFill>
  );
};
