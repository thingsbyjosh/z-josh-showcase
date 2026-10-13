// test_paths_wasm.cpp -- links ONLY paths_wasm.cpp, never paths.cpp (duplicate symbols).
//
// Exercises every non-inline symbol declared in paths.h at least once, with real assertions
// wherever there is real behaviour to assert (RULING 5, fix round 1: the committed test
// originally called only 6 of the 24 symbols paths_wasm.cpp defines).
//
// Build and run it natively -- it carries its own main() and is never linked into the module:
//
//     g++ -std=c++17 -iquote dll/career -o test_paths_wasm \
//         engine-wasm/test_paths_wasm.cpp engine-wasm/paths_wasm.cpp && ./test_paths_wasm
//
// -iquote for the same reason the module build uses it (see build_wasm.sh): dll/career/paths.h
// shares its name with a real POSIX <paths.h>, and -iquote is searched only for the quoted form
// this file actually writes. Plain g++ is enough -- nothing here needs Emscripten.
//
// THE ROOTS ARE A PRIVATE TEMPORARY DIRECTORY, not the four absolute paths this build uses in the
// browser ("/world", "/save", "/cache", "/log"). Those are correct there -- Emscripten's MEMFS is a
// virtual disk created empty at page load, where "/save" costs nothing and collides with nothing.
// On a real filesystem they are neither: on a POSIX host mkdir("/save") is EACCES for any ordinary
// user (which is why no CI job could run this test while it hard-coded them), and on Windows they
// resolve against the root of the current drive, which is how an earlier native run of this very
// file left an E:\save\ behind on someone's disk.
//
// A temporary directory fixes both at once, and it is the same test either way: paths_wasm.cpp
// takes its roots as strings through SetRootsForTest and has no opinion about what they contain.
// The four compiled-in browser defaults are still asserted, at the very bottom, where checking them
// is pure string comparison and touches no disk at all.
//
// Honest about the limit: a run that aborts on a failed assertion still leaves the directory behind,
// because abort() runs no destructors. What changes is WHERE -- the directory the OS hands out for
// exactly this and reclaims on its own, instead of the root of whatever drive the test ran from.
#include "paths.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

