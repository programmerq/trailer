// Reports whether THIS process holds macOS Screen Recording permission.
// CGPreflightScreenCaptureAccess only checks — it never prompts and never
// adds the caller to the Screen Recording list — so it is safe to run in a test.
// NOTE: it reports the calling (harness) process's state, not Trailer's; the
// authoritative per-app check is the TCC.db query in smoketest.sh.
import CoreGraphics
let granted = CGPreflightScreenCaptureAccess()
print(granted ? "granted" : "not-granted")
exit(granted ? 0 : 1)
