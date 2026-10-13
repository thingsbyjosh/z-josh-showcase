// msvc_crt_wasm.h -- the handful of MSVC "secure CRT" functions the engine calls, for a toolchain
// that does not ship them.
//
// The engine is built with MSVC for its real target, so a call like gmtime_s() is ordinary code
// there, not a portability mistake. It is a Microsoft extension all the same: no other toolchain is
// obliged to provide it, and Emscripten's musl-derived C library does not. Most of the engine
// already guards its use behind _WIN32 and calls the POSIX _r form instead (career_econ.cpp's
// localtime_s/localtime_r pair is the pattern); this header covers what is left.
//
// Force-included into every translation unit by build_wasm.sh (-include), rather than patched into
// the engine sources, for the same reason the three *_wasm.cpp counterparts exist: the engine's own
// code stays byte-for-byte what compiles for the game, and everything this build has to do
// differently is visible in this directory instead of scattered through it.
#pragma once

#if !defined(_WIN32)
#include <time.h>

// MSVC's argument order, which is what the call sites use: destination first, source second,
// returning 0 on success (an errno_t). NOT C11 Annex K's gmtime_s, which takes the same two
// arguments the other way round and returns a pointer -- passing MSVC-ordered arguments to that one
// compiles on some toolchains and then reads the struct as a time_t, so the distinction matters.
#ifndef gmtime_s
inline int gmtime_s(struct tm* result, const time_t* timer) {
    return (timer && result && ::gmtime_r(timer, result)) ? 0 : -1;
}
#endif

#endif  // !_WIN32
