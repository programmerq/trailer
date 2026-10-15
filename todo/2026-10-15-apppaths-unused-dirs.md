# AppPaths declares directories nothing uses

Priority: P3. Tags: `infra`.

- Done when: AppPaths declares only directories that something reads or writes, which removes
  versions, icc, filters and plugins.
- Src: `AppPaths.h:16-22`.
