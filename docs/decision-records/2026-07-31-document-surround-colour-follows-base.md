# PDF/TwoPage document-surround colour self-heals to match the image viewer's Base when Dark would be lighter

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the agent role named for this record (session
  `claude/ui-deference-polish`); the owner (programmerq) is the
  escalation-only override.
- **Date proposed:** 2026-07-31
- **Date accepted / superseded:** 2026-07-31 (owner directive, relayed via
  the session brief — see Context)

## Context

Owner directive (verbatim, relayed via the coordinator brief for this PR):

> "I like the color behind an image that isn't large enough to take up the
> whole document area. But when a PDF is open, it's using a grey that's
> too light in this dark mode. Maybe it's an image<>qpdf difference?"

**What ships today on `main` (before this record):** `ImageDocument`'s
`QScrollArea` reads `QPalette::Base` for the letterbox behind a small image
(`ImageAdapter.cpp`). `PdfDocument`'s `QPdfView` and the `TwoPageView`
facing-pages surface both instead read `QPalette::Dark` — a bevel/groove
shading role, not a page-backdrop one. Trailer's dark palette is
synthesized entirely by `QStyleHints::setColorScheme` (no hand-built dark
`QPalette` of our own — see DR 2026-07-20-theme-applies-live), and in that
synthesis `Dark` is not guaranteed to stay darker than `Base` the way it
reliably does in the light palette. The owner's hypothesis (an
image-vs-qpdf render-path difference) is confirmed: two independently
chosen palette roles, not one shared rule.

**The naive fix, and why it's rejected below:** literally switching the PDF
surround to `QPalette::Base` unconditionally (matching the image path
byte-for-byte in every theme) was the first attempt. It makes the reported
dark-mode bug disappear, but ALSO makes a light-mode PDF page invisible
against its own canvas — Trailer's stock light palette has `Base` = pure
white (`#ffffff`), identical to a typical PDF page, while `Dark`
(`#9f9f9f`) is a genuinely distinct, darker grey. This is not a hypothetical
concern: the pre-existing regression guard
`uat_vwr_079_zoomReadoutMatchesRenderScale`
(`tests/uat/test_uat_two_page.cpp`) measures a rendered page by scanning
for white-vs-canvas contrast, and it started failing under the naive fix —
an independent, mechanical confirmation that "PDF canvas == page colour" is
a real regression, not just a test-implementation quirk. It also matches
how every mainstream PDF viewer (Preview, Acrobat, Chrome's built-in
viewer) treats a canvas: visibly recessed behind a typically-white page.

## Options

- **A. Unconditional `Base`.** PDF/TwoPage surround always reads
  `QPalette::Base`, matching the image viewer literally in every theme.
  Fixes dark mode; breaks the light-mode page/canvas contrast described
  above.
- **B. Leave `Dark` alone; special-case dark mode with a hand-picked
  literal colour.** A new magic-constant dark grey, chosen once, used only
  when the app theme is Dark. Reintroduces exactly the "two independently-
  derived constants that can drift apart" problem this record is meant to
  close, just with a different pair of constants.
- **C. Clamped shared rule (what ships): prefer `Dark`, fall back to
  `Base` whenever `Dark` would resolve lighter than `Base`.** One function,
  `trailer::documentSurroundColor(const QPalette&)`
  (`src/util/DocumentSurroundColor.h`), used by both `PdfDocument`
  (`PdfAdapter.cpp`, `applyViewPalette`) and `TwoPageView`
  (`TwoPageView.cpp`, `paintEvent`). Light mode is a deliberate no-op
  (`Dark` already reads darker there); dark mode self-heals to `Base`
  exactly when `Dark` would otherwise read lighter.

## Personas debate

- **Office non-technical user:** Never looks at a raw palette value; sees
  "the PDF background looks weird/washed-out in dark mode" (today) vs. "it
  matches the rest of the dark app" (after). Option A's light-mode
  side-effect (white-on-white PDF pages) would read as "the page
  disappeared" to this same user when zoomed out or in Two-Pages mode — a
  new, unrelated complaint. Favours C.
- **Older careful user:** Relies on the visible page boundary to feel
  oriented in a long or zoomed-out document; a canvas identical to the page
  removes that landmark. Favours C; would object to A on sight.
- **Power migrator:** Compares against Preview/Acrobat muscle memory — a
  recessed grey canvas behind a page is the expected convention on every
  platform they've used. Favours C.