int main() {
    namespace fs = std::filesystem;

    // A private directory under the OS temp area. The suffix loop is how uniqueness is obtained
    // without a PID header that differs per platform: create_directory returns false when the name
    // is already taken, so the first name it actually creates is one no other run owns.
    fs::path baseDir;
    for (int i = 0; i < 1000 && baseDir.empty(); i++) {
        std::error_code ec;
        fs::path candidate = fs::temp_directory_path(ec) / ("paths_wasm_test." + std::to_string(i));
        if (!ec && fs::create_directory(candidate, ec) && !ec) baseDir = candidate;
    }
    assert(!baseDir.empty() && "could not create a private temporary directory");

    // generic_string(), not string(): forward slashes on every host, so the expected values below
    // are the exact strings paths_wasm.cpp's own '/'-joining Resolve() produces.
    const std::string world    = (baseDir / "world").generic_string();
    const std::string save     = (baseDir / "save").generic_string();
    const std::string game     = (baseDir / "game").generic_string();
    const std::string gameuser = (baseDir / "gameuser").generic_string();

    paths::ResetForTest();
    paths::SetRootsForTest(world, save, game, gameuser);

    assert(paths::RootDir(paths::Root::Data) == world);
    assert(paths::RootDir(paths::Root::GameInstall) == game);
    assert(paths::RootDir(paths::Root::GameUserDir) == gameuser);
    // POSIX separators, and no doubled slash when the relative part already leads with one
    assert(paths::Data("career/drivers.json") == world + "/career/drivers.json");
    assert(paths::Data("/career/drivers.json") == world + "/career/drivers.json");
    assert(paths::UserData("x.json") == save + "/x.json");
    assert(paths::GameInstall("a") == game + "/a");
    assert(paths::GameUserDir("b") == gameuser + "/b");

    // EnsureParentDir builds the whole chain, and is idempotent
    paths::EnsureParentDir(save + "/a/b/c.json");
    paths::EnsureParentDir(save + "/a/b/c.json");
    FILE* f = paths::FOpen(save + "/a/b/c.json", "wb");
    assert(f != nullptr);
    fputs("hello", f);   // 5 bytes, checked below via FileSize
    fclose(f);

    // Widen/Narrow round-trip: ASCII and accented UTF-8 ("Usuário")
    assert(paths::Narrow(paths::Widen("plain")) == "plain");
    std::string accented = "Usu\xC3\xA1rio";
    assert(paths::Narrow(paths::Widen(accented)) == accented);

    // PathExists / FileExists / DirExists / FileSize -- found and not-found
    assert(paths::PathExists(save + "/a/b/c.json"));
    assert(paths::FileExists(save + "/a/b/c.json"));
    assert(!paths::DirExists(save + "/a/b/c.json"));
    assert(paths::DirExists(save + "/a/b"));
    assert(!paths::PathExists(save + "/a/b/nope.json"));
    assert(paths::FileSize(save + "/a/b/c.json") == 5);
    assert(paths::FileSize(save + "/a/b/nope.json") == -1);

    // CreateDir: created, and idempotent (already-exists still reports true)
    assert(paths::CreateDir(save + "/a/d"));
    assert(paths::CreateDir(save + "/a/d"));

    // ListDir: an explicit extension glob, a wildcard-everything glob, and a bare directory
    // (no wildcard at all -- must also mean "everything", not "nothing")
    {
        auto entries = paths::ListDir(save + "/a/*");
        assert(entries.size() == 2);   // "b" and "d", both directories
        bool sawB = false, sawD = false;
        for (auto& e : entries) {
            if (e.name == "b") { sawB = true; assert(e.isDir); }
            if (e.name == "d") { sawD = true; assert(e.isDir); }
        }
        assert(sawB && sawD);
    }
    {
        auto entries = paths::ListDir(save + "/a/b/*.json");
        assert(entries.size() == 1);
        assert(entries[0].name == "c.json");
        assert(!entries[0].isDir);
    }
    {
        auto entries = paths::ListDir(save + "/a/b/");   // trailing slash, no wildcard
        assert(entries.size() == 1);
        assert(entries[0].name == "c.json");
    }

    // RenameFile: refuses to overwrite an existing destination, succeeds onto a free one
    FILE* other = paths::FOpen(save + "/a/b/other.json", "wb");
    assert(other != nullptr);
    fclose(other);
    assert(!paths::RenameFile(save + "/a/b/c.json", save + "/a/b/other.json"));
    assert(paths::RenameFile(save + "/a/b/c.json", save + "/a/b/renamed.json"));
    assert(paths::FileExists(save + "/a/b/renamed.json"));
    assert(!paths::PathExists(save + "/a/b/c.json"));

    // ReplaceFileAtomic: DOES overwrite an existing destination, in one call
    assert(paths::ReplaceFileAtomic(save + "/a/b/renamed.json", save + "/a/b/other.json"));
    assert(paths::FileExists(save + "/a/b/other.json"));
    assert(paths::FileSize(save + "/a/b/other.json") == 5);   // the "hello" content survived the replace
    assert(!paths::PathExists(save + "/a/b/renamed.json"));

    // RemoveFile
    assert(paths::RemoveFile(save + "/a/b/other.json"));
    assert(!paths::PathExists(save + "/a/b/other.json"));

    // RemoveDirIfEmpty: refuses a non-empty directory, succeeds once it actually is empty
    assert(!paths::RemoveDirIfEmpty(save + "/a"));    // "b" and "d" still inside
    assert(paths::RemoveDirIfEmpty(save + "/a/b"));   // now empty
    assert(!paths::PathExists(save + "/a/b"));
    assert(paths::RemoveDirIfEmpty(save + "/a/d"));
    assert(paths::RemoveDirIfEmpty(save + "/a"));
    assert(!paths::PathExists(save + "/a"));

    // MigrateLegacyDir: a real rename when the source exists and the destination doesn't,
    // and refusal both when the source is missing and when the destination already exists
    paths::EnsureParentDir(save + "/legacy/file.txt");
    FILE* lf = paths::FOpen(save + "/legacy/file.txt", "wb");
    assert(lf != nullptr);
    fclose(lf);
    assert(paths::MigrateLegacyDir(save + "/legacy", save + "/rebranded"));
    assert(!paths::DirExists(save + "/legacy"));
    assert(paths::FileExists(save + "/rebranded/file.txt"));
    assert(!paths::MigrateLegacyDir(save + "/still-missing", save + "/also-missing"));
    assert(!paths::MigrateLegacyDir(save + "/rebranded", save + "/rebranded"));   // dest already exists
    assert(paths::RemoveFile(save + "/rebranded/file.txt"));
    assert(paths::RemoveDirIfEmpty(save + "/rebranded"));

    // Migration/mojibake no-ops: correct behaviour on a POSIX/UTF-8 virtual filesystem is to do
    // nothing, because the ANSI-vs-UTF-8 twin folder problem cannot exist there.
    assert(paths::MigrateMojibakeTwins() == 0);
    assert(paths::AcpTwinUtf8("anything") == "");
    assert(paths::MigrateLegacyUserRoot() == false);

    // Take the temporary directory back down. RemoveDirIfEmpty (the engine's own call, not the
    // filesystem library's) is what does it, so this doubles as the assertion that the test left
    // nothing behind anywhere under it: both calls REFUSE a directory that still has content, so a
    // stray file created by any step above would fail here rather than be silently deleted.
    assert(paths::RemoveDirIfEmpty(save));
    assert(paths::RemoveDirIfEmpty(baseDir.generic_string()));
    assert(!paths::PathExists(baseDir.generic_string()));

    // ResetForTest + Init(): back to uninitialized, then Init()'s own hardcoded defaults -- the four
    // virtual roots of the browser build, never exercised above because SetRootsForTest overrode
    // them. Pure string comparison; nothing here touches a disk.
    paths::ResetForTest();
    assert(paths::RootDir(paths::Root::Data) == "/world");
    assert(paths::RootDir(paths::Root::UserData) == "/save");
    assert(paths::RootDir(paths::Root::GameInstall) == "/cache");
    assert(paths::RootDir(paths::Root::GameUserDir) == "/log");

    // DefaultUserRoot: fixed browser-build fallback, likewise a compiled-in constant
    assert(paths::DefaultUserRoot() == "/save");

    printf("paths_wasm: ok\n");
    return 0;
}
