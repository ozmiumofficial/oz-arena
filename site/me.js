// Ozmium Arena: the map editor guide. Marks the section being read in the contents, closes the
// contents on narrow screens once a section is picked, and filters the key reference as you type.
// The page reads the same without it.
(() => {
  // the contents
  const links = [...document.querySelectorAll(".toc a[href^='#']")];
  const sections = links.map(a => document.querySelector(a.getAttribute("href"))).filter(Boolean);
  const box = document.querySelector(".toc details");
  const list = document.querySelector(".toc");
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
    if (on && list && !narrow.matches) {
      const r = on.getBoundingClientRect(), b = list.getBoundingClientRect();
      if (r.top < b.top) list.scrollTop -= b.top - r.top + 8;
      else if (r.bottom > b.bottom) list.scrollTop += r.bottom - b.bottom + 8;
    }
  };
  addEventListener("scroll", () => { if (!ticking) { ticking = true; requestAnimationFrame(mark); } }, { passive: true });
  addEventListener("resize", mark);
  mark();

  // the key reference: every word typed must be in a row (its keys or what it does); a group's
  // heading stays while any of its rows does, and matching the heading keeps the whole group
  const input = document.getElementById("key-filter");
  const table = document.getElementById("key-table");
  const none = document.getElementById("key-none");
  if (!input || !table) return;
  const groups = [...table.tBodies].map(tb => {
    const head = tb.querySelector("tr.grp");
    return { tb, head: (head?.textContent || "").toLowerCase(), rows: [...tb.rows].filter(r => r !== head).map(r => ({ r, text: r.textContent.toLowerCase().replace(/\s+/g, " ") })) };
  });
  const filter = () => {
    const words = input.value.toLowerCase().trim().split(/\s+/).filter(Boolean);
    let shown = 0;
    groups.forEach(g => {
      const whole = words.length > 0 && words.every(w => g.head.includes(w));
      let any = false;
      g.rows.forEach(({ r, text }) => {
        const ok = whole || words.every(w => text.includes(w));
        r.classList.toggle("is-hidden", !ok);
        if (ok) { any = true; shown++; }
      });
      g.tb.classList.toggle("is-hidden", !any);
    });
    if (none) none.hidden = shown > 0;
  };
  input.addEventListener("input", filter);
  input.addEventListener("keydown", e => { if (e.key === "Escape" && input.value) { input.value = ""; filter(); } });
})();
