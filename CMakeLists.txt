#-----------------------------------------------------------------------------
# VST3 / AUv2 plug-in build - DXi porting template
#
# This file is the part of a DXi->VST3 port that has nothing to do with any
# particular plug-in: fetching the SDKs, the Audio Unit wrapper, code signing,
# and the traps in each. It was extracted from the SpaceDub port; every comment
# below records something that actually went wrong there.
#
# TO USE IT: edit the block marked "PLUG-IN IDENTITY" and nothing else, then
# drop your sources in source/ and your artwork in resource/.
#
# Quick start:
#     ./setup-xcode.sh
#
# Or by hand:
#     cmake -B build -G Xcode
#     cmake --build build --config Release
#
# The Steinberg VST3 SDK is fetched automatically on the first configure.
# To use an SDK you already have:
#     cmake -B build -G Xcode -DVST3_SDK_ROOT=/path/to/vst3sdk
#
# On macOS with the Xcode generator an Audio Unit (AUv2) wrapper is built
# alongside the VST3. Turn it off with -DPORT_BUILD_AU=OFF.
#-----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.25.0)

set(CMAKE_OSX_DEPLOYMENT_TARGET 10.13 CACHE STRING "macOS deployment target")

#-----------------------------------------------------------------------------
# PLUG-IN IDENTITY  --  this is the block to edit
#
# PLUGIN_NAME is used for the target, the bundle, the Xcode scheme and the
# built .vst3 / .component, so it wants to be a plain identifier.
#
# The two bundle identifiers and the four-character codes in
# resource/au-info.plist are as permanent as the class UIDs once you have
# shipped a build: change one afterwards and every session using the plug-in
# loses it.
#-----------------------------------------------------------------------------
set(PLUGIN_NAME         "Project6")
set(PLUGIN_VERSION      "1.0.0.1")
set(PLUGIN_DESCRIPTION  "Project6 - VST3 / AUv2 instrument")
set(PLUGIN_COMPANY      "A. E. Cobley")
set(PLUGIN_BUNDLE_ID    "audio.project6.vst3")
set(PLUGIN_AU_BUNDLE_ID "audio.project6.audiounit")

project(${PLUGIN_NAME}
    VERSION ${PLUGIN_VERSION}
    DESCRIPTION "${PLUGIN_DESCRIPTION}"
)

#-----------------------------------------------------------------------------
# Locate (or fetch) the VST3 SDK
#-----------------------------------------------------------------------------
set(PORT_SDK_TAG "v3.8.1_build_84" CACHE STRING "VST3 SDK git tag to fetch")
set(VST3_SDK_ROOT "" CACHE PATH "Path to an existing Steinberg VST3 SDK checkout")

# -Dvst3sdk_SOURCE_DIR=... is the spelling Steinberg's own tutorials use;
# accept it as well as our own -DVST3_SDK_ROOT=...
if(vst3sdk_SOURCE_DIR AND NOT VST3_SDK_ROOT)
    set(VST3_SDK_ROOT "${vst3sdk_SOURCE_DIR}")
endif()

if(NOT VST3_SDK_ROOT)
    set(_bundled_sdk "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk")
    if(NOT EXISTS "${_bundled_sdk}/CMakeLists.txt")
        message(STATUS "${PLUGIN_NAME}: VST3 SDK not found - cloning ${PORT_SDK_TAG} into external/vst3sdk (this takes a few minutes)")
        find_package(Git REQUIRED)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} clone --depth 1 --recurse-submodules --shallow-submodules
                    --branch ${PORT_SDK_TAG}
                    https://github.com/steinbergmedia/vst3sdk.git
                    "${_bundled_sdk}"
            RESULT_VARIABLE _clone_result
        )
        if(NOT _clone_result EQUAL 0)
            message(FATAL_ERROR
                "${PLUGIN_NAME}: could not clone the VST3 SDK.\n"
                "Clone it yourself and re-run cmake with -DVST3_SDK_ROOT=/path/to/vst3sdk")
        endif()
    endif()
    set(VST3_SDK_ROOT "${_bundled_sdk}" CACHE PATH "" FORCE)
endif()

