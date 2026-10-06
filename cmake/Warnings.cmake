# Strict warnings, applied per source file rather than per target.
#
# Why per-file: juce_add_plugin compiles the JUCE module sources *into* our target, so a
# target-wide /WX would make JUCE's own warnings fail our build -- warnings we cannot fix
# and should not patch. Attaching the flags to our own translation units gives us a genuine
# zero-warning-with-/WX guarantee on the code we actually wrote, while JUCE builds at its
# own tuned warning level.

function(vbd_strict_sources)
    if(MSVC)
        set(_vbd_flags /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    else()
        set(_vbd_flags
            -Wall -Wextra -Wpedantic -Werror
            -Wshadow -Wconversion -Wsign-conversion
            -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual)
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
