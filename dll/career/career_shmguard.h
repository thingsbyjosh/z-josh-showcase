#pragma once

namespace career {

// SEQUENCE GUARD for the $pcars2$ shared memory -- the piece that was losing entire races.
//
// The Project CARS 2 SDK says: `mSequenceNumber` is ODD while the game is writing the block,
// and EVEN at rest; a reader skips the frame if it catches it odd. That's always been our rule, and it's
// correct... as long as rest is even.
//
// **It isn't always.** AMS2 RESETS the counter on every session change (a player's log, 2026-08-06:
// qualifying ends with seq=31814 and 2s later the race starts with seq=110; in another, 60278 -> 24). The
// startup value after that reset is effectively a coin flip, and when it lands ODD, rest
// stays odd for the rest of the session: the SDK's rule vetoes 100% of reads, and it does it
// SILENTLY. In that log it's six and a half minutes of racing without a single read -- no signature, no grid,
// no settlement -- with the player winning the race and the whole thing lost. Their two previous races,
// identical, landed even and went fine. That's exactly the "happens to me now and then" they reported.
//
// The REAL anti-tearing guarantee isn't parity, it's STABILITY: if the counter holds the same value before and
// after reading, nobody wrote underneath it. Parity is only a cheap shortcut to avoid reading
// knowingly while a write is in flight. So parity stops being a permanent veto and becomes
// a veto WITH A DEADLINE: a real write takes microseconds, so if the counter has been odd
// for longer than kOddParkMs, that block has inverted rest, and from then on it reads validating only
// by stability. Seeing an even value again (new session, normal parity) rearms the usual rule.
struct ShmSeqGate {
    unsigned long oddSinceMs = 0;   // tick of the FIRST odd observation in the streak (0 = no streak)
    bool          parked     = false;   // odd streak already declared "inverted rest" -> read anyway

    // Can a read be attempted with the counter at `seq`? `nowMs` = monotonic clock in ms.
    // Even -> yes (and cuts any odd streak). Odd -> no, until the streak exceeds kOddParkMs.
    bool Allow(unsigned seq, unsigned long nowMs);

    // Was inverted rest just declared on this call? (to log it ONCE, not every frame)
    bool JustParked() const { return justParked; }

    bool justParked = false;
};

// Deadline for the odd veto. A write in flight resolves in microseconds; 750ms is three orders
// of magnitude of margin and is still invisible to the player (a settle sampling is ~2s).
const unsigned long kOddParkMs = 750;

} // namespace career
