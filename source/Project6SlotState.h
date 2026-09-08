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

#include "Project6Params.h"
#include "Project6Slots.h"

#include "base/source/fstreamer.h"

namespace Project6 {

/** The state stream version this build writes.

    1 was parameters and bypass only. 2 appended the slot paths. 3
    appended the slot levels - which are parameters, but sit past the
    published block and so cannot ride the contiguous run from id 0. 4
    appended the row levels and 5 the launch divisions, in further blocks
    of the same shape. The
    version is INFORMATIONAL: every reader below copes with a stream that
    simply stops early, which is the rule the parameter block already
    follows. It is written so that a future format change that cannot be
    handled that way has something to test. */
constexpr Steinberg::int32 kStateVersion = 5;

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
/** A run of normalised parameter values, length-prefixed.

    Called "value" and not "level" because it now carries the launch
    divisions too - the shape is a run of normalised doubles and has
    nothing to do with what they mean.


    BLOCKS RATHER THAN AN EXTENSION of the parameter run at the top of the
    stream, because these are ids 132 onwards and that run stops at 1.
    Appending is the rule; moving an id is not an option. There are three
    of these now - the slot levels, the row levels and the launch
    divisions - and they share one pair of functions rather than being the
    same twenty lines typed three times.

    `normalized` is `count` doubles: a pointer into `mParams` on the
    processor's side, and a local array on the controller's, which then
    pushes them through setParamNormalized. */
bool writeValueBlock (Steinberg::IBStreamer& streamer, const double* normalized, int count);

/** Reads one such block, having FIRST filled the array with `fallback` -
    same rule as readSlots, and for the same reason: a project saved
    before a block existed must load at that block's default rather than
    inheriting the balance of whatever was open before.

    Returns false when the block is absent, which is an older stream and
    not an error. */
bool readValueBlock (Steinberg::IBStreamer& streamer, double* normalized, int count,
                     double fallback);

//------------------------------------------------------------------------
} // namespace Project6
