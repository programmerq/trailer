# The UAT ratchet compares counts, not tests

Priority: P3. Tags: `infra`.

- Done when: the ratchet is deleted with [CI tests Windows only under Wine, and most of CI guards process, not the product](2026-10-15-ci-windows-only-under-wine.md), or `compare-uat-baseline.sh` reports `worse` when
  a test that passed in the baseline fails, even if the passing total rose.
- Src: `scripts/compare-uat-baseline.sh:59-65`.
