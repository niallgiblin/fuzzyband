// Metal Accompaniment — download page logic.
// Fetches the GitHub Releases API and populates the download links automatically.
// No build step, no external dependencies: when a new release is tagged and the
// assets are attached, this page updates by itself on next load.

(function () {
  "use strict";

  // ══ Config ══════════════════════════════════════════════════════════════════
  const OWNER = "niallgiblin";
  const REPO = "fuzzyband";
  const API = `https://api.github.com/repos/${OWNER}/${REPO}/releases?per_page=30`;
  const RELEASES_URL = `https://github.com/${OWNER}/${REPO}/releases`;

  // Which assets belong to which platform, in priority order.
  const PLATFORM_PATTERNS = {
    macos: [/macos/i, /osx/i, /universal/i],
    windows: [/windows/i, /win64/i, /win32/i],
    linux: [/linux/i],
  };

  const PLATFORM_META = {
    macos: {
      label: "macOS",
      subtitle: "Apple Silicon + Intel · Universal",
      formats: ["VST3", "AU", "Standalone"],
      hint: "One download works on every Mac. Comes with the AU and the standalone app.",
      icon: "",
    },
    windows: {
      label: "Windows",
      subtitle: "Windows x64",
      formats: ["VST3", "Standalone"],
      hint: "VST3 + standalone app. No AU on Windows.",
      icon: "",
    },
    linux: {
      label: "Linux",
      subtitle: "Linux x64",
      formats: ["VST3", "Standalone"],
      hint: "VST3 + standalone binary. No AU on Linux.",
      icon: "",
    },
  };

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
    while (n >= 1024 && i < units.length - 1) {
      n /= 1024;
      i++;
    }
    return `${n.toFixed(n >= 10 || i === 0 ? 0 : 1)} ${units[i]}`;
  }

  function verOf(release) {
    return (release.tag_name || release.name || "").replace(/^v/, "");
  }

  function dateOf(iso) {
    try {
      return new Date(iso).toLocaleDateString(undefined, {
        year: "numeric",
        month: "short",
        day: "numeric",
      });
    } catch {
      return "";
    }
  }

  function assetFor(release, platform) {
    const assets = release.assets || [];
    const pats = PLATFORM_PATTERNS[platform];
    for (const rx of pats) {
      const hit = assets.find((a) => rx.test(a.name));
      if (hit) return hit;
    }
    return null;
  }

  function sortByTagDesc(releases) {
    // Sort newest semver first; fall back to published_at.
    return [...releases].sort((a, b) => {
      const c = semverCompare(a.tag_name, b.tag_name);
      if (c !== 0) return c;
      return new Date(b.published_at || 0) - new Date(a.published_at || 0);
    });
  }

  function semverCompare(a, b) {
    const pa = (a || "").replace(/^v/, "").split(/[.-]/).map((x) => parseInt(x, 10) || 0);
    const pb = (b || "").replace(/^v/, "").split(/[.-]/).map((x) => parseInt(x, 10) || 0);
    for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
      const da = pa[i] || 0;
      const db = pb[i] || 0;
      if (da !== db) return db - da;
    }
    return 0;
  }

  // ══ Rendering ═══════════════════════════════════════════════════════════════
  function cardFor(platform, release) {
    const meta = PLATFORM_META[platform];
    const asset = assetFor(release, platform);
    const releaseUrl = release.html_url || RELEASES_URL;

    const iconSvg = {
      macos: "",
      windows: "⊞",
      linux: "🐧",
    }[platform] || "";

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
        <span class="btn-ic">${iconSvg}</span> Download
      </a>
      <p class="filemeta">${esc(asset.name)} · ${fmtSize(asset.size)}</p>
      <p class="muted">${esc(meta.hint)}</p>
    </article>`;
  }

  function renderLatest(release) {
    const host = $("#latest-load");
    const ver = verOf(release);
    const isPrerelease = !!release.prerelease;
    // Hard-code the product name for the hero title.
    host.innerHTML = `
      <div class="hero-release">
        <div class="hero-version">
          <span class="version-pill">v${esc(ver)}</span>
          ${isPrerelease ? `<span class="pre-pill">pre-release</span>` : ""}
        </div>
        <p class="muted">${esc(dateOf(release.published_at))} on
          <a href="${esc(release.html_url || RELEASES_URL)}" target="_blank" rel="noopener">GitHub Releases</a></p>
        <p class="muted">Every download below is the newest build for that platform.</p>
      </div>`;
  }

  function renderGrid(release) {
    const grid = $("#grid");
    grid.innerHTML = PLATFORMS.map((p) => cardFor(p, release)).join("");
  }

  function renderHistory(releases) {
    const host = $("#history");
    if (!releases.length) {
      host.innerHTML = `<p class="muted">No releases yet.</p>`;
      return;
    }
    host.innerHTML = `<ul class="hist">` + releases.map((r) => {
      const ver = verOf(r);
      const hasDl = (r.assets || []).length;
      return `<li>
        <span class="hist-ver">v${esc(ver)}</span>
        <span class="hist-date">${esc(dateOf(r.published_at))}</span>
        ${r.prerelease ? `<span class="chip chip-amber">pre</span>` : ""}
        <span class="hist-note">${hasDl ? `${r.assets.length} ${r.assets.length === 1 ? "file" : "files"}` : "no assets"}</span>
      </li>`;
    }).join("") + `</ul>`;
  }

  // ══ Bootstrap ══════════════════════════════════════════════════════════════
  const PLATFORMS = ["macos", "windows", "linux"];

  async function main() {
    const res = await fetch(API, {
      headers: { Accept: "application/vnd.github+json" },
    });
    if (!res.ok) throw new Error(`GitHub API returned ${res.status}`);
    const releases = await res.json();
    const sorted = sortByTagDesc(releases);

    const latest = sorted.find((r) => !r.draft && !r.prerelease) || sorted[0];

    if (latest) {
      renderLatest(latest);
      renderGrid(latest);
    } else {
      $("#latest-error").hidden = false;
      $("#latest-pending").hidden = true;
      $("#grid").innerHTML = `<p class="muted">No releases published yet. Check back soon.</p>`;
    }
    renderHistory(sorted);
    $("#latest-pending").hidden = true;
  }

  main().catch((err) => {
    console.error("Metal Accompaniment download page:", err);
    $("#latest-pending").hidden = true;
    $("#latest-error").hidden = false;
  });
})();
