//------------------------------------------------------------------------
// Project6 - the slot block of the state stream, implementation
//------------------------------------------------------------------------

#include "Project6SlotState.h"

#include <string>

using namespace Steinberg;

namespace Project6 {

//------------------------------------------------------------------------
bool writeSlots (IBStreamer& streamer, const SlotBank& slots)
{
	if (!streamer.writeInt32 (static_cast<int32> (kSlotCount)))
		return false;

	for (int index = 0; index < kSlotCount; ++index)
	{
		const std::string& path = slots.path (index);
		const int32 length = static_cast<int32> (path.size ());

		if (!streamer.writeInt32 (length))
			return false;

		// NO TERMINATOR. The length is the length, so a path containing
		// anything at all round-trips - and nothing downstream has to
		// wonder whether the byte count includes a NUL, which is the
		// question that bites when reading a dropped file's path out of
		// a platform drag package.
		if (length > 0 && streamer.writeRaw (path.data (), length) != length)
			return false;
	}

	return true;
}

//------------------------------------------------------------------------
bool readSlots (IBStreamer& streamer, SlotBank& slots)
{
	// EMPTIED FIRST, unconditionally. Loading an old project after a new
	// one must not inherit the new one's samples, and the way that
	// happens is a reader that only writes the slots it finds.
	slots.clearAll ();

	int32 count = 0;
	if (!streamer.readInt32 (count))
		return false;                       // a stream from before slots existed

	if (count < 0 || count > kMaxSlotPathBytes)
		return false;                       // nonsense: stop rather than loop on it

	for (int32 i = 0; i < count; ++i)
	{
		int32 length = 0;
		if (!streamer.readInt32 (length))
			return false;

		if (length < 0 || length > kMaxSlotPathBytes)
			return false;

		std::string path;
		if (length > 0)
		{
			path.resize (static_cast<std::string::size_type> (length));
			if (streamer.readRaw (&path[0], length) != length)
				return false;
		}

		// A stream written by a build with MORE slots than this one has
		// is read to the end - the bytes have to be consumed either way,
		// or everything after the block is misread - and the extras are
		// dropped. setPath refuses the out-of-range index by itself.
		slots.setPath (static_cast<int> (i), path);
	}

	return true;
}

//------------------------------------------------------------------------
} // namespace Project6
