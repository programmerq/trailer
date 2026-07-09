# On Windows the unit-test environment needs two tweaks that are
# safe-but-redundant elsewhere:
#
#   QT_QPA_PLATFORM=offscreen — the default `windows` plugin tries to
#     talk to a real GUI session, which over SSH (or any non-
#     interactive shell) is at best noisy and at worst makes
#     widget/icon-pixel tests behave differently due to DPI
#     awareness, monitor scaling, etc. Linux CI sets this in its
#     workflow `env:` block; we set it per-test here so it survives
#     any shell that runs ctest.
#
#   QT_QPA_FONTDIR=%SystemRoot%\Fonts — under the offscreen plugin
#     Qt 6 on Windows has no font directory, so QPdfWriter renders
#     text as filled vector paths (no Tj, no /ToUnicode). See the
#     longer explanation in tests/uat/CMakeLists.txt.
#
# Both are harmless on Linux/macOS — their offscreen plugin routes
# through fontconfig / CoreText. We still scope to WIN32 so the
# property stays empty and unit-test runs on other platforms keep
# inheriting whatever the workflow / shell already set.
set(_trailer_unit_env "")
if(WIN32)
    list(APPEND _trailer_unit_env
        "QT_QPA_PLATFORM=offscreen"
        "QT_QPA_FONTDIR=$ENV{SystemRoot}/Fonts"
    )
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
trailer_add_test(test_document_type_defaults test_document_type_defaults.cpp)
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
trailer_add_test(test_background_candidate_scorer test_background_candidate_scorer.cpp)
trailer_add_test(test_sam_session test_sam_session.cpp)
trailer_add_test(test_sam_controller test_sam_controller.cpp)
trailer_add_test(test_ocr_engine test_ocr_engine.cpp)
trailer_add_test(test_selectable_text_store test_selectable_text_store.cpp)
trailer_add_test(test_selectable_text_layer test_selectable_text_layer.cpp)
trailer_add_test(test_cancellation_token test_cancellation_token.cpp)
trailer_add_test(test_power_source test_power_source.cpp)
trailer_add_test(test_ml_scheduler test_ml_scheduler.cpp)
trailer_add_test(test_preferences test_preferences.cpp)

add_subdirectory(uat)
