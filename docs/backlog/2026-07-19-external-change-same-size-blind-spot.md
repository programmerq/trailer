---
id: 2026-07-19-external-change-same-size-blind-spot
title: External-change detection misses a same-size + same-second overwrite (mtime+size only)
priority: P3
status: open
source: code review of claude/external-file-change-handling, finding F3, 2026-07-19
created: 2026-07-19
---

## Threshold

An external overwrite that lands in the **same wall-clock second** as the
load-time baseline **and** produces a file of the **exact same byte size** is
detected as a change (the save-time guard blocks the clobber and/or the
watcher surfaces a reload/banner), rather than being classified `NoChange`.
Checkable headlessly: baseline a file, overwrite it with different content of
identical length within the same second, and assert
`classifyExternalChange(...)` does **not** return `NoChange`.

The accepted resolution may be an **optional content-hash fallback** consulted
only when mtime+size are inconclusive (equal), so the common path stays
stat-only and large PDFs are not hashed on every save.

## Context

`classifyExternalChange`
([`src/document/ExternalChangeState.cpp:34`](../../src/document/ExternalChangeState.cpp))
compares only `mtimeMs` and `size` against the baseline:

```cpp
const bool changed = (curMtimeMs != baseline.mtimeMs) || (curSize != baseline.size);
```

On a filesystem with 1s mtime granularity, an external writer that replaces a
file with equal-length content inside the same second is indistinguishable
from "no change." This is an accepted cost tradeoff — hashing every file on
every save (PDFs can be tens of MB) is expensive and was deliberately not done
— but it is a residual hole in a "never lose data" feature. It is documented in
the ADR's *Known limitation* section
([`docs/decision-records/2026-07-19-external-file-change-handling.md`](../decision-records/2026-07-19-external-file-change-handling.md)).

Doing this item means: add an opt-in content-hash (or partial-hash) fallback
that runs only when mtime+size are equal — the narrow window where the cheap
signals cannot decide — and wiring it through `FileBaseline` /
`classifyExternalChange` without changing the fast path. Weigh the hash cost
budget against the size envelope in
[`docs/performance-budgets.md`](../performance-budgets.md).
