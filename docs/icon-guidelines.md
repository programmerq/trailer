# Trailer Icon Guidelines

Trailer is a Qt 6 / C++20 desktop PDF + image workbench (Linux /
macOS / Windows, MIT licensed). The main toolbar calls
`setIconSize(QSize(18, 18))`; today every toolbar action is a text
label. Goal: a quiet, glanceable, document-focused icon family —
closer to macOS Preview than to GIMP — without coupling ourselves to
any one platform's chrome.

This document is deliberately polyphonic. §1 surveys the major
systems on their own terms, §2 stages the disagreements between them,
§3 commits to a Trailer-specific recommendation, §4 is the standalone
artist brief.

---

## 1. The landscape

**Apple SF Symbols.** 1000×1000 pt grid, **9 weights × 3 scales**,
optically balanced against San Francisco. Active state = outline →
filled swap of the same glyph. License is free for Apple-platform
apps only; redistribution outside one is not permitted. Wonderful
inspiration, unusable as a shipped asset for us.

**Google Material / Material Symbols.** 24 dp on a 24 × 24 grid,
20 dp live area, 2 dp padding. **Filled** and **outlined** variants;
Material Symbols adds 7 weights × 3 styles as a variable font. 2 dp
outlined stroke. Apache 2.0. Reads "web-app-y" on desktop chrome.

**Microsoft Fluent UI System Icons.** Multi-size *native*: dedicated
drawings at **16 / 20 / 24 / 28 / 32 / 48 px**. Stroke **1 px at 16,
1.5 px at 20–24**, pixel-snapped. Two themes — **Regular** (default)
and **Filled** (selected). MIT. Standout property: small sizes are
redrawn, not scaled, so strokes don't go muddy.

**Phosphor.** **Six weights** — Thin (1 px) / Light (1.5) / Regular
(2) / Bold (2.5) / Fill / Duotone — on a 16 × 16 grid. Rounded
terminals, characterful flourishes. MIT. Duotone is unique in the
open-source landscape.

**Lucide / Feather.** **Single weight, stroke only.** 24 × 24, 2 px
stroke, 2 px corner radius, rounded caps and joins. No filled
variant — rejects the question on aesthetic grounds. ISC / MIT.
Cheapest correct-looking option; the uniform-stroke aesthetic sits
most comfortably ≥ 24 px.

**IBM Carbon.** Artboards at **16 / 20 / 24 / 32 px**, 2 px stroke,
2 px outer padding, 2 px corner radius only where the metaphor
demands; otherwise hard 90°. Apache 2.0. Reads "serious software for
serious work."

**Tango / FreeCAD heritage.** The 2000s freedesktop.org palette and
descendants (GIMP, Inkscape, Scribus, FreeCAD): **colorful,
isometric, illustrative** — a tiny scene per button. Public domain
since 2009. Personality and recognition for unusual verbs, but mud
at 18 px, no clean dark-mode path (you can't template-tint a colored
illustration), and the "toy box" feel is wrong for a workbench.

---

## 2. Where these systems disagree

**Outline vs. filled-as-active-state.** Apple, Material, and Fluent
all pair outline/filled variants and use *fill* to mean "selected."
Lucide rejects the question — state is a job for color and background,
not glyph swap. Phosphor's six weights make it fluid. For Trailer's
markup toolbar, where the user must see *which* drawing tool is
armed, glyph swap (outline → fill) is a much louder signal than a
tint alone. We will pay the cost of authoring two variants for tools
with a meaningful armed state.

**Single weight vs. multi-weight.** Lucide's one-weight-everywhere
produces uncanny consistency; SF Symbols' nine weights produce nuance
no small team can match. Honest middle: **one weight** for ~95 % of
icons, **one heavier "filled" sibling** for armed states. More than
that is rope to hang yourself with.

**Grid size.** Fluent and Carbon insist that **16 / 20 px must be
drawn at those sizes**, not downsampled from 24. Trailer renders at
**18 px** — between Fluent's 16 and 20 masters. The move: **design at
24 px** with explicit downscale rules (§3.2), and hand-tune a
separate 16 px master later if a tight-density mode arrives.

**Corner radius and terminals.** Carbon: hard 90° default. Lucide /
Phosphor: rounded everywhere. **Hard 90° reads as engineer's tool;
universally rounded reads as consumer app.** A document workbench
wants the middle: square corners where the real-world object has
them (page edge, redact block), rounded where the metaphor is gentle
(highlight tip, pen).

**Stroke weight at 18 px.** A 2 px stroke at a 24 px grid scales to
**1.5 px at 18 px** — exactly Fluent's 20-px stroke. That is the
readability floor. A 1.5 px stroke at 24 px scales to 1.125 px at 18,
which goes grey on Windows (SVG strokes get no ClearType help).

