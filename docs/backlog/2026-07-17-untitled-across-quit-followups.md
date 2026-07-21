---
id: 2026-07-17-untitled-across-quit-followups
title: Untitled-doc follow-ups — quit safety net, disambiguation, crash-safety
priority: P2
status: open
source: persona-review of fix/untitled-close-save (UX NEEDS-WORK + correctness SHIP-WITH-MINORS); deferred out of that PR
created: 2026-07-17
---

## Threshold

All three sub-items are closed when:

1. **Quit safety net.** An untitled (transient temp-file-backed) document is
   never silently lost across application quit — regular quit prompts per
   unsaved/untitled doc, and any "keep windows" quit persists the untitled
   *content* (not just a temp path that may be GC'd), such that a doc that
   was untitled before quit returns as untitled (still gated on close) rather
   than as a titled temp-path doc. Owned by the **Quit-and-Keep-Windows**
   decision record — resolve there, not here.
2. **Disambiguation.** Two or more open untitled docs are individually
   identifiable in the tab bar and in sequential close prompts (e.g.
   "Untitled 1" / "Untitled 2", or "Untitled (1 of N)" in the prompt).
3. **Crash-safety.** The tradeoff of auto-save-skip for untitled docs (no
   crash recovery for content that lives only in a temp file) is either
   accepted-and-documented or covered by a draft-store; decision recorded.

## Context / Body

Deferred follow-ups surfaced while landing `fix/untitled-close-save` (the
close-time silent-data-loss fix: `isUntitled()`, close-gate prompt, Save-As
routing, auto-save skip, friendly Save-As filename, proxy-icon/recent-files
hygiene). These are out of scope for that PR and tracked here.

### 1. macOS ⌘Q untitled silent-loss + session-restore mismarking

The close-time fix guards the per-window / per-tab close path, but the
**application-quit** path bypasses it:

- `Application::onAboutToQuit` snapshots open-file **temp paths** without
  prompting the user, so an untitled doc's content is entrusted to a
  transient temp file (subject to OS cleanup) with no Save prompt.
- `restorePreviousSession` / the reopen-persisted path restores those paths
  via `openFiles(paths)` **without** `markUntitled=true`. A doc that was
  untitled before quit therefore returns as a **titled** temp-path doc: the
  `isUntitled()` safety net is gone, and the next close will silently discard
  it again (or, worse, silently "save" to the temp path).

This is the domain of the **Quit-and-Keep-Windows decision record**
(`docs/decision-records/2026-07-16-quit-and-keep-windows.md`, **PR #74**,
branch `adr/quit-and-keep-windows`): the intended behaviour is that a regular
⌘Q prompts per unsaved/untitled document, while ⌥⌘Q ("Quit and Keep Windows")
persists untitled *content* so it can be faithfully restored as untitled.
Resolve this sub-item as part of that feature rather than patching the quit
path piecemeal here. Cross-links: this backlog item ⇄ PR #74 / the
Quit-and-Keep-Windows record.

### 2. Multiple-untitled disambiguation

Every untitled doc renders as the identical string "Untitled" in the tab bar
(`ImageDocument::displayName()`), and sequential close prompts on quit/close
all read "Save changes to Untitled?" — with two pasted images open the user
cannot tell which is which. Follow-up: number them ("Untitled 1" /
"Untitled 2") or add a "(1 of N)" counter to the close prompt so each is
distinguishable. (The headless test
`uat_fnd_014_multipleUntitledDocsEachPromptOnClose` confirms each is
*independently gated*; it does not address *labelling*.)

### 3. Untitled crash-safety gap

Auto-save deliberately **skips** untitled docs (it must not pick a
destination for the user — ADR-0004 says the user chooses the location), so
an untitled doc has **no crash recovery**: its content exists only in the
transient temp file. If the app crashes before the user runs Save-As, the
pasted/acquired content is unrecoverable through the normal recent/auto-save
machinery. Tradeoff is intentional (no silent writes to a user-unchosen
path), but a **draft-store** — the same mechanism the Quit-and-Keep-Windows
record proposes for persisting untitled content across quit — would close
this gap too. Candidate to fold into that work rather than a standalone
auto-save-to-temp scheme.

## Provenance

Harvested from the two persona reviews of `fix/untitled-close-save`
(UX: NEEDS-WORK; correctness: SHIP-WITH-MINORS). The in-PR minor fixes
(friendly Save-As filename, proxy-icon leak, close-prompt "Save…" +
informative text, recent-files temp-path skip) landed on that branch; these
three larger items were explicitly deferred.
