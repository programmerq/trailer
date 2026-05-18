# On Windows, unit tests that exercise QPdfWriter (test_adapters,
# test_pdf_editor, …) need access to a font directory or Qt renders
# text as filled vector paths instead of real PDF text. See the longer
# explanation in tests/uat/CMakeLists.txt. The variable is harmless on
# other platforms.
set(_trailer_unit_env "")
if(WIN32)
    list(APPEND _trailer_unit_env "QT_QPA_FONTDIR=$ENV{SystemRoot}/Fonts")
endif()

function(trailer_add_test name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE trailer_core Qt6::Test)
    trailer_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    if(_trailer_unit_env)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "${_trailer_unit_env}")
    endif()
endfunction()

trailer_add_test(test_settings   test_settings.cpp)
trailer_add_test(test_recent     test_recent.cpp)
trailer_add_test(test_app_paths  test_app_paths.cpp)
trailer_add_test(test_registry   test_registry.cpp)
trailer_add_test(test_adapters   test_adapters.cpp)
trailer_add_test(test_pdf_editor test_pdf_editor.cpp)
trailer_add_test(test_annotation_store test_annotation_store.cpp)
trailer_add_test(test_form_toolbar test_form_toolbar.cpp)
trailer_add_test(test_icon_resources test_icon_resources.cpp)
trailer_add_test(test_markup_toolbar test_markup_toolbar.cpp)
trailer_add_test(test_cards test_cards.cpp)
trailer_add_test(test_signatures test_signatures.cpp)
trailer_add_test(test_image_filter test_image_filter.cpp)
trailer_add_test(test_onnx_session test_onnx_session.cpp)
trailer_add_test(test_model_registry test_model_registry.cpp)
trailer_add_test(test_background_remover test_background_remover.cpp)
trailer_add_test(test_sam_session test_sam_session.cpp)
trailer_add_test(test_ocr_engine test_ocr_engine.cpp)

add_subdirectory(uat)
