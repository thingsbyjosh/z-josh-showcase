#include "career_customai.h"
#include "career_balance.h"   // GRIDXML_SKILL_FLOOR/MID (the race_skill band at write time)
#include "career_gridcurve.h"   // Floor/shape of the band derived from the target spread
#include "career_traitband.h"   // the human band — translates each driver trait on writing it
#include "career_series.h"    // VehicleInstalled (filters out cars not installed in VehiclesForClass)
#include "career_userlog.h"   // of the portal: warning when a race's car rule does not apply
#include "paths.h"            // SlugForVehicleEnsured: the catalog path (paths::Data) for the guarded load
#include "career_text.h"      // 23/09: CleanNameSpaces — the rival's name, without hard spaces
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <functional>
#include <mutex>
#include <set>
#include <mutex>
#include <set>

namespace career {

// fwd (career_fields.h): including it here would be circular — career_fields.h includes career_customai.h.
void ResetGridSizeCache();

// PSV parser ----------
static std::vector<std::string> SplitPipe(const char* line) {
    std::vector<std::string> out; std::string cur;
    for (const char* p = line; *p; ++p) {
        char c = *p;
        if (c == '\r' || c == '\n') continue;
        if (c == '|') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

static std::map<std::string, std::vector<std::string>> g_classLiveries;   // className -> livery_name[]
static std::map<std::string, std::string>              g_classXmlStem;    // className -> xml_name
static std::map<std::string, std::string>              g_displayToClass;  // display (verbatim) -> class_name
static std::map<std::string, std::string>              g_normToClass;     // normalized(class_name | display) -> class_name
static bool g_liveriesLoaded = false;

// normalized key: lowercase + [a-z0-9] only (absorbs spaces and "Gen 1" vs "Gen1").
static std::string NormKey(const std::string& s) {
    std::string o;
    for (char c : s) { unsigned char u = (unsigned char)c; if (isalnum(u)) o += (char)tolower(u); }
    return o;
}

bool LoadLiveries(const char* path) {
    ResetGridSizeCache();   // the livery catalog bounds the grid size
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_classLiveries.clear(); g_classXmlStem.clear();
    g_displayToClass.clear(); g_normToClass.clear();
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;                       // comment
        auto t = SplitPipe(line);
        if (t.size() < 3) continue;
        if (t[0] == "class_name") continue;                 // header
        const std::string& cls = t[0];
        if (cls.empty()) continue;
        g_classXmlStem[cls] = t[1];
        g_classLiveries[cls].push_back(t[2]);
        g_normToClass[NormKey(cls)] = cls;                  // class_name -> itself (normalized)
        if (t.size() >= 5 && !t[4].empty()) {               // display (translated_name)
            g_displayToClass[t[4]] = cls;
            g_normToClass[NormKey(t[4])] = cls;
        }
    }
    fclose(f);
    g_liveriesLoaded = true;
    return true;
}

std::string ResolveClass(const std::string& key) {
    if (key.empty()) return std::string();
    if (g_classXmlStem.count(key)) return key;              // already a catalog class_name
    auto d = g_displayToClass.find(key);
    if (d != g_displayToClass.end()) return d->second;      // exact display ("Kingsmere Uma713")
    auto n = g_normToClass.find(NormKey(key));
    if (n != g_normToClass.end()) return n->second;         // normalized (class_name or display)
    return std::string();
}

// Aero-kit variants — see career_customai.h and docs/ams2_class_variants.psv.
static std::map<std::string, std::vector<std::string>> g_classVariants;   // base_stem -> variant_stems

bool LoadClassVariants(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_classVariants.clear();
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 2 || t[0].empty() || t[1].empty()) continue;
        if (t[0] == "base_stem") continue;                  // header
        g_classVariants[t[0]].push_back(t[1]);
    }
    fclose(f);
    return true;
}
int ClassVariantCount() {
    int n = 0;
    for (const auto& kv : g_classVariants) n += (int)kv.second.size();
    return n;
}
const std::vector<std::string>& VariantStemsFor(const std::string& baseStem) {
    static const std::vector<std::string> kEmpty;
    auto it = g_classVariants.find(baseStem);
    return (it == g_classVariants.end()) ? kEmpty : it->second;
}

// vehHash -> class_name (ams2_vehclass.psv) ----------
static std::map<int, std::string> g_vehToClass;
static std::map<std::string, std::vector<VehicleEntry>> g_classVehicles;   // className -> car models
// car name <-> vehHash. DLC detection by CLASS lies for classes shared across
// multiple packs (Hypercars is touched by Coldstead Motors Pt1/Pt2 and Highbourne Motors Pt1/Pt2; F-Classic is touched by Brazilian
// Racing Legends on top of base cars), so dlc_products.psv can declare content CAR BY CAR and
// needs to resolve the catalog name to its hash.
static std::map<std::string, int> g_vehNameToHash;   // normalized name -> vehHash
static std::map<int, std::string> g_vehHashToName;

static std::string NormVehName(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;   // the catalog's spacing isn't data
        o += (char)tolower((unsigned char)c);
    }
    return o;
}

bool LoadVehClass(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_vehToClass.clear();
    g_classVehicles.clear();
    g_vehNameToHash.clear();
    g_vehHashToName.clear();
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 2) continue;
        if (t[0] == "veh_hash" || t[0].empty()) continue;   // header / empty
        int vh = (int)strtol(t[0].c_str(), nullptr, 10);
        g_vehToClass[vh] = t[1];
        VehicleEntry e; e.vehHash = vh; e.name = (t.size() > 2) ? t[2] : t[1];
        g_classVehicles[t[1]].push_back(e);
        if (t.size() > 2 && !t[2].empty()) { g_vehNameToHash[NormVehName(t[2])] = vh; g_vehHashToName[vh] = t[2]; }
    }
    fclose(f);
    return true;
}

int VehClassCount() {
    return (int)g_vehToClass.size();
}

void ResetVehClassForTest() {
    g_vehToClass.clear();
    g_classVehicles.clear();
    g_vehNameToHash.clear();
    g_vehHashToName.clear();
}

int VehicleHashByName(const std::string& name) {
    auto it = g_vehNameToHash.find(NormVehName(name));
    return it == g_vehNameToHash.end() ? 0 : it->second;
}
std::string VehicleNameForHash(int vehHash) {
    auto it = g_vehHashToName.find(vehHash);
    return it == g_vehHashToName.end() ? std::string() : it->second;
}

std::string ClassForVehicle(int vehHash) {
    auto it = g_vehToClass.find(vehHash);
    return it != g_vehToClass.end() ? it->second : std::string();
}

// vehHash -> image slug (ams2_vehicle_catalog.csv, comma-separated) ----------
static std::map<int, std::string> g_vehToSlug;

