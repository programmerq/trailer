# UAT harness — drives Application + MainWindow in-process under
# QT_QPA_PLATFORM=offscreen. Tests here are labelled `uat` so a normal
# `ctest` run (which is `ctest -LE uat` in CI) skips them. Invoke
# explicitly with `ctest -L uat` locally or via scripts/run-uat.sh.

# Per-test environment. On Windows the `offscreen` Qt platform plugin
# does not enumerate fonts through GDI the way the `windows` plugin
# does, so without QT_QPA_FONTDIR pointing at the system font dir Qt
# falls back to drawing text as filled vector paths. That looks fine on
# screen but produces PDFs with no Tj operators and no /ToUnicode CMap
# — searchable-text UATs (UAT-VWR-061..066) all fail because there is
# nothing to find. Pointing at C:\Windows\Fonts gives QPdfWriter access
# to Arial etc. and the resulting PDF has real PDF fonts. The variable
# is harmless on Linux/macOS (their offscreen plugin uses fontconfig).
set(_trailer_uat_env "QT_QPA_PLATFORM=offscreen")
if(WIN32)
    # CMake's ENVIRONMENT property is a ;-separated list; CMake will
    # split on the embedded ; so we quote the value separately.
    list(APPEND _trailer_uat_env "QT_QPA_FONTDIR=$ENV{SystemRoot}/Fonts")
endif()

# Build a UAT executable (link + warnings) WITHOUT registering a ctest
# entry, so a caller can add extra link libraries and then choose how the
# binary is registered (plain, or across the HiDPI dpr matrix below).
function(trailer_build_uat_exe name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE trailer_core Qt6::Test)
    trailer_set_warnings(${name})
endfunction()

# Register an already-built UAT binary as a single ctest entry at the
# platform's ambient devicePixelRatio (dpr = 1 under offscreen).
function(trailer_register_uat_test name)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "uat"
        ENVIRONMENT "${_trailer_uat_env}"
    )
endfunction()

# Register an already-built UAT binary THREE times, once per injected
# devicePixelRatio in {1, 1.5, 2}, via a per-process QT_SCALE_FACTOR (read
# by Qt before QGuiApplication starts, so each fresh ctest process gets the
# requested screen dpr — the only in-process handle for a realized widget's
# dpr is QGuiApplication::primaryScreen()->devicePixelRatio(), which the
# offscreen plugin fixes at startup). This closes the HiDPI blind spot
# tracked in docs/backlog/2026-07-16-hidpi-uat-harness.md: dpr=1-only runs
# hid the #55 thumbnail and #76 capture-blur bugs that broke on Retina.
#
# All three entries carry the plain `uat` label so they gate at release
# like the rest of the suite (AGENTS.md "CI cadence"); they carry NO label
# containing a distinct substring that would exclude them. The dpr tag in
# the test name uses '_' (1.5 -> 1_5) since ctest names avoid '.'.
function(trailer_register_uat_dpr_matrix name)
    foreach(_dpr IN ITEMS 1 1.5 2)
        string(REPLACE "." "_" _tag "${_dpr}")
        set(_tname "${name}_dpr${_tag}")
        add_test(NAME ${_tname} COMMAND ${name})
        # ENVIRONMENT is a ;-list; append QT_SCALE_FACTOR as one more entry.
        set_tests_properties(${_tname} PROPERTIES
            LABELS "uat"
            ENVIRONMENT "${_trailer_uat_env};QT_SCALE_FACTOR=${_dpr}"
        )
    endforeach()
endfunction()

# Convenience: build + register a plain (ambient-dpr) UAT test.
function(trailer_add_uat_test name source)
    trailer_build_uat_exe(${name} ${source})
    trailer_register_uat_test(${name})
endfunction()

trailer_add_uat_test(test_uat_foundations test_uat_foundations.cpp)
trailer_add_uat_test(test_uat_search_and_markup test_uat_search_and_markup.cpp)
# test_uat_search_and_markup links to Qt PDF for the search model and
# the Annotation overlay needs Widgets (pulled in transitively via
# trailer_core, but spell it out for the search-model header).
target_link_libraries(test_uat_search_and_markup PRIVATE Qt6::Pdf Qt6::PdfWidgets)
# Outline UAT (uat_toc_*) hand-builds a PDF with an /Outlines tree via
# qpdf so QPdfBookmarkModel has something to parse.
if(TARGET qpdf::libqpdf)
    target_link_libraries(test_uat_search_and_markup PRIVATE qpdf::libqpdf)
