# CLAUDE.md

**This file intentionally contains no project instructions.** It was previously a
near-duplicate of `AGENTS.md` (304 lines, differing in only four places), and the
duplication caused agents to read a *staler* architecture summary depending on
which file their tool loaded. It is now a single pointer.

👉 **Read [`AGENTS.md`](AGENTS.md) — it is the one canonical agent instruction
file for this repository.**

And before making any change, read the canonical documentation set:

| Doc | Use it for |
|---|---|
| [`docs/CONTEXT_HANDOFF.md`](docs/CONTEXT_HANDOFF.md) | Start here: build/test, real architecture, the active bug, what is not wired |
| [`docs/PITFALLS_AND_INVARIANTS.md`](docs/PITFALLS_AND_INVARIANTS.md) | Traps, contracts, pre-flight checklist |
| [`docs/BASS_MIRRORING.md`](docs/BASS_MIRRORING.md) | The hard active bug (bass live mirror) |
| [`docs/PROJECT_TIMELINE.md`](docs/PROJECT_TIMELINE.md) | Every era, what failed and was retried |
| [`docs/TEST_AUDIT.md`](docs/TEST_AUDIT.md) | Test coverage and gaps |
| [`docs/DOCS_INDEX.md`](docs/DOCS_INDEX.md) | Which docs are current, which are archived |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Source-verified architecture |

**Hard rules** (repeated from `AGENTS.md`):

- `.planning/` is the only planning authority. **Never read `.gsd/`** — it is a
  stale, untracked symlink that claims the project is blocked on a milestone that
  finished months ago.
- The project version is authoritative only in `CMakeLists.txt` line 4.
- Never weaken a test assertion or add `[!mayfail]` to make a fix pass.

> If a `generate-claude-profile` / GSD run recreates a full profile here, delete
> the regenerated duplicate and keep this pointer. The duplication is the bug.