static std::vector<std::string> SplitComma(const char* line) {
    std::vector<std::string> out; std::string cur;
    for (const char* p = line; *p; ++p) {
        char c = *p;
        if (c == '\r' || c == '\n') continue;
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool LoadVehicleCatalog(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_vehToSlug.clear();
    char line[512]; bool first = true;
    while (fgets(line, sizeof(line), f)) {
        const char* p = line;
        if (first) { first = false; if ((unsigned char)p[0] == 0xEF) p += 3; }   // BOM UTF-8
        if (p[0] == '#') continue;
        auto t = SplitComma(p);
        if (t.size() < 2) continue;
        if (t[0] == "slug" || t[0].empty()) continue;       // header / empty
        int vh = (int)strtol(t[1].c_str(), nullptr, 10);
        if (!vh) continue;
        // tester FIRST row wins. The CSV ends in a skins-world backfill (5e12c4fa:
        // rothbury_c8r/c5r/z06_gt3r, castellane_bt46, ashmere_motors_garrowby_orn468_1965b) that reuses 5 vehHash values under the
        // livery-pack slugs -- slugs with no folder in vehimages. With last-wins, they clobbered the real slug from the extraction
        // (which runs first), and those 5 cars showed up with no photo in the app (a tester spotted the Z06 GT3.R). The backfill
        // stays in the CSV for the skins pipeline (slug->hash), which reads it on its own.
        if (!g_vehToSlug.count(vh)) g_vehToSlug[vh] = t[0];  // lowercase slug, taken as-is
    }
    fclose(f);
    return true;
}

std::string SlugForVehicle(int vehHash) {
    auto it = g_vehToSlug.find(vehHash);
    return it != g_vehToSlug.end() ? it->second : std::string();
}

// Same as SlugForVehicle but guarantees the catalog is loaded (1 attempt, from paths::Data). Used by the
// query wrappers (prerace/garage) that run outside the overlay's image path -> the catalog might
// not have loaded yet. With no CSV -> "" (cars show with no photo, a clean degradation in the app).
std::string SlugForVehicleEnsured(int vehHash) {
    static bool tried = false;
    if (g_vehToSlug.empty() && !tried) { tried = true; LoadVehicleCatalog(paths::Data("ams2_vehicle_catalog.csv").c_str()); }
    return SlugForVehicle(vehHash);
}

int VehicleCatalogCount() { return (int)g_vehToSlug.size(); }

// track_hash -> track image slug (ams2_track_images.psv, pipe-delimited) ----------
// track_hash (signed int) -> the map's .dds slug. One PSV row per circuit, deduped by hash
// at file-generation time (override > auto). Same pattern as the vehicle catalog.
static std::map<int, std::string> g_trackToSlug;

bool LoadTrackImages(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_trackToSlug.clear();
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;                       // comment
        auto t = SplitPipe(line);
        if (t.size() < 2) continue;
        if (t[0] == "track_hash" || t[0].empty()) continue;  // header / empty
        int th = (int)strtol(t[0].c_str(), nullptr, 10);
        if (t[1].empty()) continue;
        g_trackToSlug[th] = t[1];                            // slug taken as-is
    }
    fclose(f);
    return true;
}

std::string SlugForTrack(int trackHash) {
    auto it = g_trackToSlug.find(trackHash);
    return it != g_trackToSlug.end() ? it->second : std::string();
}

// SlugForTrack + a guarded load of the PSV from paths::Data (1 attempt). Used by the query handlers
// (calendar/memories) that serialize trackSlug without going through the overlay's image path -> the PSV
// might not have loaded yet. With no PSV -> "" (missing map, a clean degradation in the app).
std::string SlugForTrackEnsured(int trackHash) {
    static bool tried = false;
    if (g_trackToSlug.empty() && !tried) { tried = true; LoadTrackImages(paths::Data("ams2_track_images.psv").c_str()); }
    return SlugForTrack(trackHash);
}

int TrackImageCount() { return (int)g_trackToSlug.size(); }

// reverse of the curated catalog (linear scan: <300 circuits, called as soon as the engine loads).
int TrackHashForSlug(const std::string& slug) {
    for (const auto& kv : g_trackToSlug)
        if (kv.second == slug) return kv.first;
    return 0;
}

// LIVE catalog of INSTALLED circuits (ams2_track_catalog_live.psv: hash|slug|country|dlc) ----------
// As of 2026-07-16, the curated image map (track_images.psv, ~157 entries) only covers circuits with a matched trackmap,
// but an EVENT can run on ANY installed circuit (~230). This is the live dump of `trackcat` (the
// source of truth for what's installed). The hashes are the SAME namespace as the image map (same hash per
// circuit; only the slug labels differ), so the dump's hash applies directly. Guarded load
// from paths::Data, same as the images. Don't gate on PNG presence: the hero map is best-effort.
static std::map<int, std::string> g_liveTrackToSlug;

bool LoadTrackCatalogLive(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_liveTrackToSlug.clear();
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;                       // comment / header
        auto t = SplitPipe(line);
        if (t.size() < 2 || t[0].empty() || t[1].empty()) continue;
        int th = (int)strtol(t[0].c_str(), nullptr, 10);
        if (th == 0) continue;
        g_liveTrackToSlug[th] = t[1];
    }
    fclose(f);
    return true;
}

// slug (of an INSTALLED circuit) -> the applicable track_hash. 0 if not found. Guarded load, 1 attempt.
int TrackHashForSlugLive(const std::string& slug) {
    static bool tried = false;
    if (g_liveTrackToSlug.empty() && !tried) { tried = true; LoadTrackCatalogLive(paths::Data("ams2_track_catalog_live.psv").c_str()); }
    for (const auto& kv : g_liveTrackToSlug)
        if (kv.second == slug) return kv.first;
    return 0;
}

// slots per circuit (ams2_track_slots.psv: hash|slug|grid_slots|pit_boxes) ----------
// 16/09: the data the engine was missing to be able to say "those that fit on this track" (contract and
// provenance of the numbers in career_customai.h). The two RAW numbers are stored and the min() is done when
// asking, not when loading: that way the file remains evidence and not an already-drawn conclusion.
static std::map<int, std::pair<int, int> > g_trackSlots;   // hash -> (grid slots, garage slots)

bool LoadTrackSlots(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_trackSlots.clear();
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;                       // comment / header
        auto t = SplitPipe(line);
        if (t.size() < 4) continue;
        const int th   = (int)strtol(t[0].c_str(), nullptr, 10);
        const int grid = (int)strtol(t[2].c_str(), nullptr, 10);
        const int pits = (int)strtol(t[3].c_str(), nullptr, 10);
        // a half row is NOT a circuit of capacity 0: it is incomplete data. Storing it would make "the
        // track maximum" come out empty at that circuit instead of falling to the usual path.
        if (th == 0 || grid <= 0 || pits <= 0) continue;
        g_trackSlots[th] = std::make_pair(grid, pits);
    }
    fclose(f);
    return true;
}

// Cars that fit = min(grid slots, garage slots). 0 = circuit with no known slots. Guard-load 1 attempt.
int TrackGridCapacity(int trackHash) {
    static bool tried = false;
    if (g_trackSlots.empty() && !tried) { tried = true; LoadTrackSlots(paths::Data("ams2_track_slots.psv").c_str()); }
    auto it = g_trackSlots.find(trackHash);
    if (it == g_trackSlots.end()) return 0;
    return it->second.first < it->second.second ? it->second.first : it->second.second;
}

int TrackSlotsCount() { return (int)g_trackSlots.size(); }

// class_name -> runtime class hash (ams2_class_hash.psv) ----------
static std::map<std::string, unsigned> g_classHash;       // exact class_name -> hash (value JSON)
static std::map<std::string, unsigned> g_classHashNorm;   // normalized -> hash (fallback)

bool LoadClassHash(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_classHash.clear(); g_classHashNorm.clear();
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 2) continue;
        if (t[0] == "class_name" || t[0].empty()) continue;   // header / empty
        unsigned h = (unsigned)strtoul(t[1].c_str(), nullptr, 10);
        if (!h) continue;
        g_classHash[t[0]] = h;
        g_classHashNorm[NormKey(t[0])] = h;                   // absorbs spaces / "Gen 1" / case
    }
    fclose(f);
    return true;
}

