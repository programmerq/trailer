# Nothing has been measured on the reference Intel Mac

Priority: P1. Tags: `platform`.

- Done when: the counting tests ([GUI-thread work grows with page count: layout, sidebar and thumbnails](2026-10-15-gui-work-grows-with-pages.md)) and the cold-launch measurement ([Cold launch to the first window has never been measured on the reference machine](2026-10-15-measure-cold-launch-on-intel-mac.md)) run on that
  machine under Linux or Windows, with the results in the closing PR's description.
- Src: the owner chose it as a yardstick, not a build target (PRODUCT.md, 2026-10-15). No
  official macOS build runs on it: Qt 6.10+ needs macOS 13 and ONNX Runtime 1.25 needs 14,
  while 2014 Macs stop at macOS 11 or 12.