elseif(TARGET PkgConfig::QPDF)
    target_link_libraries(test_uat_search_and_markup PRIVATE PkgConfig::QPDF)
endif()

trailer_add_uat_test(test_uat_password test_uat_password.cpp)
target_link_libraries(test_uat_password PRIVATE Qt6::Pdf Qt6::PdfWidgets)

trailer_add_uat_test(test_uat_forms test_uat_forms.cpp)
target_link_libraries(test_uat_forms PRIVATE Qt6::Pdf Qt6::PdfWidgets)

trailer_add_uat_test(test_uat_autofill test_uat_autofill.cpp)
target_link_libraries(test_uat_autofill PRIVATE Qt6::Pdf Qt6::PdfWidgets)

trailer_add_uat_test(test_uat_signature test_uat_signature.cpp)
target_link_libraries(test_uat_signature PRIVATE Qt6::Pdf Qt6::PdfWidgets)

trailer_add_uat_test(test_uat_redaction test_uat_redaction.cpp)
target_link_libraries(test_uat_redaction PRIVATE Qt6::Pdf Qt6::PdfWidgets)

trailer_add_uat_test(test_uat_background_removal test_uat_background_removal.cpp)

trailer_add_uat_test(test_uat_instant_alpha_and_smart_lasso
    test_uat_instant_alpha_and_smart_lasso.cpp)

trailer_add_uat_test(test_uat_recognize_text test_uat_recognize_text.cpp)
trailer_add_uat_test(test_uat_ocr_disk_cache test_uat_ocr_disk_cache.cpp)

# External file-change handling (ADR 2026-07-19): silent clean-reload, dirty-
# conflict banner, deleted banner, G3 Compare placeholder. Also emits the G2
# before/after evidence pair when TRAILER_EXTCHANGE_EVIDENCE_DIR is set.
trailer_add_uat_test(test_uat_external_change test_uat_external_change.cpp)

# Curated before/after grab() evidence for the image OCR pipeline
# (search-enabled + honest "No text found" + single-image dialog-skip).
# Writes PNGs to $TRAILER_OCR_EVIDENCE_DIR when set; otherwise just
# asserts the wired behaviour like any other uat slot.
trailer_add_uat_test(test_uat_ocr_evidence test_uat_ocr_evidence.cpp)

# Representative offscreen evidence for the screen-capture permission preflight
# (the screen-capture preflight ADR). The real explainer/degrade UI is mac-gated
# and can't render on Linux, so these slots reconstruct widgets with the same
# strings/buttons and grab them. Writes PNGs to $TRAILER_CAPTURE_EVIDENCE_DIR
# when set; otherwise just asserts the strings like any other uat slot.
trailer_add_uat_test(test_uat_capture_permission_evidence
    test_uat_capture_permission_evidence.cpp)

# ML status-bar affordances (ADR 0002): progress widget, scoped cancel,
# missing-model in-context hint. Builds a multi-page PDF via QPdfWriter
# (QtGui) for the OCR batch gates; the OCR itself runs through a test
# recogniser seam, so no Qt6::Pdf link nor real models are needed.
trailer_add_uat_test(test_uat_ml_affordances test_uat_ml_affordances.cpp)

trailer_add_uat_test(test_uat_pdf_pages test_uat_pdf_pages.cpp)
target_link_libraries(test_uat_pdf_pages PRIVATE Qt6::Pdf Qt6::PdfWidgets)

# Page-changed signal replaces the former 150 ms polls (backlog
# 2026-07-12-page-changed-signal-no-poll). Drives the QPdfView navigator via
# doc->goToPage() and reads back the Sidebar selection + large-doc OCR hint, so
# it links Qt PDF.
trailer_add_uat_test(test_uat_page_change_signal test_uat_page_change_signal.cpp)
target_link_libraries(test_uat_page_change_signal PRIVATE Qt6::Pdf Qt6::PdfWidgets)

# Sidebar thumbnail sizing (scale-to-width + aspect-fit rows, ADR 0006).
# Builds a mixed portrait/landscape deck via QPdfWriter and reads back
# per-page point sizes through QPdfDocument, so it links Qt PDF.
#
# Runs across the HiDPI dpr matrix {1, 1.5, 2}: the scale-to-width paint
# oracle (painted logical width == column width) is exactly the #55 Retina
# bug this guards, and is invisible at dpr = 1 only.
trailer_build_uat_exe(test_uat_thumbnail_sidebar test_uat_thumbnail_sidebar.cpp)
target_link_libraries(test_uat_thumbnail_sidebar PRIVATE Qt6::Pdf Qt6::PdfWidgets)
trailer_register_uat_dpr_matrix(test_uat_thumbnail_sidebar)

