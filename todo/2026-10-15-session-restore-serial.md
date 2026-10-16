# Session restore loads documents one after another

Priority: P2. Tags: `open`.

- Done when: restoring five documents paints the frontmost document's first page before the
  other four finish loading. This is an ordering test, with no clock.
- Src: `Application.cpp:1163-1185`.
