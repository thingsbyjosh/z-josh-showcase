#!/usr/bin/env bash
# build_wasm.sh -- compiles the career engine to WebAssembly.
# Run from the repository root. Requires emsdk on PATH (source emsdk_env.sh first).
#
# Usage: bash engine-wasm/build_wasm.sh [OUT_DIR]        (default: app/public/engine)
set -euo pipefail

OUT="${1:-app/public/engine}"
mkdir -p "$OUT"

# Three engine sources are Windows-only and are replaced, one for one, by a counterpart in this
# directory. Each counterpart is a separate translation unit supplying the same symbols -- never an
# #ifdef threaded through the original -- so the engine's own code stays exactly as it compiles for
# the game:
#
#   paths.cpp          -> paths_wasm.cpp         the Win32 UTF-8/UTF-16 filesystem layer; the
#                                                virtual filesystem here is POSIX and UTF-8 already.
#   career_appgate.cpp -> appgate_wasm.cpp       a named kernel event asking "is the app alive?";
#                                                in a browser the page IS the app.
#   save_migrate.cpp   -> save_migrate_wasm.cpp  CopyFileW rescuing saves an older installed build
#                                                left behind; a filesystem built at page load has none.
#
# All three include <windows.h> unguarded, so none of them can compile here whatever flags are
# passed. Every other engine source is either portable or already guards its Win32 use behind
# _WIN32 (career_econ.cpp, career_userlog.cpp), and compiles unchanged.
#
# vr_mirror_wasm.cpp is a fourth counterpart with nothing to exclude: dll/vr_mirror.h declares three
# functions the engine calls whose implementation this repository does not publish, so every build
# has to supply them from somewhere (the test harness supplies its own). A browser tab has no
# headset, so this one answers "no VR installed" -- the same answer an ordinary PC gives.
#
# owned_wasm.cpp is a fifth, of the same kind: career_data.cpp calls four owned-car accessors that
# dll/career/career_ui.h declares and only the game-side host defines. Here they read the save's
# owned-car list through the career-state seat, the way the view providers read everything else.
SRCS=$(find dll/career -name '*.cpp' \
    ! -name 'paths.cpp' \
    ! -name 'career_appgate.cpp' \
    ! -name 'save_migrate.cpp' | sort)

# test_paths_wasm.cpp is deliberately absent from the list below: it carries its own main() and is
# built separately as a native smoke test, not linked into the module.
#
# em++, not emcc. emcc compiles a .cpp as C++ perfectly well, but it LINKS as C: it leaves out the
# C++ runtime, so the link fails on every operator new, every out-of-line std::string member and
# every __cxa_* exception symbol in the engine at once. em++ is the same driver with libc++ linked.
#
# -iquote, NOT -I. Two engine headers share a name with a real POSIX header this toolchain ships:
# dll/career/strings.h and dll/career/paths.h, against <strings.h> and <paths.h>. Windows has
# neither of those system headers, so the clash cannot arise there -- but -I puts dll/career on the
# ANGLE-BRACKET search path too, ahead of the sysroot, and musl's own <string.h> includes
# <strings.h>. The engine's strings.h then gets pulled into the middle of a libc header, inside its
# extern "C" block, and every declaration in it silently acquires C linkage. The result links
# nowhere and says so confusingly: strings.cpp defines the C++-mangled TR/TRHas/SetLang (it includes
# its own header first, at C++ linkage) while every caller that reached <string.h> first calls the C
# ones. -iquote is searched ONLY for quoted includes, which is what the engine writes, so
# <strings.h> resolves to the sysroot's and "strings.h" resolves to the engine's.
#
# -include msvc_crt_wasm.h supplies the MSVC "secure CRT" functions the engine calls that this
# toolchain does not ship (see that header). Force-included rather than patched into the engine
# sources, so the engine's own code stays exactly what compiles for the game.
#
# -D AMS2_PUBLIC_EXPORT_BUILD has ZERO call sites in the sources this build compiles -- checked, not
# assumed. It is the export pipeline's macro: emit.py wraps includes and function bodies it must
# excise from the published tree in `#ifndef AMS2_PUBLIC_EXPORT_BUILD`, and today every such guard it
# writes lands in tools/career_test.cpp, which this build does not compile. It is kept anyway, and
# only for this reason: the day that mechanism wraps something in dll/career/, a build without the
# macro would silently compile the branch meant to be excised. Inert on purpose, not by oversight.
#
# The host's view providers (one file per query.* view, named <view>_view_wasm.cpp, each installed
# from wasm_main.cpp's Core() by an Install<View>View call) are picked up by glob, so adding one never edits this
# script. Sorted, so the link order does not depend on the filesystem.
VIEWS=$(find engine-wasm -maxdepth 1 -name '*_view_wasm.cpp' | sort)

em++ $SRCS \
  engine-wasm/paths_wasm.cpp \
  engine-wasm/appgate_wasm.cpp \
  engine-wasm/save_migrate_wasm.cpp \
  engine-wasm/vr_mirror_wasm.cpp \
  engine-wasm/owned_wasm.cpp \
  $VIEWS \
  engine-wasm/wasm_main.cpp \
  -iquote dll/career \
  -include engine-wasm/msvc_crt_wasm.h \
  -std=c++17 -O2 \
  -D AMS2_PUBLIC_EXPORT_BUILD \
  -s MODULARIZE=1 -s EXPORT_ES6=1 -s EXPORT_NAME=createEngine \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS='["_engine_dispatch","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS"]' \
  --preload-file testdata@/world \
  -o "$OUT/engine.mjs"

ls -la "$OUT"