**Color usage.** Three positions: **monochrome template** (SF Symbols,
Carbon, Lucide, Fluent Regular) — single foreground, host paints, dark-
mode-trivial; **duotone** (Phosphor) — two paint colors, beautiful but
two tints must both survive both themes; **illustrative** (Tango,
FreeCAD) — disqualified for us (muddy at 18 px, no dark-mode path). We
recommend monochrome template with one accent-tinted *fill* for the
armed-tool state.

**Custom vs library.** Lucide and Phosphor are MIT and would drop in
cleanly, but Trailer's verbs (Redact, AutoFill, Sign Here, Zoom Lens,
Speech Bubble) either don't exist in those libraries or carry the
wrong metaphor. Recommendation: **hybrid** — use Lucide where a
generic icon already says exactly what we need (Zoom In, Rotate,
Sidebar), commission custom drawings in a Lucide-compatible style for
the rest. Keeps the family coherent; cuts commission scope.

---

## 3. Recommendation for Trailer

### 3.1 Visual style
- **Stroke-based, monochrome template**, drawn as `<path>` with
  `stroke="currentColor"` and `fill="none"` by default — one color
  in the file so Qt / CSS recolors at runtime.
- **2 px stroke on a 24 × 24 grid.** Rounded caps and joins
  (`stroke-linecap="round"`, `stroke-linejoin="round"`) — the single
  decision that buys the most softness without going Phosphor-friendly.
- **Corner radius 2 px** for rounded metaphors; **90° corners** where
  the real-world object is square (page edge, redact block,
  text-frame outline). Do not universally round.
- **Filled "active" sibling** for drawing tools only. Same path,
  `fill="currentColor"`, `stroke="none"` (or 2 px stroke if the glyph
  would otherwise lose its silhouette).
- **No shadows, no gradients, no embedded raster.** Ever.

### 3.2 Grid and sizing
- **Design at 24 × 24** with a **20 × 20 live area** (2 px outer
  padding). A circle and a square that both fill the live area read
  as the same visual weight.
- **Snap every endpoint, junction, and arc center to the 1 px grid at
  24 px.** A line from (4, 12) to (20, 12) is correct; (4.3, 12.1) is
  not.
- **Target render at 18 px.** Effective stroke 1.5 px, effective live
  area 15 × 15. To survive the downscale:
  - **≥ 3 px** spacing between any two visual elements on the 24 px
    grid (= 2.25 px at 18 px). Anything closer smudges.
  - **≤ 3 distinct visual elements** per icon; two is better. A
    "speech bubble with a redact bar through it" is too much.
  - When in doubt, simplify the silhouette — the 18 px viewer reads
    silhouette first, interior second.
- **Future 16 px master**, if a tight-density mode arrives: hand-tuned,
  1.5 px stroke, 1 px padding. Do not auto-scale from 24.

### 3.3 State variants
- **Idle**: outline glyph, `currentColor` (toolbar foreground role).
- **Hover**: no glyph change; the button background changes.
- **Armed (drawing tools)**: **filled sibling**, tinted to the system
  accent (`palette(Highlight)`). Glyph swap is the primary signal,
  tint is secondary.
- **Checked but not armed (toolbar visibility toggles)**: no glyph
  swap; standard checked background, outline glyph stays.
- **Disabled**: same glyph at **40 % opacity** of `currentColor`. Let
  Qt's `QIcon::Disabled` handle it via opacity; don't ship a separate
  gray asset.

### 3.4 Color and theming
- Every SVG: `stroke` and/or `fill` = `currentColor`, **no other
  colors**. Qt's SVG renderer honors `currentColor` when the host
  stylesheet sets `color`, which gives light / dark / high-contrast
  support in one file.
- Load via `QIcon::fromTheme("tool-rectangle",
  QIcon(":/icons/tool-rectangle.svg"))` so XDG icon themes can
  override on Linux with a Qt resource fallback elsewhere.
- **Light theme** foreground `#1c1c1e` (pure black is harsh against
  the paper-white doc view). **Dark theme** foreground `#e6e6e6`
  (pure white against `#1e1e1e` chrome is too hot).
- **Accent (armed)**: OS accent via `palette(Highlight)`. Never bake
  color into the SVG.
