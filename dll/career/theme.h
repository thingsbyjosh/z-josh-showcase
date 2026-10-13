#pragma once
#include "imgui.h"
#include <cstring>
// theme.h is excluded from the manifest: most of it (Fonts::, ApplyCareerStyle) is legacy-overlay
// rendering code, used only by career_ui.cpp/screen_*.cpp, neither of which is published. Col:: is
// reproduced here in full because five published engine sources call it directly for domain colour
// values; theme.cpp (Col::'s own implementation) is not published either, so its two functions are
// inlined here instead of merely declared.
namespace Col {
  constexpr ImU32 bg       = IM_COL32(0x0b,0x0c,0x10,0xff);
  constexpr ImU32 bg2      = IM_COL32(0x10,0x12,0x18,0xff);
  constexpr ImU32 surface  = IM_COL32(0x16,0x19,0x22,0xff);
  constexpr ImU32 surface2 = IM_COL32(0x1d,0x21,0x2c,0xff);
  constexpr ImU32 surface3 = IM_COL32(0x26,0x2b,0x38,0xff);
  constexpr ImU32 line     = IM_COL32(0x2c,0x31,0x3e,0xff);
  constexpr ImU32 line2    = IM_COL32(0x3a,0x41,0x50,0xff);
  constexpr ImU32 text     = IM_COL32(0xf3,0xf4,0xf7,0xff);
  constexpr ImU32 text2    = IM_COL32(0xaa,0xb2,0xc0,0xff);
  constexpr ImU32 text3    = IM_COL32(0x6b,0x72,0x80,0xff);
  constexpr ImU32 accent   = IM_COL32(0xff,0x4d,0x2e,0xff);
  constexpr ImU32 gold     = IM_COL32(0xff,0xcf,0x4d,0xff);
  constexpr ImU32 green    = IM_COL32(0x19,0xd2,0x7c,0xff);
  constexpr ImU32 danger   = IM_COL32(0xff,0x4d,0x4d,0xff);
  constexpr ImU32 cyan     = IM_COL32(0x5e,0xc8,0xff,0xff);
  constexpr ImU32 amber    = IM_COL32(0xff,0xb8,0x4d,0xff);
  constexpr ImU32 purple   = IM_COL32(0xb9,0x4b,0xff,0xff);

  inline ImU32 Disc(const char* d) {
    if (!d) return text3;
    if (!strcmp(d,"single"))    return cyan;
    if (!strcmp(d,"touring"))   return amber;
    if (!strcmp(d,"gt"))        return accent;
    if (!strcmp(d,"endurance")) return purple;
    if (!strcmp(d,"entry"))     return green;
    if (!strcmp(d,"offroad"))   return IM_COL32(0x9a,0xa0,0xaa,0xff);
    return text3;
  }
  inline ImU32 Alpha(ImU32 c, float a) {
    ImU32 al = (ImU32)(((c >> IM_COL32_A_SHIFT) & 0xff) * a);
    return (c & ~IM_COL32_A_MASK) | (al << IM_COL32_A_SHIFT);
  }
}
