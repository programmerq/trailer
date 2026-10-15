# ML caches have no memory cap, and MlScheduler runs one job at a time

Priority: P3. Tags: `ocr`.

- Done when: the SAM encoder cache and the background-removal cache stay under a stated cap
  (with a test), and MlScheduler runs more than one job when cores allow.
- Src: old TODO.md:519-528.
