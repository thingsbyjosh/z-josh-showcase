#pragma once
// 23/09 — THE ONLY PLACE where the whitespace of a NAME (person or team) is cleaned on load.
//
// The data had «Nelson Piquet Jr.» with a NO-BREAK SPACE (U+00A0, bytes C2 A0) between «Piquet» and «Jr.»:
// on screen it's identical to a space, but to the code it's a different string. The folding (`FoldDriverName`)
// already ignored them, but everything that compares or hashes the RAW name does not: the id minted from the name, the
// keys (class, label) of `pack_team_class.psv` against `livery_team_equiv_packs.psv`… Measured on 23/09:
// «Cavaleiro Sports - N.Piquet⍽Jr.» didn't find its curated team because the other table writes it with a
// normal space.
//
// The rule: every loader that reads a person's or team's name passes it through here BEFORE storing it.
// LIVERY names don't: those have to match byte for byte what the game or the pack writes.
//
// What it does: Unicode spaces (U+00A0, U+1680, U+2000–U+200A, U+202F, U+205F, U+3000) become a
// normal space and the invisible zero-width ones (U+200B, U+FEFF) disappear. The rest of the UTF-8 (accents,
// Cyrillic…) is left as is, and spaces aren't trimmed or merged: only what you can't see is changed.
#include <string>

namespace career {

inline std::string CleanNameSpaces(const std::string& s) {
    // Fast path: almost all names are ASCII or carry none of these prefixes.
    bool maybe = false;
    for (unsigned char c : s) if (c == 0xC2 || c == 0xE1 || c == 0xE2 || c == 0xE3 || c == 0xEF) { maybe = true; break; }
    if (!maybe) return s;
    std::string o; o.reserve(s.size());
    const size_t n = s.size();
    for (size_t i = 0; i < n; ) {
        const unsigned char b0 = (unsigned char)s[i];
        const unsigned char b1 = i + 1 < n ? (unsigned char)s[i + 1] : 0;
        const unsigned char b2 = i + 2 < n ? (unsigned char)s[i + 2] : 0;
        if (b0 == 0xC2 && b1 == 0xA0) { o += ' '; i += 2; continue; }                       // U+00A0
        if (b0 == 0xE1 && b1 == 0x9A && b2 == 0x80) { o += ' '; i += 3; continue; }         // U+1680
        if (b0 == 0xE2 && b1 == 0x80) {
            if (b2 >= 0x80 && b2 <= 0x8A) { o += ' '; i += 3; continue; }                   // U+2000..U+200A
            if (b2 == 0xAF)               { o += ' '; i += 3; continue; }                   // U+202F
            if (b2 == 0x8B)               {           i += 3; continue; }                   // U+200B (zero width)
        }
        if (b0 == 0xE2 && b1 == 0x81 && b2 == 0x9F) { o += ' '; i += 3; continue; }         // U+205F
        if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x80) { o += ' '; i += 3; continue; }         // U+3000
        if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {           i += 3; continue; }         // U+FEFF
        o += (char)b0; i++;
    }
    return o;
}

}  // namespace career
