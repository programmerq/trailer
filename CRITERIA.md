# Criteria

The third keystone document. [PHILOSOPHY.md](PHILOSOPHY.md) says what stays true and what
is out of scope — the constraints. [DESIGN.md](DESIGN.md) says how the thing is built.
**This file says what "done," "priority," and "1.0" mean, and therefore what to work on
next.** It is deliberately short; if it grows into another spec, it has failed. Precedence:
on a *priority* or a *bar*, this file outranks DESIGN.md; on a *constraint*, PHILOSOPHY.md
outranks everything.

## What this revises, and what it does not

The PHILOSOPHY.md constraints are untouched and non-negotiable: local-first, no telemetry,
no accounts, no cloud sync, no ads, no premium tier, MIT, conservative feature surface. The
adjudication machinery stands too: PHILOSOPHY.md's *How design decisions get adjudicated* —
the Arbiter (an agent role named per decision in that record's `Arbiter:` field), the
admissible-objection test, the decision-record lifecycle in
[`docs/decision-records/`](docs/decision-records/) (**proposed → accepted → superseded-by**),
and the rule that **the owner is escalation-only** — plus its enforcement as gates **G1–G9**
in [`AGENTS.md`](AGENTS.md). This file adds nothing to that machinery and overrides none of it.

The one thing this document changes is the **completion gate** — whose satisfied workflow
certifies a job *done in real use*. PHILOSOPHY.md's *How decisions get made* step 1 ("Does it
serve the reference user?") is a **scope-admission filter** — whether a thing is built at all —
and CRITERIA leaves it untouched. What CRITERIA recenters is *when built work is finished*:
**the owner**, not the non-developer reference user. The four DESIGN.md §2.5.2 personas — the
office non-technical user, the older careful user, the power migrator, the occasional user —
stay exactly as that adjudication section keeps them: **"unranked adversarial lenses, not a
priority order,"** which file findings (§6), not stakeholders who co-own "done." The owner
confirmed this recentering; recording it as a PHILOSOPHY amendment or a decision record is
housekeeping, not a reopening. Priority here is *computed*, not assigned (§4), so the owner
never becomes a bottleneck for ordinary work.

---

## 1. What Trailer is for

Trailer is **Preview for Windows and Linux.** The owner lives on macOS, where Preview is the
fallback for nearly every document task; on Windows and Linux nothing single fills that hole.
Trailer removes the **last** major reason the owner cannot leave the Apple ecosystem.

This is an **exit visa from Apple**, not a migration to one OS. The point is the *freedom to
choose* the desktop, so **Windows and Linux must both reach parity** — neither is the "real"
target. macOS stays a build target but is the *reference habitat and fallback*: where Preview
lives and the oracle we compare against. This sharpens DESIGN.md §2.1's cross-platform-parity
goal.

**Parity rule.** Every bar is evaluated independently on Windows and on Linux. A job is *met*
only when it passes on **both**; macOS is not scored. A bug that blocks a job on Windows but
not Linux leaves that job unmet, scored where it hurts.

**How parity is checked, today.** CI proves only *unit-test-level* parity per PR — Linux
natively, Windows via a mingw-w64 cross-build under Wine (the native-MSVC Windows job is
**temporarily disabled** to conserve Actions minutes, so PR CI no longer catches
MSVC-specific regressions), and the end-to-end UAT tier runs only at release. Green CI is
necessary, not sufficient: **every bar in §5 is a live-session claim (HITL / dogfood), never
a CI status.**

## 2. Who says "done"

**The owner is the primary user and the acceptance bar** — a power user who does these jobs
for real. "Done" means the owner, working on Windows or Linux, does not reach for Preview or
another tool. The gate is a real person doing real work, not a hypothetical. The four personas
stay for critique — "would this confuse the older careful user?" still shapes design, and their
objections file findings (§6) — but the owner's real session, not a persona, decides done.

---

## 3. The always-clean invariant

The stall that stopped this project was **exogenous** — attention pulled away by unrelated
factors, not backlog overwhelm or process weight. The consequence the owner chose is not a
re-entry ritual but a steady state that never needs one:

> **Session-start cost is constant. Returning after five minutes or five weeks must feel
> identical.**

- **Single next action.** At any moment the ranking function (§4) yields **exactly one**
  head-of-queue item. It is not *chosen* at session start; it is merely *read*.
- **No archaeology.** Acting on it never requires reconstructing history or re-deriving
  priorities from commits. Each finding carries inline what the next session needs — repro,
  job, degree, platform (§6).

**Self-test (under a minute).** Open the project cold after weeks away. Without reading
history, name the single next action and one sentence on why. If you can't, the invariant is
broken and repairing it *is* the next action. This is a property of the working documents
(this file, ROADMAP.md, TODO.md), not the app. Collapsing ROADMAP.md's *Now* from a pickable
list to the single head the §4 ranking function computes was the first application of the
invariant, not an exception; TODO.md's *Next action* now reads off that same head.

## 4. The ranking function

There is **no per-item severity taxonomy** — no P0/P1, no critical/major/minor stamped by
hand. Priority is *read off* a finding:

> **priority(item) = which listed job it degrades × how badly × how often that job is done.**

- **Which job** — one of J1–J8 (§5), or none.
- **How badly** — the degree ladder, worst first. **Blocks:** the job cannot be finished in
  Trailer; the owner leaves for Preview. **Workaround:** it finishes only via extra steps
  Preview would not require. **Mars:** it finishes but something is wrong, ugly, or slow
  enough to notice.
- **How often** — the job's frequency tier (§5). **The whole frequency column is PROVISIONAL**
  — inferred, not owner-hardened.

**The sort needs no invented weights.** On (degree, frequency): if one finding is at least as
bad *and* at least as frequent, and worse on at least one, it outranks the other. Fabricating a
numeric score to trade the axes off is the hand-tuned-magic-number trap PHILOSOPHY.md warns
against; the owner confirmed the three *factors*, not their exchange rate. **Incomparable
pairs** — worse degree but rarer job, or the reverse — are genuine owner judgment (§8), so a
**PROVISIONAL working rule** keeps one head: **a Blocks outranks any lesser degree** (only a
Blocks forces the Preview fallback 1.0 forbids), then higher frequency, then the oldest
finding. This default is the top ledger question; the owner may invert it. An item that
degrades *no* listed job is **someday** — it never competes for the head.

**This file only orders the queue; it does not relax the gates.** "Done" for any ranked item
still runs AGENTS.md gates G1–G9 unchanged. The degree ladder and the tie-break are **internal
triage classifications, not user-visible defaults**, so they trip no decision-record gate (G6).

---

## 5. The substitution contract

Each job is a clause in a contract against macOS Preview, in the owner's words. For each: what
**Preview** does with zero configuration, and the **Bar** — what the owner *does* on
Windows/Linux and *observes*, testable in a real session, never by vibes and never in
implementation terms. The universal gate: **the owner finishes without wishing for Preview, on
both Windows and Linux.** These Bars are the G1 thresholds for job-shaped work. A *Today* note
appears only where current behavior would fool a naive tester or the job is not yet buildable —
this file states bars, not status (TODO.md tracks status). Frequency tiers are PROVISIONAL (§8).

**J1 — Read a multipage PDF · *frequent (PROVISIONAL)*.**
*Preview:* double-click opens at once, scrolls continuously, thumbnail sidebar, remembers your
place, smooth zoom. *Bar:* owner opens a real 100+ page PDF via the OS default-app path; first
page renders promptly; reading front-to-back needs no more than ~one keypress or scroll gesture
per page in Continuous mode; Single↔Continuous toggles by keyboard cleanly; keyboard/menu zoom
(fit-width, fit-page, actual, in/out) is crisp; sidebar rows sit tight to the thumbnails; and
after quit+relaunch, reopening **lands on the same page, zoom, and scroll offset.** *Today:*
exercised by live HITL on macOS and Windows, **never on Linux** — parity unproven, so J1 is not
met.

**J2 — Search within a PDF · *frequent (PROVISIONAL)*.**
*Preview:* one shortcut, incremental highlight, a count, next/previous scrolls each hit into
view. *Bar:* owner types a phrase deep in the doc and sees matches highlighted with an accurate
"N of M"; next/previous cycles and wraps through every hit; a genuinely absent phrase yields an
unambiguous "no matches" (not a blank counter); and a phrase living only on an auto-OCR'd
scanned page is also found — or the UI states plainly it searched only embedded text. *Today:*
search queries only the native text layer, not the OCR layer, so scanned-only hits return zero
silently, and an absent phrase looks identical to "not searched yet."

**J3 — Mark up a screenshot or image · *frequent (PROVISIONAL)*.**
*Preview:* markup toolbar — arrow, box, text, highlight — draw and save, no setup. *Bar:* owner
adds an arrow, box, text label, and highlight; restyles one via the Inspector without it
vanishing; drags a line/arrow endpoint and the line actually moves; then **copies the page to
the clipboard, pastes into another app, and every mark appears exactly as drawn** — and
reopening the saved file shows the same. *Today:* fails at the terminal step — Copy Page as
Image (base feature merged) puts the *un-annotated* raster on the clipboard, dropping every
mark; line/arrow endpoint drag is inert. Both Blocks for getting the result into another app.

**J4 — Fix the rotation of an image · *frequent (PROVISIONAL)*.**
*Preview:* one command rotates a sideways photo, save writes it upright. *Bar:* owner rotates a
sideways photo upright and saves; **another app opening the file shows it upright** (baked to
pixels, not a view flag), and a rotate-and-save round-trip does not visibly degrade the image
or strip its EXIF/camera metadata. *Today:* rotation is solid, but save re-encodes at an
uncontrolled quality with no metadata round-trip — repeated cycles Mars the photo.

**J5 — Open an image and get automatic, selectable OCR · *frequent (PROVISIONAL)*.**
*Preview:* open an image with text and it is *just selectable* — no button, no dialog. *Bar:*
owner opens a photo/screenshot with text and, **without invoking any command,** within a few
seconds drag-selects a line and pastes the correct characters into another app. *Today:* the
pipeline is real and becomes Preview-like — but only *after* a one-time manual "Recognize
Text" + consent. Before that, first-run auto-OCR is a silent no-op with no on-screen signal: a
Blocks for the "automatic" promise on a fresh machine.

**J6 — Fill a form, including PDFs with no fillable fields · *regular (PROVISIONAL)*.**
*Preview:* click a real field and type; on a *flat* PDF with none, use the text tool to type
anywhere and it lands. *Bar:* (a) owner fills a PDF with real fields — including a radio-button
group and a multiline comment — and saves; (b) owner opens a PDF with **no** fillable fields
(no AcroForm), types answers anywhere, signs. Both files are sent to another machine and opened
in three viewers the owner does not control (browser PDF viewer, phone/Acrobat, print-to-PDF):
**every answer, checkmark, radio selection, multiline comment, and signature appears, correctly
placed and legible.** *Today:* both paths render only inside Trailer's in-memory state — no PDF
appearance stream is baked, so external-viewer fidelity is unverified; radio-button fields
cannot be filled at all. Blocks on "survives being sent to someone else."

**J7 — Combine PDFs and reorder pages · *occasional (PROVISIONAL)*.**
*Preview:* drag pages between PDFs' sidebars, reorder, delete, export. *Bar:* owner merges a
second PDF in by **dragging its file onto the sidebar**; reorders pages (including two at once)
by dragging; multi-selects and deletes pages; rotates a non-current sidebar-selected page;
saves; and after close-and-reopen the **page order on disk matches the sidebar and every
surviving annotation is on the exact page it was drawn on** — none dropped, none migrated.
*Today:* the qpdf engine and the unified undo log are solid, but merge is a single-PDF
file-picker (no drag-in), multi-drag reorder and non-current/multi rotate are absent, and
annotations are written against stale page indices on save-after-delete/move — silently landing
markup on the wrong page, which the in-app tester won't see until reopen.

**J8 — Scan a document to PDF, with minor editing · *periodic (PROVISIONAL)*.**
*Preview:* import from a scanner into a PDF, then rotate/reorder/delete in the sidebar. *Bar:*
owner scans a multipage document (one page crooked, one to drop), rotates and reorders in the
sidebar, deletes the stray page, and exports one clean, right-side-up PDF — finished entirely
in Trailer. *Today:* **scanner acquisition is entirely unimplemented** on every OS (no SANE /
WIA / ImageCaptureCore; Phase 7 stretch on ROADMAP.md's *Later* list), so J8 stands at **Blocks
by construction.** Even the narrower fallback — import already-scanned pages and finish in
Trailer — is unmet: Insert Pages accepts a single PDF only, not images or multi-select. The bar
stays; the state does not meet it. See §8.

---

## 6. Intake: how a finding becomes a ranked item

Every friction observation flows the same way, and it is **not telemetry** (the capture is
local, owner-run, lives only on the owner's disk):

1. **Capture** a self-contained finding: **{ timestamp, repro, job (J1–J8 or none), degree
   (Blocks / Workaround / Mars), platform (Windows / Linux / macOS) }.**
2. **Compute** its priority by §4 — the two judgments (which job, which degree) are
   observations, not arguments, so no triage meeting assigns one.
3. **Slot** it into the order; **job = none** lands in *someday*.

Those five fields are exactly what a future session needs to act cold — which is what keeps §3
affordable. The durable artifact is the **findings**, not the raw video. **Channels** (all emit
the same shape):

- **Recorder → agent mining** *(the forward pipeline).* The owner's webcam+screen self-recorder
  captures real sessions; AI agents mine **future** recordings into structured findings. The
  recorder stalled early; this is its revived purpose. The existing 2–3 recordings are
  **already reviewed and closed — ignore them.**
- **HITL passes, reference-user smoke sessions, and audits** — the channels TODO.md already
  documents; they now emit findings in this shape.
- **Persona-lens critique** (§2) and ordinary issue reports feed the same funnel.

## 7. What 1.0 means

This document **adds** gates to 1.0; it does not remove or supersede any PHILOSOPHY.md gate.
Those remain necessary and unchanged: on-disk formats and public APIs settled; the user-visible
surface stable for a minor cycle or two; a GUI Preferences pane present (gate G7); and the
dogfood-default milestone passed with its accessibility surface in place (gate G8). On top:

> **1.0** = every job J1–J8 meets its bar (§5) on **both** Windows and Linux, **and** the owner
> has gone **N consecutive weeks** (**PROVISIONAL N = 4**, owner-tunable) of real document work
> on those OSes with Trailer as the **only** document tool for any listed job — no fallback to
> Preview or anything else for a listed job during that window.

Feature-complete is necessary but not sufficient; the second clause is the real gate,
sharpening PHILOSOPHY.md's vaguer "lived with it long enough." A format thrash or UX regression
that forces the owner back to Preview **resets the clock.** 1.0 requires *all* gates —
PHILOSOPHY's stability set and this file's parity + dwell set. The wording and N are logged in
§8; proposed and not objected to, not ratified.

## 8. Ambiguity ledger

Genuinely open questions that need owner judgment. Per the scope rule these are **not
guessed** — they wait for the next interview pass, while the PROVISIONAL defaults above stand so
the machine keeps producing a single next action. Resolve an entry by editing the relevant
section and deleting it here; a non-empty ledger is normal, an *ignored* ledger is the failure
mode.

1. **N for the 1.0 dwell test.** Provisional N = 4 weeks — confirm or tune.
2. **1.0 bar ratification.** The §7 wording was proposed and not objected to, not explicitly
   ratified. Needs an explicit yes.
3. **Frequency tiers (J1–J8).** The entire §5 column is inferred (J1–J5 frequent, J6 regular,
   J7 occasional, J8 periodic). Confirm or re-tier — the §4 sort depends on it.
4. **Ranking tie-break (§4).** The three factors are confirmed; their exchange rate is not. The
   provisional default (any Blocks outranks any lesser degree) is the highest-leverage open
   question — ratify or invert it.
5. **The "someday" pool (§4).** Do items degrading no listed job get a periodic sweep, or
   surface only when a future finding implicates a job?
6. **J8 scope.** Does the J8 bar require in-app scanner drivers (Phase 7 stretch), or is "import
   already-scanned pages, then edit" sufficient for parity?
7. **macOS regressions.** macOS is reference/fallback, not scored (§1). Not asked: is a macOS
   *regression* a release blocker, or purely best-effort?
8. **"No silent data loss" as a job clause.** No §5 Bar tests PHILOSOPHY.md's *Never worry
   about saving* floor — J1 checks *session position* surviving quit+relaunch, not *unsaved
   edits* surviving a close, which decision record
   [`0004-never-worry-save-invariant`](docs/decision-records/0004-never-worry-save-invariant.md)
   (**proposed**, owner-escalation) shows Trailer silently discards. Making "no silent data
   loss" a per-job clause is CRITERIA's call; the invariant-vs-opt-out design is ADR-0004's.
   (Distinct from ranked "changed on disk" external-edit detection.)
