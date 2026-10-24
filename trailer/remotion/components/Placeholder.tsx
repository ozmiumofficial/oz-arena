import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { color, font } from "../lib/theme";

/** A segment whose footage isn't recorded yet: a dark slate saying what goes here and why it waits. */
export const Placeholder: React.FC<{ label: string; note?: string }> = ({ label, note }) => {
  const frame = useCurrentFrame();
  const drift = (frame * 0.6) % 80;
  return (
    <AbsoluteFill style={{ backgroundColor: "#0b0807" }}>
      <AbsoluteFill
        style={{
          background: `repeating-linear-gradient(135deg, rgba(255,74,28,0.035) 0px, rgba(255,74,28,0.035) 40px, transparent 40px, transparent 80px)`,
          backgroundPosition: `${drift}px 0`,
        }}
      />
      <AbsoluteFill style={{ justifyContent: "center", alignItems: "center", textAlign: "center", opacity: interpolate(frame, [0, 8], [0, 1], { extrapolateRight: "clamp" }) }}>
        <div style={{ fontFamily: font.body, fontWeight: 700, fontSize: 22, letterSpacing: "0.4em", color: color.faint }}>TO BE RECORDED</div>
        <div style={{ fontFamily: font.head, fontWeight: font.headWeight, fontSize: 60, color: color.dim, marginTop: 16 }}>{label}</div>
        {note && <div style={{ fontFamily: font.body, fontWeight: 500, fontSize: 26, color: color.faint, marginTop: 12 }}>{note}</div>}
      </AbsoluteFill>
    </AbsoluteFill>
  );
};
