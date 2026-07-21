---
name: electricsim-worktree-fleet-regen-needs-top-level
description: In programmerq/electricsim, connector/BOM/scorecard fleet regens that read ev1-connections/ MUST run from the top-level checkout, not a nested .claude/worktrees worktree — a path-resolution constraint (repo_paths.py REDUX_REPO default + git-symlink module yamls), NOT redux being unreachable. Workaround from a worktree: set REDUX_DIR=/home/user/ev1-manual-redux (fixes real-file modules like bpm/ad/pim; git-symlink ones like htcm still need the top level). BEST worktree fix (2026-07-18, PR #334): create the sibling symlink .claude/worktrees/ev1-manual-redux -> the real redux checkout, which resolves the ev1-connections/* relative symlinks for ALL modules from any nested worktree
metadata:
  type: project
---

In programmerq/electricsim, any connector/BOM/scorecard **fleet regeneration** that reads `ev1-connections/` MUST run from the **TOP-LEVEL checkout** (`/home/user/electricsim`), never a nested `.claude/worktrees/<branch>/` worktree.

**Reason:** several `ev1-connections/*.yaml` (e.g. `ev1_htcm_module.yaml`, `ev1_apm_module.yaml`, `ev1_connector_catalog.yaml`, `ev1_peripherals.yaml`) are **relative symlinks** `../../ev1-manual-redux/harness/…` that resolve only from the top level. From a worktree the `../../` points at a non-existent `.claude/worktrees/ev1-manual-redux/…`.

**Symptoms in a worktree:**
- `check_pinouts.py` errors on a "missing" `ev1_apm_module.yaml`
- `module_compile --module <m>` fails
- `audit_kicad_coverage` silently skips
- the Definition-of-Manufacturable scorecard's redux-pin column reads "sibling absent"

This is why BL-0150 (Stage-5 termini) and other fleet-regens can't be worktree-parallelized.

**NOTE:** `audit_manufacturable.py` itself is worktree-**SAFE** — it reports the redux sibling as "absent" from ANY checkout, so it doesn't depend on the symlink resolving.

---

## Refinement (2026-07-18, correcting a mislabeled note on PR #287)

**This is NOT "redux unreachable."** Redux is present and reachable at `/home/user/ev1-manual-redux`. The failure is a **nested-worktree path-resolution constraint only**. An agent mislabeled it "redux unreachable" on PR #287 and the owner flagged the correction — do not repeat that framing.

**The mechanism has TWO independent parts:**

1. **`scripts/repo_paths.py` default resolution.** It defines
   `REDUX_REPO = _sibling("REDUX_DIR", "ev1-manual-redux")`, whose DEFAULT is
   `REPO_ROOT.parent / ev1-manual-redux`. From a nested
   `.claude/worktrees/<branch>/` checkout that resolves to a **non-existent**
   `.claude/worktrees/ev1-manual-redux` — so redux "looks" absent purely because
   the parent-of-repo-root is the worktrees dir, not the top level.

2. **git-symlink module yamls.** Some `ev1-connections/*.yaml` are git symlinks
   (e.g. `ev1_htcm_module.yaml`) that **don't resolve from a nested worktree**,
   while others (bpm / ad / pim module yamls) are **REAL files** that DO compile
   from a worktree.

**KEY REFINEMENT — the `REDUX_DIR` env-var override.** Setting
`REDUX_DIR=/home/user/ev1-manual-redux` makes `module_compile.py --module bpm`
succeed (**exit 0**) EVEN FROM a nested worktree — proving redux is reachable
and the issue is purely path resolution, not redux being down. Note that `htcm`
**still fails** from a worktree even with `REDUX_DIR` set, because of its
git-symlink module yaml (part 2 above) — independent of `REDUX_DIR`.

**Practical guidance for a fleet-regen from a worktree — pick one:**
- Run from the **top-level checkout** (`/home/user/electricsim`) — fixes both
  parts; OR
- Set `REDUX_DIR=/home/user/ev1-manual-redux` — fixes the real-file modules
  (bpm/ad/pim), but **NOT** the git-symlink ones (e.g. htcm), which still need
  the top-level checkout.

---

## WORKAROUND (2026-07-18, discovered on the SDM MC33797 KiCad respin, PR #334) — a worktree fleet-regen / `make kicad-export` can be made to WORK

The nested-worktree `ev1-connections/*` redux-symlink breakage documented above
can be **FIXED in place** so that a worktree's `make kicad-export` / fleet-regen
**WORKS** — no top-level checkout required. Create ONE sibling symlink:

```bash
ln -sfn /home/user/ev1-manual-redux /home/user/electricsim/.claude/worktrees/ev1-manual-redux
```

**Why it works:** the `ev1-connections/*.yaml` relative symlinks point at
`../../ev1-manual-redux/...`. From a nested `.claude/worktrees/<branch>/`
checkout, that `../../` resolves to `.claude/worktrees/ev1-manual-redux`. Placing
the symlink THERE (pointing at the real sibling redux checkout) makes ALL those
relative symlinks resolve — for every module, real-file and git-symlink alike.
Create it once; it persists for all worktrees under `.claude/worktrees/`.

**Proven genuine (not a silent skip)** on PR #334: a real 35→26 SDM BOM change,
plus passing netlist↔IR parity and ERC (KiCad's automated electrical rules
check), after running `make kicad-export` from the nested worktree.

**Also required for the SDM respin regen to run clean from a worktree:**
- `export REDUX_DIR=/home/user/ev1-manual-redux`
- `pip install matplotlib` (for the `kicad_sch_preview` PNG regeneration)
- Docker up: `nohup dockerd`, then `docker pull kicad/kicad:9.0` — the EXACT CI
  image, so regens come out **byte-identical to CI**.

**Practical guidance is now THREE options for a worktree fleet-regen / kicad-export:**
1. Run from the **top-level checkout** (`/home/user/electricsim`) — fixes both parts.
2. Set `REDUX_DIR=/home/user/ev1-manual-redux` — fixes real-file modules only
   (bpm/ad/pim), NOT the git-symlink module yamls (e.g. htcm).
3. **BEST** — create the `.claude/worktrees/ev1-manual-redux` sibling symlink
   (above), which fixes the `ev1-connections` relative-symlink resolution for
   **ALL** modules from any nested worktree, so `make kicad-export` / fleet-regen
   runs from the worktree exactly as it would from the top level.
