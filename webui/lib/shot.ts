// A map's levelshot as an <img> src. Quake 3's arenas name a file under /q3/; maps published from
// the map editor carry their thumbnail inline as a data: URL, since the page can't read user://, and
// one published without a thumbnail (headless) gets a dark plate instead of a broken image.
const NO_SHOT =
  "data:image/svg+xml," +
  encodeURIComponent(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 9"><defs><linearGradient id="g" x2="0" y2="1">' +
      '<stop offset="0" stop-color="#2a1210"/><stop offset="1" stop-color="#0b0807"/></linearGradient></defs>' +
      '<rect width="16" height="9" fill="url(#g)"/></svg>',
  );

export function shotSrc(levelshot: string) {
  if (!levelshot) return NO_SHOT;
  return levelshot.startsWith("data:") ? levelshot : `/q3/${levelshot}`;
}
