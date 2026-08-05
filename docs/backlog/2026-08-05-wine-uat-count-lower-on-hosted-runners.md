---
id: 2026-08-05-wine-uat-count-lower-on-hosted-runners
title: Wine UAT passes 22-24/44 on ubuntu-latest vs 25/44 on the old trailer-k8s pods
priority: TBD
status: open
source: dispatched nightly dry runs validating the move to GitHub-hosted runners (PR "ci: move every PR-reachable job to GitHub-hosted runners")
created: 2026-08-05
---

## Threshold

Decide, from **at least five** hosted nightly Wine UAT counts, whether the
lane's hosted floor is genuinely below its k8s floor:

- If the hosted counts settle at **25/44 or above**, this was sampling noise —
  close the item, no action.
- If they settle **below 25/44**, name the tests that pass under Wine on a
  self-hosted pod and fail under Wine on `ubuntu-latest`, and either fix them
  or record them on `2026-07-24-wine-uat-failures-triage` as
  hosted-runner-specific. Close only once the delta is *explained*, not merely
  observed.

The counts are free to collect: every nightly release carries a
`uat-summary.json` asset.

```sh
for t in <recent nightly tags>; do
  printf '%s ' "$t"
  curl -sL "https://github.com/programmerq/trailer/releases/download/$t/uat-summary.json" \
    | python3 -c 'import json,sys; d=json.load(sys.stdin)["lanes"]["wine_uat"]; print(d["passed"], "/", d["total"])'
done
```

## Context

Measured from the `uat-summary.json` asset on each nightly release (all five
produced on the self-hosted `trailer-k8s` pods), against two dispatched dry
runs on `ubuntu-latest`:

| Source | Runner | Wine UAT |
|---|---|---|
| `nightly-20260801` | trailer-k8s | 20/42 |
| `nightly-20260802` | trailer-k8s | 22/43 |
| `nightly-20260803` | trailer-k8s | 21/43 |
| `nightly-20260804` | trailer-k8s | **25/44** |
| `nightly-20260805` | trailer-k8s | **25/44** |
| dry run [31018077661](https://github.com/programmerq/trailer/actions/runs/31018077661) | `ubuntu-latest` | **22/44** |
| dry run [31018708642](https://github.com/programmerq/trailer/actions/runs/31018708642) | `ubuntu-latest` | **24/44** |

Comparing only the rows with the **same suite total (44)** — the earlier rows
have a smaller suite, so their absolute counts are not comparable:

- trailer-k8s: **{25, 25}**
- `ubuntu-latest`: **{22, 24}**

Two samples each, **non-overlapping**. That is not enough to call a
regression, and it is *not* enough to dismiss as noise either. Both readings
are live:

- **Noise:** the lane's own history swings (20 → 22 → 21 → 25) across a
  changing suite size, so a 1–3 test wobble is not obviously outside its
  normal behaviour.
- **Real:** the only two like-for-like k8s samples are both exactly 25, and
  both hosted samples are below it.

## Why this is not a merge blocker for the runner migration

Wine UAT is **explicitly non-gating** (owner decision, 2026-07-24 — see
`nightly.yml`'s "UAT suite (Wine)" step): a Wine UAT count never withholds the
Windows artifact, and never fails the Windows lane. What it does move is the
UAT ratchet's verdict, which reds the nightly RUN's signal for one night after
the baseline changes and then reads `same`.

The migration is also not obviously the *cause* even if the drop is real —
Wine emulating Windows on a shared-tenancy cloud VM is a different timing
environment from Wine on a dedicated pod, and this suite already has a filed
history of contention-sensitive failures
(`2026-08-03-load-sensitive-offscreen-test-races`). Determining which is a
question for the data above, not for the PR that changed the runner label.

## Correction

The PR body for the migration originally described this delta as noise on the
grounds that "the lane's own run-to-run spread (±2) is wider than the delta the
ratchet flagged." That is wrong for the 22/44 sample: the delta from the 25/44
baseline is 3, which is *larger* than the observed hosted spread of 2. The
claim was corrected in that PR and this item filed in its place.

## Related

- `2026-07-24-wine-uat-failures-triage` — the standing item for why Wine UAT
  sits well below 100% at all, and why it is non-gating.
- `2026-08-03-load-sensitive-offscreen-test-races` — contention-sensitive
  failures in this suite; the most likely mechanism if the drop is real.
- `2026-08-02-uat-ratchet-per-test-identity` — the ratchet compares absolute
  counts, not test identity, which is exactly why this delta cannot currently
  be attributed to specific tests without reading both logs by hand.
