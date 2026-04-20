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
