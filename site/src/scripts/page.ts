type Note = [start: number, length: number, pitch: number];
type Parts = Record<string, Note[]>;

const colors: Record<string, string> = {
  "Distorted electric guitar": "#a78bfa",
  "Electric bass": "#22d3ee",
  Drums: "#f472b6",
};
const labels: Record<string, string> = {
  "Distorted electric guitar": "Guitar",
  "Electric bass": "Bass",
};
const ALL = "All parts";
const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;

export function initPage() {
  const nav = document.getElementById("nav");
  const onScroll = () => nav?.classList.toggle("scrolled", scrollY > 8);
  addEventListener("scroll", onScroll, { passive: true });
  onScroll();

  const io = new IntersectionObserver(
    (entries) => {
      for (const e of entries) {
        if (!e.isIntersecting) continue;
        e.target.classList.add("in");
        io.unobserve(e.target);
      }
    },
    { rootMargin: "0px 0px -8% 0px" },
  );
  document.querySelectorAll(".reveal").forEach((el) => io.observe(el));

  initMeter();
  const data = document.getElementById("notes-data")?.textContent;
  if (data) initStudio(JSON.parse(data) as Parts);
}

function initMeter() {
  const input = document.getElementById("minutes") as HTMLInputElement | null;
  const out = document.getElementById("minutes-out");
  const credits = document.getElementById("cloud-credits");
  const bar = document.getElementById("cloud-bar");
  const sub = document.getElementById("cloud-sub");
  if (!input || !out || !credits || !bar || !sub) return;
  const fmt = new Intl.NumberFormat("en-US");
  const FREE = 5000;
  const update = () => {
    const minutes = Number(input.value);
    const used = minutes * 60 * 2.5;
    out.textContent = minutes >= 60 ? `${(minutes / 60).toFixed(minutes % 60 ? 1 : 0)} h` : `${minutes} min`;
    credits.textContent = fmt.format(used);
    bar.style.width = `${Math.min(100, (used / (1200 * 150)) * 100)}%`;
    sub.textContent =
      used <= FREE
        ? `${Math.round((used / FREE) * 100)}% of the free plan's 5,000 monthly credits`
        : `${(used / FREE).toFixed(1)}× the free plan's 5,000 monthly credits`;
  };
  input.addEventListener("input", update);
  update();
}

