# Cold launch to the first window has never been measured on the reference machine

Priority: P3. Tags: `open`.

- Done when: a developer-only flag (`--quit-after-first-frame`) reports launch-to-first-frame
  time, and a run after a reboot on the reference machine is in the closing PR's description.
  Never gated.
- Src: warm `trailer --version` takes 17–30 ms here; cold launch on old hardware is unknown.
