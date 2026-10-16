# The MSI shows no licence and isn't signed

Priority: P3. Tags: `packaging`.

- Done when: the MSI shows the MIT licence and the third-party notices. A comment in
  `trailer.wxs` settles the WiX 4 question and the Authenticode question (sign only if a
  certificate is ever bought).
- Src: `platform/windows/trailer.wxs:6-11`.
