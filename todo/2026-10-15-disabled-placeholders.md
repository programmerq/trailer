# The UI shows disabled placeholders

Priority: P1. Tags: `dialogs`.

- Done when: the File menu has no Scanner or Camera items; the file-changed banner has no
  disabled Compare button; Preferences offers no disabled Stable channel; Share… is absent
  where it can't work.
- Src: `Application.cpp:1391-1397`; `FileChangeBanner.cpp:36` and `:57-64`;
  `PreferencesDialog.cpp:329-345`.