unsigned ClassHashForName(const std::string& className) {
    auto it = g_classHash.find(className);
    if (it != g_classHash.end()) return it->second;
    auto n = g_classHashNorm.find(NormKey(className));        // normalized fallback
    return n != g_classHashNorm.end() ? n->second : 0u;
}

int ClassHashCount() { return (int)g_classHash.size(); }   // number of rows loaded (diag/test); see header

std::vector<VehicleEntry> VehiclesForClass(const std::string& className) {
    auto it = g_classVehicles.find(className);
    if (it == g_classVehicles.end()) return {};
    // filters out cars that are NOT installed (ams2_vehclass.psv is static and includes DLC the user
    // might not own; a vehHash missing from the game would break SetCar/the grid). FAIL-OPEN: with no
    // live catalog populated, VehicleInstalled returns true and the list stays intact. If the filter would empty the class
    // entirely, the unfiltered list is returned instead (better possibly-missing content than a dead class).
    std::vector<VehicleEntry> out;
    out.reserve(it->second.size());
    for (const auto& v : it->second)
        if (VehicleInstalled(v.vehHash)) out.push_back(v);
    return out.empty() ? it->second : out;
}

// Task 7 fix round 2: CarsByClass calls VehiclesForRace on EVERY pre-race query (the app polls it), and its
// two warnings came out on each one. One warning per (class, rule) and process is enough for support, like the s_logged of
// CalUiOfferSeat. With a lock: the pre-race is queried from the API thread and from the UI.
static bool FirstCarRuleLog(const std::string& className, int ruleVehHash) {
    static std::mutex s_mx;
    static std::set<std::string> s_logged;
    std::lock_guard<std::mutex> lk(s_mx);
    return s_logged.insert(className + "|" + std::to_string(ruleVehHash)).second;
}

std::vector<VehicleEntry> VehiclesForRace(const std::string& className, int ruleVehHash) {
    std::vector<VehicleEntry> all = VehiclesForClass(className);
    // Fix round 1 (Important): an UNKNOWN class has no "whole class" to fail-open to (all is already
    // empty from VehiclesForClass). It is detected BEFORE the rule so as not to say "the whole class is offered"
    // about a class that does not exist.
    if (all.empty()) {
        if (FirstCarRuleLog(className, ruleVehHash))
            UserLog("cars", "car rule: class '%s' unknown -> no cars", className.c_str());
        return all;
    }
    if (ruleVehHash == 0) return all;
    for (const auto& v : all) if (v.vehHash == ruleVehHash) return std::vector<VehicleEntry>{ v };
    if (FirstCarRuleLog(className, ruleVehHash))
        UserLog("cars", "car rule %d is not a car of class '%s' (or not installed) -> the whole class is offered",
                ruleVehHash, className.c_str());
    return all;
}

int EffectiveCarRule(const std::string& className, int ruleVehHash) {
    if (ruleVehHash == 0) return 0;
    for (const auto& v : VehiclesForClass(className)) if (v.vehHash == ruleVehHash) return ruleVehHash;
    return 0;   // not installed / of another class / unknown class: the rule does not apply (the caller logs it)
}

// playability of a class with content actually present. A double FAIL-OPEN: with no LIVE car catalog
// (InstalledVehicleCount()==0) or with the class missing from ams2_vehclass (unknown), it returns true — never
// block without evidence. raw=true ignores the user's overrides (for query.content DETECTION).
bool ClassHasInstalledVehicle(const std::string& className, bool raw) {
    if (InstalledVehicleCount() == 0) return true;
    auto it = g_classVehicles.find(className);
    if (it == g_classVehicles.end() || it->second.empty()) return true;
    for (const auto& v : it->second)
        if (raw ? VehicleInstalledRaw(v.vehHash) : VehicleInstalled(v.vehHash)) return true;
    return false;
}

// A STRICT variant, for DETECTING (query.content), not for blocking. No fail-open: if there's no
// live catalog, or the class isn't in ams2_vehclass, or none of its cars are installed -> false.
// "I can't see it" is NOT "you have it": the fail-open above exists so as not to BLOCK without evidence, but
// applying it to detection made the ENTIRE catalog show up as owned on the DLC screen (the
// users reported it as "DLCs I don't own are marked as owned").
bool ClassHasInstalledVehicleStrict(const std::string& className) {
    if (InstalledVehicleCount() == 0) return false;
    auto it = g_classVehicles.find(className);
    if (it == g_classVehicles.end() || it->second.empty()) return false;
    for (const auto& v : it->second) if (VehicleInstalledRaw(v.vehHash)) return true;
    return false;
}

std::vector<std::string> LiveriesForClass(const std::string& className) {
    auto it = g_classLiveries.find(className);
    return it != g_classLiveries.end() ? it->second : std::vector<std::string>();
}

std::string LiveryForClassAt(const std::string& className, int idx) {
    auto it = g_classLiveries.find(className);
    if (it == g_classLiveries.end() || idx < 0 || idx >= (int)it->second.size()) return std::string();
    return it->second[idx];
}

std::string XmlStemForClass(const std::string& className) {
    auto it = g_classXmlStem.find(className);
    return it != g_classXmlStem.end() ? it->second : std::string();
}

int LiveryClassCount() { return (int)g_classLiveries.size(); }

// XML ----------
std::string XmlEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;";  break;
            case '<': o += "&lt;";   break;
            case '>': o += "&gt;";   break;
            case '"': o += "&quot;"; break;
            default:  o += c;        break;
        }
    }
    return o;
}

// 4 DECIMAL PLACES (it used to be %.2f). Measured on track: drivers with different race_skill posted
// IDENTICAL times down to the millisecond (Novak 0.70 and Andrews 0.77 -> both 144.530; Bamber and Farfus -> 126.560).
// Part of that "clumping" was OUR OWN rounding: the model computes in double precision and we were throwing it away
// at write time. With 0.01 of race_skill worth ~0.08s a lap, two-hundredths of resolution is ~8ms of
// minimum granularity between consecutive drivers, and a tight class's band (Pemberton_Zel121: 1.00..0.92) only
// had 9 possible values for 15 cars -> ties forced by arithmetic, not by design.
// No need to add noise: the precision already exists upstream, we just had to stop throwing it away.
// The game accepts it with room to spare ([competitor] writes a full floating point, 0.476945892510062).
// Same reasoning as the scalar-rounding function below, which already moved to 3 decimals for this same reason.
static std::string F2(double v) { char b[24]; snprintf(b, sizeof(b), "%.4f", v); return b; }
// Car scalars live in [0.900, 1.100] and their useful spread reaches only ±0.34% in a class with BoP;
// at two decimals that spread would round to 1.00 for the entire grid.
static std::string F3(double v) { char b[16]; snprintf(b, sizeof(b), "%.3f", v); return b; }

// AI personality by archetype (ABSOLUTE fields). Profiles grounded in 229+ real
// driver packs and the tooltips from Reiza's own documentation. race_skill/qualifying/aggression are NOT touched here
// (they're RELATIVE: spread + the global slider). ----
namespace {
// fuel/blue/weather ADDED: derived from the archetype in ApplyAiPersonality (previously
// flat values). fuel = fuel management (high = conserves; low = burns through it, typical of the charger/aggressive archetype);
// blue = yielding to blue flags (high = clean/disciplined; low = resists, typical of the scrapper); weather
// = accuracy calling weather tire changes (high = technical/cool-headed; low = erratic/young).
static double clampD(double v, double lo, double hi){ return v<lo?lo:(v>hi?hi:v); }
} // anon

