#pragma once
// VR support is not part of this showcase (see the licence and README): the real vr_mirror.h
// mirrors the app inside a VR headset, which needs hardware and a runtime this repository does not
// publish. This stub declares only the three symbols the published engine actually calls
// (dll/career/career_api_queries.cpp); the definitions live where the real header's do not ship,
// same as any other header/source split -- here, tools/career_test.cpp already provides them.
namespace vrmirror {
    void NotifyRaceLaunched();
    bool IsInstalled();
    void SetPanelMode(int mode, bool forceOnTrack);
}
