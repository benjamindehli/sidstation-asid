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

# Logic Pro aborts on quit with "std::system_error: mutex lock failed". Since
# 9.0.3 JUCE creates its CoreMIDI client once and never disposes of it, so
# CoreMIDI keeps calling systemChangeCallback until the process is gone. That
# callback locks the mutex of a function-local static, which __cxa_finalize has
# already destroyed by the time the host closes its own MIDI ports at exit.
# Leaking the static keeps the mutex alive. Not creating a MessageManager from
# that callback stops a late notification from building a new one mid exit.
juce_fix("modules/juce_audio_devices/native/juce_CoreMidi_mac.mm"
    "CoreMIDI notifications after static destruction"
    "static Listeners listeners;"
    "static auto* listeners = new Listeners; // leaked, see cmake/PatchJuce.cmake"
    "return listeners;"
    "return *listeners;"
    "if (MessageManager::getInstance()->isThisTheMessageThread())"
    "if (auto* mm = MessageManager::getInstanceWithoutCreating(); mm == nullptr) return; else if (mm->isThisTheMessageThread())")
