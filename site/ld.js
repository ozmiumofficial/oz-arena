// Ozmium Arena: the level design guide's contents. Marks the section being read, and closes the
// contents on narrow screens once a section is picked. The page reads the same without it.
(() => {
  const links = [...document.querySelectorAll(".toc a[href^='#']")];
  const sections = links.map(a => document.querySelector(a.getAttribute("href"))).filter(Boolean);
  const box = document.querySelector(".toc details");
  const narrow = matchMedia("(max-width: 1000px)");
  if (box && narrow.matches) box.open = false;
  narrow.addEventListener?.("change", e => { if (box) box.open = !e.matches; });
  links.forEach(a => a.addEventListener("click", () => { if (box && narrow.matches) box.open = false; }));

  let ticking = false;
  const mark = () => {
    ticking = false;
    const line = innerHeight * 0.3;
    let cur = null;
    for (const s of sections) { if (s.getBoundingClientRect().top <= line) cur = s; else break; }
    links.forEach(a => a.setAttribute("aria-current", !!cur && a.getAttribute("href") === "#" + cur.id));
    // keep the marked entry in view inside the sticky list
    const on = links.find(a => a.getAttribute("aria-current") === "true");
    const list = document.querySelector(".toc");
    if (on && list && !narrow.matches) {
      const r = on.getBoundingClientRect(), b = list.getBoundingClientRect();
      if (r.top < b.top) list.scrollTop -= b.top - r.top + 8;
      else if (r.bottom > b.bottom) list.scrollTop += r.bottom - b.bottom + 8;
    }
  };
  addEventListener("scroll", () => { if (!ticking) { ticking = true; requestAnimationFrame(mark); } }, { passive: true });
  addEventListener("resize", mark);
  mark();
})();
