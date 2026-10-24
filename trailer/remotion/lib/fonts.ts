// Fonts load before any frame renders (Google Fonts through @remotion/google-fonts): Teko for
// headlines (Ozmium's pick, 2026-10-23: the traced Quake 3 menu font looked rough at trailer
// sizes), Barlow, the menu's body font, for lines.
import { loadFont as loadBarlow } from "@remotion/google-fonts/Barlow";
import { loadFont as loadTeko } from "@remotion/google-fonts/Teko";

loadTeko("normal", { weights: ["500", "600", "700"], subsets: ["latin"] });
loadBarlow("normal", { weights: ["500", "600", "700"], subsets: ["latin"] });
