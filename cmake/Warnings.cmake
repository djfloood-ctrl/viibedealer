# Strict warnings, applied per source file rather than per target.
#
# Why per-file: juce_add_plugin compiles the JUCE module sources *into* our target, so a
# target-wide /WX would make JUCE's own warnings fail our build -- warnings we cannot fix
# and should not patch. Attaching the flags to our own translation units gives us a genuine
# zero-warning-with-/WX guarantee on the code we actually wrote, while JUCE builds at its
# own tuned warning level.

# Warnings-as-errors is separable from the warnings themselves. This code was written under
# MSVC /W4, and Clang's -Wconversion / -Wold-style-cast fire in places MSVC is silent, so a
# first build on macOS can trip a warning that has no effect on the binary. Turning the
# promotion off lets such a build finish and still prints every warning; it is ON by default
# so the zero-warning guarantee on Windows is unchanged.
option(VBD_WERROR "Promote warnings in our own sources to errors" ON)

function(vbd_strict_sources)
    if(MSVC)
        set(_vbd_flags /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
        if(VBD_WERROR)
            list(APPEND _vbd_flags /WX)
        endif()
    else()
        set(_vbd_flags
            -Wall -Wextra -Wpedantic
            -Wshadow -Wconversion -Wsign-conversion
            -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual)
        if(VBD_WERROR)
            list(APPEND _vbd_flags -Werror)
        endif()
    endif()

    foreach(_src ${ARGN})
        if(NOT IS_ABSOLUTE "${_src}")
            set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
        endif()
        get_source_file_property(_existing "${_src}" COMPILE_OPTIONS)
        if(_existing)
            set_source_files_properties("${_src}" PROPERTIES
                COMPILE_OPTIONS "${_existing};${_vbd_flags}")
        else()
            set_source_files_properties("${_src}" PROPERTIES
                COMPILE_OPTIONS "${_vbd_flags}")
        endif()
    endforeach()
endfunction()
