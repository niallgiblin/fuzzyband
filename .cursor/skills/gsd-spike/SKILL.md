---
name: gsd-spike
description: "Rapidly spike an idea with throwaway experiments to validate feasibility before planning"
---

<cursor_skill_adapter>
## A. Skill Invocation
- This skill is invoked when the user mentions `gsd-spike` or describes a task matching this skill.
- Treat all user text after the skill mention as `{{GSD_ARGS}}`.
- If no arguments are present, treat `{{GSD_ARGS}}` as empty.

## B. User Prompting
When the workflow needs user input, prompt the user conversationally:
- Present options as a numbered list in your response text
- Ask the user to reply with their choice
- For multi-select, ask for comma-separated numbers

## C. Tool Usage
Use these Cursor tools when executing GSD workflows:
- `Shell` for running commands (terminal operations)
- `StrReplace` for editing existing files
- `Read`, `Write`, `Glob`, `Grep`, `Task`, `WebSearch`, `WebFetch`, `TodoWrite` as needed

## D. Subagent Spawning
When the workflow needs to spawn a subagent:
- Use `Task(subagent_type="generalPurpose", ...)`
- The `model` parameter maps to Cursor's model options (e.g., "fast")
</cursor_skill_adapter>

<objective>
Rapid feasibility validation through focused, throwaway experiments. Each spike answers one
specific question with observable evidence. Spikes live in `.planning/spikes/` and integrate
with GSD commit patterns, state tracking, and handoff workflows.

Does not require `/gsd-new-project` — auto-creates `.planning/spikes/` if needed.
</objective>

<execution_context>
@/Users/ng/Desktop/fuzzyband/.cursor/get-shit-done/workflows/spike.md
@/Users/ng/Desktop/fuzzyband/.cursor/get-shit-done/references/ui-brand.md
</execution_context>

<runtime_note>
**Copilot (VS Code):** Use `vscode_askquestions` wherever this workflow calls `conversational prompting`.
</runtime_note>

<context>
Idea: {{GSD_ARGS}}

**Available flags:**
- `--quick` — Skip decomposition/alignment, jump straight to building. Use when you already know what to spike.
</context>

<process>
Execute the spike workflow from @/Users/ng/Desktop/fuzzyband/.cursor/get-shit-done/workflows/spike.md end-to-end.
Preserve all workflow gates (decomposition, risk ordering, verification, MANIFEST updates, commit patterns).
</process>
