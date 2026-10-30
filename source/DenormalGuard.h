//------------------------------------------------------------------------
// Project6 - flush-to-zero for the length of one process() call
//------------------------------------------------------------------------
//
// Taken from SpaceDub. A one-pole smoother aimed at exactly zero - a
// row, pad or trim fader at the bottom of its travel - does not reach
// zero: about 15 s later it sticks at the bottom of the denormal range for
// good, and every multiply by it is slow on x86 (measured: 1 ms of CPU
// per second for eight muted rows, then 30-37 ms for ever after). The
// smoothers now snap to their target (Project6Dsp.cpp, kGainSnap); this
// guard covers every other float and double operation on the audio
// thread.
//
// The FPU mode belongs to the host's thread, so it is saved on entry and
// put back on exit - never left switched on.
//
//   x86_64 : MXCSR FTZ (bit 15) and DAZ (bit 6)
//   arm64  : FPCR FZ (bit 24), which flushes inputs and outputs both
//
// Standard library only, so tests/DspTests.cpp can check it without
// the VST3 SDK.
//------------------------------------------------------------------------

#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#define PROJECT6_FTZ_X86 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define PROJECT6_FTZ_ARM64 1
#endif

namespace Project6 {

class ScopedFlushDenormals
{
public:
	ScopedFlushDenormals ()
	{
#if defined(PROJECT6_FTZ_X86)
		mSaved = _mm_getcsr ();
		_mm_setcsr (static_cast<unsigned> (mSaved) | 0x8040u);
#elif defined(PROJECT6_FTZ_ARM64)
		std::uint64_t fpcr;
		asm volatile ("mrs %0, fpcr" : "=r" (fpcr));
		mSaved = fpcr;
		asm volatile ("msr fpcr, %0" : : "r" (fpcr | (std::uint64_t (1) << 24)));
#endif
	}

	~ScopedFlushDenormals ()
	{
#if defined(PROJECT6_FTZ_X86)
		_mm_setcsr (static_cast<unsigned> (mSaved));
#elif defined(PROJECT6_FTZ_ARM64)
		asm volatile ("msr fpcr, %0" : : "r" (mSaved));
#endif
	}

	/** False on an architecture the guard does not know (it then does
	    nothing, and DelayLine's own flushing is all there is). */
	static constexpr bool supported ()
	{
#if defined(PROJECT6_FTZ_X86) || defined(PROJECT6_FTZ_ARM64)
		return true;
#else
		return false;
#endif
	}

	ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
	ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
	std::uint64_t mSaved = 0;
};

} // namespace Project6