// Moved out of the anonymous namespace (declared in career_customai.h). career_fields derives the
// archetype bias for the factor model from here; the table remains the single source of authored personality.
ArchProfile ProfileForArch(const std::string& a, bool* found) {
    if (found) *found = true;   // disproved below, in the one return path where no name matched
    // ~~~28/08 — FIFTEEN STYLES, EACH WITH ITS OWN PERSONALITY, AND THE LEVEL OUT.
    //
    // The measured defect: the fifteen words were FOUR FAMILIES REPEATED AT FOUR LEVELS. The mean of
    //  the seven quality shifts went from -0.200 (`novato`) to +0.164 (`metronomo`) and correlated
    //  **r = 0.639** with the level of whoever carried it: the word said "this one is good" ON TOP of the talent.
    //  And five pairs were the same shape written twice -- `luchador`~`peleon` **0.99**,
    //  `agresivo`~`cargador` 0.97, `guerrera`~`luchador` 0.96.
    //
    // What was tried BEFORE and is no good: normalizing the old table without giving each word its own shape
    //  dissolves **9 of the 15** below the noise floor 0.13 documented by `career_test.cpp:5798`
    //  (`apex` 0.200 -> 0.059, `novato` 0.290 -> 0.100). Their personality WAS their level.
    //
    // The FOUR invariants of this table, all re-measurable:
    //   1. the mean of the seven quality shifts (cons, avoidM, avoidF, start, wet, tyre, stam)
    //      relative to `solido` is **ZERO** in all fifteen -> the level is set by `base_skill`, and only by it;
    //   2. each signature has a maximum magnitude of **0.25**, well above the noise floor 0.13;
    //   3. no pair exceeds **0.85** cosine (they came from 0.99);
    //   4. and in plain words: the fifteen "their thing / their weakness" descriptions are DIFFERENT. It is the test
    //      that matters -- in cosines they already came out different before and even so four said "attacks, start".
    //
    // The DOMINANT trait of each is different on purpose, spread across the nine axes instead of
    // piled up in attack/defend. `def` and `aggr` are set by hand: they are character, not quality, so they
    // do not enter the zero sum and are the most visible on track.
    //
    //                            def  cons avoidM avoidF start  wet  tyre  stam  rel  err  fuel blue weath aggr
    if (a=="apex")              return {0.73,0.79,0.58,0.63,0.85,0.77,0.57,0.61,0.98,false,0.82,0.70,0.80,0.78};  // defends and attacks without breaking; burns tyres and suffers on long runs
    if (a=="metronomo")         return {0.44,0.89,0.78,0.46,0.58,0.59,0.77,0.72,1.04,false,0.92,0.92,0.86,0.38};  // never fails and is consistent; starts slow and does not fight
    if (a=="tecnico")           return {0.44,0.74,0.53,0.48,0.58,0.59,0.99,0.89,1.04,false,0.94,0.90,0.94,0.42};  // tyres and long-run; starts slow and does not attack
    if (a=="frio")              return {0.50,0.72,0.51,0.73,0.58,0.87,0.77,0.61,1.02,false,0.86,0.88,0.92,0.48};  // holds up under pressure and in the wet; starts slow
    if (a=="solido")            return {0.48,0.74,0.53,0.48,0.80,0.72,0.79,0.74,1.00,false,0.80,0.85,0.78,0.60};  // the model's ZERO: the reference against which the others are measured
    if (a=="consistente")       return {0.46,0.99,0.51,0.46,0.67,0.50,0.77,0.89,1.04,false,0.90,0.88,0.82,0.44};  // consistent and lasts the distance; bad in the wet
    if (a=="limpia")            return {0.26,0.66,0.78,0.63,0.72,0.64,0.71,0.66,1.02,false,0.86,0.96,0.84,0.34};  // never fails and does not break; neither defends nor attacks, and yields the blue
    if (a=="rapido")            return {0.44,0.72,0.51,0.46,0.95,0.97,0.57,0.61,0.94,true ,0.72,0.74,0.76,0.66};  // wet and start; burns tyres and suffers on long runs
    if (a=="guerrera"||a=="guerrero")
                           return {0.80,0.89,0.58,0.53,0.58,0.59,0.84,0.79,0.98,false,0.78,0.48,0.76,0.76};  // defends to the death, does not yield the blue; starts slow
    if (a=="luchador")          return {0.74,0.79,0.31,0.53,0.95,0.77,0.66,0.79,0.96,true ,0.74,0.50,0.74,0.82};  // defends AND attacks from the start; makes own errors
    if (a=="peleon"||a=="peleona")
                           return {0.70,0.61,0.58,0.26,0.85,0.77,0.84,0.89,0.92,true ,0.70,0.44,0.72,0.90};  // attacks dirty and lasts the distance; breaks under pressure
    if (a=="agresivo")          return {0.56,0.79,0.31,0.53,0.85,0.87,0.66,0.79,0.90,true ,0.68,0.50,0.70,0.92};  // pure attack and wet; many own errors
    if (a=="cargador")          return {0.52,0.78,0.57,0.52,0.99,0.59,0.57,0.78,0.92,true ,0.70,0.52,0.72,0.82};  // start and comeback; burns tyres
    if (a=="joven")             return {0.42,0.61,0.53,0.26,0.99,0.72,0.79,0.89,0.94,true ,0.74,0.72,0.70,0.72};  // start and long run; breaks under pressure and is irregular
    if (a=="novato"||a=="novata")
                           return {0.18,0.78,0.57,0.26,0.82,0.76,0.83,0.78,0.96,true ,0.78,0.92,0.70,0.40};  // timid: does not defend, breaks and always yields

    // real pinned (codes AP/MT/CH/AM of series_rosters.psv) -> to the same vocabulary, without its own table
    // that can fall out of sync (which is what happened to `ratingSkill` with its two copies).
    if (a=="AP") return ProfileForArch("apex");        // apex-predator
    if (a=="MT") return ProfileForArch("metronomo");   // metronome
    if (a=="CH") return ProfileForArch("cargador");    // charger
    if (a=="AM") return ProfileForArch("novato");      // gentleman-am: does not defend, yields, erratic
    if (found) *found = false;   // no name matched: the caller gets the NEUTRAL profile, not their archetype
    return ProfileForArch("solido");                          // default (field): the neutral one, which is the model's zero
}

// This exists because a misspelled archetype meant a driver with NO personality and nothing said so. In the cast
// there was "metodico" (instead of `tecnico`) and "fria" (instead of `frio`): two of the 28 in the cohort had been silently falling
// back to neutral for months. A single table — the one above — is now the sole authority, so it can't drift out of sync.
bool IsKnownArchetype(const std::string& a) {
    bool found = false;
    ProfileForArch(a, &found);
    return found;
}

// ~~~28/08 — THE LEVEL BIAS WAS CENTERED ON THE WRONG QUARTILE AND SATURATED AT A QUARTER OF THE CAST.
//
// It was `clampD((base_skill - 0.85) / 0.15, -1, 1)` and the comment said "ref = real median 0.85". Measured today
// over the 559 cards of `driver_profiles.psv`: the median is **0.78**; 0.85 is the p75. Two consequences,
// both measured:
//
//   · the MEDIAN driver received t = -0.47, i.e. a permanent NEGATIVE bias. "Neutral" was not neutral.
//   · the data goes from 0.50 to 0.99 (and the rosters go down to 0.45), but the ramp was calibrated for
//     [0.70 - 1.00]: **152 of 559 cards (27 %) and 207 of 718 roster rows (29 %) saturated**. For all
//     of them the level differentiated NOTHING — a 0.50 and a 0.70 received the same personality.
//
// Center = the MEASURED median. Half-range chosen so that the best card in the data (0.99) falls exactly
// on +1: (0.99 - 0.78) / 0.21 = 1.00. Saturation: 27 % -> 1.6 % in cards, 29 % -> 8.8 % in rosters (those that
// remain are the tail below 0.57, which is where saturating IS what is wanted).
//
// A single place: `ReliabilityForArchetype` and `ApplyAiPersonality` use it, and they were already separated once.
static inline double SesgoDeNivel(double base_skill) {
    return clampD((base_skill - 0.78) / 0.21, -1.0, 1.0);   // 1 tail .. 0 median .. +1 the best card
}

