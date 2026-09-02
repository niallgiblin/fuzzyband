// fuzzyband — download page logic (multi-plugin hub).
// Fetches each product's GitHub Releases API and populates its download links and
// version history automatically. No build step, no external dependencies: when a new
// version of any listed plugin is tagged and its assets are attached, this page updates
// itself on next load.

(function () {
  "use strict";

  // ══ Products ═════════════════════════════════════════════════════════════════
  // Each entry = one plugin/software, hosted in its own repo. Add a new plugin by
  // appending one object here (and giving that repo its own release workflow).
  const PRODUCTS = [
    {
      id: "fuzzyband",
      name: "fuzzyband",
      repo: "niallgiblin/fuzzyband",
      assetTag: /fuzzyband/i,
      tagline: "AI metal drum + bass MIDI accompaniment for guitarists",
      desc: "Play guitar into it; it listens and writes drum + bass MIDI in real time, locked to your project tempo. No tempo tapping, no pattern picking.",
      buildCmd: [
        "git clone https://github.com/niallgiblin/fuzzyband.git && cd fuzzyband",
        "cmake -B build -DCMAKE_BUILD_TYPE=Release \\",
        "  -DMA_ENABLE_ONNX=ON \\",
        "  -DONNXRUNTIME_ROOT=/opt/homebrew/opt/onnxruntime",
        "cmake --build build --parallel",
        "# artefacts: build/MetalAccompaniment_artefacts/Release/{VST3,AU,Standalone}",
      ],
      buildNote: "Requires ONNX Runtime (macOS: `brew install onnxruntime`). Use `-DMA_ENABLE_ONNX=OFF` for a rule-based-only build.",
    },
    {
      id: "fairo",
      name: "Fairo",
      repo: "niallgiblin/fairo",
      assetTag: /fairo/i,
      tagline: "Pharaoh-style fuzz pedal emulation",
      desc: "A physically-informed model of the Black Arts Toneworks Pharaoh fuzz: switchable clipping (Si/Ge/bypass) and the coupled Tone/High tonestack, solved per sample.",
      buildCmd: [
        "git clone https://github.com/niallgiblin/fairo.git && cd fairo",
        "cmake -B build -DCMAKE_BUILD_TYPE=Release",
        "cmake --build build --parallel",
        "# artefacts: build/Fairo_artefacts/Release/{VST3,AU,Standalone}",
      ],
      buildNote: "DSP-only — no ONNX Runtime needed.",
    },
  ];

  const PLATFORM_PATTERNS = {
    macos: [/macos/i, /osx/i, /universal/i],
    windows: [/windows/i, /win64/i, /win32/i],
    linux: [/linux/i],
  };

  const PLATFORM_META = {
    macos: { label: "macOS", subtitle: "Apple Silicon + Intel · Universal",
             formats: ["VST3", "AU", "Standalone"],
             hint: "One download works on every Mac." },
    windows: { label: "Windows", subtitle: "Windows x64",
               formats: ["VST3", "Standalone"],
               hint: "VST3 + standalone app. No AU on Windows." },
    linux: { label: "Linux", subtitle: "Linux x64",
             formats: ["VST3", "Standalone"],
             hint: "VST3 + standalone binary. No AU on Linux." },
  };
  const PLATFORMS = ["macos", "windows", "linux"];

  // ══ Data helpers ════════════════════════════════════════════════════════════
  const $ = (sel) => document.querySelector(sel);
  const esc = (s) =>
    String(s).replace(/[&<>"']/g, (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]),
    );

  function fmtSize(bytes) {
    if (!bytes && bytes !== 0) return "";
    const units = ["B", "KB", "MB", "GB"];
    let n = Number(bytes);
    let i = 0;
    while (n >= 1024 && i < units.length - 1) { n /= 1024; i++; }
    return `${n.toFixed(n >= 10 || i === 0 ? 0 : 1)} ${units[i]}`;
  }

  const verOf = (r) => (r.tag_name || r.name || "").replace(/^v/, "");

  function dateOf(iso) {
    try {
      return new Date(iso).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
    } catch { return ""; }
  }

  function semverCompare(a, b) {
    const pa = (a || "").replace(/^v/, "").split(/[.-]/).map((x) => parseInt(x, 10) || 0);
    const pb = (b || "").replace(/^v/, "").split(/[.-]/).map((x) => parseInt(x, 10) || 0);
    for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
      const da = pa[i] || 0, db = pb[i] || 0;
      if (da !== db) return db - da;
    }
    return 0;
  }

  const sortByTagDesc = (releases) =>
    [...releases].sort((a, b) => semverCompare(a.tag_name, b.tag_name) ||
      (new Date(b.published_at || 0) - new Date(a.published_at || 0)));

  // An asset belongs to a product if its name matches the product's assetTag and the
  // platform pattern. Scoped per repo already, but this is robust if a repo ever hosts
  // more than one product.
  function assetFor(product, release, platform) {
    const assets = (release.assets || []).filter((a) => product.assetTag.test(a.name));
    for (const rx of PLATFORM_PATTERNS[platform]) {
      const hit = assets.find((a) => rx.test(a.name));
      if (hit) return hit;
    }
    return null;
  }

  // ══ Rendering ═══════════════════════════════════════════════════════════════
  const ICONS = { macos: "", windows: "⊞", linux: "🐧" };

  function platformCards(product, release) {
    return PLATFORMS.map((p) => {
      const meta = PLATFORM_META[p];
      const asset = assetFor(product, release, p);
      const releaseUrl = release.html_url || `https://github.com/${product.repo}/releases`;
      const icon = ICONS[p] || "";

      if (!asset) {
        return `<article class="card card-empty">
          <h4>${esc(meta.label)}</h4>
          <p class="sub">${esc(meta.subtitle)}</p>
          <p class="formats">${meta.formats.map((f) => `<span class="chip">${f}</span>`).join("")}</p>
          <p class="muted">Not available in this release.</p>
          <a class="btn btn-ghost" href="${esc(releaseUrl)}" target="_blank" rel="noopener">View release</a>
        </article>`;
      }
      return `<article class="card">
        <h4>${esc(meta.label)}</h4>
        <p class="sub">${esc(meta.subtitle)}</p>
        <p class="formats">${meta.formats.map((f) => `<span class="chip">${f}</span>`).join("")}</p>
        <a class="btn btn-download" href="${esc(asset.browser_download_url)}" download>
          <span class="btn-ic">${icon}</span> Download
        </a>
        <p class="filemeta">${esc(asset.name)} · ${fmtSize(asset.size)}</p>
        <p class="muted">${esc(meta.hint)}</p>
      </article>`;
    }).join("");
  }

  function historyList(releases) {
    if (!releases.length) return `<p class="muted">No releases yet.</p>`;
    return `<ul class="hist">` + releases.map((r) => {
      const n = (r.assets || []).length;
      return `<li>
        <span class="hist-ver">v${esc(verOf(r))}</span>
        <span class="hist-date">${esc(dateOf(r.published_at))}</span>
        ${r.prerelease ? `<span class="chip chip-amber">pre</span>` : ""}
        <span class="hist-note">${n ? `${n} ${n === 1 ? "file" : "files"}` : "no assets"}</span>
      </li>`;
    }).join("") + `</ul>`;
  }

  function productSection(product, releases, err) {
    const sorted = sortByTagDesc(releases);
    const latest = sorted.find((r) => !r.draft && !r.prerelease) || sorted[0];

    const buildBlock = `<details class="panel-sub">
      <summary>Build from source</summary>
      <p class="muted">${esc(product.buildNote)}</p>
      <pre><code>${product.buildCmd.map(esc).join("\n")}</code></pre>
    </details>`;

    const historyBlock = `<details class="panel-sub" open>
      <summary>Version history</summary>
      ${historyList(sorted)}
    </details>`;

    const body = err
      ? `<p class="latest-error">Couldn't load <strong>${esc(product.name)}</strong> releases. Please refresh, or grab it from the
          <a href="https://github.com/${product.repo}/releases" target="_blank" rel="noopener">Releases page</a>.</p>`
      : latest
        ? platformCards(product, latest)
        : `<p class="muted">No releases published yet. Check back soon.</p>`;

    return `<section class="product" id="product-${esc(product.id)}">
      <div class="product-head">
        <h3>${esc(product.name)}</h3>
        ${latest ? `<span class="version-pill">v${esc(verOf(latest))}</span>` : ""}
      </div>
      <p class="muted product-tag">${esc(product.tagline)}</p>
      <p class="muted">${esc(product.desc)}</p>
      ${err ? "" : (latest ? `<div class="grid">${body}</div>` : body)}
      ${buildBlock}
      ${historyBlock}
    </section>`;
  }

  // ══ Bootstrap ═══════════════════════════════════════════════════════════════
  async function loadProduct(product) {
    const url = `https://api.github.com/repos/${product.repo}/releases?per_page=30`;
    const res = await fetch(url, { headers: { Accept: "application/vnd.github+json" } });
    if (!res.ok) throw new Error(`GitHub API returned ${res.status} for ${product.repo}`);
    return res.json();
  }

  async function main() {
    const host = $("#products");
    const results = await Promise.all(PRODUCTS.map(async (p) => {
      try { return { p, releases: await loadProduct(p), err: null }; }
      catch (e) { console.error("loadProduct", p.id, e); return { p, releases: [], err: e }; }
    }));
    host.innerHTML = results.map(({ p, releases, err }) => productSection(p, releases, err)).join("");
  }

  main().catch((err) => {
    console.error("fuzzyband download page:", err);
    const host = $("#products");
    host.innerHTML = `<p class="latest-error">Couldn't load plugin releases. Please refresh, or visit the
      <a href="https://github.com/niallgiblin" target="_blank" rel="noopener">GitHub account</a>.</p>`;
  });
})();
