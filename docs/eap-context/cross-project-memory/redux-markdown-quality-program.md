---
name: redux-markdown-quality-program
metadata:
  type: project
  modified: 2026-07-21T01:07:33.571Z
description: Owner-commissioned phased program to fix corpus-wide markdown defects in ev1-manual-redux (image triage, defect shakeout, figure canonicalization, figpair redo).
---

Owner-commissioned program (2026-07-21 ~00:50Z), spawned from the prop-434/435 spot check that exposed corpus-wide markdown defects ("I'm afraid the markdown in this repo is in worse shape than I thought!"). Owned by the figure-re-pairing session; phased: Phase 0 corpus-wide image triage (tesseract over small-dimension images + size heuristics + .txt cross-check → committed classification manifest {real-figure/caption-strip/prose-as-image/artifact/full-page/DTC-header}, one manual as calibration chunk first); Phase 1 defect shakeout (caption strips rejoined, prose-as-image → real text, marker-dropped step numbers restored, all scan-verified, chunked PRs); Phase 2 figure canonicalization (owner: best-copy scoring per figure ID corpus-wide, "even if they only appear once... rename to the figure ID in the common directory" figures/PSM<id>.jpg — extends the early best-copy precedent); Phase 3 figpair redo on cleaned inputs, 272-page wave frozen until owner approves shape.

Owner rulings recorded: figpair markers ONLY where an image sits next to text it illustrates — NOT images atop two-column DTC pages, NOT full-page images. originals/EV1*/*.txt = a DIFFERENT OCR pass than marker, NOT authoritative (jpgs are) but useful for cheap eyeball comparison to save vision cycles. Process norm: small chunks → clarifying questions on variations → owner shares thoughts → standing instructions updated iteratively. Everything hardens the S-10 ingest process too.

Link [[figure-repair-traceable-shape-rule]], [[ev1-s10-electric-manual]], [[owner-decision-queue]].
