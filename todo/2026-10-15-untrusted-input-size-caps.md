# Untrusted input has no size caps

Priority: P3. Tags: `security`.

- Done when: clipboard images, form values and embedded /Contents above a stated cap are
  refused with a message. File type is decided by content, not extension. EXIF location can be
  stripped on Export. Password buffers are zeroed after use. Each of these has a unit test.
- Src: the May 2026 audit (`git show 09bc1e3:docs/audit-2026-05-19.md`), SEC-MOD-1, -3, -4 and
  SEC-MIN-1..3.
