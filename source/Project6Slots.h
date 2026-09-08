//------------------------------------------------------------------------
// Project6 - the sample slots
//
// SDK-FREE, like Project6Dsp.*, and for the same two reasons: it can be
// tested standalone (tests/SlotTests.cpp), and BOTH SIDES OF THE VST3
// SPLIT USE IT. The processor owns the authoritative bank, the controller
// owns the one the panel draws, and the state stream carries the same
// sixty-four strings between them - so what counts as a loadable file, and
// what a slot is called on screen, are answered HERE and not three times.
//
// A path is all this holds. Nothing reads a file yet: dropping a .wav on a
// slot records where it is, and the audio that eventually gets loaded from
// it will be loaded by the DSP, from this path, on a thread that is
// allowed to touch a disk.
//------------------------------------------------------------------------

#pragma once

#include <string>

namespace Project6 {

//------------------------------------------------------------------------
// The grid
//
// Eight by eight, so the whole bank is on the panel at once and a slot's
// position is its identity - "third row, second in" is how someone
// remembers where they put a kick.
//------------------------------------------------------------------------
constexpr int kSlotColumns = 8;
constexpr int kSlotRows    = 8;
constexpr int kSlotCount   = kSlotColumns * kSlotRows;

/** ROW-MAJOR, and this is the only place that arithmetic lives. Reading
    order on the panel is reading order in the array, so slot 0 is the
    top-left one and slot 63 the bottom-right. */
constexpr int slotIndex (int column, int row) { return row * kSlotColumns + column; }

/** What a click on a column's launch button should do, given how many of
    that column's slots can play and how many of those are already armed:
    true to arm them all, false to stop them all.

    THE MIXED COLUMN IS THE DECISION HERE, and it is written down as a
    function so that changing it is deliberate. Three of eight playing,
    and you hit the column: this arms the other five rather than stopping
    the three, because the button's job is to PLAY the column - stopping
    is what the second press is for. The other reading, "any playing means
    stop", makes the first press on a half-full column do the opposite of
    what the button is called.

    A column with nothing loaded in it (`playable == 0`) arms nothing;
    the caller checks that before asking. */
constexpr bool columnClickArms (int playable, int armed)
{
	return armed < playable;
}

constexpr bool isSlotIndex (int index) { return index >= 0 && index < kSlotCount; }

/** True for a path this plug-in will take.

    The list is deliberately a list, even though there is one entry in it:
    adding .aif is one line here and nothing anywhere else, because the
    slot view, the drop handler and the state code all ask this function
    rather than testing the extension themselves. */
bool isAcceptedSampleFile (const std::string& path);

/** The file's own name, with its extension: "kick 01.wav".

    Directory separators are BOTH '/' and '\\'. A drop from Finder is a
    POSIX path, but a path that came back out of a project file was
    written by whatever host saved it, and one of those will one day be
    Windows. */
std::string slotFileName (const std::string& path);

/** The same, with the extension taken off: "kick 01".

    The panel prefers the full name and falls back to this when the full
    name does not fit - see SpySampleSlot::draw. Every file in a slot is a
    .wav, so those four characters are the first thing worth losing and
    the last thing worth reading. */
std::string slotFileStem (const std::string& path);

//------------------------------------------------------------------------
/** Sixty-four paths, and nothing else.

    Deliberately not a map from index to path: the array is fixed, empty
    is the empty string, and there is no state in which a slot exists but
    is missing. That is what makes the state stream a plain run of
    kSlotCount strings and both readers of it trivially agree. */
class SlotBank
{
public:
	SlotBank () = default;

	/** Records a path against a slot. An out-of-range index is REFUSED
	    rather than clamped - a drop that lands nowhere must not silently
	    land somewhere else - and the return says which happened. */
	bool setPath (int index, const std::string& path);

	/** Empties one slot. Same range rule. */
	bool clear (int index);

	void clearAll ();

	/** The path in a slot, or an empty string for an empty slot AND for
	    an out-of-range index. Callers that need to tell those two apart
	    have isSlotIndex. */
	const std::string& path (int index) const;

	bool loaded (int index) const;
	int  loadedCount () const;

private:
	std::string mPaths[kSlotCount];
};

//------------------------------------------------------------------------
} // namespace Project6
