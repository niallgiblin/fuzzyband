# Releasing fuzzyband (and the fuzzyband plugin site)

This is the runbook for shipping a new version of the **fuzzyband** plugin (formerly
"Metal Accompaniment") to users via the download website (`website/`) and GitHub Releases.
It covers the one-time setup, the day-to-day "cut a release" flow, and the things that
need a real CI run to verify.

> **What "releasing" means here.** You tag a version. GitHub Actions builds the plugin for
> macOS (universal), Windows and Linux, attaches self-contained packages to a GitHub
> Release, and the download page automatically shows download links for them. Users who
> don't want to compile just download; developers who do can keep building from source.

## About the download hub

The site at `website/` is a **multi-plugin hub**. It lists every plugin in `website/assets/app.js`
(`PRODUCTS`) and fetches each one's own repo's Releases API, so each section auto-updates
independently:

| Product | Repo | Release workflow |
|---------|------|------------------|
| **fuzzyband** (this repo) | `niallgiblin/fuzzyband` | `.github/workflows/release.yml` (this doc) |
| **Fairo** | `niallgiblin/fairo` | `.github/workflows/release.yml` (in that repo) |

To add another plugin later: add one entry to `PRODUCTS` in `app.js` and give that repo the
same `release.yml` pattern. No other site change is needed.

---

## What a release produces (fuzzyband)

Each package is **self-contained**: the ONNX model (bundled via BinaryData) and the ONNX
Runtime library are packed in, so an end user needs nothing on their machine.

| Asset | Contents |
|-------|----------|
| `fuzzyband-<ver>-macOS-Universal.zip` | VST3 + AU + Standalone app (universal arm64 + x86_64) |
| `fuzzyband-<ver>-Windows-x64.zip` | VST3 + Standalone exe |
| `fuzzyband-<ver>-Linux-x64.tar.gz` | VST3 + Standalone binary |

The package bundles are named after the product (`fuzzyband.vst3` / `fuzzyband.component` /
`fuzzyband.app`). Internal CMake/pipeline identifiers (e.g. the `MetalAccompaniment` target and
`MetalAccompaniment_artefacts` build dir) are unchanged — they never appear to users.

The download site fetches the public `/releases` API on load and shows the latest version's
buttons for each platform. It keeps itself up to date automatically — there is **nothing to
edit on the website** when you ship a new version.

---

## One-time setup

### 1. Make the repos public

GitHub Pages and the public Releases API only work on **public** repositories. This applies
to **both** the fuzzyband repo (site + plugin releases) and the Fairo repo (its releases):

```
Settings → General → Danger Zone → Change visibility → Make public
```

The API returns `404` for a private repo, and that plugin's section on the download page
shows "Couldn't load ... releases."

### 2. Give the workflows permission to write releases

The `release.yml` (in both repos) creates the GitHub Release and uploads assets. Confirm
`Contents: read and write` is allowed:

```
Settings → Actions → General → Workflow permissions → Read and write permissions
```

(`release.yml` also declares `permissions: contents: write` per job, so this is belt-and-braces.)

### 3. Enable GitHub Pages from Actions

```
Settings → Pages → Build and deployment → Source: "GitHub Actions"
```

Do **not** pick "Deploy from a branch" — the `pages.yml` workflow publishes `website/`.

---

## Cutting a release (the normal flow)

### 1. Bump the version

Edit `CMakeLists.txt` line 4 and bump the patch number:

```cmake
project(MetalAccompaniment VERSION 0.9.30)
```

Follow the project rule: bump the **patch** (Z) for each build; a **minor** bump (Y) is for
a milestone. Also update the version shown in `README.md` and add a `CHANGELOG.md` entry.
The version string is what shows top-right in the plugin UI, so it is the sanity check that
the right build loaded.

### 2. Commit and push

```bash
git add -A
git commit -m "Release v0.9.30"
git push origin main
```

### 3. Tag and push (this triggers the release)

```bash
git tag v0.9.30
git push origin v0.9.30
```

Pushing a `v*` tag runs `.github/workflows/release.yml`. The macOS job builds both
architectures, merges them into a universal package, creates the GitHub Release, and uploads
the macOS asset; then the Windows and Linux jobs build and upload theirs. When all three
finish, the Release has three assets.

### 4. Verify

- The **Actions** tab shows the `Release` workflow green.
- The **Releases** page has `fuzzyband-<ver>-macOS-Universal.zip`, `...-Windows-x64.zip`
  and `...-Linux-x64.tar.gz`.
- Open the download site and confirm the latest version + buttons appear (for each plugin).

### Releasing without pushing a tag

Use **Actions → Release → Run workflow**, set the `tag` input (e.g. `v0.9.30`). If the tag
does not exist yet, the macOS job creates it.