- If a glyph genuinely needs two tones (e.g., Note's fold), use
  `opacity="0.55"` on the secondary path — it inherits `currentColor`
  and degrades to a half-tone in both themes.

### 3.5 File format and naming
- SVG 1.1; root attributes: `xmlns`, `viewBox="0 0 24 24"`, **no
  `width` / `height`** (let the consumer size it).
- Single `<svg>` root, no `<defs>` unless required, **no `<style>`
  blocks** (Qt's SVG renderer is conservative about CSS), no
  `<script>`, no `<image>`.
- Optimize with SVGO: `removeViewBox: false`,
  `convertColors: { currentColor: true }`, `removeDimensions: true`.
  Strip editor metadata.
- **Naming**: snake-case with group prefix indicating the toolbar the
  icon lives on — `tool-` (markup verbs), `view-` (view ops), `page-`
  (page ops), `panel-` (chrome toggles), `sidebar-` (sidebar modes).
  Active sibling appends `-filled` (describes the asset, not the
  semantic — `-active` and `-on` are wrong).
- Prefer a single `currentColor` file over light/dark twin files;
  twins only when a glyph genuinely must change shape in dark mode
  (none in §3.6 should).

### 3.6 Specific tool inventory
Each entry: shape concept, the *why* where the metaphor isn't
obvious, and `-filled` variant behavior. All coordinates on the 24 px
grid.

**Selection and shape tools**

- `tool-select` — Arrow cursor pointing up-left, square tail. The
  universal selection arrow; don't reinvent the most-used tool. No
  filled variant (selection is implicit default).
- `tool-rectangle` — Empty square, 16 × 12, hard 90° corners. Filled:
  same outline plus a 2 px-inset filled interior — "fills with ink."
- `tool-ellipse` — Empty ellipse on the same 16 × 12 envelope. Filled
  mirrors the rectangle pattern.
- `tool-line` — Diagonal (4, 20) → (20, 4), rounded caps. Filled:
  3 px stroke.
- `tool-arrow` — Same diagonal with a 5 px arrowhead at (20, 4).
  Filled: 3 px stroke + filled arrowhead.
- `tool-freehand` — Single wavy stroke that crosses itself once
  (suggests handwriting). Pen metaphor without drawing a pen.
- `tool-text` — Capital "A" with a baseline tick. Not "abc" or
  cursor-in-text — the bare letter is more legible at 18 px.
- `tool-note` — Folded-corner sticky-note rectangle, two paths.
  Filled: body fills, fold stays outlined (use `opacity="0.55"` on the
  fold — the one acceptable near-duotone).
- `tool-speech-bubble` — Rounded rect (4 px corner) with a single
  triangular tail at bottom-left. Filled: bubble and tail both fill.
- `tool-highlight-shape` — Small rectangle with one diagonal corner
  shaded (quarter-circle). The *shape* highlight, distinct from the
  text highlighter — should not look like a marker pen.
- `tool-zoom-lens` — Magnifying glass at 30° with two horizontal
  "scan lines" inside the lens. The scan lines distinguish it from
  `view-zoom-in`.

**Text-markup tools** (apply to text runs, not freehand shapes)

- `tool-highlight` — Marker-pen tip at 30° with a short horizontal
  stroke beneath it (the highlighted text-line). The pen tip is what
  prevents collision with `tool-zoom-lens`.
- `tool-underline` — Capital "U" sitting on a 2 px horizontal line.
- `tool-strikeout` — Capital "S" with a horizontal line through it.
- `tool-redact` — **Solid filled black block, 16 × 8, centered.** No
  text under it, no scissors, no marker pen. Redaction destroys
  content; the metaphor must be "this region becomes a black bar."
  The *un-armed* variant is the same rectangle drawn as 2 px stroke
  with no fill. This is the inverse polarity of other tools, and
  that's the point — it telegraphs danger.

**Form-filling tools**

- `tool-autofill` — Form-field rectangle with a downward chevron
  above it: "drop into field."
- `tool-sign-here` — Cursive "x" with a baseline tick, like the X on
  a signature line. The flourish distinguishes it from `tool-xmark`.
- `tool-checkmark` — Heavy check, 2 px stroke, rounded caps,
  (5, 13) → (10, 18) → (19, 7). Inserts a check glyph.
- `tool-xmark` — Two diagonals corner-to-corner of the live area,
  rounded caps. Inserts an X glyph.

**View and navigation**

- `view-zoom-in` — Magnifying glass at 30°, plain lens, "+" inside.
- `view-zoom-out` — Same, "−" inside.
- `view-zoom-actual` — Same, "1:1" inside (or "1" if 1:1 doesn't fit).
- `view-fit-page` — 3:4 page rectangle, square corners, two opposing
  arrows pointing inward corner-to-corner.
- `view-fit-width` — Same page, two arrows on the horizontal axis only.

**Page operations**

- `page-rotate-left` / `page-rotate-right` — Page rectangle with a
  curved arrow above indicating direction. Show *intent*, not motion:
  do not draw the page mid-rotation.

**Panel toggles**

- `panel-sidebar` — Page rectangle with a thin vertical column on the
  left. Filled: column fills, page stays outlined.
- `panel-markup` — Page rectangle with a pen-nib glyph hovering over
  it. Filled when markup toolbar is visible.
- `panel-form` — Page rectangle with a single horizontal field line
  inside. Filled when form-filling toolbar is visible.

Sidebar-mode picker should use individual icons in the same family,
not a generic "menu":

- `sidebar-thumbnails` — 2 × 2 grid of small rounded rectangles.
- `sidebar-outline` — Three stacked horizontal lines of varied length
  (nested-list metaphor).
- `sidebar-annotations` — Speech-bubble silhouette at 80 % size of
  `tool-speech-bubble`.
- `sidebar-bookmarks` — Pennant / ribbon: square at top, v-notch at
  bottom.

### 3.7 Acceptance criteria
Checklist for reviewing any icon, internal or commissioned:

- [ ] `viewBox="0 0 24 24"`, no `width` / `height` attributes.
- [ ] Only `currentColor` (optionally `opacity` ≤ 0.6 on at most one
      secondary path). No hex, `rgb()`, gradients, filters,
      `<image>`, `<script>`, `<style>`.
- [ ] Stroke width exactly **2 px**; caps and joins `round`.
- [ ] Endpoints, junctions, and arc centers on the integer grid at
      24 px (half-pixels only where stroke centering demands).
- [ ] Live area ≤ 20 × 20; ≥ 2 px outer padding.
- [ ] **≤ 3** distinct visual elements; **≥ 3 px** clearance between
      any two.
- [ ] Clean at **18 px**; silhouette recognizable at **14 px** even
      if interior detail collapses.
- [ ] Silhouette unique at 18 px against every other icon in §3.6 —
      especially `tool-highlight` vs `tool-zoom-lens`, `tool-checkmark`
      vs `tool-sign-here`, `view-fit-page` vs `view-fit-width`.
- [ ] Filled variant exists iff the tool has an armed state; shares
      the outline's silhouette; unambiguous against the outline at
      18 px; named with `-filled` suffix.
- [ ] SVGO with project config is idempotent (re-run produces a
      byte-identical file).
- [ ] Snake-case file name with correct group prefix.
- [ ] Light *and* dark Qt theme review at 18 px and 36 px (HiDPI):
      contrast ≥ 4.5:1 against the toolbar background in both states.

---

## 4. Brief for an external artist

**Project.** Trailer — cross-platform desktop PDF + image workbench
(Qt 6, MIT licensed).

**Scope.** ~32 monochrome line icons plus ~20 filled siblings (the
markup and form-fill tools). Full inventory in §3.6: Select,
Rectangle, Ellipse, Line, Arrow, Freehand, Text, Note, Speech Bubble,
Highlight Shape, Zoom Lens, Highlight, Underline, Strikeout, Redact,
AutoFill, Sign Here, Checkmark, X-mark, Zoom In/Out/Actual, Fit Page,
Fit Width, Rotate Left/Right, Sidebar, Markup-toolbar, Form-toolbar,
Sidebar-thumbnails/outline/annotations/bookmarks.

**Style.** 24 × 24 grid, 20 × 20 live area, **2 px stroke**, rounded
caps and joins, square exterior corners by default, 2 px rounding
only where the metaphor demands it. Closest reference: **Lucide** for
line discipline, but with **square corners** where the object is
square (page edges, redact block) and a **filled active sibling** in
the spirit of Fluent / Material's pairing. Avoid Phosphor-style
friendliness — Trailer reads as a quiet workbench, not a consumer app.

**Deliverables.** (1) SVG source per icon, authored to §3.5
(`viewBox="0 0 24 24"`, `currentColor` only, no width/height, no
embedded raster, no styles or scripts). (2) Filled-variant SVG for
every tool tagged with `-filled` in §3.6. (3) A single Figma or
SVG-source file with all glyphs on one page for family review at
18 px. (4) Notes on any glyph where the metaphor required a judgment
call.

**Naming.** Snake-case with group prefix (`tool-`, `view-`, `page-`,
`panel-`, `sidebar-`). Filled siblings append `-filled`.

**Acceptance.** Each icon must pass the §3.7 checklist. Particular
attention to silhouette uniqueness at 18 px and the contrast pairs
listed there. Review order: 18 px first, 14 px second, 36 px third.

**License.** Deliverable must be **MIT-compatible**. Original work
under MIT is preferred. Derivations from MIT-licensed bases (Lucide,
Phosphor, Fluent System Icons) or Apache-2.0 sources (Material,
Carbon) are acceptable with attribution in the repo `NOTICE` file.
**Not acceptable**: derivations from SF Symbols or any source that
cannot be represented as MIT-compatible.

**Timeline.** First pass: 8-10 representative icons covering the
visual hard cases (Redact, Speech Bubble, Highlight vs Zoom Lens,
Fit Page vs Fit Width, Sign Here vs Checkmark). Two-week review
window. Full set after the pattern locks. Budget revisions for the
silhouette-collision pairs.

**Out of scope.** App / launcher icon, marketing illustration,
animation, status-bar glyphs, document-type icons (handled by OS file
association).
