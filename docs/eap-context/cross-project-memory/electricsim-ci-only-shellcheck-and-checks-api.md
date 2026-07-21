---
name: electricsim-ci-only-shellcheck-and-checks-api
description: Two CI-vs-local gotchas in programmerq/electricsim — audit_shellcheck.py vacuously PASSes when the shellcheck binary is absent (so local /verify never enforces it), and CI status is reported via the GitHub Checks API where the legacy combined commit-status endpoint shows a red-herring pending total_count:0
metadata:
  type: feedback
---

Two CI-vs-local gotchas in programmerq/electricsim.

**(1) shellcheck is CI-only.** `scripts/audit_shellcheck.py` prints a vacuous PASS when the `shellcheck` binary is absent from PATH (it is absent on the dev image), so local `make test` / `/verify` do **NOT** enforce shellcheck — only the `ubuntu-latest` CI "Lint & syntax checks" job does.
- **Why it matters:** a shell-script edit (e.g. an SC2086 word-split) can pass local `/verify` and still fail CI.
- **How to apply:** after editing any `scripts/*.sh`, install shellcheck locally (`shellcheck 0.9.0`) and run `python3 scripts/audit_shellcheck.py` before pushing.

**(2) Judge PR CI by the Checks API, not the commit-status endpoint.** This repo posts CI via the GitHub **Checks API**; the legacy combined commit-status endpoint returns an empty `total_count:0` "pending" that is a **red herring** — judge PR CI by the Checks-API check-runs. `Unit tests (Windows / MinGW64)` and `review` are SKIPPED by design (neutral, non-blocking).
