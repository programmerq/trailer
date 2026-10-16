# PDFs that forbid modification are edited without notice

Priority: P3. Tags: `save`.

- Done when: PRODUCT.md states the policy for owner-password permission flags, and a PDF opened
  with `--modify=none` behaves as it says.
- Src: Trailer edits such files without asking (verified with qpdf `--modify=none`).
