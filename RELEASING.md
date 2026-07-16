# Releasing Trailer

Maintainer runbook for cutting a tagged Trailer release. The
underlying *why* — what each workflow does, why tag-after-merge,
why squash merges break the contract — lives in [README's "Release
process" section](README.md#release-process). This doc is the
operational checklist plus the recovery paths.

If you are an AI agent and were just asked to cut a release: stop
and ask the maintainer. Releases involve pushing tags, publishing
to GitHub Releases, and committing to bytes users will install.
The maintainer makes those calls.

---

## Quick reference

| Task | Command |
|---|---|
| Local sanity build, host platform | `make release` |
| Local sanity build, Windows (Docker) | `make release-windows` |
| Local UAT (Docker) | `make release-uat` |
| Strip `-dev` and bump for release | `scripts/bump-version.sh release` |
| Draft CHANGELOG entries since `v0.1.0` | `scripts/release-notes.sh v0.1.0..HEAD` |
| Print a CHANGELOG section | `scripts/extract-changelog.sh 0.2.0` |
| Post-release bump back to `-dev` | `scripts/bump-version.sh post-release` |
| Advance the dev-build counter | `scripts/bump-version.sh dev-bump` |

`make help` lists every release-related target with one-line
descriptions.

---

## Version numbering

Pick the version number **at release time**, not in advance. Apply
[SemVer](https://semver.org/) to what actually shipped:

- **In 0.x**, a backward-incompatible change to user-visible
  behaviour, on-disk formats (settings, signature layout, recent-
  files schema, annotation persistence), the `IDocument`
  interface, or the CLI surface bumps the **minor**. Until 1.0 we
  permit that on minor bumps — see [PHILOSOPHY.md](PHILOSOPHY.md)
  "What 1.0 means."
- **Backward-compatible feature work** can also bump the minor in
  0.x, or sit alongside fixes in a patch — judgment call.
- **Bugfix-only releases** bump the **patch**: `0.2.0 → 0.2.1`.
- **1.0** is declared when on-disk formats and public APIs have
  been stable for one or two minor cycles without thrash, *not* on
  a calendar date. See PHILOSOPHY.md.

Don't plan toward a future version number ("ship X by 0.3.0") —
let the version fall out of the diff.

---

## Pre-release checklist

Run through this before applying the `release-candidate` label.

### 1. Update `CHANGELOG.md`

The `[Unreleased]` section is the staging area between releases.
Move its entries into a new versioned section.

```sh
# Draft new entries since the previous tag:
scripts/release-notes.sh v0.1.0..HEAD > /tmp/release-notes.md

# Edit /tmp/release-notes.md (group by Added / Changed / Fixed /
# Infrastructure; drop anything not user-visible), then paste into
# CHANGELOG.md under a new `## [X.Y.Z] - YYYY-MM-DD` heading.
```

Add a fresh empty `[Unreleased]` section above it so the next
contributor has somewhere to land entries. Update the compare /
release-tag link footers at the bottom of `CHANGELOG.md`.

### 2. Bump `VERSION`

```sh
scripts/bump-version.sh release   # strips the -dev suffix
```

Validate locally:

```sh
cat VERSION                       # e.g. 0.2.0
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target trailer
./build/trailer --version         # confirm the About string updates
```

### 3. Run the local sanity check

The Makefile targets mirror exactly what CI's `release.yml` runs,
so a green local result is a strong signal CI will pass:

```sh
make release          # host platform (macOS host → arm64 DMG, Linux host → cmake+ctest)
make release-windows  # Windows cross-build via Docker
make release-uat      # UAT suite via Docker
```

Each artifact appears under `build-macos/`, `build-windows/`, or
`build/`. Smoke-test the host-platform artifact: open a PDF, do a
markup pass, save it, reopen it. The harness can't catch "looks
right when a human drives it" the way you can.

### 4. Open the release PR

```sh
git checkout -b release/<version>
git add VERSION CHANGELOG.md
git commit -m "Release <version>"
git push -u origin release/<version>
gh pr create --title "Release <version>" --body "See CHANGELOG.md."
```

### 5. Label the PR `release-candidate`

The `labeled` event re-triggers `release.yml`. Watch the run:

- **`precheck` ✅, heavy jobs skipped (warning)** — `VERSION`
  still carries `-dev` / `-rc`. Re-run step 2.
- **`precheck` ❌** — `v$VERSION` tag or release already exists.
  Real bug. Bump `VERSION` again, or `git push --delete origin
  v$VERSION` if the dangling tag was a previous false start.
- **`precheck` ✅ + Linux ✅ + Windows ✅ + macOS ✅ + UAT ✅** —
  proceed.

### 6. Merge with a SHA-preserving policy

Use **merge commit** or **fast-forward**. Do **not** squash —
`release-autotag.yml` tags PR HEAD, and squash drops that SHA, so
the publish job can't find the artifacts that were built against
the PR.

GitHub's "Squash and merge" button is the trap here. The repo's
default merge button should be "Merge pull request"; double-check
before clicking.

### 7. Confirm autotag + publish

`release-autotag.yml` fires within ~30 seconds of merge. It:

1. Reads `VERSION` at PR HEAD,
2. Tags PR HEAD as `v$VERSION`,
3. Dispatches `release-publish.yml`.

`release-publish.yml` then:

1. Finds the prior successful `Release` workflow run for that SHA,
2. Downloads its artifacts (no rebuild),
3. Renames them to `trailer-$VERSION-<platform>.<ext>`,
4. Creates the GitHub Release with the CHANGELOG section spliced
   into the body (see `scripts/extract-changelog.sh`).

Open the Release in the GitHub UI and confirm:

- All three platform artifacts are attached.
- The body shows the CHANGELOG section for this version (not the
  fallback boilerplate).
- The macOS install command shows `Trailer.app` (Title Case) and
  not `trailer.app`.

### 8. Post-release: bump back to `-dev`

```sh
git checkout main
git pull
scripts/bump-version.sh post-release   # 0.2.0 → 0.2.1-dev
git add VERSION
git commit -m "Bump VERSION to 0.2.1-dev post-<version> release"
git push
```

The `-dev` suffix is the gate that keeps `release.yml` from
rebuilding heavy artifacts for in-progress work (`precheck` skips
heavy jobs when `VERSION` carries `-dev`).

---

## Dev builds

`X.Y.Z-dev.N` is a [SemVer](https://semver.org/) prerelease used for
internal, manual dogfooding of work-in-progress code *before* it is a
real release. It lets a maintainer hand a tester a portable build
without cutting a tag.

- The `-dev.N` counter increments (`dev.0`, `dev.1`, …) for each new
  dev build of the same target version. Advance it with
  `scripts/bump-version.sh dev-bump`: a clean release version bumps
  the patch and starts a fresh `-dev.0` (`0.3.0` → `0.3.1-dev.0`), an
  existing counter increments (`0.3.1-dev.0` → `0.3.1-dev.1`), and a
  bare `-dev` promotes to `-dev.0`.
- A `-dev.N` version is **not** release-ready. The `release.yml`
  precheck and `release-autotag.yml` both treat any `-dev`, `-dev.N`,
  or `-rc` suffix as not-release-ready, so the heavy build / tag /
  publish jobs are skipped — a dev build cannot accidentally become a
  real release.
- Cutting the real `X.Y.Z` supersedes all its `X.Y.Z-dev.N`
  predecessors; `scripts/bump-version.sh release` strips the suffix
  (`0.3.1-dev.3` → `0.3.1`).
- Dev artifacts come from the on-demand
  [`dev-build.yml`](.github/workflows/dev-build.yml) workflow
  (`workflow_dispatch`): unsigned, per-OS portable builds uploaded to
  the run instead of a Release. The `build_linux` / `build_windows` /
  `build_macos` booleans pick which OSes to build — e.g. a mac-only
  dev build sets `build_macos` true and the other two false. (Adding
  the `dev-build` label to a PR builds Linux + Windows; macOS is
  dispatch-only because its runners bill at 10×.)

---

## Recovery paths

### "A successful `Release` run wasn't found for this SHA"

`release-publish.yml` failed because nothing was built against the
tagged commit. Two common causes:

- **Squash merge dropped the PR HEAD.** Most common.
- **A maintainer tagged a non-PR commit by hand** (e.g. `git tag
  v0.2.0` on `main` after the fact).

Recovery, from the README:

```sh
gh workflow run Release --ref=<SHA>
```

Wait for the dispatched run to finish, then re-run the failed
`Publish Release` job from the Actions tab.

### "Tag already exists upstream"

Either the autotag job ran twice (rare; race condition or replay)
or a maintainer tagged manually before the workflow could. If the
tag is dangling (no release attached):

```sh
git push --delete origin v$VERSION
git tag -d v$VERSION
```

Then re-run `release-autotag.yml` from the Actions tab or push
again.

If the tag has a release attached, treat it as shipped — bump
`VERSION` to the next patch and start over.

### "macOS bundle is named `trailer.app` in the release body"

Historical bug in v0.1.0's release body — the bundle is
`Trailer.app` (Title Case) but the body said `trailer.app`. The
template in `release-publish.yml` has been fixed; future releases
will be correct. To fix v0.1.0:

```sh
gh release edit v0.1.0 --notes-file -  <<'EOF'
<paste corrected body here>
EOF
```

### CI minutes blew up

The release pipeline burns macOS minutes (10× Linux rate). If a
build is failing and you keep pushing fixes:

- Remove the `release-candidate` label between attempts to pause
  the heavy jobs; the `labeled` event re-triggers them on next
  add.
- Or use `workflow_dispatch` against the specific SHA you want to
  test, not via the PR.

---

## Things to keep an eye on

- **Auto-updater integration.** Once wired (Sparkle 2 is the
  leading candidate — see [ROADMAP.md](ROADMAP.md) Now item 1
  for the requirement, which is "ed25519-signed update channel,"
  not the specific library), each release will need to sign and
  publish a feed entry. Likely a new step between (7) and (8)
  above; revisit this doc when the implementation lands.
- **Notarized macOS builds.** Off the table indefinitely — Trailer
  is not in the Apple Developer Program. If that ever changes, a
  signing + notarization step plugs in between `make release` and
  the artifact upload in CI.
- **Intel Mac binary.** When ML-disabled mode or a third-party
  ONNX x86_64 build lands, the macOS artifact list grows by one
  and `release-publish.yml`'s rename map needs the new entry.
- **CHANGELOG drift.** If contributors forget to update
  `[Unreleased]`, `scripts/release-notes.sh v$PREV..HEAD` will
  surface what's missing — the script reads `git log`, not
  `CHANGELOG.md`. Run it at the start of step (1) and reconcile.
