//------------------------------------------------------------------------
// Project6 - version strings
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/fplatform.h"

#define MAJOR_VERSION_STR "1"
#define MAJOR_VERSION_INT 1

#define SUB_VERSION_STR "1"
#define SUB_VERSION_INT 1

#define RELEASE_NUMBER_STR "1"
#define RELEASE_NUMBER_INT 1

#define BUILD_NUMBER_STR "0"
#define BUILD_NUMBER_INT 0

#define FULL_VERSION_STR MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR "." BUILD_NUMBER_STR
#define VERSION_STR      MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR

#define stringOriginalFilename "Project6.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription "Project6 VST3 (64Bit)"
#else
#define stringFileDescription "Project6 VST3"
#endif
// The VST3 vendor. NO FULL STOPS, and the same as the maker in the AU name
// (resource/au-info.plist): hosts list the plug-in under it and look for
// its presets in /Library/Audio/Presets/<it>/<plug-in>, and REAPER cuts an
// AU name at the first full stop. tools/check-versions.py checks all three.
#define stringCompanyName    "AE Cobley"
#define stringLegalCopyright "Copyright 2026 A. E. Cobley"
#define stringLegalTrademarks "VST is a trademark of Steinberg Media Technologies GmbH"
