#pragma once

#include <optional>

namespace trailer::platform {

// Best-effort OS "Reduce Motion" accessibility query, used to gate
// non-essential UI animations (DESIGN §6.12, docs/accessibility-
// checklist.md row A6). Checked once at the moment an animation is
// about to start -- there is no live-update signal, since none of the
// current call sites need to react to the setting flipping mid-session.
//
// Honours a test override (see setReducedMotionOverrideForTest below)
// before falling back to the real platform query, so UAT/unit coverage
// is deterministic on every platform -- including macOS, where the real
// query reads the developer/CI machine's actual accessibility setting
// and would otherwise make the test's outcome depend on that machine's
// state.
bool prefersReducedMotion();

// The actual, un-overridden platform query. Implemented per-platform:
//   - macOS (ReducedMotion.mm): NSWorkspace.
//     accessibilityDisplayShouldReduceMotion -- the real system toggle.
//   - Windows / Linux (ReducedMotion_stub.cpp): a best-effort Qt
//     UI-effect proxy; see that file for the documented limitation.
// Application code should call prefersReducedMotion() above, not this
// directly -- this is exposed so prefersReducedMotion() can delegate to
// it after checking the test override.
bool prefersReducedMotionFromOS();

// Test-only seam: force prefersReducedMotion() to return `value` instead
// of querying the OS, or pass std::nullopt to clear the override and
// resume querying the real platform state. Not thread-safe by design --
// call only from a single-threaded test's setup/teardown, matching the
// rest of this codebase's test-seam conventions (e.g.
// ImageDocument::triggerInitialZoomForTest()).
void setReducedMotionOverrideForTest(std::optional<bool> value);

} // namespace trailer::platform
