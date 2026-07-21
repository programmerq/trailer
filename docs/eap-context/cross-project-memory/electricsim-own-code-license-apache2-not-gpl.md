---
name: electricsim-own-code-license-apache2-not-gpl
description: Owner ruling (2026-07-15) — electricsim's OWN code is Apache-2.0, NOT GPLv3; vendoring/linking GPL tools does not force the project to GPL.
metadata:
  type: feedback
---

Owner correction 2026-07-15 (electricsim, on PR #255's LICENSE): do NOT default the project's own code to GPLv3. The owner prefers Apache-2.0/MIT and pushed back hard on a root GPLv3 LICENSE.

**Why:** the "we link/vendor GPL tools so the repo must be GPL" reasoning is wrong. (1) Using a GPL compiler (gcc-avr) imposes nothing on output — GCC Runtime Library Exception. (2) GPL copyleft attaches only on **conveying** a work to third parties (GPLv3 §0/§2); electricsim is not distributed (audience is the owner), so obligations are dormant. (3) Linking a GPL lib does not rewrite the license on your own source files — only a *distributed combined binary* would need GPL-compatible conveyance terms; Apache-2.0 is one-way compatible with GPLv3. No court has ever compelled a project to relicense its own code merely for linking.

**Facts (verified):** simavr (`third_party/simavr/COPYING`) is GPLv3-or-later (NOT LGPL), statically linked into 7 host controllers (pim/htcm/rsa/apm/btcm/lhjb/rhjb) via electricsim_core, ungated by the firmware flag. The other 7 module hosts use the simavr-free electricsim_connector. m68hc11 GDB sim (GPLv3) links only under off-by-default ELECTRICSIM_ENABLE_HC11. Firmware ELFs are separate; avr-libc is BSD-3-Clause.

**How to apply:** electricsim's own code = Apache-2.0 (root LICENSE, set on PR #255 commit 07bb9f50). Keep vendored GPL/LGPL/EPL components isolated in third_party/ under their own licenses. The combined-binary caveat + full reasoning/citations live in root `LICENSING.md`. Related: BL-0204 (owner comfortable following GPLv3 for the vendored tools, no redistribution). Apache-2.0 confirmed as the owner's final choice (2026-07-16): 'We're not distributing this software, so we don't need to pick any license at all. I'll go with Apache' — settled, do not re-raise.
