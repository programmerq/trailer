# A pasted capture's scale comes from what the source declares; the zoom ladder carries a pixel-exact stop

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-08-02
- **Date accepted / superseded:** 2026-08-02 (accepted)

## Context

Owner dogfooding report, nightly build `0.3.1-dev+768.gce56b4b8`, macOS,
Retina (2x). A macOS **window** screenshot was pasted with `File > New from
Clipboard`. Two things went wrong, and they are separable:

1. The app reported **100% zoom** but drew the image visibly **2x too
   large** — the size of the whole screen rather than the size of the
   window that was captured.
2. Tapping ⌘− a couple of times to correct it landed on **51%**, so the
   image was resampled and read blurry, instead of sitting crisp at the
   exact 1:2 device mapping it needed. **50% was unreachable.**

**What shipped on `main` before this change.**

*Scale recovery.* `Application::newFromClipboard()` recovered a
devicePixelRatio for a paste by comparing the pasted image's raw pixel size
against each connected screen's **full device resolution**
(`QSizeF(scr->size()) * scr->devicePixelRatio()`), and stamping that
screen's dpr on an exact match. That rule only ever matched a
**whole-screen** capture. A window- or region-sized capture matched nothing
and fell through to `dpr = 1.0`, so `ImageDocument`'s capture-origin default
of Actual Size (`applyInitialFitZoom`, `src/document/ImageAdapter.cpp`)
mapped one image *device* pixel to one *logical* point — exactly 2x too
large on a 2x screen. The zoom readout was not lying: the factor really was
1.0. The dpr underneath it was wrong.

The narrow rule was itself a correction. An earlier blanket "stamp the
primary screen's dpr whenever `dpr <= 1`" was reverted because on Retina it
halved the logical size of **every** ordinary paste — a copied logo, a
diagram, pixel art. That regression is the fence any change here has to stay
inside.

*Zoom ladder.* `ImageDocument::zoomIn/zoomOut`
(`src/document/ImageAdapter.cpp`) multiply/divide the current factor by
`kZoomStep = 1.25` (`src/document/ImageAdapter.cpp:54`) with no fixed stops
at all. From 100% the descending rungs are 80%, 64%, **51.2%**, 41.0%. 50%
is not on that ladder and never will be; nor is 100% once you have left it
by any route other than exact powers of 1.25. And
`buildDisplayPixmap` (`src/document/ImageAdapter.cpp:103`) had a
no-resample fast path only at factor 1.0, stamping the *image's* dpr — so
even the near-miss 51.2% was a resample that the compositor then upscaled
again on a 2x screen: a double resample, permanently soft.

## Options

**A — Widen the size heuristic.** Treat a paste as a 2x capture when
interpreting it at 1x would make it larger than the screen but interpreting
it at 1/dpr would fit. Cheap, no platform code, and it would have fixed the
owner's paste.

**B — Read the scale the source declares, and only that.** Add the
platform pasteboard's own declared scale as a signal (macOS records
screenshots as 144-dpi bitmaps, 2 px per point — the same fact Preview uses
to open a Retina screenshot at half size), accept it only when it names one
of the attached screens, and answer "dpr 1, nothing declared" otherwise.

**C — Consult `QImage::dotsPerMeterX()` as a second declared-scale source**
so the rule also fires on platforms whose clipboard preserves PNG `pHYs`.

**D — Do nothing about recovery; fix only the zoom ladder** so the user can
correct any mis-scaled paste in one keystroke, crisply.

**E — Zoom ladder: add fixed stops** at Actual Size and at the pixel-exact
factor (`imageDpr / screenDpr`), snapping a step to a stop when the stop is
the nearest rung; and let `buildDisplayPixmap` pass the source through
unresampled at that stop.

## Personas debate

- **Office non-technical user:** pastes a screenshot into Trailer to circle
  something. Does not know what a devicePixelRatio is, and will not go
  hunting for a zoom percentage — the paste has to *look right*. Would be
  actively harmed by option A misfiring on the photos and diagrams they
  paste far more often than screenshots: a silently half-size logo is a
  worse bug than a too-large screenshot, because nothing on screen hints
  that a scale decision was made at all.
