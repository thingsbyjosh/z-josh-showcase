// save_migrate_wasm.cpp -- legacy-save migration for the WASM/browser build.
//
// save_migrate.cpp (dll/career/save_migrate.cpp) exists for one historical situation: an installed
// Windows build whose saves were written, by an older version, into a flat folder outside the
// userdata tree. It copies those files across once, never overwriting a newer destination.
//
// That situation cannot arise here. The virtual filesystem this build runs on is created fresh
// when the page loads -- the world is mounted read-only from the packaged data file, and the
// writable roots start empty. No older version of anything ever wrote into it, so there is no
// legacy folder to read and nothing has ever been left behind to rescue. The honest answer is
// "nothing to migrate", and returning zero says exactly that in the units the contract already
// uses: the number of files copied.
//
// Porting the copy loop itself would not make this more faithful, only longer: it would walk a
// directory that is guaranteed absent, on every boot, forever. What IS worth keeping is the
// contract -- idempotent, safe to call on every boot, never overwrites -- which zero satisfies
// trivially.
//
// Separate translation unit supplying the same symbol, never an #ifdef threaded through the
// original; build_wasm.sh excludes save_migrate.cpp and compiles this instead. (The Windows
// original's only unportable dependency was CopyFileW, from <windows.h>.)
#include "save_migrate.h"

namespace career {

int MigrateLegacySavesOnce(const char* /*legacyDir*/) { return 0; }

}  // namespace career