if(NOT EXISTS "${VST3_SDK_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "${PLUGIN_NAME}: VST3_SDK_ROOT is not a VST3 SDK: ${VST3_SDK_ROOT}")
endif()

set(vst3sdk_SOURCE_DIR "${VST3_SDK_ROOT}")

# The SDK appends this to CMAKE_MODULE_PATH in its own CMakeLists, but that is
# a child scope: the append does not reach back up here. Without it,
# include(SMTG_AddVST3AuV2) below fails with "include could not find requested
# file".
list(APPEND CMAKE_MODULE_PATH "${VST3_SDK_ROOT}/cmake/modules")

#-----------------------------------------------------------------------------
# Audio Unit (AUv2) wrapper
#
# Steinberg's wrapper loads the VST3 inside an .component bundle, so there is
# no second implementation to maintain - but it needs Apple's AudioUnitSDK,
# and the SDK only defines the AUv2 helper under the Xcode generator. Both are
# resolved here, before the SDK subdirectory is added, because that is where
# setupCoreAudioSupport() reads SMTG_AUDIOUNIT_SDK_PATH.
#-----------------------------------------------------------------------------
option(PORT_BUILD_AU "Also build an Audio Unit (AUv2) wrapper - macOS + Xcode only" ON)

# Which AudioUnitSDK to fetch.
#
# This is pinned deliberately. Steinberg's smtg_target_add_auv2 compiles the
# wrapper with target_compile_features(cxx_std_17), and Apple moved on:
#
#   1.0.0, 1.1.0   C++17          <- matches the wrapper as shipped
#   1.2.0          C++20 project, but headers still C++17-clean
#   1.3.0, 1.4.0   C++20 headers  <- std::span, concepts, requires-clauses
#
# Building 1.3.0+ against the C++17 wrapper fails in AUUtility.h with
# "no member named 'span' in namespace 'std'". 1.1.0 is the newest release
# whose own build standard matches what the wrapper compiles at, so the static
# library and our translation units agree.
#
# If you do point this at a newer one, the C++ standard of the Audio Unit
# target is raised automatically - see the detection further down.
set(PORT_AU_SDK_TAG "AudioUnitSDK-1.1.0" CACHE STRING "Apple AudioUnitSDK git tag to fetch")

set(PORT_AU_ENABLED OFF)
if(PORT_BUILD_AU)
    if(NOT APPLE)
        message(STATUS "${PLUGIN_NAME}: not macOS - skipping the Audio Unit target")
    elseif(NOT CMAKE_GENERATOR STREQUAL "Xcode")
        message(STATUS "${PLUGIN_NAME}: the Audio Unit target needs the Xcode generator - skipping it "
                       "(configure with -G Xcode to get one)")
    elseif(SMTG_AUDIOUNIT_SDK_PATH AND EXISTS "${SMTG_AUDIOUNIT_SDK_PATH}/AudioUnitSDK.xcodeproj")
        # Somebody has pointed us at their own checkout; leave it alone.
        message(STATUS "${PLUGIN_NAME}: using the AudioUnitSDK at ${SMTG_AUDIOUNIT_SDK_PATH}")
        set(PORT_AU_ENABLED ON)
    else()
        find_package(Git REQUIRED)
        set(_au_sdk "${CMAKE_CURRENT_SOURCE_DIR}/external/AudioUnitSDK")

        # A checkout left over from a different pin would silently be the wrong
        # version, which is exactly how the C++17/C++20 mismatch above bites.
        if(EXISTS "${_au_sdk}/AudioUnitSDK.xcodeproj")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} -C "${_au_sdk}" describe --tags --exact-match
                OUTPUT_VARIABLE _au_have
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(NOT _au_have STREQUAL "${PORT_AU_SDK_TAG}")
                message(STATUS "${PLUGIN_NAME}: external/AudioUnitSDK is '${_au_have}' but "
                               "'${PORT_AU_SDK_TAG}' is wanted - refetching")
                file(REMOVE_RECURSE "${_au_sdk}")
            endif()
        endif()

        if(NOT EXISTS "${_au_sdk}/AudioUnitSDK.xcodeproj")
            message(STATUS "${PLUGIN_NAME}: fetching Apple's AudioUnitSDK (${PORT_AU_SDK_TAG}) into external/AudioUnitSDK")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} clone --depth 1 --branch ${PORT_AU_SDK_TAG}
                        https://github.com/apple/AudioUnitSDK.git "${_au_sdk}"
                RESULT_VARIABLE _au_clone_result
            )
        else()
            set(_au_clone_result 0)
        endif()

        if(_au_clone_result EQUAL 0 AND EXISTS "${_au_sdk}/AudioUnitSDK.xcodeproj")
            # Picked up by the SDK's setupCoreAudioSupport(), which then turns
            # SMTG_ENABLE_AUV2_BUILDS on for us.
            set(SMTG_AUDIOUNIT_SDK_PATH "${_au_sdk}" CACHE PATH "" FORCE)
            set(PORT_AU_ENABLED ON)
        else()
            # Never fatal: a missing AudioUnitSDK must not stop the VST3 build.
            message(WARNING
                "${PLUGIN_NAME}: could not fetch Apple's AudioUnitSDK, so the Audio Unit target is "
                "skipped. The VST3 build is unaffected. Clone it yourself into "
                "external/AudioUnitSDK, or configure with -DPORT_BUILD_AU=OFF.")
        endif()
    endif()