- **Older careful user:** notices the readout says 100% while the image is
  plainly not 100% and does not trust the app afterwards. Cares most that
  the number and the render agree, and that a correction they make *stays*
  made. Wants a stop they can land on reliably, not a percentage that
  wanders (51%, then 41%, then 32%).
- **Power migrator (from Preview / Acrobat):** expects Preview's behaviour —
  a Retina screenshot opens at the size of the thing it captured, because
  Preview honours the bitmap's declared resolution. Also expects ⌘0 to mean
  Actual Size and to be reachable. Would read option A as "the app is
  guessing at my images" and option D alone as "the app still can't do what
  Preview does".
- **Occasional user:** opens Trailer a few times a month. Has no memory of
  the workaround. Any fix that requires knowing to press ⌘− exactly three
  times is not a fix for them — which is why D alone is not enough, and why
  the recovery half matters even though it cannot always succeed.

## Admissible objections

- **A silently halves ordinary pastes — office user, "copy a photo, paste
  it, mark it up", the image opens at half size with no explanation.** Any
  size-based rule has to fire on images that are bigger than the screen,
  and most photos are. This is the same failure the reverted blanket rule
  caused; re-introducing it in a new shape is not progress.
- **C cannot distinguish "no metadata" from "72 dpi" — every user on a host
  whose logical DPI is 2x the 72-dpi baseline, on every paste.** Qt seeds
  `QImage::dotsPerMeterX()` from the platform's logical DPI when the source
  declared nothing, so there is no sentinel for "unset". On such a host the
  rule would fire on ordinary pastes and halve them. This is objection 1
  again, wearing a metadata costume.
- **D alone leaves the occasional user stranded — occasional user, "paste a
  screenshot", it is twice the size and they do not know the remedy.** A
  correction that only a user who knows about it can apply is not a fix for
  the reported behaviour, only for its severity.
- **B cannot fix Windows/Linux — any user pasting a HiDPI window screenshot
  on those platforms, at step 2, gets a 2x-too-large image.** True, and not
  hidden: neither platform exposes a trustworthy declared scale through Qt
  (see `src/platform/ClipboardScale_stub.cpp`). B is a partial answer, which
  is why it ships *with* E rather than instead of it.
- **B mis-sizes a genuine 144-dpi scan on a 2x screen — power migrator,
  "paste a scanned page", it opens at half size.** Admitted. But 144 dpi
  genuinely declares 2 pixels per point, and Preview gives the same answer,
  so the behaviour is consistent with the reference app rather than novel;
  and E puts the correction one keystroke away.
- **A pixel-exact stop that is also a trap would be worse than none —
  careful user, "keep zooming in past the crisp point", the zoom sticks.**
  Real, and designed against: a detent is never re-snapped when the factor
  is already sitting on it.

### Rejected as naked preference

- "51% is close enough to 50%." — rejected: states no concrete user, step,
  or failure, and contradicts the reported one (the image is resampled at
  51% and crisp at 50%; that is a visible difference, not a rounding taste).
- "Just always open pastes at Fit." — rejected: names no user or step, and
  changes an unrelated shipped default (capture-origin images open at
  Actual Size) to avoid stating what the scale is.

## Checkable threshold this record would establish

1. **Scale recovery (`src/util/CaptureScale.h`,
   `recoverCaptureDpr`).** A pasted image's devicePixelRatio is taken from,
   in order: the image's own stamp when > 1; the scale the platform
   clipboard declares, **only when it equals a connected screen's
   devicePixelRatio**; an exact match between the raw pixel size and a
   screen's full device resolution. Otherwise **1.0**. Concretely: a
   2048x1330 image with a declared scale of 2.0 on a host with a 2x screen
   recovers 2.0; the same image with nothing declared recovers 1.0; a
   512x512 image with nothing declared recovers 1.0; a declared scale of
   300/72 with no 4.167x screen attached recovers 1.0.
2. **True size at Actual Size.** A capture of `W x H` device pixels whose
   dpr is recovered as 2 occupies exactly `W/2 x H/2` logical points at
   Actual Size (already held by `contentSizeHintIsLogical` /
   `actualSizeIsPixelExact`; rule 1 is what makes it *reachable* for a
   window-sized capture).
