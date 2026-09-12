#!/usr/bin/env python3
"""Trace the AU property traffic between the host and Steinberg's wrapper.

WHY THIS EXISTS. The MIDI log proved the plug-in emits correctly under the AU
wrapper and that Reaper receives none of it (PORTING-NOTES 0a). Everything
between those two facts happens inside `auwrapper.mm`, and the whole question
reduces to one line in MIDIOutputCallbackHelper::fireAtTimeStamp:

    if (!mMIDIMessageList.empty () && mMIDICallbackStruct.midiOutputCallback != nullptr)

That callback is null until a host sets kAudioUnitProperty_MIDIOutputCallback
(2019). No log this plug-in can write reaches inside there, and guessing is
what this bug has already cost four wrong fixes, so the wrapper gets
instrumented instead.

WHAT IT ADDS. Nothing that changes behaviour - four fprintf sites:

  * every SetProperty id the host writes, once per id
  * every GetPropertyInfo id the host asks about, once per id
  * setCallbackInfo, saying whether the callback handed over is real or null
  * fireAtTimeStamp, ONCE, when it has events and no callback to send them to

It writes to ~/p6-au-properties.txt, and like the MIDI log it only writes if
that file ALREADY EXISTS - so an instrumented build that nobody asked to trace
is silent and creates nothing.

WHY A SCRIPT RATHER THAN AN EDIT. `external/` is gitignored and re-cloned by
the first cmake configure, so a hand edit is lost the next time somebody builds
clean, silently, and the trace comes back empty for a reason that has nothing
to do with the bug. This is version-controlled, idempotent, and reversible.

    python3 tools/au-property-trace.py            # apply
    python3 tools/au-property-trace.py --revert   # take it back out

Then rebuild, `touch ~/p6-au-properties.txt`, and run the host.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WRAPPER = os.path.normpath(os.path.join(
    HERE, '..', 'external', 'vst3sdk', 'public.sdk', 'source', 'vst',
    'auwrapper', 'auwrapper.mm'))

MARK = 'P6AU_TRACE'

# The logger, dropped in just after `namespace Vst {`. Static, file-local, and
# opened lazily on first use from whichever thread gets there first - which is
# the property thread long before the audio thread, because a host sets
# properties before it renders.
HELPER = '''namespace Vst {

//------------------------------------------------------------------------
// P6AU_TRACE - inserted by tools/au-property-trace.py. Remove with --revert.
//
// ONLY EVER WRITES IF ~/p6-au-properties.txt ALREADY EXISTS, so a build
// nobody asked to trace is silent and creates nothing.
//------------------------------------------------------------------------
static std::FILE* p6auTraceFile ()
{
	static std::FILE* file = [] () -> std::FILE* {
		const char* home = std::getenv ("HOME");
		if (home == nullptr)
			return nullptr;

		const std::string path = std::string (home) + "/p6-au-properties.txt";

		// Asking the question must not answer it: opened for reading first.
		std::FILE* probe = std::fopen (path.c_str (), "r");
		if (probe == nullptr)
			return nullptr;
		std::fclose (probe);

		std::FILE* opened = std::fopen (path.c_str (), "w");
		if (opened != nullptr)
		{
			std::fprintf (opened, "Project6 AU property trace\\n"
			                      "  one line the first time the host touches each id\\n\\n");
			std::fflush (opened);
		}
		return opened;
	} ();
	return file;
}

static void p6auTrace (const char* what, unsigned long id, const char* note)
{
	std::FILE* file = p6auTraceFile ();
	if (file == nullptr)
		return;

	std::fprintf (file, "%-16s %-10lu %s\\n", what, id, note ? note : "");
	std::fflush (file);
}

/** True the FIRST time it is called with this id and tag, false after, so a
    property the host asks about a hundred times is one line.

    MUTEXED, AND FOR THE PROPERTY THREAD ONLY. A std::set is not safe against
    concurrent insertion and the audio thread must not take a lock, so the
    render-rate site below uses its own atomic instead of calling this. A
    diagnostic that crashes somebody's DAW is worse than no diagnostic. */
static bool p6auFirst (const char* tag, unsigned long id)
{
	static std::mutex lock;
	static std::set<std::pair<std::string, unsigned long>> seen;

	std::lock_guard<std::mutex> held (lock);
	return seen.insert ({std::string (tag), id}).second;
}

'''

INCLUDES = '''// P6AU_TRACE
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace Steinberg {
'''

SETPROP = '''                                        UInt32 inDataSize)
{
	// P6AU_TRACE
	if (p6auFirst ("set", inID))
		p6auTrace ("SetProperty", inID,
		           inID == kAudioUnitProperty_MIDIOutputCallback
		               ? "<-- kAudioUnitProperty_MIDIOutputCallback" : "");

	switch (inID)
'''

GETINFO = '''{
	// P6AU_TRACE
	if (p6auFirst ("info", inID))
		p6auTrace ("GetPropertyInfo", inID,
		           inID == kAudioUnitProperty_MIDIOutputCallbackInfo
		               ? "<-- kAudioUnitProperty_MIDIOutputCallbackInfo"
		               : (inID == kAudioUnitProperty_MIDIOutputCallback
		                      ? "<-- kAudioUnitProperty_MIDIOutputCallback" : ""));

	switch (inID)
	{
		//--- -----------------------
		case kAudioUnitProperty_BypassEffect:
		{
			if (inScope == kAudioUnitScope_Global && bypassParamID != -1)
			{
				outWritable = true;
'''

SETCB = '''{
	// P6AU_TRACE - THE ANSWER. A host that gets here has a callback to give.
	p6auTrace ("setCallbackInfo", 0,
	           callback != nullptr ? "REAL CALLBACK - MIDI out will work"
	                               : "NULL callback handed over");

	mMIDICallbackStruct.midiOutputCallback = callback;
'''

FIRE = '''{
	// P6AU_TRACE - THE OTHER ANSWER, once. Events to send and nowhere to
	// send them is the whole bug, stated by the only line that can see it.
	//
	// THE AUDIO THREAD. Its own atomic rather than p6auFirst, which takes a
	// lock; the exchange means the fprintf happens exactly once however many
	// threads arrive, and every later block costs one relaxed load.
	{
		static std::atomic<bool> said { false };
		if (!mMIDIMessageList.empty () && mMIDICallbackStruct.midiOutputCallback == nullptr
		    && !said.exchange (true))
		{
			p6auTrace ("fireAtTimeStamp", 0,
			           "EVENTS WAITING AND NO CALLBACK - the host never set one");
		}
	}

	if (!mMIDIMessageList.empty () && mMIDICallbackStruct.midiOutputCallback != nullptr)
'''

EDITS = [
    ('namespace Steinberg {\n', INCLUDES, 'includes'),
    ('namespace Vst {\n', HELPER, 'helper'),
    ('                                        UInt32 inDataSize)\n{\n\tswitch (inID)\n',
     SETPROP, 'SetProperty'),
    ('''{
	switch (inID)
	{
		//--- -----------------------
		case kAudioUnitProperty_BypassEffect:
		{
			if (inScope == kAudioUnitScope_Global && bypassParamID != -1)
			{
				outWritable = true;''', GETINFO, 'GetPropertyInfo'),
    ('{\n\tmMIDICallbackStruct.midiOutputCallback = callback;\n', SETCB, 'setCallbackInfo'),
    ('{\n\tif (!mMIDIMessageList.empty () && mMIDICallbackStruct.midiOutputCallback != nullptr)\n',
     FIRE, 'fireAtTimeStamp'),
]


def main():
    revert = '--revert' in sys.argv[1:]

    if not os.path.exists(WRAPPER):
        sys.exit('au-property-trace: no wrapper at %s.\n'
                 'Configure the project once so cmake clones the SDK, then '
                 'run this again.' % WRAPPER)

    with open(WRAPPER, encoding='utf-8') as handle:
        text = handle.read()

    backup = WRAPPER + '.p6-orig'

    if revert:
        if not os.path.exists(backup):
            sys.exit('au-property-trace: nothing to revert - no %s' % backup)
        with open(backup, encoding='utf-8') as handle:
            original = handle.read()
        with open(WRAPPER, 'w', encoding='utf-8') as handle:
            handle.write(original)
        os.remove(backup)
        print('au-property-trace: reverted; the wrapper is Steinberg\'s again')
        return

    if MARK in text:
        print('au-property-trace: already applied - nothing to do')
        return

    # The untouched original, so --revert restores rather than un-edits.
    if not os.path.exists(backup):
        with open(backup, 'w', encoding='utf-8') as handle:
            handle.write(text)

    for old, new, what in EDITS:
        count = text.count(old)
        if count != 1:
            os.remove(backup)
            sys.exit('au-property-trace: the %s anchor matched %d times, not 1.\n'
                     'The SDK moved under this script. Nothing was written; fix '
                     'the anchor rather than editing the wrapper by hand, or the '
                     'next clean clone loses it silently.' % (what, count))
        text = text.replace(old, new)

    with open(WRAPPER, 'w', encoding='utf-8') as handle:
        handle.write(text)

    print('au-property-trace: applied to %s' % WRAPPER)
    print('  now rebuild, then:  touch ~/p6-au-properties.txt')
    print('  run the host, and read ~/p6-au-properties.txt')


if __name__ == '__main__':
    main()
