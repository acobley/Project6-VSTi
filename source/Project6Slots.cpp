//------------------------------------------------------------------------
// Project6 - the sample slots, implementation
//
// SDK-free. If a Steinberg header ever appears here the standalone test
// build stops working - see the banner in Project6Slots.h.
//------------------------------------------------------------------------

#include "Project6Slots.h"

#include <algorithm>
#include <cctype>

namespace Project6 {

namespace {

/** What the slots will take. One entry today; see the header. */
const char* const kAcceptedExtensions[] = { ".wav" };

/** Case-insensitive, because a file called KICK.WAV is a wav file and
    refusing it would look like the drop had failed. */
bool endsWithNoCase (const std::string& text, const std::string& suffix)
{
	if (suffix.size () > text.size ())
		return false;

	return std::equal (suffix.rbegin (), suffix.rend (), text.rbegin (),
	                   [] (char a, char b)
	                   {
		                   return std::tolower (static_cast<unsigned char> (a))
		                          == std::tolower (static_cast<unsigned char> (b));
	                   });
}

/** One past the last directory separator, or 0. */
size_t nameStart (const std::string& path)
{
	const size_t slash = path.find_last_of ("/\\");
	return (slash == std::string::npos) ? 0 : slash + 1;
}

const std::string kNoPath;

} // namespace

//------------------------------------------------------------------------
bool isAcceptedSampleFile (const std::string& path)
{
	// A path that is nothing but an extension - ".wav" - is a UNIX hidden
	// file with no name, not a sample. slotFileStem would have nothing to
	// show for it, so it is refused here rather than drawn as a blank slot
	// that is nonetheless full.
	const std::string name = slotFileName (path);
	if (name.empty () || name.front () == '.')
		return false;

	for (const char* extension : kAcceptedExtensions)
		if (endsWithNoCase (name, extension))
			return true;

	return false;
}

//------------------------------------------------------------------------
std::string slotFileName (const std::string& path)
{
	// A trailing separator means a directory was dropped. There is no name
	// to take, and returning the parent's would be a lie.
	if (!path.empty () && (path.back () == '/' || path.back () == '\\'))
		return std::string ();

	return path.substr (nameStart (path));
}

//------------------------------------------------------------------------
std::string slotFileStem (const std::string& path)
{
	const std::string name = slotFileName (path);

	const size_t dot = name.find_last_of ('.');

	// dot == 0 is a hidden file, not an extension: ".profile" is called
	// ".profile" and taking the "extension" off it would leave nothing.
	if (dot == std::string::npos || dot == 0)
		return name;

	return name.substr (0, dot);
}

//------------------------------------------------------------------------
bool SlotBank::setPath (int index, const std::string& path)
{
	if (!isSlotIndex (index))
		return false;

	mPaths[index] = path;
	return true;
}

//------------------------------------------------------------------------
bool SlotBank::clear (int index)
{
	if (!isSlotIndex (index))
		return false;

	mPaths[index].clear ();
	return true;
}

//------------------------------------------------------------------------
void SlotBank::clearAll ()
{
	for (auto& path : mPaths)
		path.clear ();
}

//------------------------------------------------------------------------
const std::string& SlotBank::path (int index) const
{
	if (!isSlotIndex (index))
		return kNoPath;

	return mPaths[index];
}

//------------------------------------------------------------------------
bool SlotBank::loaded (int index) const
{
	return !path (index).empty ();
}

//------------------------------------------------------------------------
int SlotBank::loadedCount () const
{
	int count = 0;
	for (const auto& path : mPaths)
		if (!path.empty ())
			++count;
	return count;
}

//------------------------------------------------------------------------
} // namespace Project6
