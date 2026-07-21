# EAP environment facts (observed 2026-07-21)

## Topology
- claude.ai Projects EAP, "Claude Code on the web" managed remote containers.
- One long-lived COORDINATOR session (this export's author) + child sessions
  spawned via the webagent MCP (start_project_session / list_project_activity
  / list_project_sessions). The coordinator's only user-visible channel is
  mcp__webagent__reply; plain text output goes nowhere user-visible.
- Cross-session steering via claude-code-remote MCP: send_message (peer
  messages arrive as untrusted cross-session-message turns), list_events
  (reads another session's transcript), send_later (the ONLY way to get a
  future-time wake; returns trig_… IDs).
- Child sessions get instructions <= 8192 bytes at spawn; Agent-tool workers
  (in-container subagents) take arbitrarily long prompts and share the
  session's filesystem.

## Versions (as observed in session init events)
- Claude Code CLI 2.1.216.
- Coordinator model: claude-fable-5[1m] (fallback claude-opus-4-8[1m]).
- Child sessions: claude-opus-4-8[1m], permissionMode "auto".
- MCP servers connected in children: github, webagent, Parallel_Search,
  claude-code-remote.

## Permission layer
- Merges/pushes to protected targets need the OWNER'S words in the acting
  session; coordinator/peer relays do not clear the classifier. Blocked
  sessions should attempt at most twice, then report the exact denial.

## Container & network quirks
- Containers are ephemeral; repo cloned fresh; a SessionStart hook
  (scripts/session-setup.sh, in-repo) provisions Qt 6.11.0 (aqtinstall) and
  ONNX Runtime 1.25.0 via the NuGet route (GitHub release assets 403 through
  the proxy). Build recipe details: team-memory/trailer-remote-build-recipe.md.
- Outbound HTTPS via an agent proxy (CA bundle /root/.ccr/ca-bundle.crt).
- The local git relay allows fetch/push/force-push but returns HTTP 403 on
  the branch-DELETE verb; the GitHub MCP server has no delete-ref tool ->
  branch deletion needs the GitHub UI.
- PR-body writes through the GitHub MCP HTML-escape ampersands/quotes; use
  the raw API for body rewrites. Raw URLs longer than ~150 chars get
  backtick-defanged by the egress layer -> keep committed evidence filenames
  short and embed with HTML <img> pinned to a commit SHA.
- Team memory at /tmp/claude/memory/team/… replicates across containers for
  registered .md files only; loose files do not replicate.
- df misleads: writable disk is a fixed allowance; 0 Avail with low Used
  means allowance spent.

## CI (in-repo, but context helps)
- Self-hosted k8s runners: groups trailer-k8s and trailer-small; docker via
  sidecar. ci.yml fires only on push/PR targeting main -> stacked PRs show
  zero checks; a base-change is an 'edited' event that does NOT trigger CI
  (re-push a main-based head to attach checks).
- Tiers: Linux build+unit, Windows cross-build + Wine unit (Wine artifacts
  QSKIPped with documented reasons), version gating, clang-format advisory,
  and (from #117) a wayland-smoke launch+screenshot job on headless sway.

## Session lifecycle gotchas
- Long single turns kill workers; checkpoint-push per item.
- send_later timers and PR-activity subscriptions die with their session;
  re-arm on revival.
- EAP shutdown: sessions read-only ~6 months, then deleted. Session IDs in
  coordinator-state.md remain readable during that window.