// Car reliability by archetype. The SAME formula ApplyAiPersonality uses (which now calls this):
// a single place where the number lives, so the two paths can't drift apart.
double ReliabilityForArchetype(const std::string& archetype, double base_skill) {
    ArchProfile p = ProfileForArch(archetype);
    double t = SesgoDeNivel(base_skill);
    return clampD(p.rel + 0.08 * t, 0.20, 1.20);                // ratio, allows >1.0
}

void ApplyAiPersonality(GridDriver& g, const std::string& archetype, double base_skill) {
    ArchProfile p = ProfileForArch(archetype);
    double t = SesgoDeNivel(base_skill);
    double kE = p.erratic ? 0.05 : 0.10;   // an erratic driver improves LESS in cleanliness as skill rises (keeps their signature)
    g.defending                    = clampD(p.def, 0.05, 0.99);   // flat per archetype (doesn't scale with skill)
    // consistency is NOT touched here: the caller keeps the AUTHORED value (series_roster/series_rosters, per-driver).
    g.avoidance_of_mistakes        = clampD(p.avoidM + kE*t,   0.05, 0.99);
    g.avoidance_of_forced_mistakes = clampD(p.avoidF + kE*t,   0.05, 0.99);
    g.start_reactions              = clampD(p.start  + 0.05*t, 0.65, 0.97);   // real floor ~0.65
    g.wet_skill                    = clampD(p.wet    + 0.08*t, 0.05, 0.99);
    g.tyre_management              = clampD(p.tyre   + 0.06*t, 0.05, 0.99);
    g.stamina                      = clampD(p.stam   + 0.05*t, 0.05, 0.99);
    g.reliability                  = ReliabilityForArchetype(archetype, base_skill);   // ratio, allows >1.0
    // fuel/blue/weather DERIVED from the archetype (previously flat). Same clampD pattern +
    // skill bias (+k*t): the higher the skill, the better the fuel/weather management; yielding to blue flags is a
    // matter of character (doesn't scale with skill). race_skill/qualifying/aggression are NOT touched (they're RELATIVE, set by the pipeline).
    g.fuel_management              = clampD(p.fuel    + 0.05*t, 0.05, 0.99);
    g.blue_flag_conceding          = clampD(p.blue,             0.05, 0.99);   // flat by character
    g.weather_tyre_changes         = clampD(p.weather + 0.06*t, 0.05, 0.99);
}

void AgeAdjustedSkill(GridDriver& g, double ageMult) {
    if (ageMult == 1.0) return;
    auto scale = [ageMult](double v) {
        double r = v * ageMult;
        if (r < 0.05) r = 0.05;
        if (r > 1.0)  r = 1.0;
        return r;
    };
    g.race_skill = scale(g.race_skill);
    g.qualifying = scale(g.qualifying);
    g.defending  = scale(g.defending);
    // race_skill/qualifying/defending are RELATIVE (they scale with the global slider); the ABSOLUTE
    // personality traits (stamina, wet_skill, tyre_management, ...) are NOT touched: age affects pace, not
    // a driver's base character.
}

// See career_customai.h. A mirror of COUNTRY_SYNONYMS (tools/psv_to_season_json.py) + typos
// verified against community packs (JAP/NDL/AUD — an audit confirmed who they actually name).
// Only pairs for the SAME real country — it never collapses distinct countries. YUG (historical) is deliberately left OUT:
// it's resolved PER DRIVER (a Croatian -> HRV, a Serbian -> SRB), not by table.
std::string NormalizeCountryAlias(const std::string& code) {
    struct Alias { const char* from; const char* to; };
    static const Alias kAliases[] = {
        {"GER", "DEU"},   // Germany (reported: Vettel/Hulkenberg)
        {"POR", "PRT"},   // Portugal
        {"NED", "NLD"},   // Netherlands (IOC)
        {"NDL", "NLD"},   // Netherlands (typo seen in packs)
        {"SUI", "CHE"},   // Switzerland
        {"DEN", "DNK"},   // Denmark
        {"JAP", "JPN"},   // Japan (typo seen in packs)
        {"AUD", "AUS"},   // Australia (typo seen in packs; audited: Scott Lyddiard, Australian)
        {"CRO", "HRV"},   // Croatia
        {"SLO", "SVN"},   // Slovenia
        {"RSA", "ZAF"},   // South Africa
        {"GRE", "GRC"},   // Greece
        {"URU", "URY"},   // Uruguay
        {"PAR", "PRY"},   // Paraguay
        {"CHI", "CHL"},   // Chile
        {"BUL", "BGR"},   // Bulgaria
        {"LAT", "LVA"},   // Latvia
        {"PHI", "PHL"},   // Philippines
        {"INA", "IDN"},   // Indonesia
        {"MAS", "MYS"},   // Malaysia
        {"UAE", "ARE"},   // United Arab Emirates
    };
    for (const auto& a : kAliases) if (code == a.from) return a.to;
    return code;
}

