# The macOS UAT crashes in recognize-text on hosted runners

Priority: P2. Tags: `infra`.

- Done when: the faulting stack is captured first. Then three consecutive macOS nightlies on new
  main commits finish `test_uat_recognize_text` with `crashed=false`.
- Src: 8 of 12 nights crashed. valgrind memcheck on Linux finds 0 errors, but the slot that
  runs the model was skipped there for lack of weights: capture the lldb stack on macOS, or run
  that slot under ASan on Linux with the weights present.
