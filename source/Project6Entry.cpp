//------------------------------------------------------------------------
// Project6 - plug-in factory
//------------------------------------------------------------------------

#include "Project6Controller.h"
#include "Project6IDs.h"
#include "Project6Processor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "Project6"

using namespace Steinberg::Vst;
using namespace Project6;

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF (stringCompanyName, "https://github.com/", "mailto:aecobley@googlemail.com")

	// Project6 is an INSTRUMENT, not an effect: PlugType::kInstrumentSynth.
	// This string is what a host reads to decide which list to put it in,
	// and it has to agree with the 'aumu' type code in
	// resource/au-info.plist and with the buses the processor adds.
	DEF_CLASS2 (INLINE_UID_FROM_FUID (kProject6ProcessorUID),
	            PClassInfo::kManyInstances,
	            kVstAudioEffectClass,
	            stringPluginName,
	            Vst::kDistributable,
	            PlugType::kInstrumentSynth,
	            FULL_VERSION_STR,
	            kVstVersionString,
	            Project6Processor::createInstance)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (kProject6ControllerUID),
	            PClassInfo::kManyInstances,
	            kVstComponentControllerClass,
	            stringPluginName "Controller",
	            0,
	            "",
	            FULL_VERSION_STR,
	            kVstVersionString,
	            Project6Controller::createInstance)

END_FACTORY
