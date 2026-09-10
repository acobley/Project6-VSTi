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

/** What the slots will take, by kind. Lists even where there is one
    entry - see the header. */
const char* const kAudioExtensions[] = { ".wav" };
const char* const kMidiExtensions[]  = { ".mid", ".midi" };

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
const char* const kSlotDragPrefix = "Project6/slot:";

namespace {

/** One hex digit, or -1. */
int hexDigit (char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/** The first line of `text`, with surrounding whitespace taken off.

    A text drag usually arrives with a trailing newline, and a drag of
    several files arrives as several lines. Taking the first is the same
    rule the drop handler follows for a multi-file package: a drop on ONE
    slot is a drop on one slot. */
std::string firstLine (const std::string& text)
{
	const std::size_t end = text.find_first_of ("\r\n");
	std::string line = (end == std::string::npos) ? text : text.substr (0, end);

	const std::size_t from = line.find_first_not_of (" \t");
	if (from == std::string::npos)
		return std::string ();
	const std::size_t to = line.find_last_not_of (" \t");

	return line.substr (from, to - from + 1);
}

/** %20 -> space, and so on. Only ever called on the URL form. */
std::string percentDecode (const std::string& text)
{
	std::string out;
	out.reserve (text.size ());

	for (std::size_t i = 0; i < text.size (); ++i)
	{
		if (text[i] == '%' && i + 2 < text.size ())
		{
			const int high = hexDigit (text[i + 1]);
			const int low  = hexDigit (text[i + 2]);
			if (high >= 0 && low >= 0)
			{
				// A LITERAL PER-CENT THAT IS NOT AN ESCAPE is left alone
				// rather than swallowed: "100%.wav" is a legal filename.
				out.push_back (static_cast<char> (high * 16 + low));
				i += 2;
				continue;
			}
		}
		out.push_back (text[i]);
	}

	return out;
}

} // namespace

//------------------------------------------------------------------------
std::string encodeSlotDrag (int index, const std::string& path)
{
	if (!isSlotIndex (index) || path.empty ())
		return std::string ();

	// The index first, then a newline, then the path - so the path may
	// contain anything at all except a newline, which no filesystem this
	// runs on allows in a name.
	return std::string (kSlotDragPrefix) + std::to_string (index) + "\n" + path;
}

//------------------------------------------------------------------------
bool decodeSlotDrag (const std::string& text, int* index, std::string* path)
{
	const std::string prefix (kSlotDragPrefix);
	if (text.compare (0, prefix.size (), prefix) != 0)
		return false;

	const std::size_t newline = text.find ('\n', prefix.size ());
	if (newline == std::string::npos)
		return false;

	const std::string digits = text.substr (prefix.size (), newline - prefix.size ());
	if (digits.empty ())
		return false;

	// PARSED BY HAND rather than with stoi, which throws on a bad string
	// and would turn a malformed drag into an exception crossing a
	// platform callback.
	int value = 0;
	for (const char c : digits)
	{
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + (c - '0');
		if (value > kSlotCount)      // cannot be a slot; stop before it overflows
			return false;
	}

	if (!isSlotIndex (value))
		return false;

	const std::string found = text.substr (newline + 1);
	if (found.empty ())
		return false;

	if (index != nullptr)
		*index = value;
	if (path != nullptr)
		*path = found;
	return true;
}

//------------------------------------------------------------------------
std::string sampleFilePathFromDragText (const std::string& text)
{
	std::string line = firstLine (text);
	if (line.empty ())
		return std::string ();

	// THE URL FORM, which is what a file dragged on macOS comes back as.
	// "file://" then an authority that is empty for a local file, then
	// the path - so the third slash is where the path starts.
	const std::string scheme = "file://";
	if (line.compare (0, scheme.size (), scheme) == 0)
	{
		const std::size_t slash = line.find ('/', scheme.size ());
		if (slash == std::string::npos)
			return std::string ();

		line = percentDecode (line.substr (slash));
	}

	return isAcceptedSlotFile (line) ? line : std::string ();
}

//------------------------------------------------------------------------
SlotFileKind slotFileKind (const std::string& path)
{
	// A path that is nothing but an extension - ".wav" - is a UNIX hidden
	// file with no name, not a sample. slotFileStem would have nothing to
	// show for it, so it is refused here rather than drawn as a blank slot
	// that is nonetheless full.
	const std::string name = slotFileName (path);
	if (name.empty () || name.front () == '.')
		return SlotFileKind::None;

	for (const char* extension : kAudioExtensions)
		if (endsWithNoCase (name, extension))
			return SlotFileKind::Audio;

	// MIDI IS TESTED SECOND AND ".midi" BEFORE ".mid" cannot matter,
	// because endsWithNoCase compares whole suffixes: "loop.midi" ends
	// with ".midi" and does not end with ".mid".
	for (const char* extension : kMidiExtensions)
		if (endsWithNoCase (name, extension))
			return SlotFileKind::Midi;

	return SlotFileKind::None;
}

//------------------------------------------------------------------------
const char* slotFileKindName (SlotFileKind kind)
{
	switch (kind)
	{
		case SlotFileKind::Audio: return "audio";
		case SlotFileKind::Midi:  return "MIDI";
		case SlotFileKind::None:  return "empty";
	}
	return "empty";
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
