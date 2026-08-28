---
id: 2026-08-06-open-dedup-case-insensitive-volumes
title: Already-open dedup misses two spellings of one file on case-insensitive volumes (macOS/Windows)
priority: TBD
status: open
source: self-review while fixing the Wine CI break on the already-open dedup PR (#156)
created: 2026-08-06
---

## Threshold

On macOS and Windows, with `Report.pdf` open, asking to open `report.pdf`
(same file, different spelling) surfaces the existing window instead of
opening a second document. Checkable headlessly:

- Open a fixture at a path with mixed case; assert
  `Application::windowForOpenPath()` finds it when given the same path
  lower-cased, and that `openFiles()` on that spelling leaves
  `windowCount()` and the total open-document count unchanged.
- On Linux the opposite must hold: two files that genuinely differ only by
  case stay two documents. A single blanket case-fold fails this half, so
  the fix has to be volume- or platform-aware, not global.

## Context

`trailer::canonicalPathKey` (`src/util/PathKey.h`) resolves symlinks, `..`
and relative paths, but **does not case-fold** — the limitation is stated
at the rule itself. On a case-insensitive volume (APFS default, NTFS)
`A.pdf` and `a.pdf` are one file yet produce two different keys, so the
already-open dedup (UAT-FND-053..058) does not fire and the user gets the
duplicate-window bug the owner reported — the very thing that work exists
to prevent.

**This matters most on the platform the report came from.** The owner's
macOS volume is case-insensitive by default, so the gap is reachable there,
not merely theoretical. It is narrow, though: every ordinary entry point
(Finder, Spotlight, the Open panel, Open Recent, drag-and-drop, argv from a
file manager) hands over the on-disk spelling already, so reaching one file
under two spellings takes a hand-typed command line, a hand-made shortcut,
or a path pasted from another tool.

**Why it was not just fixed inline.** Blanket case-folding is the obvious
move and it is wrong: it would merge two genuinely distinct files on a
case-sensitive Linux volume, turning a missing-dedup gap into a
wrong-document bug, which is worse. Doing it properly means deciding
case-sensitivity per platform (`Q_OS_WIN` / `Q_OS_MACOS` compare
case-insensitively, Linux case-sensitively) or, better, per volume — and Qt
exposes no portable per-volume query, so the platform split is the
realistic option, with its own inaccuracy (case-sensitive APFS volumes and
case-insensitive Linux mounts both exist).

That is a deliberate behaviour decision about document identity rather than
a mechanical fix, so it is filed rather than folded into the bug-fix PR
that introduced the rule.

## Provenance

Surfaced by self-review during the Wine CI fix on branch
`claude/open-already-open-file`, not by a user report. Flagged to the owner
in that PR's report as worth an explicit ruling. The limitation is already
documented in `src/util/PathKey.h`'s header comment and in PR #156's body;
this item exists so it is tracked to a decision rather than living only as
a code comment.
