#pragma once
// / — wheel button, button box or gamepad as confirmation of the injected VR panel.
//
// The app stores it in config.json ("vr_confirm_btn") with the SAME text the SteamVR host reads (Rust,
// app/src-tauri/src/vr/padbutton.rs) and that the capture generates (app/src/ui/vrConfirm.ts):
//   · "joy:<vid>:<pid>:<n>" -> DirectInput device (wheel, button box) by USB vendor/product,
//     button n from 0. Read with joyGetPosEx (winmm, 32 buttons), without acquiring the device: the game
//     still holds it for force feedback and we only watch.
//   · "xinput:<n>"          -> Xbox-compatible gamepad, n from the Gamepad API's STANDARD mapping (0 = A).
//
// Pure (no windows.h) so career_test can test it without dragging in the overlay.
#include <cstdint>
#include <string>

namespace vrconfirm {

enum class Kind { None, Joy, XInput };

struct ButtonSpec {
    Kind     kind = Kind::None;
    uint16_t vid = 0;
    uint16_t pid = 0;
    int      button = -1;
};

const int kJoyMaxButtons = 32;      // joyGetPosEx: 32-bit mask
const int kXInputButtons = 16;      // standard mapping 0..15 (16, the center button, isn't given by XInput)
const unsigned char kTriggerThreshold = 30;   // XINPUT_GAMEPAD_TRIGGER_THRESHOLD

namespace detail {
inline bool Hex4(const std::string& s, uint16_t* out) {
    if (s.size() != 4) return false;
    unsigned v = 0;
    for (char c : s) {
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = v * 16 + (unsigned)d;
    }
    *out = (uint16_t)v;
    return true;
}
inline bool Index(const std::string& s, int max, int* out) {
    if (s.empty() || s.size() > 2) return false;
    int v = 0;
    for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + (c - '0'); }
    if (v >= max) return false;
    *out = v;
    return true;
}
}  // namespace detail

// Anything not understood leaves `out` at Kind::None and returns false: the panel keeps the key.
inline bool Parse(const std::string& spec, ButtonSpec* out) {
    *out = ButtonSpec{};
    std::string parts[5];
    int n = 0;
    size_t start = 0;
    for (;;) {
        size_t colon = spec.find(':', start);
        if (n >= 5) return false;
        parts[n++] = spec.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    ButtonSpec b;
    if (n == 4 && parts[0] == "joy") {
        if (!detail::Hex4(parts[1], &b.vid) || !detail::Hex4(parts[2], &b.pid) ||
            !detail::Index(parts[3], kJoyMaxButtons, &b.button)) return false;
        b.kind = Kind::Joy;
    } else if (n == 2 && parts[0] == "xinput") {
        if (!detail::Index(parts[1], kXInputButtons, &b.button)) return false;
        b.kind = Kind::XInput;
    } else {
        return false;
    }
    *out = b;
    return true;
}

// Where each button of the standard mapping comes from in XINPUT_GAMEPAD: wButtons mask, or trigger (1 left, 2 right).
struct XInputSource { uint16_t mask = 0; int trigger = 0; };

inline bool XInputSourceFor(int index, XInputSource* out) {
    static const uint16_t kMasks[kXInputButtons] = {
        0x1000, 0x2000, 0x4000, 0x8000,   // A B X Y
        0x0100, 0x0200, 0, 0,             // LB RB (LT RT are triggers)
        0x0020, 0x0010,                   // View(Back) Menu(Start)
        0x0040, 0x0080,                   // LS RS
        0x0001, 0x0002, 0x0004, 0x0008,   // d-pad up down left right
    };
    if (index < 0 || index >= kXInputButtons) return false;
    XInputSource s;
    if (index == 6) s.trigger = 1;
    else if (index == 7) s.trigger = 2;
    else s.mask = kMasks[index];
    *out = s;
    return true;
}

}  // namespace vrconfirm