# Unified undo log past the annotation history cap (UAT-UND-150) —
# the cap is shrunk to 5 via the store's test seam so the 70 edits
# genuinely cross it and eviction fires. Needs Qt PDF for the
# page-rotation observable (pagePointSize).
trailer_add_uat_test(test_uat_undo_interleaved_cap test_uat_undo_interleaved_cap.cpp)
target_link_libraries(test_uat_undo_interleaved_cap PRIVATE Qt6::Pdf Qt6::PdfWidgets)

# Viewer navigation (continuous-mode keyboard scroll, future nav pins).
# Drives the QPdfView in MainWindow, so it links Qt PDF directly.
trailer_add_uat_test(test_uat_viewer test_uat_viewer.cpp)
target_link_libraries(test_uat_viewer PRIVATE Qt6::Pdf Qt6::PdfWidgets)

# Two-page (facing) continuous-spread view, PR1 (UAT-VWR-070..073). Drives the
# custom TwoPageView + View-menu toggle in MainWindow, so it links Qt PDF for
# the QPdfDocument / QPdfView surfaces. Runs across the HiDPI dpr matrix
# {1, 1.5, 2}: the per-page Actual-Size render path is pts x zoom x dpr device
# pixels, so fractional and 2x scaling are exactly the geometry this guards
# (decision record 2026-07-21-two-page-layout, clauses 3 and 4).
trailer_build_uat_exe(test_uat_two_page test_uat_two_page.cpp)
target_link_libraries(test_uat_two_page PRIVATE Qt6::Pdf Qt6::PdfWidgets)
trailer_register_uat_dpr_matrix(test_uat_two_page)

# Layer-1 config-matrix robustness sweep (font-scale x RTL). Drives the
# MainWindow shell (incl. the markup toolbar and side docks), so it needs
# Qt PDF for the document view. Hard oracle + fast, so it carries the plain
# `uat` label like the rest.
#
# Also runs across the HiDPI dpr matrix {1, 1.5, 2}: this is the
# toolbar/shell-geometry surface the backlog names, and fractional scaling
# (1.5) is where a control's minimumSizeHint can outrun its allotted box.
# The former in-file note that "per-monitor DPI needs a per-process
# QT_SCALE_FACTOR matrix" is now satisfied here.
trailer_build_uat_exe(test_uat_sweep test_uat_sweep.cpp)
target_link_libraries(test_uat_sweep PRIVATE Qt6::Pdf Qt6::PdfWidgets)
trailer_register_uat_dpr_matrix(test_uat_sweep)

# Staged (async) image open UAT + G2 evidence (ADR 0008): the honest
# "Loading image…" placeholder painted immediately, then the off-thread
# decode swap. Runs across the HiDPI dpr matrix {1, 1.5, 2} (#92) so the
# staged-open view holds on Retina/fractional scaling, not just dpr=1.
trailer_build_uat_exe(test_uat_staged_image_open test_uat_staged_image_open.cpp)
trailer_register_uat_dpr_matrix(test_uat_staged_image_open)

# Empty-state window model UAT. Drives Application + MainWindow; PDF
# fixtures use QPdfWriter (QtGui), so no Qt6::Pdf link is needed.
trailer_add_uat_test(test_uat_empty_state test_uat_empty_state.cpp)

# File-menu IA redesign UAT (DR 2026-07-18-file-menu-acquire-ia): ⌘N =
# New from Clipboard, Screenshot submenu + Scanner/Camera placeholders,
# acquire group stays present with a document window open, non-image
# clipboard disables + no-ops New from Clipboard, dead ⌘⇧3 removed.
# Drives Application + MainWindow; PDF fixture uses QPdfWriter (QtGui),
# so no Qt6::Pdf link is needed.
trailer_add_uat_test(test_uat_file_menu_ia test_uat_file_menu_ia.cpp)

# UX recorder UI evidence (UXR-002). Recorder-only: the "● REC" chip and
# "◉ Recording" menu exist solely in TRAILER_ENABLE_UX_RECORDER builds.
# Writes curated offscreen grab() PNGs to the committed
# docs/evidence/ux-recorder/ tree (overridable via
# $TRAILER_UX_EVIDENCE_DIR) so they can be embedded in the PR — the
# project's UX-Done gate. Drives Application + MainWindow; no Qt PDF.
if(TRAILER_ENABLE_UX_RECORDER)
    trailer_add_uat_test(test_uat_ux_recorder_evidence
        test_uat_ux_recorder_evidence.cpp)
    target_compile_definitions(test_uat_ux_recorder_evidence PRIVATE
        "TRAILER_UX_EVIDENCE_DIR=\"${CMAKE_SOURCE_DIR}/docs/evidence/ux-recorder\"")
