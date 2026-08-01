#!/usr/bin/env bash
# Probe whether THIS process has a real Aqua/WindowServer session available
# — i.e. whether it's running as a launchd user *agent* inside a logged-in
# GUI session, vs. a daemon/background context with no window-server
# connection. This is a standing fact about a macOS CI runner's capability,
# not a one-off check: if true, `QT_QPA_PLATFORM=offscreen` is a CHOICE this
# project makes, not a hard requirement, and on-screen UI testing / real
# screenshots / real Dock behaviour / real Gatekeeper prompts become
# possible for the first time. If false, none of that is available and
# everything GUI-observable stays a manual, real-Mac-only verification step.
#
# Five independent signals, each printed and exit-coded so a caller can
# reason about partial/conflicting results rather than trusting one probe:
#   1. `launchctl managername` — "Aqua" for a full GUI login session,
#      "Background"/"StandardIO"/etc. otherwise. The most direct single
#      signal launchd itself exposes.
#   2. `launchctl print gui/$(id -u)` — succeeds only if a GUI launchd
#      domain exists for this UID (i.e. someone is logged in graphically
#      as this user).
#   3. Console-owner match — /dev/console's owning user vs. this process's
#      user. A mismatch (or no console owner) means this process isn't the
#      one driving the physical/virtual display.
#   4. `CGSessionCopyCurrentDictionary` (compiled inline via a tiny ObjC
#      probe) — Apple's own supported API for "is there a graphical login
#      session, and is it the one on the console." Returns NULL/no
#      kCGSessionOnConsoleKey when there isn't one.
#   5. A REAL (non-offscreen) Qt GUI process boot, bounded by `timeout` so
#      a hung WindowServer connection attempt can't stall the caller —
#      the signal that matters most in practice, since it's the actual
#      capability Trailer's own test/UI binaries would need.
#
# Usage: scripts/probe-macos-gui-session.sh [path-to-a-qt-test-binary]
# The optional argument enables signal 5; without it, signals 1-4 only.
# Writes a Markdown table to $GITHUB_STEP_SUMMARY when set (harmless no-op
# otherwise so this also runs standalone on a real Mac for a human).
# Always exits 0 — this is a REPORTING tool, not a pass/fail gate; the
# caller decides what to do with the printed signals.

set -uo pipefail

QT_TEST_BINARY="${1:-}"

echo "== Signal 1: launchctl managername =="
MANAGER="$(launchctl managername 2>&1)"
echo "$MANAGER"

echo
echo "== Signal 2: launchctl print gui/\$(id -u) =="
GUI_DOMAIN_OUTPUT="$(launchctl print "gui/$(id -u)" 2>&1 | head -5)"
echo "$GUI_DOMAIN_OUTPUT"
if launchctl print "gui/$(id -u)" >/dev/null 2>&1; then
    GUI_DOMAIN_RC=0
else
    GUI_DOMAIN_RC=1
fi
echo "exit code: $GUI_DOMAIN_RC"

echo
echo "== Signal 3: console owner vs current user =="
CONSOLE_USER="$(stat -f '%Su' /dev/console 2>&1)"
CURRENT_USER="$(whoami)"
echo "console user: $CONSOLE_USER, current user: $CURRENT_USER"
if [[ "$CONSOLE_USER" == "$CURRENT_USER" ]]; then
    CONSOLE_MATCH="yes"
else
    CONSOLE_MATCH="no"
fi

echo
echo "== Signal 4: CGSessionCopyCurrentDictionary (compiled probe) =="
# Plain $$-based paths rather than `mktemp` — the trailing-suffix-after-X's
# template shape isn't guaranteed portable across mktemp implementations,
# and a fixed-per-process-id name is unique enough for a throwaway CI file.
PROBE_SRC="/tmp/trailer-session-probe-$$.m"
PROBE_BIN="/tmp/trailer-session-probe-$$"
cat > "$PROBE_SRC" <<'EOF'
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
int main(void) {
    CFDictionaryRef d = CGSessionCopyCurrentDictionary();
    if (!d) {
        printf("CGSessionCopyCurrentDictionary: NULL (no graphical session)\n");
        return 1;
    }
    NSDictionary *nsd = (__bridge NSDictionary *)d;
    NSLog(@"session dict: %@", nsd);
    CFBooleanRef onConsole = (CFBooleanRef)CFDictionaryGetValue(d, kCGSessionOnConsoleKey);
    BOOL isOnConsole = onConsole && CFBooleanGetValue(onConsole);
    printf("onConsole=%d\n", isOnConsole ? 1 : 0);
    CFRelease(d);
    return isOnConsole ? 0 : 1;
}
EOF
if clang -fobjc-arc -framework Foundation -framework ApplicationServices -o "$PROBE_BIN" "$PROBE_SRC" 2>&1; then
    "$PROBE_BIN"
    SESSION_RC=$?
else
    echo "compile failed"
    SESSION_RC=2
fi
rm -f "$PROBE_SRC" "$PROBE_BIN"
echo "exit code: $SESSION_RC"

COCOA_RC="skipped"
if [[ -n "$QT_TEST_BINARY" && -x "$QT_TEST_BINARY" ]]; then
    echo
    echo "== Signal 5: real (-platform cocoa) Qt process boot, 20s bound =="
    # NOT `timeout 20 ...` — stock macOS ships BSD userland with no
    # timeout(1) (that's GNU coreutils; Homebrew's coreutils formula
    # installs it as `gtimeout`, not guaranteed present). Hand-rolled
    # background-process + watchdog bound instead, portable to any bash.
    unset QT_QPA_PLATFORM
    "$QT_TEST_BINARY" -platform cocoa > /tmp/trailer-cocoa-probe-$$.log 2>&1 &
    QT_PID=$!
    ( sleep 20 && kill -9 "$QT_PID" 2>/dev/null ) &
    WATCHDOG_PID=$!
    wait "$QT_PID" 2>/dev/null
    COCOA_RC=$?
    kill "$WATCHDOG_PID" 2>/dev/null
    wait "$WATCHDOG_PID" 2>/dev/null
    tail -40 /tmp/trailer-cocoa-probe-$$.log
    rm -f /tmp/trailer-cocoa-probe-$$.log
    echo "exit code: $COCOA_RC (137 = killed by the 20s watchdog — treat as NO/hung real session)"
fi

{
    echo "### macOS GUI/WindowServer session probe"
    echo
    echo "| Signal | Result |"
    echo "|---|---|"
    echo "| \`launchctl managername\` | \`$MANAGER\` |"
    echo "| \`launchctl print gui/\$(id -u)\` reachable | $([ "$GUI_DOMAIN_RC" = 0 ] && echo yes || echo no) (exit $GUI_DOMAIN_RC) |"
    echo "| console owner == current user | $CONSOLE_MATCH (\`$CONSOLE_USER\` vs \`$CURRENT_USER\`) |"
    echo "| \`CGSessionCopyCurrentDictionary\` onConsole | exit $SESSION_RC (0 = yes, 1 = no session, 2 = compile failed) |"
    echo "| real \`-platform cocoa\` Qt process boot | exit $COCOA_RC |"
} | tee -a "${GITHUB_STEP_SUMMARY:-/dev/null}"

# Machine-readable line for a caller to `grep`/`eval` without re-parsing
# the table above. Never influences this script's own (always-0) exit.
echo "TRAILER_GUI_PROBE manager=$MANAGER gui_domain_rc=$GUI_DOMAIN_RC console_match=$CONSOLE_MATCH session_rc=$SESSION_RC cocoa_rc=$COCOA_RC"

exit 0