endif()

# Does this AudioUnitSDK need C++20? Detected from the headers rather than from
# the tag, so a hand-placed checkout is handled too.
set(PORT_AU_NEEDS_CXX20 OFF)
if(PORT_AU_ENABLED)
    set(_au_util "${SMTG_AUDIOUNIT_SDK_PATH}/include/AudioUnitSDK/AUUtility.h")
    if(EXISTS "${_au_util}")
        file(STRINGS "${_au_util}" _au_cxx20_hits REGEX "std::span|^concept |^[ \t]*requires ")
        if(_au_cxx20_hits)
            set(PORT_AU_NEEDS_CXX20 ON)
        endif()
    endif()
endif()

#-----------------------------------------------------------------------------
# Code signing
#
# The SDK defaults SMTG_CODE_SIGN_IDENTITY_MAC to "Apple Development" and its
# Audio Unit post-build step then runs
#
#     /usr/bin/codesign -v -s "Apple Development" --force --timestamp <bundle>
#
# unconditionally. On a machine with no developer certificate that fails with
# "Apple Development: no identity found" and takes the whole build down - after
# everything has already compiled and installed.
#
# So unless a real identity is given, the SDK's signing is switched off and the
# bundles are ad-hoc signed below by a step we control. Ad-hoc is what a local
# development build wants anyway, and it is enough for macOS to load the
# plug-in on Apple Silicon. Note that --timestamp is deliberately not passed:
# an ad-hoc signature cannot be timestamped.
#
# To sign properly, pass your identity:
#     cmake -B build -G Xcode -DPORT_CODE_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
#-----------------------------------------------------------------------------
set(PORT_CODE_SIGN_IDENTITY "" CACHE STRING
    "macOS signing identity. Empty means ad-hoc signing, which needs no certificate.")

set(PORT_ADHOC_SIGN OFF)
if(APPLE)
    if(PORT_CODE_SIGN_IDENTITY)
        set(SMTG_CODE_SIGN_IDENTITY_MAC "${PORT_CODE_SIGN_IDENTITY}" CACHE STRING "" FORCE)
        message(STATUS "${PLUGIN_NAME}: signing with '${PORT_CODE_SIGN_IDENTITY}'")
    else()
        set(SMTG_DISABLE_CODE_SIGNING ON CACHE BOOL "" FORCE)
        set(PORT_ADHOC_SIGN ON)
        message(STATUS "${PLUGIN_NAME}: no signing identity given - bundles will be ad-hoc signed")
    endif()
endif()

# Only build what this plug-in needs.
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES 0 CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES  0 CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT        1 CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK           1 CACHE BOOL "" FORCE)

set(SMTG_VSTGUI_ROOT "${VST3_SDK_ROOT}")