3. **The zoom ladder contains an exact pixel-exact stop
   (`src/document/ZoomStops.h`).** For an image of devicePixelRatio `i` on a
   screen of devicePixelRatio `s`, the ladder contains a stop at exactly
   `i / s`, and tapping zoom out/in reaches it exactly rather than passing
   near it. Concretely: `i = 1, s = 2` — three zoom-out taps from 100% land
   on **exactly 0.5** (readout `50%`), not 0.512; `i = 2, s = 1` — three
   zoom-in taps land on **exactly 2.0** (readout `200%`), not 1.953125.
   100% is likewise a stop. Neither stop is a trap: one further tap leaves
   it.
4. **The pixel-exact stop renders unresampled.** At factor `i / s` the
   pixmap handed to the view has pixel size equal to the source image's and
   carries the **screen's** devicePixelRatio.
5. **Inert when the dprs match.** When `i == s` the pixel-exact stop
   collapses onto 100% and the ladder is bit-identical to the pure
   geometric one (80% / 64% / 51.2% / 41.0%; 125% / 156.25% / 195.3%).

## Arbiter verdict + rationale

**Adopt B + E. Reject A and C. Reject D as a complete answer, but adopt its
substance as half of the fix.**

A and C are rejected on the same admissible objection from two personas —
they silently halve ordinary pastes, which is the exact regression the
current narrow rule exists to prevent. The difference between B and them is
not conservatism for its own sake: B only ever repeats a number the *source*
stated, and refuses it unless it names a screen that is actually attached.
That is a recovered fact, not an inference from shape. C fails not because
image DPI is a bad idea but because Qt's API has no "unset" sentinel for it,
so the fact cannot be recovered reliably; B's platform seam reads the
pasteboard's own bitmap representation, which does report a true 1.0 when
nothing was declared.

D is rejected alone because the occasional user has no way to know the
remedy. But B cannot succeed everywhere — it is honestly empty on Windows
and Linux — so shipping B without E would leave those users with no route at
all. E is what makes the residual case recoverable *and* crisp, and it is
independently right: a ladder with no fixed stops cannot return to 100%
either, which is a defect in its own right that this record also closes.

The two together give: the paste opens correctly when anything declares its
scale, and is one keystroke from correct-and-crisp when nothing does. What
is deliberately *not* claimed is that Trailer can always know a bare
bitmap's intended scale. It cannot, and `src/util/CaptureScale.h` says so in
the code rather than in a comment nobody reads.

Constants and their in-code rationale (PHILOSOPHY → *Hand-tuned values stay
hand-tuned*):

- `src/document/ImageAdapter.cpp:54` — `kZoomStep = 1.25`, **unchanged** by
  this record. The ladder's ratio is not what was wrong.
- `src/document/ZoomStops.h` — `pixelExactZoomFactor()` (the `imageDpr /
  screenDpr` derivation) and `steppedZoomFactor()` (the half-step snap
  threshold, `sqrt(step)`, i.e. the midpoint between two rungs in log
  space). The half-step rule rather than a crossing rule is load-bearing:
  0.512 never *crosses* 0.5, it stops just short of it, so a crossing rule
  would have left the owner parked on the blurry 51% this record exists to
  fix.
- `src/document/ZoomStops.h` — `kZoomStopEpsilon = 1e-9`, the "already
  sitting on a detent" tolerance.
- `src/util/CaptureScale.cpp` — `kDprMatchTolerance = 1e-6`, how exactly a
  declared scale must name a screen's dpr.

## Evidence required to reopen

- **On the recovery half:** a dogfooding report where rule 2 fires on an
  image that is *not* a screen capture and visibly mis-sizes it (the
  144-dpi-scan case made real), or a report where a macOS capture pasted
  from the clipboard still opens 2x too large — which would mean the macOS
  pasteboard does not declare the scale after all, and the record's premise
  for that platform is wrong. Either reopens B.
- **On the ladder half:** a report that the detents make zooming feel sticky
  or unpredictable in ordinary same-dpr use — which the "inert when the dprs
  match" threshold says cannot happen, so such a report would be evidence
  the implementation does not match this record.
- **On both:** owner sign-off, per the escalation-only override.
