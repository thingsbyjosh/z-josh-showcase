#include "career_aiverify.h"
#include "career_results.h"   // IsRealDriver (Safety Car / Pace Car / unnamed slot)
#include "paths.h"            // ListDir / FOpen (UTF-8 -> wide; mojibake fix, Jul 22)
#include <cctype>
#include <cstdio>
#include <set>
#include <string>

namespace career {

const int    kAiVerifyMinCounted = 4;
const double kAiVerifyMinOursPct = 0.50;

namespace {
// Comparison key: trims whitespace and lowercases byte by byte. It does NOT touch accents (the
// names travel as UTF-8 and the XML writes them exactly as shared memory returns them), so two bytes that are
// equal stay equal, and two that differ stay different.
std::string Key(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    std::string k;
    k.reserve(b - a);
    for (size_t i = a; i < b; i++) {
        unsigned char c = (unsigned char)s[i];
        k += (c < 0x80) ? (char)tolower(c) : (char)c;   // ASCII to lowercase; multibyte left intact
    }
    return k;
}

std::set<std::string>& Known() {
    static std::set<std::string> g;   // union of ALL classes: a multiclass race mixes several XML files
    return g;
}
std::set<std::string>& ScannedDirs() {
    static std::set<std::string> g;   // dirs already re-read from disk (AiVerifyLoadFromDisk is idempotent)
    return g;
}

// Extracts the <name>...</name> tags from one of our XML files (BuildGridXml emits one per line inside
// <driver livery_name="...">). Deliberately a minimal parser: it only looks for the tag, it doesn't validate the XML.
int HarvestNames(const std::string& xml, std::set<std::string>& into) {
    const char* kOpen = "<name>";
    const char* kClose = "</name>";
    const size_t nOpen = 6, nClose = 7;
    int added = 0;
    size_t p = 0;
    while ((p = xml.find(kOpen, p)) != std::string::npos) {
        size_t s = p + nOpen;
        size_t e = xml.find(kClose, s);
        if (e == std::string::npos) break;
        std::string k = Key(xml.substr(s, e - s));
        if (!k.empty() && into.insert(k).second) added++;
        p = e + nClose;
    }
    return added;
}
}  // anon

void AiVerifyRegisterNames(const std::string& className, const std::vector<std::string>& driverNames) {
    (void)className;   // the union is enough: in multiclass, several XML files coexist on the SAME grid
    for (const auto& n : driverNames) {
        std::string k = Key(n);
        if (!k.empty()) Known().insert(k);
    }
}

int AiVerifyLoadFromDisk(const std::string& customAiDir) {
    if (customAiDir.empty()) return 0;
    if (!ScannedDirs().insert(customAiDir).second) return 0;   // already re-read in this process
    int added = 0;
    for (const auto& e : paths::ListDir(customAiDir + "\\*.xml")) {
        if (e.isDir) continue;
        std::FILE* f = paths::FOpen(customAiDir + "\\" + e.name, "rb");
        if (!f) continue;
        std::string xml;
        char buf[8192];
        size_t r;
        while ((r = fread(buf, 1, sizeof(buf), f)) > 0) xml.append(buf, r);
        fclose(f);
        added += HarvestNames(xml, Known());
    }
    return added;
}

AiOverrideCheck AiVerifyCheckGrid(const std::vector<std::string>& trackNames,
                                  const std::string& playerName) {
    AiOverrideCheck r;
    if (Known().empty()) return r;                   // no reference -> we don't weigh in
    const std::string plKey = Key(playerName);
    for (const auto& raw : trackNames) {
        if (!IsRealDriver(raw)) continue;            // Safety Car / Pace Car / empty slot
        std::string k = Key(raw);
        if (k.empty()) continue;
        if (!plKey.empty() && k == plKey) continue;  // the player isn't a driver we wrote
        r.counted++;
        if (Known().count(k)) { r.ours++; continue; }
        r.foreign++;
        if ((int)r.foreignSample.size() < 4) r.foreignSample.push_back(raw);
    }
    if (r.counted < kAiVerifyMinCounted) return r;   // grid too short for an honest ratio
    r.evaluated = true;
    r.oursPct = (double)r.ours / (double)r.counted;
    r.broken  = (r.oursPct < kAiVerifyMinOursPct);
    return r;
}

size_t AiVerifyKnownCount() { return Known().size(); }

void AiVerifyResetForTest() { Known().clear(); ScannedDirs().clear(); }

}  // namespace career