> **Fairo:** the identical flow applies in the `niallgiblin/fairo` repo — bump `project(Fairo
> VERSION ...)`, tag `v0.4.x`, push. Its workflow is ONNX-free and builds a single universal
> mac binary (no per-arch merge).

---

## First-release verification checklist (do this on the first real release)

The pipeline is authored to be correct, but some steps can only be proven on a real runner
(especially the universal binary lipo step and the ONNX Runtime embed on each OS). Run once
and check:

- [ ] `bash -n scripts/release-macos-universal.sh` clean on a mac.
- [ ] In the **Verify universal binaries** log: each `Contents/MacOS/fuzzyband` reports
      `Mach-O … (arm64 x86_64)`.
- [ ] In the macOS zip, the plugin's `fuzzyband` binary references `@rpath/libonnxruntime…`
      and ships a `Contents/Frameworks/libonnxruntime*.dylib`. `otool -L` on the binary should
      **not** show a homebrew/absolute ONNX path.
- [ ] Loading the downloaded plugin in a DAW shows the correct `vX.Y.Z` and no
      "damaged / unverified" error (after the Gatekeeper step below).
- [ ] Windows VST3 loads with the bundled `onnxruntime.dll` beside it.
- [ ] Linux VST3 loads with the bundled `libonnxruntime.so` and `$ORIGIN` rpath.
- [ ] **Fairo mac build**: `otool -L` on `Fairo.vst3/Contents/MacOS/Fairo` shows NO `onnx`
      dependency (DSP-only build), and the binary is universal.

If a check fails, fix it and re-run — you can re-run the workflow without changing the tag
(assets are uploaded with `--clobber`).

---

## macOS signing: why the download shows a Gatekeeper warning

Builds are **unsigned** because there is no paid Apple Developer certificate in the build.
Gatekeeper therefore warns the first time a user opens a downloaded plugin. This is expected
and safe (the package is built straight from the public source repo).

To let a macOS build run, a user does **one** of:

1. **Right-click → Open** on the bundle/app, then **Open** in the dialog; or
2. clear the quarantine flag once:
   ```
   xattr -dr com.apple.quarantine /path/to/fuzzyband.vst3
   ```
   (and the equivalent for `.component` / the `.app`).

### Signing + notarizing later (optional)

To remove the warning entirely, add an Apple Developer ID:

1. Create a Developer ID Application certificate and a Notarization profile.
2. Add them to the repo as Actions secrets (`APPLE_ID`, `APPLE_TEAM_ID`,
   `APPLE_APP_SPECIFIC_PASSWORD`, and the `.p12` + password).
3. Point the macOS release job at a `notarytool`/`codesign` step and set `--options runtime`
   for Hardened Runtime. Then users can open the plugin without the right-click step.

The packaging scripts already do ad-hoc signing and set
`com.apple.security.cs.disable-library-validation` for the bundled ONNX dylib, so this would
be an additive change to `release.yml`, not a rewrite.

---

## Cross-platform packaging notes

- **macOS (fuzzyband)** — `scripts/release-macos-universal.sh`: rewrites the ONNX load
  command to `@rpath`, adds `@executable_path/../Frameworks`, embeds the dylib in
  `Contents/Frameworks`, `lipo`s the two archs, and ad-hoc signs. Best-tested path.
- **macOS (Fairo)** — single `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` build (no external
  libs to merge), then ad-hoc sign. No ONNX, so nothing to embed.
- **Windows** — `onnxruntime.dll` is copied beside the `.vst3` module and the standalone
  `.exe` (fuzzyband only). Windows DLL resolution for a VST3 loaded into a host can be picky.
- **Linux** — built with `-DCMAKE_BUILD_RPATH_USE_ORIGIN=ON`, and `libonnxruntime.so` is
  copied beside the module so the `$ORIGIN` rpath can find it (fuzzyband only). Build deps are
  installed in the workflow (`libasound2-dev`, X11/GL/freetype, etc.).

---

## Website behavior

- Static HTML/CSS/JS in `website/`; deployed by `pages.yml` only when the site files change.
  It has **no build step**.
- `website/assets/app.js` declares `PRODUCTS` and fetches each product's public Releases API.
- Unauthenticated GitHub API calls are rate-limited (60/hour per IP). For a page this small
  that is fine; if it ever becomes a problem, the fetch can be moved to a serverless cache.

---

## Housekeeping

- **Don't** commit big build outputs. The `build*/`, `.artifacts/`, and generated dirs are
  already gitignored; keep it that way so the repo (and the release) stays lean.
- To stop publishing a version, delete its GitHub Release (or just the assets). The website
  then stops showing it.
- **Rename note:** the plugin's user-facing name is now **fuzzyband**, but the internal CMake
  target, artefact directory and code files still say `MetalAccompaniment` / `Accompaniment*`
  (invisible to users). Renaming those is a separate, larger change if you want it.
