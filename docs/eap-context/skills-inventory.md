# Skills inventory — repo vs environment (observed 2026-07-21)

Repo skills (git-tracked under `.claude/skills/`) survive the EAP shutdown:
they live in this repository and load in any future agent environment that
clones it. Environment-only skills do NOT survive — they exist in the managed
EAP session containers (on disk under `/root/.claude/skills/` or compiled into
the Claude Code CLI binary) and disappear with the EAP. Copies of the
recoverable environment-only skill definitions are under `skills-env/`.

## (a) In-repo (safe — git-tracked in `.claude/skills/`)

- `decision-brief`
- `review-before-push`
- `surface-the-ask`
- `ux-walkthrough`

Note: all four of the owner-policy skills are in-repo, verified with
`git ls-files .claude/skills/` on `origin/main` (b5eae1f). This includes
`review-before-push` and `decision-brief`, which had been flagged as possibly
environment-only — they are safe.

## (b) Environment-only, Anthropic-bundled (recoverable/replaceable; not owner content)

On disk at `/root/.claude/skills/` (per its `manifest.json`, `source:
"anthropic"` or `"anthropic-example"`); SKILL.md copies under `skills-env/`:

- `docx`, `pdf`, `pptx`, `xlsx` (source: anthropic; document tooling)
- `morning`, `skill-creator` (source: anthropic-example)
- `session-start-hook` (on disk, not in manifest; generic Anthropic guidance
  for Claude Code on the web SessionStart hooks — not owner-specific)

A larger Anthropic skill library also sits read-only at `/mnt/skills/public/`
and `/mnt/skills/examples/` (docx, pdf, pptx, xlsx, morning, skill-creator,
mcp-builder, canvas-design, theme-factory, web-artifacts-builder, and ~20
consumer example skills) — same provenance, not copied.

Compiled into the Claude Code CLI binary itself (`/opt/claude-code/bin/claude`,
verified by string-scanning the ELF; no on-disk SKILL.md exists, and the
export attempt to extract them was blocked by the permission layer — they ship
with every Claude Code install, so nothing owner-specific is lost):

- `deep-research`, `design-sync`, `dataviz`, `artifact-design`,
  `artifact-capabilities`, `update-config`, `verify`, `debug`, `code-review`,
  `simplify`, `batch`, `fewer-permission-prompts`, `doctor`, `loop`,
  `claude-api`, `run`, `run-skill-generator`

## (c) Environment-only AND likely project/owner-specific (at risk)

None found. Every skill that encodes owner policy (`review-before-push`,
`decision-brief`, `surface-the-ask`, `ux-walkthrough`) is git-tracked in the
repo; everything environment-only traced back to Anthropic provenance.

One owner-authored near-skill artifact WAS found outside the repo: the loose
memory file `/tmp/claude/memory/decision-brief-preference.md` (owner
preferences for decision-brief behavior) — preserved at
`other-memory/memory-root/decision-brief-preference.md`.

## Full environment skill roster observed in child sessions on 2026-07-21 (Claude Code 2.1.216)

docx, morning, pdf, pptx, session-start-hook, skill-creator, xlsx,
decision-brief, review-before-push, surface-the-ask, ux-walkthrough,
deep-research, design-sync, dataviz, artifact-design, artifact-capabilities,
update-config, verify, debug, code-review, simplify, batch,
fewer-permission-prompts, doctor, loop, claude-api, run, run-skill-generator.
