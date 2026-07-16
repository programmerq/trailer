function(trailer_set_warnings target)
    if(MSVC)
        # MSVC is not our CI toolchain (CI is GCC/Clang on Linux); keep these
        # changes minimal. /we4062 promotes "enumerator not handled in switch
        # and no default label" to an error, the MSVC analogue of the
        # unconditional -Werror=switch below. It is set unconditionally so
        # exhaustive default-less mode switches are enforced even without /WX.
        if(TRAILER_WERROR)
            target_compile_options(${target} PRIVATE /W4 /permissive- /WX /we4062)
        else()
            target_compile_options(${target} PRIVATE /W4 /permissive- /we4062)
        endif()
    else()
        set(_trailer_warn_flags
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
        )
        # Promote ONLY -Wswitch to a hard error, unconditionally (i.e. NOT
        # gated behind TRAILER_WERROR, which is OFF in the default/CI build).
        # -Wswitch (part of -Wall) fires when a switch over an enum omits an
        # enumerator AND has no `default:`. Making just this one warning an
        # error forces exhaustive, default-less mode switches over our domain
        # enums (ViewMode, ZoomMode, DocumentType, ...) to be compiler-checked
        # — so adding an enumerator breaks the build at every switch that
        # forgot it — without enabling full -Werror (kept off because Qt and
        # other third-party headers are warning-noisy). Scope stays narrow:
        # only `switch`. Motivating regression: the Two-Pages silent-alias bug,
        # where a swallowing default let TwoPages be mapped to Continuous.
        list(APPEND _trailer_warn_flags -Werror=switch)
        if(TRAILER_WERROR)
            list(APPEND _trailer_warn_flags -Werror)
        endif()
        target_compile_options(${target} PRIVATE ${_trailer_warn_flags})
    endif()
endfunction()
