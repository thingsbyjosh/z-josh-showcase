#pragma once
// The pure domain layer needs exactly two things from Dear ImGui: ImU32, a packed RGBA colour
// value, and the macro that packs one. This export does not publish the ImGui library, so a
// quoted #include "imgui.h" from any file in this directory resolves here instead, ahead of
// any search path.
using ImU32 = unsigned int;
#define IM_COL32_R_SHIFT    0
#define IM_COL32_G_SHIFT    8
#define IM_COL32_B_SHIFT    16
#define IM_COL32_A_SHIFT    24
#define IM_COL32_A_MASK     0xFF000000
#define IM_COL32(R,G,B,A) \
    (((ImU32)(A) << IM_COL32_A_SHIFT) | ((ImU32)(B) << IM_COL32_B_SHIFT) | \
     ((ImU32)(G) << IM_COL32_G_SHIFT) | ((ImU32)(R) << IM_COL32_R_SHIFT))
