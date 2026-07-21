---
name: ev1-umbrella-repo
description: github.com/programmerq/ev1 (created 2026-07-17, blank private) is THE umbrella repo for the physical replica program — sourcing catalog, CAD, frame reconstruction; spaceframe recreation is the single most critical workstream.
metadata:
  type: project
---

**The umbrella repo exists (2026-07-17):** the owner created
https://github.com/programmerq/ev1 — a blank private repo (0 commits) that is
now THE home for everything about the physical EV1 replica program beyond the
existing engineering repos: the provenance-tagged parts/sourcing catalog
(previously planned for an unnamed "build-program repo" — see
[[ev1-replica-sourcing-plan]]; its planned home is now concrete), eventually
CAD documents, frame-reconstruction engineering, plus the compliance dossier,
decision log, and panel reports. The planning session for the repo's shape is
the "Beyond electronics: replica brainstorm" child session (owner flipped it
up to the Fable model).

**Frame-first process context (owner, 2026-07-17):**

- **Recreating the aluminum spaceframe is THE most critical part of the whole
  replica project.** He'll model it from chassis-manual datum points + alloy
  info + as many references as possible; he needs to learn sheet-metal CAD
  modeling; he expects to do real engineering and pay for outside engineering
  review even though a one-off personal build doesn't legally require it
  (consistent with the exceed-the-bar documentation stance in
  [[ev1-replica-regulatory-and-geometry-reframe]]).
- **Reference upside:** he hopes for 3D scan data from a physical EV1 — GM
  Heritage Center has shared extensively with the owner of car #212 (including
  original assembly-line video); YouTube videos of #212 show the bare
  spaceframe from nearly every angle; he has photos of several spaceframes.

**Parts strategy (owner, 2026-07-17):**

- Identify alternate/sibling part numbers for MOST of the vehicle — front
  turn-signal lamp assemblies and rear reflectors are direct lifts shared with
  other GM vehicles.
- **Headlamp and stoplamp assemblies are bespoke scratch-builds** — possibly
  harvesting common bulb/reflector bowls from similar-sized GM vehicles, with
  clear lenses via 3D print / forged carbon / machining / vacuforming per
  classic-replica practice.
- **Body panels** are reinforced composite plastics à la Pontiac Fiero (steel
  spaceframe analog), likely vacuformable.

**Repo operating decisions (2026-07-17 owner):**

- **CAD toolchain: FreeCAD + sheet-metal workbench** (chosen over Fusion 360 —
  cost + commercial-use clause risk on the free tier; revisit only if FreeCAD
  proves inadequate). Any cloud-CAD (Fusion etc.) document gets a POINTER in
  git, never a checked-in binary.
- **Binary policy: intentionally NO Git LFS**; stay under GitHub's plain file
  cap. Raw photogrammetry/scan data goes to external storage (Google Drive or
  the owner's many-TB homelab) referenced by committed manifests with hashes.
- **Visibility: private.** If it ever goes public the owner shares OUTPUTS,
  not full history; may grant individual collaborator access. All content is
  written outputs-shareable (no GM-copyrighted media committed — pointers
  only; scrub private-repo identifiers if content leaves).
- **First-wave contents approved:** sourcing catalog + schema, both panel
  reports, backfilled decision log.
- **Initial scaffold pushed directly to main** (repo had 0 commits, PR
  impossible); all later changes by PR.

Links: [[ev1-replica-sourcing-plan]] [[ev1-replica-definition-of-done]]
[[ev1-replica-regulatory-and-geometry-reframe]]
[[ev1-replica-frame-reconstruction-plan]] [[ev1-replica-lamps-and-panels-plan]]
[[ev1-owner-local-repos]].
