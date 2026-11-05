// Ozmium Arena: the site's small behaviors. Everything works without it; this adds the embers,
// the before/after slider, the mobile menu, the section highlight and the reveal on scroll.
(() => {
  const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;
  document.documentElement.classList.add("js");

  // mobile menu
  const toggle = document.querySelector(".nav-toggle");
  const links = document.getElementById("nav-links");
  toggle?.addEventListener("click", () => {
    const open = links.classList.toggle("open");
    toggle.setAttribute("aria-expanded", open);
  });
  links?.addEventListener("click", e => {
    if (e.target.closest("a")) { links.classList.remove("open"); toggle.setAttribute("aria-expanded", false); }
  });

  // before / after
  document.querySelectorAll("[data-compare] .compare-frame").forEach(frame => {
    const input = frame.querySelector("input");
    const set = () => frame.style.setProperty("--pos", input.value + "%");
    input.addEventListener("input", set);
    set();
  });

  // reveal on scroll, and the nav's current section
  const revealed = document.querySelectorAll(".sigil, .chapter, .gallery figure, .steps li, .plat, .hood div, .section-head");
  revealed.forEach(el => el.classList.add("reveal"));
  const navLinks = [...document.querySelectorAll(".nav ul a[href^='#']")];
  if ("IntersectionObserver" in window) {
    const io = new IntersectionObserver(entries => {
      entries.forEach(e => { if (e.isIntersecting) { e.target.classList.add("seen"); io.unobserve(e.target); } });
    }, { rootMargin: "0px 0px -8% 0px" });
    revealed.forEach(el => io.observe(el));

    // the section under the middle of the view, nothing while the hero fills it
    const sections = navLinks.map(a => document.querySelector(a.getAttribute("href"))).filter(Boolean);
    const mark = () => {
      const mid = scrollY + innerHeight * 0.45;
      let cur = null;
      sections.forEach(s => { if (s.offsetTop <= mid) cur = s; });
      if (cur && cur.id === "install" && mid > cur.offsetTop + cur.offsetHeight) cur = null;
      navLinks.forEach(a => a.setAttribute("aria-current", !!cur && a.getAttribute("href") === "#" + cur.id));
    };
    addEventListener("scroll", () => requestAnimationFrame(mark), { passive: true });
    mark();
  } else {
    revealed.forEach(el => el.classList.add("seen"));
  }

  // screenshots open full size: click, Esc to close, arrows to step through
  const shots = [...document.querySelectorAll(".shot img, .gallery img")];
  const box = document.createElement("div");
  box.className = "lightbox";
  box.setAttribute("role", "dialog");
  box.setAttribute("aria-modal", "true");
  box.setAttribute("aria-label", "Screenshot");
  box.innerHTML = '<figure><img alt=""><figcaption></figcaption></figure>' +
    '<button class="lb-close" aria-label="Close">Close ✕</button>' +
    '<button class="lb-prev" aria-label="Previous screenshot">‹</button><button class="lb-next" aria-label="Next screenshot">›</button>';
  document.body.append(box);
  const big = box.querySelector("img"), cap = box.querySelector("figcaption");
  let at = -1, back = null;
  const show = i => {
    at = (i + shots.length) % shots.length;
    const s = shots[at];
    big.src = s.currentSrc || s.src; big.alt = s.alt;
    cap.textContent = s.closest("figure")?.querySelector("figcaption")?.textContent || s.alt;
  };
  const open = i => { back = document.activeElement; show(i); box.classList.add("open"); box.querySelector(".lb-close").focus(); };
  const close = () => { box.classList.remove("open"); at = -1; back?.focus?.(); };
  shots.forEach((s, i) => {
    s.tabIndex = 0;
    s.setAttribute("role", "button");
    s.addEventListener("click", () => open(i));
    s.addEventListener("keydown", e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); open(i); } });
  });
  box.addEventListener("click", e => {
    if (e.target.closest(".lb-prev")) show(at - 1);
    else if (e.target.closest(".lb-next")) show(at + 1);
    else if (e.target.closest(".lb-close") || e.target === box) close();
  });
  addEventListener("keydown", e => {
    if (at < 0) return;
    if (e.key === "Escape") close();
    else if (e.key === "ArrowLeft") show(at - 1);
    else if (e.key === "ArrowRight") show(at + 1);
  });

  // embers rising through the hero
  const canvas = document.querySelector(".embers");
  if (!canvas || reduced) return;
  const ctx = canvas.getContext("2d");
  let w, h, dpr, parts = [], running = true;
  const resize = () => {
    dpr = Math.min(devicePixelRatio || 1, 2);
    w = canvas.clientWidth; h = canvas.clientHeight;
    canvas.width = w * dpr; canvas.height = h * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  };
  const spawn = (y) => ({
    x: Math.random() * w, y: y ?? h + 10,
    r: Math.random() * 1.8 + .4, vy: -(Math.random() * .6 + .25), vx: (Math.random() - .5) * .3,
    life: 0, max: Math.random() * 400 + 300, hue: 15 + Math.random() * 25, wob: Math.random() * 6.28,
  });
  resize();
  addEventListener("resize", resize);
  const count = () => Math.round(Math.min(90, w / 14));
  for (let i = 0; i < count(); i++) parts.push(spawn(Math.random() * h));
  new IntersectionObserver(([e]) => { running = e.isIntersecting; if (running) requestAnimationFrame(tick); }).observe(canvas);
  function tick() {
    if (!running) return;
    ctx.clearRect(0, 0, w, h);
    ctx.globalCompositeOperation = "lighter";
    while (parts.length < count()) parts.push(spawn());
    parts = parts.filter(p => {
      p.life++; p.wob += .03;
      p.x += p.vx + Math.sin(p.wob) * .25; p.y += p.vy;
      const a = Math.sin(Math.PI * Math.min(1, p.life / p.max));
      if (p.life > p.max || p.y < -10) return false;
      const g = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, p.r * 5);
      g.addColorStop(0, `hsla(${p.hue}, 100%, 65%, ${a * .9})`);
      g.addColorStop(1, `hsla(${p.hue}, 100%, 50%, 0)`);
      ctx.fillStyle = g;
      ctx.beginPath(); ctx.arc(p.x, p.y, p.r * 5, 0, 6.283); ctx.fill();
      return true;
    });
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);
})();
