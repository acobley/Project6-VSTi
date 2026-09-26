//------------------------------------------------------------------------
// Project6 - the state stream's slot and embedded-file blocks
//
// Needs the VST3 SDK for IBStreamer and MemoryStream, and nothing else of
// it - no plug-in, no host. What it proves is the one thing a project
// file has to get right: what writeSlots/writeEmbedded put in, the
// readers get back, and an older or damaged stream degrades to "read
// the file from disk" rather than to garbage.
//------------------------------------------------------------------------

// The interface ids this test needs are DEFINED here, once, the way the
// SDK's own module does it - there is no plug-in module in this build.
#define INIT_CLASS_IID
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/istringresult.h"
#include "pluginterfaces/base/iupdatehandler.h"
#undef INIT_CLASS_IID

#include "Project6SlotState.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Project6;

namespace {

int gFailures = 0;

void check (bool condition, const char* what)
{
	std::printf ("  %-64s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		++gFailures;
}

void section (const char* title) { std::printf ("\n%s\n", title); }

SlotFileBytes blob (std::size_t size, unsigned char seed)
{
	auto bytes = std::make_shared<std::vector<unsigned char>> (size);
	for (std::size_t i = 0; i < size; ++i)
		(*bytes)[i] = static_cast<unsigned char> (seed + i * 7);
	return bytes;
}

bool same (const SlotFileBytes& a, const SlotFileBytes& b)
{
	if (!a || !b)
		return !a && !b;
	return *a == *b;
}

} // namespace

int main ()
{
	//--------------------------------------------------------------------
	section ("1. Paths and files round-trip, in the order getState writes them");
	//--------------------------------------------------------------------
	{
		SlotBank slots;
		slots.setPath (0, "/Users/andy/Loops/one.wav");
		slots.setPath (9, std::string (kFactorySampleRoot) + "/Drums/two.wav");
		slots.setPath (63, "/Users/andy/Midi/three.mid");

		SlotFileBytes files[kSlotCount];
		files[0]  = blob (100000, 1);
		files[63] = blob (57, 9);          // slot 9 is factory: loadSlot never keeps one

		auto* memory = new MemoryStream ();
		{
			IBStreamer out (memory, kLittleEndian);
			check (writeSlots (out, slots),    "the paths are written");
			check (writeEmbedded (out, files), "and then the files");
		}
		memory->seek (0, IBStream::kIBSeekSet, nullptr);

		SlotBank back;
		back.setPath (5, "/left/over/from/before.wav");
		SlotFileBytes read[kSlotCount];
		read[5] = blob (3, 3);

		IBStreamer in (memory, kLittleEndian);
		check (readSlots (in, back),     "the paths read back");
		check (readEmbedded (in, read),  "and the files");

		check (back.path (0) == slots.path (0) && back.path (63) == slots.path (63),
		       "every path as written");
		check (back.path (5).empty () && !read[5],
		       "and nothing the previous project left behind");
		check (same (read[0], files[0]) && same (read[63], files[63]),
		       "every file byte for byte, 100 kB included");
		memory->release ();
	}

	//--------------------------------------------------------------------
	section ("2. A stream from before version 10");
	//--------------------------------------------------------------------
	{
		SlotBank slots;
		slots.setPath (2, "/a/b.wav");

		auto* memory = new MemoryStream ();
		{
			IBStreamer out (memory, kLittleEndian);
			writeSlots (out, slots);            // and NO embedded block
		}
		memory->seek (0, IBStream::kIBSeekSet, nullptr);

		SlotBank back;
		SlotFileBytes read[kSlotCount];
		read[2] = blob (4, 4);

		IBStreamer in (memory, kLittleEndian);
		readSlots (in, back);
		check (!readEmbedded (in, read), "the missing block is reported as absent");
		check (back.path (2) == "/a/b.wav" && !read[2],
		       "the path survives and the slot falls back to disk");
		memory->release ();
	}

	//--------------------------------------------------------------------
	section ("3. A damaged stream stops, and keeps what it read");
	//--------------------------------------------------------------------
	{
		SlotFileBytes files[kSlotCount];
		files[0] = blob (10, 1);
		files[1] = blob (5000, 2);

		auto* memory = new MemoryStream ();
		{
			IBStreamer out (memory, kLittleEndian);
			writeEmbedded (out, files);
		}

		// Cut it off half way through slot 1's bytes.
		int64 size = 0;
		memory->seek (0, IBStream::kIBSeekEnd, &size);
		memory->setSize (4 + 4 + 10 + 4 + 100);
		memory->seek (0, IBStream::kIBSeekSet, nullptr);

		SlotFileBytes read[kSlotCount];
		IBStreamer in (memory, kLittleEndian);
		check (!readEmbedded (in, read), "a truncated block is not accepted whole");
		check (same (read[0], files[0]), "the slot before the damage is kept");
		check (!read[1],                 "the damaged slot falls back to disk");
		memory->release ();
	}

	{
		// A length no real file has.
		auto* memory = new MemoryStream ();
		{
			IBStreamer out (memory, kLittleEndian);
			out.writeInt32 (kSlotCount);
			out.writeInt32 (-5);
		}
		memory->seek (0, IBStream::kIBSeekSet, nullptr);

		SlotFileBytes read[kSlotCount];
		IBStreamer in (memory, kLittleEndian);
		check (!readEmbedded (in, read), "a negative length is refused, not allocated");
		memory->release ();
	}

	std::printf ("\n%s  (%d failure%s)\n",
	             gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
	             gFailures, gFailures == 1 ? "" : "s");
	return gFailures == 0 ? 0 : 1;
}
