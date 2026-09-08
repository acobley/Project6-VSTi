#-----------------------------------------------------------------------------
# Pre-build guard: has source/ changed since the build was configured?
#
# Xcode reads its project file once, at the start of a build. A source file
# added afterwards is not compiled, and the failure never mentions that: you
# get a page of undefined symbols, or - if what is missing is the factory -
# a plug-in that links and is then rejected by the SDK's post-build check for
# not exporting GetPluginFactory.
#
# So CMakeLists.txt records the globbed file list in a manifest at configure
# time, and this runs before every build and compares.
#
# THE MANIFEST ONLY WORKS AS A WITNESS BECAUSE THE GLOBS ARE PLAIN.
# An earlier version of this guard used file(GLOB ... CONFIGURE_DEPENDS),
# which re-globs at build time through ZERO_CHECK - rewriting the manifest
# and the project file together, so the two always agreed and the check could
# never fire. It shipped, and the next added file sailed straight past it.
# If you reintroduce CONFIGURE_DEPENDS, you disable this guard.
#
# Invoked as:
#   cmake -DSRC_DIR=... -DMANIFEST=... -P cmake-check-sources.cmake
#-----------------------------------------------------------------------------

file(GLOB _now RELATIVE "${SRC_DIR}" "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h")
list(SORT _now)
string(REPLACE ";" "\n" _now_text "${_now}")

set(_then_text "")
if(EXISTS "${MANIFEST}")
    file(READ "${MANIFEST}" _then_text)
endif()

if(NOT _now_text STREQUAL _then_text)
    string(REPLACE "\n" ";" _then "${_then_text}")
    set(_added "${_now}")
    set(_removed "${_then}")
    if(_then)
        list(REMOVE_ITEM _added ${_then})
    endif()
    if(_now)
        list(REMOVE_ITEM _removed ${_now})
    endif()

    set(_what "")
    if(_added)
        string(REPLACE ";" ", " _a "${_added}")
        set(_what "${_what}\n  added:   ${_a}")
    endif()
    if(_removed)
        string(REPLACE ";" ", " _r "${_removed}")
        set(_what "${_what}\n  removed: ${_r}")
    endif()

    message(FATAL_ERROR
        "source/ has changed since this build was configured.${_what}\n\n"
        "The generated project still lists the old set, so these files would "
        "not be compiled - and the error you would get instead points "
        "somewhere else entirely.\n\n"
        "Re-run the configure step, then build again:\n\n"
        "    ./setup-xcode.sh --no-open\n\n"
        "This check exists because the failure it prevents is unrecognisable: "
        "a wall of undefined symbols, or a plug-in that links fine and is then "
        "rejected for not exporting GetPluginFactory.")
endif()