#-----------------------------------------------------------------------------
# KEEP THE BUILD MACHINE'S PATHS OUT OF THE SHIPPED BINARY
#
# VSTGUI's assert macros embed __FILE__, so a release build carries about
# fifty absolute source paths - every one of them naming the home directory it
# was built in. Harmless, and completely standard across shipped plug-ins, but
# it is public the moment the installer is published, and `strings` on the
# binary hands a stranger your username and directory layout.
#
# -ffile-prefix-map REWRITES THE PREFIX AT COMPILE TIME, so __FILE__ becomes
# "Project6/external/..." instead. Note that -fdebug-prefix-map, which is the
# one people reach for, does NOT do this: it rewrites debug info only and
# leaves __FILE__ exactly as it was. -ffile-prefix-map is the superset of that
# and -fmacro-prefix-map, and the macro half is the half that matters here.
#
# THIS MUST COME BEFORE add_subdirectory BELOW. The paths are baked into
# VSTGUI's translation units, not into ours, so a target_compile_options on
# the plug-in target would strip almost nothing. add_compile_options at
# directory scope reaches every target created after it, the SDK's included.
#
# Guarded, because an unknown flag is a hard error on an older toolchain and
# this is cosmetic - never worth failing a build over.
#-----------------------------------------------------------------------------
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-ffile-prefix-map=/a=b" PORT_HAVE_FILE_PREFIX_MAP)

if(PORT_HAVE_FILE_PREFIX_MAP)
    add_compile_options(
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=${PLUGIN_NAME}"
        "-ffile-prefix-map=${VST3_SDK_ROOT}=vst3sdk"
        "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
    message(STATUS "${PLUGIN_NAME}: build paths will be stripped from the binary")
else()
    message(STATUS "${PLUGIN_NAME}: this compiler has no -ffile-prefix-map, so "
                   "the binary will carry absolute build paths")
endif()

add_subdirectory(${VST3_SDK_ROOT} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

#-----------------------------------------------------------------------------
# The plug-in target
#-----------------------------------------------------------------------------
# Globbed so there is no file list to maintain as you add sources.
#
# Deliberately WITHOUT CONFIGURE_DEPENDS. That option re-globs at build time,
# through ZERO_CHECK, which sounds like exactly what you want and is worse
# than useless under the Xcode generator: Xcode has already loaded the old
# file list, so the reconfigure updates the project on disk while the build
# carries on without the new file. Worse, it also rewrites the manifest that
# the pre-build guard below checks against, so the guard's two witnesses move
# together and it can never fire.
#
# Without it nothing changes until you reconfigure explicitly - so the guard
# sees the gap and stops the build with a message that names the file.
file(GLOB plugin_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
)
# Docs, listed so they appear in the Xcode project. Conditional because a
# fresh copy of the template has no PORTING-NOTES.md yet, and CMake treats a
# listed file that does not exist as a hard error.
foreach(_doc README.md PORTING-NOTES.md PORTING-GUIDE.md PORT-CHECKLIST.md)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_doc}")
        list(APPEND plugin_sources "${_doc}")
    endif()
endforeach()

if(NOT plugin_sources MATCHES "\\.cpp")
    message(FATAL_ERROR
        "${PLUGIN_NAME}: source/ contains no .cpp files.\n"
        "This is the porting template - it builds the SDKs, the Audio Unit wrapper and "
        "the signing, but you still have to supply the plug-in. See README.md for the "
        "seven files a minimal VST3 needs, and ../Spaceduo-VSTi/source for a worked example.")
endif()

# Record what was globbed, and check it again before every build - see
# cmake-check-sources.cmake for why this is necessary under Xcode.
set(_manifest "${CMAKE_BINARY_DIR}/source-manifest.txt")
file(GLOB _manifest_now RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/source"
     "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
list(SORT _manifest_now)
string(REPLACE ";" "\n" _manifest_text "${_manifest_now}")
file(WRITE "${_manifest}" "${_manifest_text}")

smtg_add_vst3plugin(${PLUGIN_NAME} ${plugin_sources})
smtg_target_configure_version_file(${PLUGIN_NAME})

add_custom_command(TARGET ${PLUGIN_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/source
            -DMANIFEST=${_manifest}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake-check-sources.cmake
    COMMENT "Checking source/ against the generated project"
    VERBATIM)

target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")

target_link_libraries(${PLUGIN_NAME}
    PRIVATE
        sdk
        vstgui_support
)

# Everything in resource/ except the AU identity plist, which is not a
# plug-in resource - it is an input to smtg_target_add_auv2 below.
# CONFIGURE_DEPENDS omitted for the same reason as the sources above.
file(GLOB plugin_resources "${CMAKE_CURRENT_SOURCE_DIR}/resource/*.png")
smtg_target_add_plugin_resources(${PLUGIN_NAME} RESOURCES ${plugin_resources})

if(SMTG_MAC)
    smtg_target_set_bundle(${PLUGIN_NAME}
        BUNDLE_IDENTIFIER "${PLUGIN_BUNDLE_ID}"
        COMPANY_NAME "${PLUGIN_COMPANY}"
    )
    smtg_target_set_debug_executable(${PLUGIN_NAME}
        "/Applications/VST3PluginTestHost.app"
        "--pluginfolder;$(BUILT_PRODUCTS_DIR)"
    )
    if(PORT_ADHOC_SIGN)
        add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
            COMMAND /usr/bin/codesign --force --sign -
                    "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/${PLUGIN_NAME}.vst3"
            COMMENT "Ad-hoc signing ${PLUGIN_NAME}.vst3"
            VERBATIM
        )
    endif()
elseif(SMTG_WIN)
    if(MSVC)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PLUGIN_NAME})
    endif()
