//------------------------------------------------------------------------
// Project6 - sample slot tests
//
// SDK-FREE, like DspTests.cpp: Project6Slots.{h,cpp} include no VST3
// header, so this runs anywhere.
//
//     c++ -std=c++17 -O2 -Wall -Isource tests/SlotTests.cpp
//         source/Project6Slots.cpp -o /tmp/slottests && /tmp/slottests
//
//   (one line, wrapped; a comment line may not end in a backslash)
//
// What is under test is the part of the slot grid that has NO USER
// INTERFACE IN IT: which paths are accepted, what a slot is called on
// screen, and that the bank refuses an index rather than clamping it. All
// three are answered here and by nothing else, which is what stops the
// drop handler, the panel and the state code from each having an opinion.
//------------------------------------------------------------------------

#include "Project6Slots.h"

#include <cstdio>
#include <string>

using namespace Project6;

namespace {

int gFailures = 0;

void check (bool condition, const char* what)
{
	std::printf ("  %-64s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		++gFailures;
}

void section (const char* title)
{
	std::printf ("\n%s\n", title);
}

} // namespace

//------------------------------------------------------------------------
int main ()
{
	std::printf ("Project6 - sample slot tests\n");

	//--------------------------------------------------------------------
	section ("1. The grid, and its one piece of arithmetic");
	//--------------------------------------------------------------------
	{
		check (kSlotColumns == 8 && kSlotRows == 8, "the grid is 8 by 8");
		check (kSlotCount == 64, "which is sixty-four slots");

		// ROW-MAJOR. If this ever flips, every saved project's slots
		// transpose - so it is asserted at the corners, not assumed.
		check (slotIndex (0, 0) == 0,  "the top-left slot is 0");
		check (slotIndex (7, 0) == 7,  "the top-right slot is 7");
		check (slotIndex (0, 1) == 8,  "the second row starts at 8");
		check (slotIndex (7, 7) == 63, "the bottom-right slot is 63");

		// Every (column, row) must land on a distinct index, and they must
		// cover the whole range. A one-character slip in the arithmetic
		// passes the four corners above and fails this.
		bool seen[kSlotCount] = { false };
		bool coversAll = true;
		for (int row = 0; row < kSlotRows; ++row)
			for (int column = 0; column < kSlotColumns; ++column)
			{
				const int index = slotIndex (column, row);
				coversAll &= isSlotIndex (index);
				coversAll &= !seen[index];
				seen[index] = true;
			}
		for (bool one : seen)
			coversAll &= one;
		check (coversAll, "the 64 cells map one-to-one onto the 64 indices");

		check (! isSlotIndex (-1),        "-1 is not a slot");
		check (! isSlotIndex (kSlotCount), "and neither is one past the end");
	}

	//--------------------------------------------------------------------
	section ("2. What the slots will take");
	//--------------------------------------------------------------------
	{
		check (isAcceptedSampleFile ("/Users/andy/Samples/kick.wav"),
		       "a .wav is accepted");
		check (isAcceptedSampleFile ("/Users/andy/Samples/KICK.WAV"),
		       "and so is a .WAV - refusing it would look like a failed drop");
		check (isAcceptedSampleFile ("C:\\Samples\\snare.Wav"),
		       "and a Windows path out of an old project file");
		check (isAcceptedSampleFile ("kick.wav"),
		       "and a bare filename with no directory at all");

		check (! isAcceptedSampleFile ("/Users/andy/Samples/kick.aiff"),
		       "an .aiff is refused - the list has one entry today");
		check (! isAcceptedSampleFile ("/Users/andy/Samples/kick"),
		       "so is a file with no extension");
		check (! isAcceptedSampleFile ("/Users/andy/Samples/"),
		       "so is a directory");
		check (! isAcceptedSampleFile (""),
		       "so is an empty path");

		// NEGATIVE CONTROL for the extension test itself. "wav" has to be
		// an EXTENSION, not a substring: a folder called Samples.wav with
		// a file in it, or a file called wavetable, must not sneak past.
		check (! isAcceptedSampleFile ("/Users/andy/Samples.wav/notes"),
		       "NEGATIVE CONTROL: .wav in a DIRECTORY name does not count");
		check (! isAcceptedSampleFile ("/Users/andy/wavetable"),
		       "and neither does a name that merely contains 'wav'");

		// A hidden file whose whole name is the extension has nothing to
		// show on a slot, so it is refused rather than drawn as a full
		// slot that looks empty.
		check (! isAcceptedSampleFile ("/Users/andy/.wav"),
		       "a dot-file called '.wav' is refused, having no name to show");
	}

	//--------------------------------------------------------------------
	section ("3. What a slot is called on screen");
	//--------------------------------------------------------------------
	{
		check (slotFileName ("/Users/andy/Samples/kick 01.wav") == "kick 01.wav",
		       "the name is the last path component, spaces and all");
		check (slotFileStem ("/Users/andy/Samples/kick 01.wav") == "kick 01",
		       "the stem is that without the extension");

		check (slotFileName ("C:\\Samples\\snare.wav") == "snare.wav",
		       "backslashes separate directories too");
		check (slotFileStem ("C:\\Samples\\snare.wav") == "snare",
		       "on both kinds of path");

		check (slotFileName ("kick.wav") == "kick.wav",
		       "a bare filename is its own name");

		check (slotFileStem ("/Users/andy/Samples/two.dots.wav") == "two.dots",
		       "only the LAST dot is the extension");

		check (slotFileStem ("/Users/andy/Samples/noextension") == "noextension",
		       "a name with no dot is its own stem");

		// A dot at position 0 is a hidden file, not an extension. Taking
		// it off would leave an empty string, and an empty string on a
		// loaded slot reads as an empty slot.
		check (slotFileStem ("/Users/andy/.profile") == ".profile",
		       "a leading dot is part of the name, not an extension");

		check (slotFileName ("/Users/andy/Samples/") == "",
		       "a trailing separator has no name to give");
		check (slotFileName ("") == "", "and neither has an empty path");
	}

	//--------------------------------------------------------------------
	section ("4. The bank");
	//--------------------------------------------------------------------
	{
		SlotBank bank;

		check (bank.loadedCount () == 0, "a fresh bank is empty");
		check (! bank.loaded (0),        "and slot 0 with it");
		check (bank.path (0).empty (),   "an empty slot has an empty path");

		check (bank.setPath (0, "/a/kick.wav"), "a path can be recorded");
		check (bank.path (0) == "/a/kick.wav",  "and read back exactly");
		check (bank.loaded (0),                 "the slot is now loaded");
		check (bank.loadedCount () == 1,        "and it is the only one");

		check (bank.setPath (63, "/a/hat.wav"), "so can the last slot");
		check (bank.loadedCount () == 2,        "two loaded");

		// REFUSED, not clamped. A drop that lands nowhere must not
		// silently land on slot 0 or slot 63.
		check (! bank.setPath (-1, "/a/x.wav"),        "index -1 is refused");
		check (! bank.setPath (kSlotCount, "/a/x.wav"), "so is one past the end");
		check (bank.loadedCount () == 2,
		       "NEGATIVE CONTROL: neither refusal wrote to a real slot");
		check (bank.path (0) == "/a/kick.wav",  "slot 0 is untouched");
		check (bank.path (63) == "/a/hat.wav",  "and so is slot 63");

		check (bank.path (-1).empty (),         "reading a bad index gives nothing");
		check (bank.path (kSlotCount).empty (), "at either end");

		check (bank.clear (0),          "a slot can be emptied");
		check (! bank.loaded (0),       "and is then empty");
		check (bank.loadedCount () == 1, "leaving one loaded");
		check (! bank.clear (kSlotCount), "clearing a bad index is refused too");

		// Replacing is not appending. Dropping a second file on a full
		// slot must leave one path there, not two entries and a leak.
		bank.setPath (63, "/a/ride.wav");
		check (bank.path (63) == "/a/ride.wav", "a drop on a full slot replaces it");
		check (bank.loadedCount () == 1,        "and does not add a second");

		bank.clearAll ();
		check (bank.loadedCount () == 0, "clearAll empties the whole bank");
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
