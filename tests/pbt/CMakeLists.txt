# PBT harness — seeded property-based explorer binaries (advisory tier).
#
# LABEL RULE (load-bearing): every test registered here carries the label
# `advisory` and MUST NEVER carry any label containing the substring
# "uat". ctest -L / -LE match labels by REGEX (AGENTS.md "CI cadence" →
# label gotcha), so a label like "uat-pbt" would silently join the
# release gate's `ctest -L uat` selector. Per the AGENTS.md tier table,
# advisory checks NEVER block a merge or a release: PR CI excludes them
# (`--label-exclude 'uat|advisory'` in .github/workflows/), the
# release-candidate gate selects only `-L uat`, and the walk runs only
# when explicitly invoked (`ctest -L advisory`, or the planned nightly
# advisory workflow). A defect the explorer finds graduates into a
# deterministic `uat`-labelled regression guard — the explorer itself
# stays out of the gates permanently.

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
    # TIMEOUT: a healthy S0 walk finishes in seconds; the first 50-seed
    # exploration batch already found a seed whose walk LIVELOCKS the GUI
    # thread in a posted-event storm (see the S0 PR's findings), and
    # ctest's default 1500 s timeout would let such a run burn 25 min of
    # the advisory lane. 300 s is ~100x a healthy run and small enough
    # that a hung walk reads as the FAILURE it is, quickly. Range tried:
    # 60 (too tight for a loaded runner running 30 steps x two 5 s
    # oracle budgets), 300. Symptom to change: raise in lockstep if walk
    # length or per-step budgets grow.
    set_tests_properties(${name} PROPERTIES
        LABELS "advisory"
        ENVIRONMENT "${_trailer_pbt_env}"
        TIMEOUT 300
    )
endfunction()

# S0 minimal slice: 30-step seeded navigation walk over a generated
# mixed-orientation PDF (seed via TRAILER_PBT_SEED, evidence via
# TRAILER_PBT_EVIDENCE_DIR). Drives the QPdfView in MainWindow, so it
# links Qt PDF directly (same shape as test_uat_viewer).
trailer_add_pbt_test(test_pbt_walk_min test_pbt_walk_min.cpp)
target_link_libraries(test_pbt_walk_min PRIVATE Qt6::Pdf Qt6::PdfWidgets)
