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

		// The inverses. A slot's ROW decides which bus it sums into, so
		// an off-by-one here is a pad appearing on the wrong fader.
		bool inverses = true;
		for (int row = 0; row < kSlotRows; ++row)
			for (int column = 0; column < kSlotColumns; ++column)
			{
				const int index = slotIndex (column, row);
				inverses &= (rowOfSlot (index) == row);
				inverses &= (columnOfSlot (index) == column);
			}
		check (inverses, "row and column can be read back off every one of the 64");

		check (rowOfSlot (0) == 0 && rowOfSlot (7) == 0, "the first row is slots 0..7");
		check (rowOfSlot (8) == 1, "and the second starts at 8");
		check (rowOfSlot (63) == kSlotRows - 1, "and the last slot is on the last row");

		check (isRowIndex (0) && isRowIndex (kSlotRows - 1), "the rows are 0..7");
		check (! isRowIndex (-1) && ! isRowIndex (kSlotRows), "and nothing else is a row");
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
	section ("4. What a column button does");
	//--------------------------------------------------------------------
	{
		// The plain cases.
		check (columnClickArms (8, 0), "an idle column is armed by a click");
		check (! columnClickArms (8, 8), "a fully armed one is stopped");

		// THE JUDGEMENT. Three of eight playing: the button's job is to
		// PLAY the column, so it arms the other five. Stopping is what
		// the second press is for. This is here so that changing it is a
		// decision someone makes on purpose rather than a line someone
		// tidies.
		check (columnClickArms (8, 3),
		       "a half-armed column is FILLED, not emptied, by the first press");
		check (columnClickArms (8, 7), "even at seven of eight");
		check (! columnClickArms (8, 3) == false,
		       "NEGATIVE CONTROL: which is the opposite of 'any playing means stop'");

		// Columns are rarely full: only the slots with files in them
		// count, and the caller passes that count.
		check (columnClickArms (3, 0), "a column with three loaded slots arms them");
		check (! columnClickArms (3, 3), "and stops them when all three are armed");
		check (columnClickArms (3, 2), "two of the three is still a fill");

		// An empty column. The caller is expected to check first, but the
		// answer must not be "arm the nothing that is there".
		check (! columnClickArms (0, 0), "a column with nothing loaded arms nothing");
	}

	//--------------------------------------------------------------------
	section ("5. The bank");
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
	section ("6. Picking a pad up and dropping it on another");
	//--------------------------------------------------------------------
	{
		// The payload a pad puts on the pasteboard, and reading it back.
		const std::string packed = encodeSlotDrag (37, "/Users/andy/Loops/kick 01.wav");
		check (! packed.empty (), "a loaded pad encodes to something");

		int index = -1;
		std::string path;
		check (decodeSlotDrag (packed, &index, &path), "and it decodes again");
		check (index == 37, "to the slot it came from");
		check (path == "/Users/andy/Loops/kick 01.wav", "and the path it was holding");

		// A SPACE IN THE NAME survives, which is the whole reason the
		// payload is not a URL.
		check (path.find (' ') != std::string::npos, "spaces and all");

		// Nothing to drag out of an empty slot, or a slot that is not one.
		check (encodeSlotDrag (37, "").empty (),      "an empty slot encodes to nothing");
		check (encodeSlotDrag (-1, "/a.wav").empty (), "and neither does a bad index");
		check (encodeSlotDrag (kSlotCount, "/a.wav").empty (), "at either end");

		// TOLD APART FROM EVERY OTHER DRAG. This is the test that matters:
		// a Finder drag must not be read as a pad being moved.
		check (! decodeSlotDrag ("/Users/andy/kick.wav", &index, &path),
		       "NEGATIVE CONTROL: a bare path is not one of ours");
		check (! decodeSlotDrag ("file:///Users/andy/kick.wav", &index, &path),
		       "nor is a file URL");
		check (! decodeSlotDrag ("", &index, &path), "nor is nothing at all");

		// Malformed payloads are REFUSED rather than half-read. These
		// cross a platform callback, so a throw here would leave the drag
		// machinery mid-gesture.
		check (! decodeSlotDrag ("Project6/slot:37", nullptr, nullptr),
		       "a payload with no path is refused");
		check (! decodeSlotDrag ("Project6/slot:\n/a.wav", nullptr, nullptr),
		       "and one with no index");
		check (! decodeSlotDrag ("Project6/slot:64\n/a.wav", nullptr, nullptr),
		       "and one naming a slot that does not exist");
		check (! decodeSlotDrag ("Project6/slot:99999999999999\n/a.wav", nullptr, nullptr),
		       "and one whose number would not fit in an int");
		check (! decodeSlotDrag ("Project6/slot:3x\n/a.wav", nullptr, nullptr),
		       "and one whose number is not a number");

		// Both outputs are optional, and an untouched one stays untouched
		// on a refusal.
		index = 11;
		check (! decodeSlotDrag ("nonsense", &index, nullptr), "a refusal is a refusal");
		check (index == 11, "and leaves what it was given alone");

		//----------------------------------------------------------------
		// The other end: whatever text a drag from OUTSIDE delivered.
		//----------------------------------------------------------------
		check (sampleFilePathFromDragText ("/Users/andy/kick.wav") == "/Users/andy/kick.wav",
		       "a plain path comes through unchanged");

		// THE URL FORM. macOS hands a dragged file back like this, and a
		// path with the escapes left in is a file that is not there.
		check (sampleFilePathFromDragText ("file:///Users/andy/My%20Loops/kick%2001.wav")
		           == "/Users/andy/My Loops/kick 01.wav",
		       "a file URL is unescaped back into a path");
		check (sampleFilePathFromDragText ("file:///a/b.wav") == "/a/b.wav",
		       "and a plain one needs no unescaping");

		// A LITERAL PER-CENT in a real filename is left alone, because
		// nothing said it was a URL.
		check (sampleFilePathFromDragText ("/Users/andy/100%.wav") == "/Users/andy/100%.wav",
		       "a per-cent sign in a plain path is not an escape");
		check (sampleFilePathFromDragText ("/Users/andy/50%20off.wav")
		           == "/Users/andy/50%20off.wav",
		       "NEGATIVE CONTROL: and neither is one that looks like one");

		// Trailing newlines and several files: the first, trimmed.
		check (sampleFilePathFromDragText ("/a/one.wav\n") == "/a/one.wav",
		       "a trailing newline is trimmed off");
		check (sampleFilePathFromDragText ("  /a/one.wav  ") == "/a/one.wav",
		       "and so is surrounding space");
		check (sampleFilePathFromDragText ("/a/one.wav\n/a/two.wav") == "/a/one.wav",
		       "several files means the first, not all of them");

		// And it still refuses what the plug-in will not take, so the
		// caller has one question to ask rather than two.
		check (sampleFilePathFromDragText ("/a/track.mp3").empty (), "an mp3 is refused");
		check (sampleFilePathFromDragText ("file:///a/track.aiff").empty (),
		       "and so is an aiff, URL or not");
		check (sampleFilePathFromDragText ("").empty (),        "and nothing at all");
		check (sampleFilePathFromDragText ("   \n  ").empty (), "and whitespace");
		check (sampleFilePathFromDragText ("file://").empty (),
		       "and a URL with no path in it");
	}

	//--------------------------------------------------------------------
	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