function initStudio(parts: Parts) {
  const roll = document.getElementById("roll") as HTMLCanvasElement | null;
  const wave = document.getElementById("wave") as HTMLCanvasElement | null;
  const list = document.getElementById("part-list");
  const play = document.getElementById("play");
  if (!roll || !wave || !list || !play) return;

  const names = Object.keys(parts);
  const all = names.flatMap((n) => parts[n].map((note) => ({ part: n, note })));
  const duration = Math.ceil(Math.max(...all.map(({ note }) => note[0] + note[1])));
  const pitches = all.map(({ note }) => note[2]);
  const lo = Math.min(...pitches) - 2;
  const hi = Math.max(...pitches) + 2;
  let selected = ALL;

  // Envelope for the strip above the roll: note onsets with a short decay.
  const bins = 400;
  const env = new Float32Array(bins);
  for (const { note } of all) {
    const b = Math.floor((note[0] / duration) * bins);
    for (let i = 0; i < 10 && b + i < bins; i++) env[b + i] += Math.exp(-i / 3);
  }
  const peak = Math.max(...env);

  for (const name of [ALL, ...names]) {
    const li = document.createElement("li");
    const b = document.createElement("button");
    b.type = "button";
    b.className = "part";
    b.setAttribute("aria-pressed", String(name === selected));
    const sw = document.createElement("span");
    sw.className = "swatch";
    sw.style.background = colors[name] ?? "linear-gradient(135deg,#22c3ee,#a855f7)";
    const label = document.createElement("span");
    label.textContent = labels[name] ?? name;
    const count = document.createElement("span");
    count.className = "count";
    count.textContent = String(name === ALL ? all.length : parts[name].length);
    b.append(sw, label, count);
    b.addEventListener("click", () => {
      selected = name;
      list.querySelectorAll("button").forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
      if (!running) draw(position());
    });
    li.append(b);
    list.append(li);
  }

  const fit = (c: HTMLCanvasElement) => {
    const dpr = Math.min(devicePixelRatio || 1, 2);
    const h = Number(c.getAttribute("height"));
    c.width = Math.round(c.clientWidth * dpr);
    c.height = h * dpr;
    c.style.height = `${h}px`;
    c.getContext("2d")?.setTransform(dpr, 0, 0, dpr, 0, 0);
  };

  function draw(t: number) {
    const rc = roll!.getContext("2d");
    const wc = wave!.getContext("2d");
    if (!rc || !wc) return;
    const w = roll!.clientWidth;
    const h = 300;
    rc.clearRect(0, 0, w, h);
    const row = h / (hi - lo);
    rc.fillStyle = "rgba(255,255,255,0.025)";
    for (let p = lo; p < hi; p++) if ([1, 3, 6, 8, 10].includes(p % 12)) rc.fillRect(0, h - (p - lo + 1) * row, w, row);
    rc.strokeStyle = "rgba(255,255,255,0.05)";
    for (let s = 0; s <= duration; s += 2) {
      const x = Math.round((s / duration) * w) + 0.5;
      rc.beginPath();
      rc.moveTo(x, 0);
      rc.lineTo(x, h);
      rc.stroke();
    }
    for (const { part, note } of all) {
      const [s, len, p] = note;
      const on = selected === ALL || selected === part;
      const active = on && t >= s && t <= s + len;
      rc.globalAlpha = on ? (active ? 1 : t > s ? 0.95 : 0.55) : 0.12;
      rc.fillStyle = active ? "#ffffff" : colors[part];
      const x = (s / duration) * w;
      rc.beginPath();
      rc.roundRect(x, h - (p - lo + 1) * row + 0.5, Math.max(3, (len / duration) * w), Math.max(2.5, row - 1), 2);
      rc.fill();
    }
    rc.globalAlpha = 1;
    const px = (t / duration) * w;
    const g = rc.createLinearGradient(px - 60, 0, px, 0);
    g.addColorStop(0, "rgba(91,124,255,0)");
    g.addColorStop(1, "rgba(91,124,255,0.18)");
    rc.fillStyle = g;
    rc.fillRect(px - 60, 0, 60, h);
    rc.fillStyle = "#fff";
    rc.fillRect(px - 1, 0, 2, h);

    const ww = wave!.clientWidth;
    const wh = 56;
    wc.clearRect(0, 0, ww, wh);
    const bw = ww / bins;
    const grad = wc.createLinearGradient(0, 0, ww, 0);
    grad.addColorStop(0, "#22c3ee");
    grad.addColorStop(0.5, "#5b7cff");
    grad.addColorStop(1, "#a855f7");
    for (let i = 0; i < bins; i++) {
      const a = Math.max(0.06, env[i] / peak) * (wh / 2 - 2);
      wc.fillStyle = i / bins <= t / duration ? grad : "rgba(255,255,255,0.16)";
      wc.fillRect(i * bw, wh / 2 - a, Math.max(1, bw - 1), a * 2);
    }
  }

  // Clock: wall time while silent, AudioContext time while sound is on.
  let ctx: AudioContext | null = null;
  let master: GainNode | null = null;
  let noise: AudioBuffer | null = null;
  let running = !reduced;
  let sound = false;
  let busy = false;
  let base = reduced ? duration * 0.4 : 0;
  let baseAt = performance.now() / 1000;
  let scheduledUntil = 0;
  let timer = 0;
  const wrap = (x: number) => ((x % duration) + duration) % duration;
  const now = () => (sound && ctx ? ctx.currentTime : performance.now() / 1000);
  const position = () => (running ? wrap(base + now() - baseAt) : base);
  const shown = () => (sound && ctx ? wrap(position() - (ctx.outputLatency || ctx.baseLatency || 0)) : position());

  // Notes are scheduled ahead, so stopping or seeking swaps the bus they play through.
  function cut() {
    if (!ctx || !master) return;
    master.gain.setTargetAtTime(0, ctx.currentTime, 0.01);
    const old = master;
    setTimeout(() => old.disconnect(), 100);
    master = null;
  }

  function voice(part: string, pitch: number, when: number, len: number) {
    if (!ctx || !master) return;
    const out = ctx.createGain();
    out.connect(master);
    if (part === "Drums") {
      if (pitch <= 36) {
        const o = ctx.createOscillator();
        o.frequency.setValueAtTime(140, when);
        o.frequency.exponentialRampToValueAtTime(45, when + 0.12);
        out.gain.setValueAtTime(0.5, when);
        out.gain.exponentialRampToValueAtTime(0.001, when + 0.25);
        o.connect(out);
        o.start(when);
        o.stop(when + 0.26);
        return;
      }
      const decay = pitch === 38 || pitch === 40 ? 0.16 : pitch >= 49 && pitch !== 51 ? 0.6 : 0.05;
      const n = ctx.createBufferSource();
      n.buffer = noise!;
      const f = ctx.createBiquadFilter();
      f.type = decay > 0.1 && decay < 0.3 ? "bandpass" : "highpass";
      f.frequency.value = decay > 0.1 && decay < 0.3 ? 1800 : 7000;
      out.gain.setValueAtTime(decay > 0.1 && decay < 0.3 ? 0.35 : 0.12, when);
      out.gain.exponentialRampToValueAtTime(0.001, when + decay);
      n.connect(f).connect(out);
      n.start(when);
      n.stop(when + decay + 0.02);
      return;
    }
    const o = ctx.createOscillator();
    const bass = part === "Electric bass";
    o.type = bass ? "triangle" : "sawtooth";
    o.frequency.value = 440 * 2 ** ((pitch - 69) / 12);
    const f = ctx.createBiquadFilter();
    f.type = "lowpass";
    f.frequency.value = bass ? 900 : 2200;
    const end = when + Math.min(len, 1.5);
    const level = bass ? 0.32 : 0.07;
    out.gain.setValueAtTime(0, when);
    out.gain.linearRampToValueAtTime(level, when + 0.008);
    out.gain.setTargetAtTime(level * 0.6, when + 0.05, 0.2);
    out.gain.setTargetAtTime(0, end, 0.04);
    o.connect(f).connect(out);
    o.start(when);
    o.stop(end + 0.3);
  }

  function schedule() {
    if (!ctx || !master) return;
    scheduledUntil = Math.max(scheduledUntil, ctx.currentTime);
    const horizon = ctx.currentTime + 0.6;
    while (scheduledUntil < horizon) {
      const from = scheduledUntil;
      const to = Math.min(horizon, from + 0.25);
      const loopStart = from - wrap(base + from - baseAt);
      for (const { part, note } of all) {
        if (selected !== ALL && selected !== part) continue;
        for (const offset of [loopStart, loopStart + duration]) {
          const when = offset + note[0];
          if (when >= from && when < to) voice(part, note[2], when, note[1]);
        }
      }
      scheduledUntil = to;
    }
  }

  function restartBus() {
    if (!ctx) return;
    cut();
    master = ctx.createGain();
    master.connect(ctx.destination);
    scheduledUntil = ctx.currentTime;
    schedule();
  }

  const label = play.querySelector(".play-label")!;
  play.addEventListener("click", async () => {
    if (busy) return;
    busy = true;
    try {
      if (sound) {
        base = position();
        sound = false;
        baseAt = now();
        clearInterval(timer);
        cut();
        play.dataset.on = "false";
        label.textContent = "Hear it";
        if (reduced) running = false;
        await ctx?.suspend();
        return;
      }
      ctx ??= new AudioContext();
      if (!noise) {
        noise = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate);
        const d = noise.getChannelData(0);
        for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
      }
      await ctx.resume();
      base = position();
      sound = true;
      running = true;
      baseAt = ctx.currentTime;
      restartBus();
      timer = window.setInterval(schedule, 60);
      play.dataset.on = "true";
      label.textContent = "Stop";
      loop();
    } finally {
      busy = false;
    }
  });

  const seek = (t: number) => {
    base = wrap(t);
    baseAt = now();
    if (sound) restartBus();
    if (!running) draw(base);
  };
  roll.addEventListener("click", (e) => {
    const r = roll.getBoundingClientRect();
    seek(((e.clientX - r.left) / r.width) * duration);
  });
  roll.addEventListener("keydown", (e) => {
    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
    e.preventDefault();
    seek(position() + (e.key === "ArrowRight" ? 2 : -2));
  });

  let visible = true;
  new IntersectionObserver(([e]) => {
    visible = e.isIntersecting;
    if (visible) loop();
  }).observe(roll);

  let raf = 0;
  function loop() {
    cancelAnimationFrame(raf);
    const tick = () => {
      draw(shown());
      if (running && (visible || sound)) raf = requestAnimationFrame(tick);
    };
    tick();
  }

  const resize = () => {
    fit(roll);
    fit(wave);
    draw(shown());
  };
  new ResizeObserver(resize).observe(roll);
  resize();
  if (!reduced) loop();
}
