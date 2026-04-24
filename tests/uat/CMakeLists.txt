# UAT harness — drives Application + MainWindow in-process under
# QT_QPA_PLATFORM=offscreen. Tests here are labelled `uat` so a normal
# `ctest` run (which is `ctest -LE uat` in CI) skips them. Invoke
# explicitly with `ctest -L uat` locally or via scripts/run-uat.sh.

function(trailer_add_uat_test name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE trailer_core Qt6::Test)
    trailer_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "uat"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

trailer_add_uat_test(test_uat_foundations test_uat_foundations.cpp)
trailer_add_uat_test(test_uat_search_and_markup test_uat_search_and_markup.cpp)
# test_uat_search_and_markup links to Qt PDF for the search model and
# the Annotation overlay needs Widgets (pulled in transitively via
# trailer_core, but spell it out for the search-model header).
target_link_libraries(test_uat_search_and_markup PRIVATE Qt6::Pdf Qt6::PdfWidgets)

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