endif(SMTG_MAC)

#-----------------------------------------------------------------------------
# The Audio Unit target
#
# smtg_target_add_auv2 builds the wrapper, symlinks the VST3 into
# <plug-in>.component/Contents/Resources/plugin.vst3, and copies the bundle to
# ~/Library/Audio/Plug-Ins/Components/ after every build.
#
# The four-character codes that identify the plug-in to macOS live in
# resource/au-info.plist, not here.
#-----------------------------------------------------------------------------
if(PORT_AU_ENABLED AND SMTG_ENABLE_AUV2_BUILDS)
    include(SMTG_AddVST3AuV2)

    # The helper only defines itself under the Xcode generator. Check rather
    # than assume, so a surprise here is a warning about the Audio Unit and not
    # a hard configure failure that also costs you the VST3.
    if(COMMAND smtg_target_add_auv2)
        smtg_target_add_auv2(${PLUGIN_NAME}-au
            BUNDLE_NAME ${PLUGIN_NAME}
            BUNDLE_IDENTIFIER ${PLUGIN_AU_BUNDLE_ID}
            INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/resource/au-info.plist
            VST3_PLUGIN_TARGET ${PLUGIN_NAME}
        )
        # Steinberg's helper pins the wrapper to C++17. Newer AudioUnitSDK
        # headers need more than that, so lift it when the detection above
        # found C++20 constructs.
        if(PORT_AU_NEEDS_CXX20)
            message(STATUS "${PLUGIN_NAME}: this AudioUnitSDK needs C++20 - raising the standard on ${PLUGIN_NAME}-au")
            target_compile_features(${PLUGIN_NAME}-au PRIVATE cxx_std_20)
            set_target_properties(${PLUGIN_NAME}-au PROPERTIES
                CXX_STANDARD 20
                CXX_STANDARD_REQUIRED ON
                XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++20"
            )
        endif()

        # Runs after the helper's own post-build steps, so the copy in
        # ~/Library/Audio/Plug-Ins/Components has already been made.
        if(PORT_ADHOC_SIGN)
            add_custom_command(TARGET ${PLUGIN_NAME}-au POST_BUILD
                COMMAND /usr/bin/codesign --force --sign -
                        "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/${PLUGIN_NAME}.component"
                COMMAND /usr/bin/codesign --force --sign -
                        "$ENV{HOME}/Library/Audio/Plug-Ins/Components/${PLUGIN_NAME}.component"
                COMMENT "Ad-hoc signing ${PLUGIN_NAME}.component"
                VERBATIM
            )
        endif()

        message(STATUS "${PLUGIN_NAME}: Audio Unit target enabled (${PLUGIN_NAME}.component)")
    else()
        message(WARNING
            "${PLUGIN_NAME}: SMTG_AddVST3AuV2 did not define smtg_target_add_auv2, so the Audio Unit "
            "target is skipped. The VST3 build is unaffected.")
    endif()
elseif(PORT_AU_ENABLED)
    message(WARNING "${PLUGIN_NAME}: the VST3 SDK did not enable AUv2 builds - no Audio Unit target")
endif()

message(STATUS "${PLUGIN_NAME}: using VST3 SDK at ${VST3_SDK_ROOT}")
