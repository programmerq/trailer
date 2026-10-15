# Random UI walk — seeded property-based explorer binaries, labelled
# `advisory`.
#
# LABEL RULE (load-bearing): every test registered here carries the label
# `advisory` and MUST NEVER carry any label containing the substring
# "uat". ctest -L / -LE match labels by REGEX (AGENTS.md, Test), so a
# label like "uat-pbt" would silently join the release's `ctest -L uat`
# run. Advisory tests never block a merge or a release, because a random
# seed can trip over a bug the PR never touched: the unit-test lanes
# exclude them (`--label-exclude 'uat|advisory'` in .github/workflows/),
# the release UAT run selects only `-L uat`, and the walk runs only when
# invoked (`ctest -L advisory`). A defect the explorer finds gets a
# TODO.md entry and a deterministic test (a known-bug test while it is
# open) — the explorer itself stays out of the gates permanently.

# Same offscreen environment as tests/uat/CMakeLists.txt (see the
# QT_QPA_FONTDIR rationale there — without it, Windows offscreen renders
# PDF text as filled paths).
set(_trailer_pbt_env "QT_QPA_PLATFORM=offscreen")
if(WIN32)
    list(APPEND _trailer_pbt_env "QT_QPA_FONTDIR=$ENV{SystemRoot}/Fonts")
endif()

# Build + register one advisory PBT binary. Mirrors
# trailer_register_uat_test with the label swapped to `advisory`.
function(trailer_add_pbt_test name source)
    qt_add_executable(${name} ${source} PbtSettle.h)
    target_link_libraries(${name} PRIVATE trailer_core Qt6::Test)
    trailer_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    # TIMEOUT: a healthy walk finishes in seconds, but some seeds
    # LIVELOCK the GUI thread in a posted-event storm (T-09), and ctest's
    # default 1500 s timeout would let such a run burn 25 min. 300 s is
    # ~100x a healthy run and small enough that a hung walk reads as the
    # FAILURE it is, quickly. Range tried: 60 (too tight for a loaded
    # runner running 30 steps x two 5 s oracle budgets), 300. Symptom to
    # change: raise in lockstep if walk length or per-step budgets grow.
    set_tests_properties(${name} PROPERTIES
        LABELS "advisory"
        ENVIRONMENT "${_trailer_pbt_env}"
        TIMEOUT 300
    )
endfunction()

# The walk: 30 seeded navigation steps over a generated
# mixed-orientation PDF (seed via TRAILER_PBT_SEED, evidence via
# TRAILER_PBT_EVIDENCE_DIR). Drives the QPdfView in MainWindow, so it
# links Qt PDF directly (same shape as test_uat_viewer).
trailer_add_pbt_test(test_pbt_walk_min test_pbt_walk_min.cpp)
target_link_libraries(test_pbt_walk_min PRIVATE Qt6::Pdf Qt6::PdfWidgets)
