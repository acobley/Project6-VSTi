//------------------------------------------------------------------------
// Project6 - the slot block of the state stream
//
// ONE PAIR OF FUNCTIONS, called by BOTH sides of the VST3 split:
// Project6Processor::getState / setState write and read the project's
// state, and Project6Controller::setComponentState reads the identical
// bytes to fill the panel. Two hand-written copies of this loop that had
// to agree is exactly the bug this file exists to prevent - the parameter
// block above it is already written twice and commented "if one side
// changes, both change", which is a rule kept by hand; this one is kept
// by the compiler.
//
// It cannot live in Project6Slots.h, which is deliberately SDK-free so it
// can be tested standalone. IBStreamer is an SDK type, so the streaming
// lives here and the data lives there.
//------------------------------------------------------------------------

#pragma once

#include "Project6Slots.h"

#include "base/source/fstreamer.h"

namespace Project6 {

/** The state stream version this build writes.

    1 was parameters and bypass only. 2 appended the slot block. The
    version is INFORMATIONAL - both readers below cope with a stream that
    simply stops early, which is the rule the parameter block already
    follows - but it is written so that a future format change that
    cannot be handled that way has something to test. */
constexpr Steinberg::int32 kStateVersion = 2;

/** A sanity bound on one path, so a corrupt or hostile stream cannot ask
    for an arbitrary allocation. Longer than any real path: macOS stops
    at 1024 bytes and Windows at 32767 characters only with a prefix no
    host writes. */
constexpr Steinberg::int32 kMaxSlotPathBytes = 4096;

/** Writes kSlotCount length-prefixed UTF-8 paths. An empty slot is a
    zero length, so the block is always the same shape and a reader never
    has to guess which slots were saved. */
bool writeSlots (Steinberg::IBStreamer& streamer, const SlotBank& slots);

/** Reads that block, having FIRST EMPTIED the bank.

    Returns false when the block is absent - a version 1 stream, or a
    truncated one - which leaves every slot empty. That is the same rule
    the parameters follow: anything a short stream does not mention goes
    back to its default rather than keeping what the previous patch left
    in this instance. It is not an error, and neither caller treats it as
    one. */
bool readSlots (Steinberg::IBStreamer& streamer, SlotBank& slots);

//------------------------------------------------------------------------
} // namespace Project6
