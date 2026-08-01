#include "ReducedMotion.h"

namespace trailer::platform {

namespace {
// Function-local static avoids a global-constructor ordering hazard;
// std::optional defaults to "no override" (nullopt) so production
// behaviour is unaffected unless a test explicitly opts in.
std::optional<bool> &reducedMotionOverride() {
    static std::optional<bool> value;
    return value;
}
} // namespace

bool prefersReducedMotion() {
    if (const std::optional<bool> &override = reducedMotionOverride())
        return *override;
    return prefersReducedMotionFromOS();
}

void setReducedMotionOverrideForTest(std::optional<bool> value) {
    reducedMotionOverride() = value;
}

} // namespace trailer::platform