std::string BuildGridXml(const std::vector<GridDriver>& driversIn, double spreadPct, double medianPct,
                         bool isOval, int aiPct) {
    const double kEsc = career::TransferScaleForSlider(aiPct);   // 09/09 (): see career_gridcurve.h
    // race_skill is a multiplier of the global slider, measured and revised against real data. The
    // [floor, 1.0] band is derived from the class's target SPREAD (spreadPct) by inverting the measured
    // transfer function at the high reference slider (career_gridcurve): the floor is the race_skill that loses spreadPct of
    // lap time. The SHAPE isn't linear (gamma from the median): tight head, long tail — the real-world silhouette.
    // Before: a flat [GRIDXML_SKILL_FLOOR..1.0] band with a two-segment remap -> everything fell inside the zone where the
    // slider doesn't bite — the "train" effect. This is done at WRITE time (not in ComposeSeasonGrid) so as not to touch the internal model
    // the test suite validates against absolute values. Preserves the model's ORDER (ties share a value).
    std::vector<GridDriver> drivers = driversIn;
    const int n = (int)drivers.size();
    if (n == 1) { drivers[0].race_skill = 1.0; drivers[0].qualifying = 1.0; }
    else if (n >= 2) {
        // If the grid carries TALENT data, it anchors to the ace and each driver drops by THEIR OWN gap.
        // That kills the root defect of range-remapping: there, the number was the driver's POSITION in the class, so two
        // different drivers in the same rank across two classes got the SAME value. If it does NOT carry talent data
        // (old call sites and test fixtures), the range-remap is kept as-is so as not to break them.
        // 24/08 (task 5 of the plan "the gap") — THERE IS NO THIRD BRANCH ANY MORE. The per-POSITION split in the
        // Curve bands, its `g_curveByRank` switch and the floor split (`EsparceElSuelo`) have been retired
        // whole: they were the other side of the A/B with which the wiring of 19/08 was measured, and that A/B is already
        // done and recorded (spec ). Two branches, not five knobs.
        bool haveTalent = true;
        for (const auto& d : drivers) if (d.talent < 0.0) { haveTalent = false; break; }
        if (haveTalent) {
            // =======================================================================================
            // 19/08 — THE THREE-NAIL CONVERSION (spec 2026-08-17 /, of the canonical ).
            // THIS is the production path from today.
            // =======================================================================================
            //
            // What it fixes. Until 19/08 the number was set by `RaceSkillByRank`, which split by POSITION
            // inside the five Curve bands and literally began with `(void)sh;`: it discarded the
            // shape of the whole class. That is why the 109 classes delivered THE SAME THING — the backmarker between
            // 11.3% and 11.6% of lap lost, the same for a rental kart as for an F1 — while the catalog
            // has 7.7x of real variety (1.17% to 9.00% of DRIVER fan). Now each class delivers ITS
            // fan and talent again decides the GAPS, not just the order. That mechanism was
            // retired whole on 24/08 (task 5): there are no longer two paths that can disagree.
            //
            // How. Three nails and two straight segments between them (career_gridcurve.h): the ACE and the BACKMARKER
            // come out of the GRID BEING WRITTEN — the live bar — and the MEDIAN, from the class's
            // data with the `kHeadOpen` dial already in. The lap % is translated to `race_skill` by inverting
            // `kSkillTransfer`, which is the ONLY instrument measured inside the game.
            //
            // `spreadPct` and `medianPct` ARRIVE HERE WITH THE CAR ALREADY DISCOUNTED. `WriteSeasonGridXml` applies
            // `DriverSpreadPct` and the `shrink` (career_fields.cpp:1831-1836) BEFORE calling, so what
            // comes in is the DRIVER's part. Nothing is discounted again here: counting it twice is the
            // only failure of this chain that the harness cannot catch by construction — with the live bar the
            // extreme always falls where it is told, whatever it is told — and that is why it has its own
            // test (`test_the_car_share_is_discounted_exactly_once`).
            //
            // The pins are declared INSIDE this branch on purpose. `GridPins` is born with `no_data = true`
            // precisely so that a half pins does not sink the grid to the floor, but the place is still
            // this one: the branch WITHOUT talent —kept for the old fixtures— does not use this conversion.
            std::vector<double> talentos;
            talentos.reserve(drivers.size());
            for (const auto& d : drivers) talentos.push_back(d.talent);
            const career::GridPins pins = career::PinsForGrid(talentos, spreadPct, medianPct);
            for (auto& d : drivers) {
                // The position comes from the SAME stable tiebreak that the Curve split already used (more talent;
                // at equal talent, lower name). No other order is invented, and it is only queried by the
                // FLAT grid guard: when everyone brings the same talent there is no gap to measure, so the
                // fan is split by position — the spec's last brake against two equal paces.
                int puesto = 0;
                for (const auto& o : drivers) {
                    if (o.talent > d.talent) puesto++;
                    else if (o.talent == d.talent && o.name < d.name) puesto++;
                }
                // F1/T6: QUALIFYING is independent of race pace (the official guide says so and
                // the factor model produces it: own loading 0.93, correlation 0.88 with pace). The
                // traits that arrive here are on the SAME scale as talent, so their difference is the
                // driver's own shift towards Saturday (>0) or towards Sunday (<0): it is added to the
                // talent and goes through the SAME conversion ().
                const double qualShift = d.qualifying - d.race_skill;   // BEFORE overwriting race_skill
                d.race_skill = career::RaceSkillOnPins(d.talent, pins, puesto, (int)drivers.size(), kEsc);
                // 15/08 — OVALS: THE WHOLE GRID AT 1.00 (user's decision, information from the
                // community/Reiza). It is not a simulation choice: it is how AMS2's AI behaves on an
                // oval, which does not run well with race_skill below 1. It goes AFTER the conversion, in the
                // same place and with the same effect as in the Curve branch — talent still decides the
                // order for everything else (quali, aggression, reliability, which car each one drives).
                if (isOval) d.race_skill = 1.0;
                // 25/08 — THE ARCHETYPE IS NOT SPLIT: IT TRAVELS WITH THE DRIVER.
                // The class sets the curve ONLY for pace. The own bias towards Saturday or Sunday is
                // the driver's -if Verstappen fell into a better grid he would still be the same type of
                // driver-, so it is added ON TOP of his already converted pace, without reordering anyone. What
                // is NOT done is giving quali its own position: that would make your Saturday time
                // depend on the bias of the OTHERS, which is spreading the archetype across the grid.
                // The shift arrives on the TALENT scale and is brought to lap % with the ratio of the
                // grid itself, so that the same bias is worth the same in a tight class as in an
                // open one.
                d.qualifying = career::QualifyingFromOwnShift(d.race_skill, qualShift, pins, kEsc);
            }
            // HERE WAS THE `base`/`conShift` PATCH, AND IT DISAPPEARS WITH THIS BRANCH. It existed because the two
            // magnitudes came from DIFFERENT conversions: `race_skill` by POSITION (`RaceSkillByRank`) and
            // `qualifying` by TALENT (`RaceSkillShaped`), so Saturday had to be re-stitched over
            // Sunday after the fact — without the patch, a driver with no own shift came out with quali
            // 1.0000 against a pace of 0.8630. With a single conversion there is nothing to re-stitch: the same
            // talent with no shift goes in and comes out the same, and it is pinned by
            // `test_qualifying_follows_race_pace_through_the_same_conversion`.
            //
            // And the floor is NOT split: with the live bar EXACTLY ONE reaches the floor —the backmarker— and that
            // floor is that of ITS class, not `kMinDriveableSkill`. Splitting there would be lifting off the floor the only one
            // that has to be on it. `EsparceElSuelo` existed for the stacking that the split
            // by position produced, and it went with it.
            //
            // 24/08 — AND THE SIGNATURE HAS BEEN LEFT AT THE THREE NUMBERS THIS CONVERSION READS. `refSpan` (the
            // class's talent bar), `openWheel` (Curve's per-discipline band) and `curveTrimPct` (its
            // trim) no longer enter anywhere: they are not nulled with a mute `(void)` —that pattern is
            // exactly the one that left `RaceSkillByRank` discarding the whole `GridShape` for weeks without
            // anyone seeing it—, they are retired from the prototype and from their callers. The bar stays alive OUTSIDE the
            // XML (`TalentRefSpanFor`, the `[canon-vara]` diagnostic and the measurement dumps): what dies
            // is its role as the grid's width knob, which is now set by the class's data.
        } else {
            const double floor = career::FloorForSpread(spreadPct, kEsc);
            const double gamma = career::GammaForMedian(spreadPct, medianPct, kEsc);
            auto remap = [&](double GridDriver::*field) {
                std::vector<double> sorted; sorted.reserve(n);
                for (const auto& d : drivers) sorted.push_back(d.*field);
                std::sort(sorted.begin(), sorted.end(), std::greater<double>());
                for (auto& d : drivers) {
                    // rank = position of the FIRST tie at this value -> tied drivers share a band
                    int rank = (int)(std::lower_bound(sorted.begin(), sorted.end(), d.*field,
                                                      std::greater<double>()) - sorted.begin());
                    double rankFrac = (double)rank / (double)(n - 1);   // 0=front .. 1=back
                    d.*field = career::RaceSkillForRankFrac(rankFrac, floor, gamma);
                }
            };
            remap(&GridDriver::race_skill);
            remap(&GridDriver::qualifying);
        }
    }
    // Normalize this class independently using wet ability, never dry pace or rank.
    // Equal abilities (including a single driver) stay equal at the neutral ceiling.
    if (!drivers.empty()) {
        double wetMin = drivers.front().wet_skill;
        double wetMax = wetMin;
        for (const auto& d : drivers) {
            wetMin = std::min(wetMin, d.wet_skill);
            wetMax = std::max(wetMax, d.wet_skill);
        }
        for (auto& d : drivers) {
            d.wet_skill = wetMax > wetMin
                ? bal::GRIDXML_WET_SKILL_FLOOR
                    + (bal::GRIDXML_WET_SKILL_CEILING - bal::GRIDXML_WET_SKILL_FLOOR)
                        * (d.wet_skill - wetMin) / (wetMax - wetMin)
                : bal::GRIDXML_WET_SKILL_CEILING;
        }
    }
    // 04/10 (Josh): the START, per class and anchored at 1 like the wet: the best starter writes 1.0 and the rest
    // keeps its differences down to the floor. Before, it went through the human band (p05 0.67) and a quarter of the field
    // ended up stamped on the floor; the AI started slowly ([tester], 04/10). Equal (or just one) = ceiling.
    if (!drivers.empty()) {
        double stMin = drivers.front().start_reactions, stMax = stMin;
        for (const auto& d : drivers) { stMin = std::min(stMin, d.start_reactions); stMax = std::max(stMax, d.start_reactions); }
        for (auto& d : drivers)
            d.start_reactions = stMax > stMin
                ? bal::GRIDXML_START_REACTIONS_FLOOR
                    + (bal::GRIDXML_START_REACTIONS_CEILING - bal::GRIDXML_START_REACTIONS_FLOOR)
                        * (d.start_reactions - stMin) / (stMax - stMin)
                : bal::GRIDXML_START_REACTIONS_CEILING;
    }
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<custom_ai_drivers>\r\n";
    for (const auto& d : drivers) {
        x += "    <driver livery_name=\"" + XmlEscape(d.livery_name) + "\">\r\n";
        x += "        <name>" + XmlEscape(d.name) + "</name>\r\n";
        // the alias is normalized HERE, at the last gate before the game — the data arrives byte-faithful
        // from the PSV (by design of psv_to_season_json.py), but the game only accepts the canonical ISO code.
        x += "        <country>" + XmlEscape(NormalizeCountryAlias(d.country)) + "</country>\r\n";
        // — THE TRANSLATION LAYER. Until here the traits were written RAW while the pace did
        // go through its class's band. `HB` takes each DRIVER knob to the band in which the people who author grids
        // write it (324 cards measured). It fixes two things measured on 26/08:
        //   · tails that nobody would write — our p05 of consistency was 0.15 against their 0.54;
        //   · flat combat — their defense reaches 0.90 and ours stayed at 0.50, i.e. NOT ONE
        //     of the 2,991 defended like a normal human, and hence the races came out as processions.
        // `race_skill` does NOT pass: it is positional (relative to its grid), not capacity. `blue_flag_conceding`
        // NEITHER: its 0.00 is set by BlueFlagForRole on purpose for the SLOW class of a multiclass, and
        // forcing it into the human band would break that. The car's scalars belong to the TEAM. The full table
        // of which knob belongs to whom is in career_traitband.h.
        // wet_skill also bypasses HB: its per-class band above permits the documented 1.0 endpoint.
        // 26/08 (afternoon) — THE BAND COMES BACK, with its column RE-MEASURED. It was unwired for a few hours for a
        // real reason: its table was calibrated in the morning and expired the same day, because in between came the
        // combat ceiling and the archetype notes, which moved our entire distribution. Applying it with the
        // old snapshot WIDENED TWICE -- measured, median of defense at 0.94.
        // What it closes: the 17:10 audit found 464 drivers writing EXACTLY 1.000 (stamina 82,
        // start 102, water 105, tyres 60, fuel 61, consistency 43) and 9 writing 0.000 -- the two values
        // that AMS2 treats as a special case, and the 0.00 of errors is the one the community reports as FLIPPING the
        // maximum. Plus two level tails: 34% looked after their tyres and 30% attacked below what
        // any human would write. The band kills the extremes by construction (kNever0/kNever1) and raises the
        // tails without touching the ORDER, which is monotonic: the archetype's work on who-goes-ahead is kept
        // whole and this layer only decides the RANGE.
        #define HB(t, v) F2(career::traitband::ToHumanBand(career::traitband::t, (v)))
        x += "        <race_skill>" + F2(d.race_skill) + "</race_skill>\r\n";
        // QUALI DOES NOT GO THROUGH THE BAND EITHER, and this was decided by the harness, not by me. Conceptually it belongs to the
        // DRIVER (there are one-lap specialists) and so it was decided on 26/08, but MECHANICALLY today it is a
        // DERIVATIVE of pace: the invariant "with no own shift, quali comes out EXACTLY from pace"
        // is pinned in 13 checks and is part of the per-position split. Passing it through the band and not pace
        // desynchronizes them for the whole grid — tried: 14 new failures, among them "the backmarker has no
        // room left under his class's floor: Saturday = Sunday" (0.7200 against 0.5929).
        // If one day you want qualifying to truly belong to the driver, it is its OWN ATTRIBUTE with its
        // own shift, not a band on top of a derivative. That is , not this.
        x += "        <qualifying_skill>" + F2(d.qualifying) + "</qualifying_skill>\r\n";
        x += "        <aggression>" + HB(kAggression, d.aggression) + "</aggression>\r\n";
        x += "        <defending>" + HB(kDefending, d.defending) + "</defending>\r\n";
        x += "        <stamina>" + HB(kStamina, d.stamina) + "</stamina>\r\n";
        x += "        <consistency>" + HB(kConsistency, d.consistency) + "</consistency>\r\n";
        x += "        <start_reactions>" + F2(d.start_reactions) + "</start_reactions>\r\n";   // 04/10: anchored per class at the top, no HB (like wet)
        x += "        <wet_skill>" + F2(d.wet_skill) + "</wet_skill>\r\n";
        x += "        <tyre_management>" + HB(kTyreManagement, d.tyre_management) + "</tyre_management>\r\n";
        x += "        <fuel_management>" + HB(kFuelManagement, d.fuel_management) + "</fuel_management>\r\n";
        x += "        <blue_flag_conceding>" + F2(d.blue_flag_conceding) + "</blue_flag_conceding>\r\n";
        x += "        <weather_tyre_changes>" + HB(kWeatherTyre, d.weather_tyre_changes) + "</weather_tyre_changes>\r\n";
        x += "        <avoidance_of_mistakes>" + HB(kAvoidMistakes, d.avoidance_of_mistakes) + "</avoidance_of_mistakes>\r\n";
        x += "        <avoidance_of_forced_mistakes>" + HB(kAvoidForced, d.avoidance_of_forced_mistakes) + "</avoidance_of_forced_mistakes>\r\n";
        #undef HB
        x += "        <vehicle_reliability>" + F2(d.reliability) + "</vehicle_reliability>\r\n";
        // The car scalars are a TEAM property. Only emitted when the grid carries
        // equipment spread data: with no data (a single-make class, a class with no row, or missing files) all three default to
        // 1.000 and the block is NOT written, so the XML for the 51 zero-spread classes — and for
        // any class not yet authored — comes out exactly as before. 3 decimals: the game's legal range is
        // [0.900, 1.100] and with %.2f a spread of ±0.34% (GT3) would round away to nothing.
        if (std::fabs(d.weight_scalar - 1.0) > 5e-4 || std::fabs(d.power_scalar - 1.0) > 5e-4 ||
            std::fabs(d.drag_scalar   - 1.0) > 5e-4) {
            x += "        <weight_scalar>" + F3(d.weight_scalar) + "</weight_scalar>\r\n";
            x += "        <power_scalar>"  + F3(d.power_scalar)  + "</power_scalar>\r\n";
            x += "        <drag_scalar>"   + F3(d.drag_scalar)   + "</drag_scalar>\r\n";
        }
        x += "    </driver>\r\n";
    }
    x += "</custom_ai_drivers>\r\n";
    return x;
}