- **Occasional user:** No strong opinion on canvas shading either way, but
  would notice if a page "vanished" against a white background (Option A's
  light-mode regression) as a bug, not a feature. Favours C by absence of
  objection.

## Admissible objections

- **Older careful user, Option A, "zoom out or enter Two-Pages mode" step:**
  the page loses its visible boundary against a white canvas — the exact
  failure `uat_vwr_079` independently caught. Decisive against A.
- **Power migrator, Option B, "any future dark-mode retuning" step:** a
  second hand-picked literal invites the same independent-drift bug this
  record exists to close — the next contributor who nudges the app's dark
  palette has no reason to know a second, unrelated constant needs to move
  in step. Decisive against B.

### Rejected as naked preference

- "Just always use Base, it's simpler." — rejected: states no concrete
  user/step/failure, and the concrete failure (`uat_vwr_079`, plus the
  mainstream-PDF-viewer convention) already weighs the other way.

## Checkable threshold this record establishes

- **Light mode unchanged.** `documentSurroundColor(lightPalette).lightness()
  < lightPalette.color(QPalette::Base).lightness()` — the PDF/TwoPage
  canvas stays darker than a white page. (`uat_xct_005_document
  SurroundColourFollowsPaletteLive`, `tests/uat/test_uat_preferences.cpp`.)
- **Dark mode self-heals.** For a palette where `Dark` resolves lighter
  than `Base` (reproducing the reported bug on a palette Trailer's own dark
  synthesis is not guaranteed to avoid), `documentSurroundColor(palette) ==
  palette.color(QPalette::Base)` exactly — the PDF surround matches the
  image viewer's colour precisely in the case that was broken. (Same test.)
- **Live, not just at open.** A runtime theme flip (PR #105) re-derives the
  colour via `IDocument::refreshViewPalette()` (new virtual, no-op default)
  → `MainWindow::refreshThemedIcons()`, across every open document in every
  open window, not only the current tab. (Same test; also
  `tests/uat/test_uat_deference_evidence.cpp` for the G2 screenshots.)
- **No pre-existing regression.**
  `uat_vwr_079_zoomReadoutMatchesRenderScale` (`test_uat_two_page.cpp`)
  continues to pass unmodified.

## Arbiter verdict + rationale

**Option C is adopted.** The owner's directive is dispositive on the
existence of a bug; the admissible objections are decisive against both A
(a real, independently-confirmed light-mode regression) and B (reintroduces
the drift problem under a different name). C fixes exactly the reported
symptom — measured as "`Dark` resolves lighter than `Base`" — with zero
effect on the already-correct light-mode appearance, and is computed live
from the palette rather than hand-tuned, so per PHILOSOPHY's "hand-tuned
values stay hand-tuned" this is explicitly NOT a magic constant needing a
range-tried/symptom-to-change comment: it is a comparison rule, re-derived
fresh every time, that cannot itself drift.

Implementing seams: `src/util/DocumentSurroundColor.h`
(`documentSurroundColor`), `src/document/PdfAdapter.cpp`
(`applyViewPalette`, `refreshViewPalette`), `src/ui/TwoPageView.cpp`
(`paintEvent`), `src/document/IDocument.h` (`refreshViewPalette` virtual),
`src/ui/MainWindow.cpp` (`refreshThemedIcons`, extended to loop every open
document).

## Consequences

- **Positive.** The reported dark-mode bug is gone; the PDF surround now
  matches the image viewer exactly whenever the previous behaviour was
  wrong. Light mode — already correct, never complained about — is
  provably untouched. The rule lives in one place, so light and dark modes
  (and any future theme) cannot independently drift again.
- **Costs / follow-ups.** `ImageDocument` is deliberately NOT switched to
  `documentSurroundColor()` — its content is rarely pure white, so `Base`
  already reads fine there, and the owner explicitly said the image
  behaviour is correct as shipped. A future report that the image
  letterbox looks wrong for a specific (e.g. all-white) image would be a
  new, separate finding, not evidence against this record.

## Evidence required to reopen

A measured case where the clamp itself produces a wrong-looking canvas —
e.g. a real (non-offscreen) platform's dark-palette synthesis where `Dark`
stays darker than `Base` but still looks "too light" by some other measure,
or a persona-admissible objection to the light-mode grey canvas itself
(not the now-fixed dark-mode bug) — plus owner sign-off.
