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

function(trailer_add_uat_test name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE trailer_core Qt6::Test)
    trailer_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "uat"
        ENVIRONMENT "${_trailer_uat_env}"
    )
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

# ML status-bar affordances (ADR 0002): progress widget, scoped cancel,
# missing-model in-context hint. Builds a multi-page PDF via QPdfWriter
# (QtGui) for the OCR batch gates; the OCR itself runs through a test
# recogniser seam, so no Qt6::Pdf link nor real models are needed.
trailer_add_uat_test(test_uat_ml_affordances test_uat_ml_affordances.cpp)

trailer_add_uat_test(test_uat_pdf_pages test_uat_pdf_pages.cpp)
target_link_libraries(test_uat_pdf_pages PRIVATE Qt6::Pdf Qt6::PdfWidgets)

# Sidebar thumbnail sizing (scale-to-width + aspect-fit rows, ADR 0006).
# Builds a mixed portrait/landscape deck via QPdfWriter and reads back
# per-page point sizes through QPdfDocument, so it links Qt PDF.
trailer_add_uat_test(test_uat_thumbnail_sidebar test_uat_thumbnail_sidebar.cpp)
target_link_libraries(test_uat_thumbnail_sidebar PRIVATE Qt6::Pdf Qt6::PdfWidgets)

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

# Layer-1 config-matrix robustness sweep (font-scale x RTL). Drives the
# MainWindow shell, so it needs Qt PDF for the document view. Hard
# oracle + fast, so it carries the plain `uat` label like the rest.
trailer_add_uat_test(test_uat_sweep test_uat_sweep.cpp)
target_link_libraries(test_uat_sweep PRIVATE Qt6::Pdf Qt6::PdfWidgets)

# Empty-state window model UAT. Drives Application + MainWindow; PDF
# fixtures use QPdfWriter (QtGui), so no Qt6::Pdf link is needed.
trailer_add_uat_test(test_uat_empty_state test_uat_empty_state.cpp)