// assembly (TOTAL override: one driver per livery) ----------
std::vector<GridDriver> AssembleGrid(const std::vector<std::string>& liveries,
                                     const std::vector<RivalDef>& roster) {
    std::vector<GridDriver> out;
    if (roster.empty()) return out;
    std::map<std::string, int> used;
    for (size_t i = 0; i < liveries.size(); i++) {
        const RivalDef& r = roster[i % roster.size()];
        GridDriver d;
        d.livery_name = liveries[i];
        d.country = r.country;
        int& c = used[r.name]; c++;
        d.name = (c > 1) ? (r.name + " " + std::to_string(c)) : r.name;   // unique names when cycling the roster
        d.race_skill = r.skill; d.qualifying = r.skill;
        d.aggression = r.aggression; d.consistency = r.consistency;
        d.defending = 0.80; d.reliability = 0.95;
        out.push_back(d);
    }
    return out;
}

// files (safe backup/restore of the user's XML) ----------
static bool FileExists(const std::string& p) { return paths::FileExists(p); }   // Mojibake fix: delegates to paths (UTF-8 -> wide)

// TOMBSTONES — the `.careerbak` file is the ONLY mark of ownership for the slot, and its SIZE tells you
// who owned the XML that was there before: non-empty means the user's original, empty is a tombstone ("there was
// nothing of theirs here"). It used to only create a backup when the user had a file, and that turned
// our own XML into a fake backup: on the second launch it would see `path` with no `bak` next to it, rename it
// to `.careerbak`, and from then on the "user's original" was actually our own file from a previous launch.
bool BackupAndWriteAi(const std::string& dir, const std::string& stem, const std::string& xml) {
    std::string path = dir + "\\" + stem + ".xml";
    std::string bak  = path + ".careerbak";
    if (!FileExists(bak)) {                                  // untouched slot: marked ONCE, permanently
        if (FileExists(path)) {
            // If the rename fails (locked file, permissions, disk full...) there's NO real backup. Continuing
            // to write anyway would lose the user's XML without ever having saved a copy: the one state
            // where someone ends up with neither their file NOR a backup. We abort without touching `path`: we'd rather have
            // a class without our grid than a user without their file.
            if (!paths::RenameFile(path, bak)) return false;
        }
        else { FILE* t = paths::FOpen(bak, "wb"); if (t) fclose(t); }   // there was nothing -> a 0-byte tombstone
    }
    FILE* f = paths::FOpen(path, "wb");
    if (!f) return false;
    fwrite(xml.data(), 1, xml.size(), f);
    fclose(f);
    return true;
}

