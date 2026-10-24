import { AbsoluteFill, Easing, interpolate, useCurrentFrame, useVideoConfig } from "remotion";
import { Title } from "../lib/timeline";
import { color, font } from "../lib/theme";

/**
 * Text over a segment. "card": the headline slams in at full screen (Act 1's promise cards), with
 * an optional second line at the same size, a small line under it, small print at the bottom and a
 * COMING SOON stamp. "feature": a headline and the line that explains it, lower left (Act 2).
 * startFrame / endFrame: where in the segment it shows.
 */
export const TitleOverlay: React.FC<{ title: Title; startFrame: number; endFrame: number }> = ({ title, startFrame, endFrame }) => {
  const frame = useCurrentFrame() - startFrame;
  const { durationInFrames } = useVideoConfig();
  const len = Math.min(endFrame, durationInFrames) - startFrame;
  if (frame < 0 || frame >= len) return null;
  const outIn = interpolate(frame, [len - 8, len], [1, 0], { extrapolateLeft: "clamp", extrapolateRight: "clamp" });

  if (title.style === "feature") {
    const slide = interpolate(frame, [0, 14], [-40, 0], { extrapolateRight: "clamp", easing: Easing.out(Easing.cubic) });
    const show = Math.min(interpolate(frame, [0, 10], [0, 1], { extrapolateRight: "clamp" }), outIn);
    return (
      <AbsoluteFill style={{ justifyContent: "flex-end", padding: "0 0 90px 90px", opacity: show }}>
        <div style={{ transform: `translateX(${slide}px)`, maxWidth: 1100 }}>
          <div style={{ width: 90, height: 5, background: color.red, boxShadow: `0 0 14px ${color.glow}`, marginBottom: 18 }} />
          <div style={{ fontFamily: font.head, fontWeight: font.headWeight, fontSize: 76, color: color.inkHot, lineHeight: 1.0, textShadow: "0 4px 24px rgba(0,0,0,0.95)" }}>
            {title.text}
          </div>
          {title.sub && (
            <div style={{ fontFamily: font.body, fontWeight: 500, fontSize: 32, color: color.ink, marginTop: 14, textShadow: "0 2px 14px rgba(0,0,0,0.95)" }}>
              {title.sub}
            </div>
          )}
        </div>
      </AbsoluteFill>
    );
  }

  // "card": slams in (scale down, blur off), holds, fades
  const slam = interpolate(frame, [0, 9], [1.28, 1], { extrapolateRight: "clamp", easing: Easing.out(Easing.cubic) });
  const blur = interpolate(frame, [0, 9], [14, 0], { extrapolateRight: "clamp" });
  const show = Math.min(interpolate(frame, [0, 5], [0, 1], { extrapolateRight: "clamp" }), outIn);
  const stampAt = 14;
  const stampScale = interpolate(frame, [stampAt, stampAt + 7], [1.9, 1], { extrapolateLeft: "clamp", extrapolateRight: "clamp", easing: Easing.out(Easing.back(1.6)) });
  const stampShow = interpolate(frame, [stampAt, stampAt + 4], [0, 1], { extrapolateLeft: "clamp", extrapolateRight: "clamp" });
  // long headlines shrink to fit the frame (Teko runs about 0.46 em a letter)
  const longest = Math.max(title.text.length, title.second?.length ?? 0);
  const big: React.CSSProperties = {
    fontFamily: font.head, fontWeight: font.headWeight, fontSize: Math.min(150, Math.floor(1680 / (longest * 0.46))), color: color.inkHot, lineHeight: 1.05, letterSpacing: "0.02em",
    textShadow: `0 0 30px ${color.red}88, 0 6px 30px rgba(0,0,0,0.95)`,
  };
  return (
    <AbsoluteFill style={{ opacity: show }}>
      <AbsoluteFill style={{ background: "radial-gradient(ellipse at center, rgba(0,0,0,0.55) 0%, rgba(0,0,0,0.15) 70%)" }} />
      <AbsoluteFill style={{ justifyContent: "center", alignItems: "center", textAlign: "center", padding: "0 120px" }}>
        <div style={{ transform: `scale(${slam})`, filter: `blur(${blur}px)` }}>
          <div style={big}>{title.text}</div>
          {title.second && <div style={big}>{title.second}</div>}
          {title.sub && (
            <div style={{ fontFamily: font.body, fontWeight: 600, fontSize: 34, color: color.ink, letterSpacing: "0.28em", marginTop: 22, textTransform: "uppercase" }}>
              {title.sub}
            </div>
          )}
        </div>
        {title.stamp && (
          <div
            style={{
              marginTop: 34, opacity: stampShow, transform: `rotate(-7deg) scale(${stampScale})`,
              fontFamily: font.body, fontWeight: 700, fontSize: 46, letterSpacing: "0.3em", color: color.glow,
              border: `5px solid ${color.glow}`, padding: "8px 26px 8px 36px", boxShadow: `0 0 22px ${color.red}aa, inset 0 0 18px ${color.red}66`,
              textShadow: `0 0 12px ${color.red}`,
            }}
          >
            {title.stamp}
          </div>
        )}
      </AbsoluteFill>
      {title.footnote && (
        <div style={{ position: "absolute", bottom: 46, width: "100%", textAlign: "center", fontFamily: font.body, fontWeight: 500, fontSize: 26, color: color.ink, opacity: 0.9 }}>
          {title.footnote}
        </div>
      )}
    </AbsoluteFill>
  );
};
