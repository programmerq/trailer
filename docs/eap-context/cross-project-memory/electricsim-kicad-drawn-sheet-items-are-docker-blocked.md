---
name: electricsim-kicad-drawn-sheet-items-are-docker-blocked
description: SUPERSEDED 2026-07-19 — Docker IS available in sessions: containers run as root and dockerd starts on demand (nohup dockerd; wait for docker info; pull kicad/kicad:9.0 = KiCad 9.0.9, the exact CI image). Byte-identical regens proven. Historical Docker-blocked guidance below no longer applies.
metadata:
  type: project
  modified: 2026-07-18T16:00:49.065Z
---

# electricsim: KiCad drawn-sheet regen items are Docker-blocked

## SUPERSEDED 2026-07-19: Docker works in-session

Schematic Studio proved (2026-07-19): session containers run as root with working sudo; the docker daemon simply isn't started at boot. Start it with `nohup dockerd >/var/log/dockerd.log 2>&1 &` then poll `docker info`; `docker pull kicad/kicad:9.0` fetches KiCad 9.0.9 — the exact CI image, zero version skew. Deterministic artifacts (.kicad_sch/.net/.kicad_pro) regenerate byte-identical to committed; `make kicad-validate` Tier 1/2 and the full `--audit` fleet gate run for real. The owner is adding the dockerd start to his environment session-start script. All previously Docker-parked items (SDM MC33797 respin, BTCM named-pin symbols, J1962 footprint, BOM regens, TJB fab seed, board-envelope regens) are unparked as of 2026-07-19. The content below is retained for history only.

## The pattern

Fixing a KiCad symbol/sheet issue (e.g. a part drawn as a generic numeric-pad
`Conn_01xNN` that loses its named pins) requires regenerating the drawn
`.kicad_sch` via `populate_kicad_sch.py` **inside the `kicad/kicad:9.0`
container** — it embeds stock symbols from `/usr/share/kicad/symbols`, a path
that only exists in the container — then re-running ERC (`kicad-cli`) and
`kicad_validate.py --audit`. Entry point:
`make kicad-export MODULE=<m>` → `circuit_ir_to_kicad.py::_write_sch_via_kicad_docker()`
→ `docker run … kicad/kicad:9.0 …`.

## Why these sessions can't do it

The environment has the `docker` binary but **NO daemon**
(`/var/run/docker.sock` absent) and the `kicad/kicad:9.0` image is absent. So
the sheet regen, ERC, and `kicad_validate --audit` all cannot run or be
verified here.

## The trap: "author the IR half only" is NOT a valid non-Docker slice

The named pins usually **already exist** in the generated netlist
(`export/kicad/<m>/<m>.net`). The non-Docker gates `kicad_check` and
`audit_kicad_ir_parity` compare against that generated `.net`/IR and pass
**VACUOUSLY** on a mis-wired drawn sheet. The ONLY gate that catches the
drawn-sheet loss is `kicad_validate.py --audit` (Docker,
`enforcement: local-only`, skips clean without Docker). So changing the `.net`
libsource + regenerating it green delivers ZERO of the item's value and leaves
a half-migrated export — a "dishonest green." **Do NOT ship that.**

## How to apply

When picking up a kicad symbol/drawn-sheet item, first check whether the fix
needs a `.kicad_sch` regen (Docker):

- **If yes** → report DOCKER-BLOCKED and park behind the owner's pending
  Docker-environment decision. Do NOT open a red PR or a vacuous-green
  non-Docker slice.
- A genuinely non-Docker sub-part (e.g. swapping a passive to a stock
  `Device:L` symbol that needs no named-pin regen) may be committable — assess
  per item.

## Seen twice

- **SDM-boost respin**: inductor half L1 → `Device:L` committed `c3dcb21c`; IC
  half deferred as "generated named-pin symbols."
- **BL-2026-07-15-btcm-driver-ic-named-pin-symbol** (BTCM U2/U3 motor-driver
  ICs) — parked 2026-07-18.

U2/U3 (and the LHJB whole module) are already grandfathered in
`notes/kicad_validate_grandfather.yaml` pending exactly this Docker work.