// The exact mirror of the rule above, and IDEMPOTENT because of it: when done, not a single `.careerbak` is left, so
// a second pass (the uninstaller's retry loop) finds nothing left to do. A `path`
// with no backup next to it is NOT ours and isn't touched; deleting it would wipe out exactly what the previous
// pass had just handed back to the user, with no backup left to fall back on.
bool RestoreAi(const std::string& dir, const std::string& stem) {
    std::string path = dir + "\\" + stem + ".xml";
    std::string bak  = path + ".careerbak";
    if (!FileExists(bak)) return true;                       // someone else's slot (or already restored): don't touch it
    // == 0, NOT <= 0: FileSize returns -1 when it DOESN'T KNOW (file unreadable/gone between the earlier
    // check and here), and that case is NOT a tombstone. Mistaking a real backup for a tombstone ERASES the user's only
    // copy (irreversible); mistaking a tombstone for a real backup only leaves a 0-byte XML (cosmetic).
    // When the size is unknown, the only safe direction is not to delete.
    bool tombstone = (paths::FileSize(bak) == 0);
    paths::RemoveFile(path);                                 // the XML that's there is ours in both cases
    if (tombstone) return paths::RemoveFile(bak);            // the slot was ours: the tombstone goes too
    return paths::RenameFile(bak, path);                     // the user's original, back in its place
}

// data-driven roster (docs/ams2_rivals.psv) ----------
static std::vector<RivalDef> g_roster;

bool LoadRivals(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_roster.clear();
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 3) continue;
        if (t[0] == "name" || t[0].empty()) continue;       // header / empty
        RivalDef r;
        r.name = CleanNameSpaces(t[0]);
        r.country = t[1];
        r.skill = atof(t[2].c_str());
        r.aggression = (t.size() > 3) ? atof(t[3].c_str()) : 0.60;
        r.consistency = (t.size() > 4) ? atof(t[4].c_str()) : 0.85;
        g_roster.push_back(r);
    }
    fclose(f);
    return true;
}

const std::vector<RivalDef>& Roster() { return g_roster.empty() ? DefaultRoster() : g_roster; }
int RivalCount() { return (int)g_roster.size(); }

// bootstrap roster (recognizable nods, legally distinct) ----------
const std::vector<RivalDef>& DefaultRoster() {
    static const std::vector<RivalDef> R = {
        { "Aryton Senda",   "BRA", 0.98, 0.65, 0.90 },
        { "Mick Schuman",   "DEU", 0.98, 0.70, 0.93 },
        { "Alan Prest",     "FRA", 0.97, 0.45, 0.95 },
        { "Lewis Hamleton", "GBR", 0.97, 0.62, 0.93 },
        { "Fernan AlonZo",  "ESP", 0.96, 0.68, 0.91 },
        { "Max Versteg",    "NLD", 0.96, 0.78, 0.88 },
        { "Niko Lauder",    "AUT", 0.95, 0.50, 0.94 },
        { "Sergio Vetteli", "DEU", 0.95, 0.66, 0.90 },
        { "Nilson Picket",  "BRA", 0.94, 0.60, 0.88 },
        { "Mika Hakinen",   "FIN", 0.94, 0.58, 0.90 },
        { "Emerson Fitti",  "BRA", 0.93, 0.55, 0.89 },
        { "Jacky Stewar",   "GBR", 0.93, 0.48, 0.92 },
        { "Kimo Raikonen",  "FIN", 0.93, 0.55, 0.86 },
        { "Charel Leclart", "MCO", 0.93, 0.63, 0.87 },
        { "Nilo Mansel",    "GBR", 0.92, 0.72, 0.84 },
        { "Niko Rosburg",   "DEU", 0.91, 0.57, 0.88 },
        { "Juan Montoyas",  "COL", 0.91, 0.74, 0.82 },
        { "Lan Norrys",     "GBR", 0.91, 0.58, 0.89 },
        { "Damon Hull",     "GBR", 0.90, 0.52, 0.88 },
        { "Jacq Villanova", "CAN", 0.90, 0.64, 0.83 },
        { "Felip Massi",    "BRA", 0.89, 0.60, 0.85 },
        { "Jens Buton",     "GBR", 0.89, 0.49, 0.91 },
        { "Dani Ricci",     "AUS", 0.89, 0.70, 0.85 },
        { "Carlo Sains",    "ESP", 0.89, 0.56, 0.88 },
        { "Rubem Barricha", "BRA", 0.88, 0.50, 0.90 },
        { "Mark Weber",     "AUS", 0.88, 0.66, 0.84 },
        { "Sergi Peres",    "MEX", 0.88, 0.62, 0.85 },
        { "Val Botas",      "FIN", 0.88, 0.52, 0.90 },
    };
    return R;
}

} // namespace career
