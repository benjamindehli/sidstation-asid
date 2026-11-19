# Local fixes to the JUCE checkout, run as the FetchContent PATCH_COMMAND with
# the JUCE source tree as the working directory.
#
# Each fix replaces exact lines and is skipped when already applied, so the
# patch step can rerun on an existing checkout. A fix whose lines are not found
# stops the configure, which is what should happen after a JUCE bump: check
# whether JUCE fixed it upstream, then update or drop the fix here.
#
# JUCE stores its sources with CRLF line endings, and file(READ) drops the CRs,
# so the edits are single lines and a CRLF file is written back as CRLF.

function(juce_fix file description)
    set(path "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
    file(READ "${path}" text)
    file(READ "${path}" hex HEX)
    string(FIND "${hex}" "0d0a" crlf)
    set(patched "${text}")

    math(EXPR last "${ARGC} - 1")
    foreach(i RANGE 2 ${last} 2)
        math(EXPR j "${i} + 1")
        set(old "${ARGV${i}}")
        set(new "${ARGV${j}}")

        string(FIND "${patched}" "${new}" already)
        if(NOT already EQUAL -1)
            continue()
        endif()

        string(FIND "${patched}" "${old}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "JUCE fix '${description}' no longer applies to ${file}. "
                                "Check whether this JUCE version fixed it, then update or remove it in cmake/PatchJuce.cmake.")
        endif()
        string(REPLACE "${old}" "${new}" patched "${patched}")
    endforeach()

    if(patched STREQUAL text)
        message(STATUS "JUCE fix already applied: ${description}")
    else()
        if(NOT crlf EQUAL -1)
            string(REPLACE "\n" "\r\n" patched "${patched}")
        endif()
        file(WRITE "${path}" "${patched}")
        message(STATUS "JUCE fix applied: ${description}")
    endif()
endfunction()

# No fixes are needed with the JUCE version pinned in CMakeLists.txt. To add one,
# call juce_fix(file description old new [old new ...]) here, with a comment
# saying what it fixes, why the plugin cannot work around it from its own code,
# and the upstream issue.
