# EAP Context Export

This branch (`claude/eap-context-export`) is a snapshot of everything the project
coordinator agent can see that the owner cannot see directly from the repo or the
claude.ai UI. It exists because the Claude Projects EAP is closing: EAP sessions
become read-only for ~6 months and are then deleted, and it is unknown whether
session history carries forward when the feature goes live.

First committed 2026-07-21 (~16:30 UTC). The owner asked for this branch to be
kept refreshed until EAP access ends; no PR is opened on purpose (owner
instruction).

## What's here

| Path | Contents |
|---|---|
| `coordinator-state.md` | Live coordinator state at export time: session map (which child session owns which PRs/areas), armed timers, merge-queue snapshot, every item pending an owner action. |
| `session-history.md` | Narrative history of the project as the coordinator experienced it — arcs, incidents, and owner interactions the repo's git history doesn't record. |
| `eap-environment.md` | EAP platform facts: versions, models, MCP servers, container/proxy quirks, and mechanics (send_later triggers, memory replication) a future agent needs. |
| `skills-inventory.md` | Which agent skills live in the repo vs only in the session environment — the environment-only ones are what would be lost. Copies under `skills-env/`. |
| `team-memory/` | Verbatim copy of this project's shared team memory (`6d4aba2d-…`) — the coordinator's persistent cross-container memory; the owner has no UI for it. `MEMORY.md` is the index. |
| `cross-project-memory/` | The sibling project's (`ef259ff8-…`, redux/electricsim/EV1 program) team memory — included because several rulings there (notably the PR draft/ready/merge policy) are cross-referenced by trailer sessions, and it is equally invisible to the owner. |

## How a future agent should use this

1. Read `team-memory/MEMORY.md` first — the curated index of standing owner
   rulings and process facts; individual files carry the detail.
2. Read `coordinator-state.md` for what was in flight and what was pending the
   owner at export time; verify against live GitHub state before acting.
3. Read `eap-environment.md` before building, pushing, or scheduling anything —
   the proxy, git-relay, and CI quirks there cost real time to rediscover.
4. Re-seeding a new memory system: files under `team-memory/` are already in
   one-fact-per-file format with frontmatter; copy them into a new memory root
   as-is.
5. Re-arm the momentum patrol (protocol in `coordinator-state.md`) — timers do
   not survive the EAP session dying.

## What is NOT here

- Raw session transcripts (.jsonl): they exist only inside ephemeral containers
  under `/root/.claude/projects/…`; the useful content is distilled into
  `session-history.md` and team memory.
- Secrets/credentials: none — team memory policy forbids them and the export was
  grep-checked before commit.
