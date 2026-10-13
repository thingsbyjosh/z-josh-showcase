#include "career_shmguard.h"

namespace career {

bool ShmSeqGate::Allow(unsigned seq, unsigned long nowMs) {
    justParked = false;
    if ((seq & 1u) == 0u) {          // normal parity: even at rest -> read allowed, streak cut
        oddSinceMs = 0;
        parked = false;
        return true;
    }
    if (parked) return true;         // inverted rest already declared: it reads and stability decides
    if (oddSinceMs == 0) {
        oddSinceMs = nowMs ? nowMs : 1;   // 0 is the "no streak" sentinel: never stored as a tick
        return false;                     // first odd observation: a write in flight, plausible
    }
    // Unsigned subtraction: survives GetTickCount wraparound (49.7 days) without producing a negative deadline.
    if ((unsigned long)(nowMs - oddSinceMs) <= kOddParkMs) return false;
    parked = true;
    justParked = true;
    return true;
}

} // namespace career
