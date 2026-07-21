---
name: trailer-ux-walkthrough-paths-11-13
description: ux-walkthrough paths 11-13 added (PR #115, draft) covering merged #77 Take-Screenshot / #90 discard-file-integrity / #78 quit-Cancel-keeps-alive; carries harness oracle-hardening lessons + Linux/Xvfb behavior facts; also drops the disabled MSVC ci.yml job
metadata:
  type: project
  modified: 2026-07-21T05:59:07.011Z
---

PR #115, branch `claude/ux-walkthrough-merged-flows`, additive-only, remote SHA ac6f391 (images commit 85ce306). Adds `tools/ux-walkthrough/paths/11-tools-screenshot-capture.sh` (#77), `12-discard-file-integrity.sh` (#90), `13-quit-and-keep-basics.sh` (#78) + the five run.sh dispatch spots + `docs/uat/images/ux-walkthrough-1{1,2,3}-*.png`. Rider: removed the `if:false` `build-and-test-windows` MSVC job from `.github/workflows/ci.yml` (redundant with the active mingw `windows-cross-build`; nothing `needs:` it). All three paths run GREEN under Xvfb (9 hard-oracle PASS lines).

**Harness oracle-hardening lessons (from adversarial persona review — apply to any new hard-oracle path):**
1. A discard oracle of "modal gone + doc closed + backing file sha unchanged" CANNOT distinguish a clean Discard from a crash-after-modal (a crash also writes nothing and leaves no windows). Add a POSITIVE non-crash proof: reap the app pid and assert exit status 0 (or `kill -0` if it stays alive).
2. A "Cancel keeps app alive" oracle that only checks win_count>=1 passes even if Escape is IGNORED and the modal is still up. Also assert the prompt window is GONE after Cancel (`xdotool search --name 'Unsaved changes'` returns nothing).
3. "No narration popup on cancel" is stronger as a window-count-DELTA (any new top-level after Escape = fail) than a title allowlist (which misses off-list titles like "Information"/"Success").

**Linux/Xvfb behavior facts observed (build/trailer, Qt 6.11):**
- Discarding the LAST open document EXITS the process cleanly with status 0 (deterministic across runs) DESPITE `Application::setQuitOnLastWindowClosed(false)` at `src/app/Application.cpp:64` — it does NOT stay alive windowless on Linux.
- Tools -> Take Screenshot opens the grab via `openFiles(markUntitled=true)` (`src/app/Application.cpp:~1092`), so the captured screen appears titled "Untitled - Trailer", NOT `trailer-screenshot-*`. Empty-state title is bare "Trailer".
- The "Unsaved changes" QMessageBox renders NO mnemonic underlines under the Xvfb theme, so `alt+d` does not trigger Discard; one Tab moves focus Save->Discard, driven with Tab+Space.
- Under Xvfb with no compositor, a just-closed dialog leaves GHOST pixels on the X root, so root-grabs of "after cancel" vs "after capture" can be byte-identical — use a window-scoped grab (`import -window $WIN_ID`) for distinct evidence.

**Boundary (honest, not faked):** the Keep-Windows relaunch-and-restore flow is NOT driven — relaunch-with-state-restore can't be reproduced in this single-process Xvfb harness; documented via the `boundary` DSL verb.

Related: [[trailer-ux-walkthrough-first-driven-run]], [[trailer-review-before-push-policy]], [[trailer-ci-on-k8s-runners]].
