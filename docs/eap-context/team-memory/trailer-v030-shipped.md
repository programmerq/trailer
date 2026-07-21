---
name: trailer-v030-shipped
description: Trailer v0.3.0 released 2026-07-13 — tag v0.3.0 @ f48f99c, 3 artifacts (linux tar.gz, macos dmg, windows zip); first release cut under the criteria machinery
metadata:
  type: project
---

v0.3.0 published 2026-07-13: https://github.com/programmerq/trailer/releases/tag/v0.3.0 — tag on f48f99c (PR #50, merge commit 84efa43, no-squash to preserve the built SHA). Assets: trailer-0.3.0-linux-x86_64.tar.gz (1.6MB), trailer-0.3.0-macos-arm64.dmg (47MB), trailer-0.3.0-windows-x86_64.zip (35MB). Full release matrix green (linux required the owner's new hyper-v k8s node — original runner sets kill pods at a fixed ~11min wall, unresolved, see backlog). Release body: full 0.3.0 CHANGELOG. Known notes gap being reconciled: install prose mentioned .msi/.deb/.rpm that aren't built (backlogged). This was the first release cut end-to-end under the criteria gates + decision machinery (ADRs 0002/0003/0004 accepted this cycle). See [[trailer-integration-batch-pr40]], [[trailer-followup-docket]] (superseded by docs/backlog/ in-repo per PR #51).
