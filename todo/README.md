# todo/

Trailer's one tracker: each file here is one piece of open work, and nothing else in the repo
tracks work. What Trailer won't do is in [PRODUCT.md](../PRODUCT.md) ("Not doing"), not here.

**Entries**
- One entry per file, named `YYYY-MM-DD-short-slug.md`: the date it was filed and a few words
  saying what it is. The name is the entry's id. Ids are never reused.
- A file starts with the title as a heading, then `Priority: P0.` (P0 to P3) and its tags, then
  **Done when** (the pass/fail bar) and **Src** (the evidence). Some also have **Test**: a
  known-bug test that fails today (below).
- **Done when** names a state a reviewer can reach and observe. Where it says "at dpr 1 and 2" or
  lists view modes, every combination must pass.
- List the open work by priority with `grep -l '^Priority: P0' todo/*.md` (P1, P2, P3 likewise).

**Citing an entry**
- In Markdown, link it with its title: `[<its title>](todo/YYYY-MM-DD-slug.md)`.
- In code, tests and scripts, write its path without `.md`: `// todo/YYYY-MM-DD-slug: …`,
  and nothing more. Known-bug markers use the same form (below).
- `scripts/check-one-tracker.sh` (CI, every PR) fails when anything cites an entry that doesn't
  exist, so the PR that closes an entry also updates what cites it.

**Closing and adding**
- To close an entry, the PR that meets its bar deletes its file and names it in the PR
  description and commit body (`Closes todo/YYYY-MM-DD-slug.`). There is no status
  field and no done list.
- A fix that meets only part of a bar deletes the `QEXPECT_FAIL` lines it fixed, keeps the
  entry, and narrows its Done when to what's left.
- To add an entry, add a file dated today, with a failing test in the same PR when the bug can
  be reproduced offscreen. Adding never touches another file, so parallel PRs don't conflict.

**Known-bug tests**
- `tests/test_known_bugs_*.cpp` (ctest label `known-bug`, run by PR CI on Linux) assert the
  correct behaviour and mark today's failure with
  `QEXPECT_FAIL("", "todo/YYYY-MM-DD-slug: …", Abort)`.
- A fix makes that check pass unexpectedly, which fails the run. The fixing PR deletes the
  `QEXPECT_FAIL` line and the entry together, so the test becomes the regression test.
- A Test line names any part of the bar its test doesn't check ("Not checked: …"). A flip
  closes only what the test checks: the PR that deletes the entry shows the rest met, or it
  narrows the Done when to what's left.

**Priorities**
- **P0**: in ordinary use (no crash, no unusual setup), Trailer damages a file or writes one
  that looks right in Trailer and wrong elsewhere, loses work, saves or undoes the wrong thing,
  or leaves in a file what the user removed.
- **P1**: the named friction: controls that move, popups that say no, status-bar clutter, broken
  select/undo/zoom basics, text you can't select, HiDPI blur, slow open.
- **P2**: Preview parity and polish.
- **P3**: later, or stretch.

**Tags:** `open` (open, render, perf), `nav` (navigation, zoom), `text` (selection, text),
`annotations` (also page edits), `undo`, `save` (save, autosave, recovery), `chrome` (layout
stability), `dialogs`, `ocr` (OCR and ML), `screenshot`, `print`, `platform`, `packaging`
(packaging, release, CI), `infra` (tests and infrastructure), `docs`, `security`, and
`data-loss` where work can be lost.

**Sources (Src)**
- `file:line` refers to `09bc1e3` (main before the reboot, 2026-08-07) unless the entry names
  another commit; a new entry cites lines at the commit it was written on, e.g.
  `PdfAdapter.cpp:120 @1a2b3c4`. Removed files (the old TODO.md, ROADMAP.md, `docs/backlog/`,
  decision records) can still be read with `git show 09bc1e3:<path>`.
