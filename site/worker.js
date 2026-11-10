// Replaces the build-time star count with a fresh one. GitHub is asked server-side and cached
// for an hour, so visitors never contact GitHub; on any failure the build-time count stays.
const repo = "https://api.github.com/repos/awesomeAE/audio2midi";
const cacheKey = new Request("https://awesomeae.com/__stars");

async function starCount(ctx) {
  const hit = await caches.default.match(cacheKey);
  if (hit) return Number(await hit.text());
  try {
    const res = await fetch(repo, {
      headers: { "User-Agent": "awesomeae-site", Accept: "application/vnd.github+json" },
      signal: AbortSignal.timeout(1500),
    });
    if (!res.ok) return undefined;
    const { stargazers_count: n } = await res.json();
    if (typeof n !== "number") return undefined;
    ctx.waitUntil(caches.default.put(cacheKey, new Response(String(n), { headers: { "Cache-Control": "max-age=3600" } })));
    return n;
  } catch {
    return undefined;
  }
}

export default {
  async fetch(request, env, ctx) {
    const page = await env.ASSETS.fetch(request);
    if (!page.headers.get("Content-Type")?.startsWith("text/html")) return page;
    const stars = await starCount(ctx);
    if (stars === undefined) return page;
    let lang = "en";
    return new HTMLRewriter()
      .on("html", { element: (e) => void (lang = e.getAttribute("lang") ?? lang) })
      .on(".stars-n", { element: (e) => void e.setInnerContent(new Intl.NumberFormat(lang).format(stars)) })
      .transform(page);
  },
};
