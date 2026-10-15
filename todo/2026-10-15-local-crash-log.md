# There is no local log file for diagnosing crashes

Priority: P3. Tags: `infra`.

- Done when: Trailer writes a size-capped local log under `AppPaths::logsDir()` that Report a
  Problem can attach. Nothing is uploaded.
- Src: `AppPaths.cpp:94`.
