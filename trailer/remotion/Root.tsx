import { Composition } from "remotion";
import { Trailer } from "./Trailer";
import { FPS, HEIGHT, WIDTH, durationInFrames } from "./lib/timeline";

// One composition, as long as the track: the edit (edit/trailer.json) lays itself out on its bars.
export const Root: React.FC = () => (
  <Composition id="Trailer" component={Trailer} durationInFrames={durationInFrames} fps={FPS} width={WIDTH} height={HEIGHT} />
);