endif()

# Zoom-% status indicator UAT. Drives Application + MainWindow with an
# image document (fixture is a PNG via QImage), so no Qt6::Pdf link is
# needed.
trailer_add_uat_test(test_uat_zoom_indicator test_uat_zoom_indicator.cpp)

# Off-GUI-thread document open (UAT-VWR-100, backlog
# 2026-07-15-offthread-pdf-open-placeholder / DR 0006 (b)). Captures the
# "Loading…" placeholder → real page before/after G2 evidence. Builds a PDF
# via QPdfWriter and drives PdfDocument + QPdfView directly, so it links Qt PDF.
#
# Runs across the HiDPI dpr matrix {1, 1.5, 2}: open-at-logical-size is
# dpr-sensitive, so the placeholder/page visuals must hold at each dpr.
trailer_build_uat_exe(test_uat_offthread_open test_uat_offthread_open.cpp)
target_link_libraries(test_uat_offthread_open PRIVATE Qt6::Pdf Qt6::PdfWidgets)
trailer_register_uat_dpr_matrix(test_uat_offthread_open)

# Inline Open Recent list in the empty state (backlog
# 2026-07-12-empty-state-open-recent-list). Drives Application +
# MainWindow; PDF fixtures use QPdfWriter (QtGui), so no Qt6::Pdf link is
# needed. Writes curated before/after grab() PNGs to
# $TRAILER_WELCOME_EVIDENCE_DIR when set (the G2 UX-Done artifact). A new
# file rather than an edit to test_uat_empty_state.cpp, to avoid a
# collision with the open PR #95.
trailer_add_uat_test(test_uat_empty_state_recent test_uat_empty_state_recent.cpp)

# Window-menu Maximize/Restore dynamic label (backlog
# 2026-07-12-maximize-restore-dynamic-label). Drives Application +
# MainWindow; no document fixture, so no Qt6::Pdf link is needed. Writes a
# grab() of the open Window menu to $TRAILER_WINDOWMENU_EVIDENCE_DIR when
# set.
trailer_add_uat_test(test_uat_window_menu_maximize test_uat_window_menu_maximize.cpp)

# macOS Dock icon recents (UAT-FND-063/064). Exercises the SHARED,
# cross-platform Dock-menu construction (Application::refreshDockRecents) —
# ordering, the 10-item cap, missing-file exclusion, Clear Menu, and that a
# chosen entry opens via the same Application::openFiles path as File > Open
# Recent. Writes a grab() of the constructed QMenu (empty vs populated) to
# $TRAILER_DOCKRECENTS_EVIDENCE_DIR when set. Does NOT and cannot cover
# whether macOS actually attaches/renders this menu on a real Dock icon, or
# the separate NSDocumentController "not running" path — see the file header
# and the PR's owner verification checklist. PDF fixtures use QPdfWriter
# (QtGui), so no Qt6::Pdf link is needed.
trailer_add_uat_test(test_uat_dock_recents test_uat_dock_recents.cpp)

# Preferences pane UAT (theme live-wire + tabs-grow-with-settings). Guards
# that the Theme control is enabled and every visible tab has >=1 enabled
# operable control. Also emits the curated G2 before/after evidence (enabled
# Theme combo + app in light/dark) to $TRAILER_PREF_EVIDENCE_DIR when set.
# Drives PreferencesDialog + Application + MainWindow with an image document
# (PNG via QImage), so no Qt6::Pdf link is needed.
trailer_add_uat_test(test_uat_preferences test_uat_preferences.cpp)

# Feedback / Diagnostic Report (Help menu). Guards the menu item is always
# enabled (G3 — there is no "unavailable" state, the report degrades
# instead), the report text is fully visible (never clipboard-only), the
# "Include full file paths" checkbox defaults off and toggles live, and
# Copy to Clipboard matches the visible text exactly. Emits the curated G2
# evidence (with-document + empty-state dialog) to
# $TRAILER_FEEDBACK_EVIDENCE_DIR when set.
trailer_add_uat_test(test_uat_feedback test_uat_feedback.cpp)
