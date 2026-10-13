// vr_mirror_wasm.cpp -- the VR mirror's three symbols, for the WASM/browser build.
//
// dll/vr_mirror.h declares three functions the engine calls but whose implementation this
// repository does not publish: mirroring the application inside a VR headset needs hardware and a
// runtime that are not part of this showcase. Every build has to supply them from somewhere -- the
// test harness already does the same for its own link.
//
// Here the honest answer is not "not published" but "not there": a browser tab has no headset and
// no companion VR mirror to notify, so IsInstalled() is false and there is nothing for the other
// two to do. The engine already handles a machine with no VR installed, which is exactly the shape
// this environment has -- so this is the same answer the Windows build gives on an ordinary PC,
// not a special case invented for the browser.
//
// Relative, unlike every other include in this directory, which resolves through the build's
// -iquote dll/career. vr_mirror.h lives one level above that, in dll/ -- and the engine's own caller
// reaches it the same relative way (career_api_queries.cpp writes "../vr_mirror.h"), so this mirrors
// the engine rather than inventing a second include root. Adding one would mean teaching the
// export's dependency-closure check about it too, for a header exactly one file needs.
#include "../dll/vr_mirror.h"

namespace vrmirror {

// Is a VR mirror installed? No. There is no headset behind a browser tab, and nothing this module
// could install one from.
bool IsInstalled() { return false; }

// The engine calls this so the VR panel gets out of the way when a race starts. With no panel in
// front of anything, there is nothing to move.
void NotifyRaceLaunched() {}

// Likewise: no panel, so no mode to put it in.
void SetPanelMode(int /*mode*/, bool /*forceOnTrack*/) {}

}  // namespace vrmirror
