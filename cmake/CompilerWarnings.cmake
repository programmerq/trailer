function(trailer_set_warnings target)
    if(MSVC)
        if(TRAILER_WERROR)
            target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
        else()
            target_compile_options(${target} PRIVATE /W4 /permissive-)
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
        if(TRAILER_WERROR)
            list(APPEND _trailer_warn_flags -Werror)
        endif()
        target_compile_options(${target} PRIVATE ${_trailer_warn_flags})
    endif()
endfunction()
