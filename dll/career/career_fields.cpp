#include "career_fields.h"
#include "career_difficulty.h"   // 09/09 (): EffectiveAiForClass
#include "career_balance.h"   // AGE_*/RETIRE_*/PROMOTE_* tunables (namespace career::bal)
#include "career_rating.h"    // RatingQuality() -- rating -> skill quality in 0..1
#include "career_data_json.h" // BirthYearFor (real-driver birth year for the alive filter)
#include "career_seats.h"     // FieldOccupantId -- the SAME id resolution used by ELO seeding and by seats
#include "career_genome.h"    // GenomeSeed -- to undo the previous occupant's affinity when a seat changes hands
#include "career_attributes.h"// MeasuredCraftFor — the MEASURED craft of the anchors, instead of the drawn one
#include "career_skins.h"     // "real skins": RebindGridForSkins (livery_name -> the installed skin's NAME)
#include "career_classpace.h" // Block B wired in: the REAL per-class spread for the XML (see WriteSeasonGridXml)
#include "career_teamcars.h"  // Per-TEAM car scalars + the driver/car split of the spread
#include "career_aiverify.h"  // Logs the names actually written, to detect a dropped override
#include "career_userlog.h"   // UserLog -- the blind-hookup warning reaches the player, not just the log
#include "career_market.h"    // market::GridSizeForClass -- the REAL grid size (classes.json)
#include "career_customai.h"  // 16/09: TrackGridCapacity — the circuit's slots, the fourth cap on the grid
#include "career_growth.h"    // 18/08: RookieProfileTalentCloned — a generated driver's card comes from the corpus
#include "paths.h"            // mojibake fix 22/07: FOpen (UTF-8 -> wide)
#include "career_text.h"      // 23/09: CleanNameSpaces — every loaded name comes out without hard spaces
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <set>
#include <map>
#include <utility>

namespace career {

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

// "88-106" -> lo=88, hi=106 (defaults to 85/100 if missing/malformed; sorted if given reversed)
static void ParseAiRange(const std::string& s, int& lo, int& hi) {
    lo = 85; hi = 100;
    size_t dash = s.find('-');
    if (dash != std::string::npos) { lo = atoi(s.substr(0, dash).c_str()); hi = atoi(s.substr(dash + 1).c_str()); }
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
}

double CurveSkill(int order, int fieldSize, int aiLo, int aiHi) {
    if (fieldSize <= 1) { double s = aiHi / 120.0; return s > 1.0 ? 1.0 : (s < 0.0 ? 0.0 : s); }
    if (order < 1) order = 1; if (order > fieldSize) order = fieldSize;
    double frac = (double)(order - 1) / (double)(fieldSize - 1);   // 0 (best) .. 1 (worst)
    double v = aiHi - (aiHi - aiLo) * frac;                        // AMS2 scale (~0-120)
    double s = v / 120.0;
    if (s < 0.0) s = 0.0; if (s > 1.0) s = 1.0;
    return s;
}

// FIELDS ----------
static std::vector<FieldDriver> g_fields;

bool LoadFields(const char* path) {
    ResetGridSizeCache(); ResetGlobalIdentityIndex();   // the data changes -> the per-class name table becomes stale
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_fields.clear();
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 5) continue;
        if (t[0] == "series_id" || t[0].empty()) continue;
        FieldDriver d;
        d.series_id = t[0]; d.order = atoi(t[1].c_str()); d.name = CleanNameSpaces(t[2]); d.country = t[3];
        d.iconic = atoi(t[4].c_str()) != 0;
        d.skill = (t.size() > 5) ? atof(t[5].c_str()) : 0.0;
        d.aggr  = (t.size() > 6) ? atof(t[6].c_str()) : 0.0;
        d.note  = (t.size() > 7) ? t[7] : std::string();
        g_fields.push_back(d);
    }
    fclose(f);
    return true;
}

// — the REAL drivers of the packs, for the slots the curated data does not cover.
// See career_fields.h. Indexed by class (file order, which is significant) and by id.
static std::vector<PackDriver> g_packDrivers;
static std::map<std::string, std::vector<int>> g_packByClass;
static std::map<std::string, int> g_packById;

std::string PackDriverId(const PackDriver& pd) {
    // The SAME chain as FieldOccupantId, and for the same reason: two different resolutions are two
    // identities for the same person, which is what the exists to prevent. Here it is replicated instead
    // of calling it because `FieldOccupantId` lives in career_seats, which includes this layer and not the other way around.
    if (pd.name.empty()) return std::string();
    const std::string poolId = DriverIdForName(pd.name, pd.country);
    if (!poolId.empty()) return poolId;
    unsigned h = 2166136261u;
    for (char c : pd.name) { h ^= (unsigned char)c; h *= 16777619u; }
    return std::string("rd_") + std::to_string(h);
}

bool LoadPackDrivers(const char* path) {
    ResetGridSizeCache(); ResetGlobalIdentityIndex();
    g_packDrivers.clear(); g_packByClass.clear(); g_packById.clear();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;                       // file absent = the world as before; nothing regresses
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 3) continue;
        if (t[0] == "class_name" || t[0].empty() || t[1].empty()) continue;
        PackDriver d;
        d.class_name = t[0]; d.name = CleanNameSpaces(t[1]); d.country = t[2];
        d.pack = (t.size() > 3) ? t[3] : std::string();
        const int idx = (int)g_packDrivers.size();
        g_packDrivers.push_back(d);
        g_packByClass[d.class_name].push_back(idx);
        const std::string id = PackDriverId(d);
        if (!id.empty()) g_packById.emplace(id, idx);   // the first wins: the same person in two classes is ONE
    }
    fclose(f);
    return true;
}

int PackDriverCount() { return (int)g_packDrivers.size(); }

const PackDriver* PackDriverForClassSlot(const std::string& className, int k) {
    if (k < 0) return nullptr;
    const auto it = g_packByClass.find(className);
    if (it == g_packByClass.end() || k >= (int)it->second.size()) return nullptr;
    return &g_packDrivers[it->second[k]];
}

const PackDriver* PackDriverById(const std::string& id) {
    const auto it = g_packById.find(id);
    return it == g_packById.end() ? nullptr : &g_packDrivers[it->second];
}

// 23/09 — the curated reserve. See career_fields.h.
static std::map<std::string, std::vector<ReservePerson>> g_reserveByName;   // folded name -> rows
static int g_reserveCount = 0;

bool LoadReservePeople(const char* path) {
    g_reserveByName.clear(); g_reserveCount = 0;
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 5) continue;
        if (t[0] == "name" || t[0].empty()) continue;
        ReservePerson p;
        p.name = CleanNameSpaces(t[0]); p.country = t[1];
        p.birth_year  = atoi(t[2].c_str());
        p.status      = (t[3] == "active") ? RESERVE_ACTIVE : (t[3] == "retired") ? RESERVE_RETIRED : RESERVE_UNKNOWN;
        p.last_season = atoi(t[4].c_str());
        const std::string key = FoldDriverName(p.name);
        if (key.empty()) continue;
        g_reserveByName[key].push_back(p);
        g_reserveCount++;
    }
    fclose(f);
    return true;
}

int ReservePeopleCount() { return g_reserveCount; }

const ReservePerson* ReservePersonFor(const std::string& name, const std::string& country) {
    const auto it = g_reserveByName.find(FoldDriverName(name));
    if (it == g_reserveByName.end() || it->second.empty()) return nullptr;
    for (const auto& p : it->second) if (!country.empty() && p.country == country) return &p;
    // no country to break the tie: only valid if the name belongs to ONE person and the row names no other country
    if (it->second.size() == 1 && (country.empty() || it->second[0].country.empty())) return &it->second[0];
    return nullptr;
}

void ReplaceFields(std::vector<FieldDriver> v) {
    ResetGridSizeCache(); ResetGlobalIdentityIndex(); g_fields = std::move(v); }
int FieldRowCount() { return (int)g_fields.size(); }   // number of rows loaded (diagnostics/test)

std::vector<FieldDriver> FieldForSeries(const std::string& seriesId) {
    std::vector<FieldDriver> out;
    for (const auto& d : g_fields) if (d.series_id == seriesId) out.push_back(d);
    std::sort(out.begin(), out.end(), [](const FieldDriver& a, const FieldDriver& b) { return a.order < b.order; });
    int lo = 85, hi = 100;
    const SeriesDef* s = SeriesById(seriesId);
    if (s) { const ConfigProfile* p = ProfileById(s->profile_id); if (p) ParseAiRange(p->get("ai"), lo, hi); }
    int n = (int)out.size();
    for (int i = 0; i < n; i++) {
        FieldDriver& d = out[i];
        if (d.skill <= 0.0) {
            d.skill = CurveSkill(d.order, n, lo, hi);
            if (d.iconic) { d.skill += 0.04; if (d.skill > 1.0) d.skill = 1.0; }
        }
        if (d.aggr <= 0.0) d.aggr = 0.60;
    }
    return out;
}

// AUTHORED PROFILES FOR THE REAL FIELD ----------
// Name folding for matching authored data against the field. NormSlug (career_events) doesn't work here: it drops
// bytes >127, so "Agustín" becomes "agustn" and does NOT match "agustin". Here UTF-8 is decoded and
// each accent folds to its base letter. Measured across the three name PSVs: 18 non-ASCII characters, all
// Latin-1 except 'Z' with caron; 'o' with stroke does NOT decompose under NFKD and needs its own entry.
static void FoldAppend(std::string& o, unsigned cp) {
    if (cp < 0x80) { if (isalnum((unsigned char)cp)) o += (char)tolower((int)cp); return; }
    // Latin-1 Supplement (U+00C0..U+00FF). Ones that expand to two letters are written out expanded.
    if (cp >= 0xC0 && cp <= 0xFF) {
        static const char* kL1[64] = {
            "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",       // C0-CF
            "d","n","o","o","o","o","o","",  "o","u","u","u","u","y","th","ss",     // D0-DF (D7 = multiplication sign, not a letter)
            "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",       // E0-EF
            "d","n","o","o","o","o","o","",  "o","u","u","u","u","y","th","y" };    // F0-FF (F7 = division sign, not a letter)
        o += kL1[cp - 0xC0];
        return;
    }
    // Latin Extended-A (U+0100..U+017F): 1:1 to its base letter. Covers Czech/Polish/Croatian/Turkish/Nordic
    // letters (Raikkonen already falls under Latin-1; this covers the accented letters that show up, and will keep showing up, in real grids).
    if (cp >= 0x100 && cp <= 0x17F) {
        // Table GENERATED from NFKD decomposition (not hand-written: the first version came out with 122
        // entries instead of 128, and everything past the gap was shifted -- one letter folded to nothing). Characters that
        // don't decompose get a reasonable ASCII stand-in; all that matters is that it's consistent.
        static const char kLA[129] =
            "aaaaaaccccccccddddeeeeeeeeeegggggggghhhhiiiiiiiiiiiijjkkklllllll"
            "lllnnnnnnnnnoooooooorrrrrrssssssssttttttuuuuuuuuuuuuwwyyyzzzzzzs";
        o += (char)tolower((unsigned char)kLA[cp - 0x100]);
        return;
    }
    // Any other script (Cyrillic, CJK...) is ignored. There are no such names in the corpus, and dragging in a
    // full Unicode table here wouldn't be worth it.
}

static std::string FoldName(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char b = (unsigned char)s[i];
        unsigned cp; int n;
        if      (b < 0x80)          { cp = b;        n = 1; }
        else if ((b & 0xE0) == 0xC0){ cp = b & 0x1F; n = 2; }
        else if ((b & 0xF0) == 0xE0){ cp = b & 0x0F; n = 3; }
        else if ((b & 0xF8) == 0xF0){ cp = b & 0x07; n = 4; }
        else                        { i++; continue; }            // a stray continuation byte: skipped
        if (i + (size_t)n > s.size()) break;                      // truncated at the end: cut off
        for (int k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        FoldAppend(o, cp);
        i += (size_t)n;
    }
    return o;
}

static std::map<std::string, AuthoredProfile> g_authored;

bool LoadAuthoredProfiles(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    // Also clears the table if it can't open the file (following LoadDriverProfiles' convention, not LoadFields'): so "file missing"
    // literally means "everyone neutral", instead of leaving a stale table half-applied.
    if (!f) { g_authored.clear(); return false; }
    g_authored.clear();
    char line[2048];   // the `history` column is long-form prose; 1024 characters was truncating rows in the middle
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 4) continue;
        if (t[0] == "name" || t[0].empty()) continue;             // header
        const std::string key = FoldName(t[0]);
        if (key.empty()) continue;
        AuthoredProfile p;
        p.is_real   = atoi(t[1].c_str()) != 0;
        p.talent    = atof(t[2].c_str());
        p.archetype = t[3];
        p.affinity  = (t.size() > 4) ? t[4] : std::string();   // The discipline (col 5)
        p.name      = CleanNameSpaces(t[0]);                    // the unfolded name (the key cannot be shown)
        // ~~~04/09 (): the first four-digit year of `era` (“F1 1991”, “1987 HARBOR_CHALLENGE_8227 Winston
        //  Cup”). 1900-2099 is accepted; without a year it stays 0 and the anchored arc is not applied.
        if (t.size() > 7) {
            const std::string& era = t[7];
            for (size_t i = 0; i + 3 < era.size(); ++i) {
                if (era[i] < 48 || era[i] > 57) continue;
                const int y = atoi(era.substr(i, 4).c_str());
                if (y >= 1900 && y <= 2099) { p.era_year = y; break; }
            }
        }
        // 16/09 — THE COUNTRY (col 9). Only carried by those that exist in no other file; the rest
        //  already have it in their pool/pack/field and here it arrives empty. `SplitPipe` leaves the last field with the
        //  line break removed, so nothing needs trimming. See `AuthoredProfile::country`.
        if (t.size() > 8) p.country = t[8];
        // A vocabulary guard: an archetype ProfileForArch doesn't recognize silently falls back to "solid", and the
        // driver ends up neutral with no warning at all. Better to drop the row and have it show up in the counter.
        if (p.archetype.empty() || p.talent <= 0.0 || p.talent > 1.0) continue;
        g_authored[key] = p;
    }
    fclose(f);
    ResetGlobalIdentityIndex();   // 16/09: the corpus feeds the global index -> it expires with it.
    return true;
}

const AuthoredProfile* AuthoredProfileFor(const std::string& name) {
    if (g_authored.empty() || name.empty()) return nullptr;
    auto it = g_authored.find(FoldName(name));
    return it == g_authored.end() ? nullptr : &it->second;
}

int AuthoredProfileCount() { return (int)g_authored.size(); }

std::string FoldDriverName(const std::string& name) { return FoldName(name); }

const std::map<std::string, AuthoredProfile>& AllAuthoredProfiles() { return g_authored; }

std::vector<double> AllAuthoredProfileTalents() {
    std::vector<double> out;
    out.reserve(g_authored.size());
    for (const auto& kv : g_authored) out.push_back(kv.second.talent);
    return out;
}

// ROSTER ----------
static std::vector<RosterDriver> g_roster;

bool LoadRoster(const char* path) {
    ResetGridSizeCache(); ResetGlobalIdentityIndex();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_roster.clear();
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 10) continue;
        if (t[0] == "driver_id" || t[0].empty()) continue;
        RosterDriver r;
        r.id = t[0]; r.name = CleanNameSpaces(t[1]); r.homage = CleanNameSpaces(t[2]); r.country = t[3];
        r.tier_min = atoi(t[4].c_str()); r.tier_max = atoi(t[5].c_str());
        r.base_skill = atof(t[6].c_str()); r.aggression = atof(t[7].c_str()); r.consistency = atof(t[8].c_str());
        r.hot = atoi(t[9].c_str()) != 0;
        r.archetype = (t.size() > 10) ? t[10] : std::string();
        r.birth_year  = (t.size() > 11) ? atoi(t[11].c_str()) : 0;   // col 12, optional (0 = unknown)
        r.retire_year = (t.size() > 12) ? atoi(t[12].c_str()) : 0;   // col 13, optional (0 = procedural)
        r.modern_only = (t.size() > 13) ? (atoi(t[13].c_str()) != 0) : false;   // col 14, optional
        g_roster.push_back(r);
    }
    fclose(f);
    return true;
}

void ReplaceRoster(std::vector<RosterDriver> v) {
    ResetGridSizeCache(); ResetGlobalIdentityIndex(); g_roster = std::move(v); }
int RosterRowCount() { return (int)g_roster.size(); }   // number of rows loaded (diagnostics/test)

// CLASS CHARACTER (see career_fields.h) ----------
static std::map<std::string, int> g_classAgeCeiling;   // class_name -> its band's max age
static std::map<std::string, std::string> g_classChar;   // class_name -> character (school/open-wheel/...)

bool LoadClassAgeProfile(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_classAgeCeiling.clear();
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 4) continue;
        if (t[0].empty() || t[0] == "class_name") continue;
        const int hi = atoi(t[3].c_str());
        if (hi > 0) g_classAgeCeiling[t[0]] = hi;
        if (!t[1].empty()) g_classChar[t[0]] = t[1];   // The character, not just the ceiling
    }
    fclose(f);
    return true;
}

int ClassAgeCeiling(const std::string& className) {
    auto it = g_classAgeCeiling.find(className);
    return it == g_classAgeCeiling.end() ? 0 : it->second;   // 0 = no data -> the usual flat threshold
}

int ClassAgeProfileCount() { return (int)g_classAgeCeiling.size(); }

std::string ClassCharacter(const std::string& className) {
    auto it = g_classChar.find(className);
    return it == g_classChar.end() ? std::string() : it->second;
}

bool IsSchoolClass(const std::string& className) { return ClassCharacter(className) == "escuela"; }

std::vector<RosterDriver> RosterForTier(int tier) {
    std::vector<RosterDriver> out;
    for (const auto& r : g_roster) if (r.tier_min <= tier && tier <= r.tier_max) out.push_back(r);
    std::sort(out.begin(), out.end(), [](const RosterDriver& a, const RosterDriver& b) {
        if (a.hot != b.hot) return a.hot > b.hot;          // hot first
        if (a.base_skill != b.base_skill) return a.base_skill > b.base_skill;
        return a.id < b.id;   // tiebreak by id: a TOTAL ORDER -> deterministic and independent of the order
                              // g_roster was loaded in (PSV<->JSON parity: same cast; the tiebreak order
                              // no longer depends on whether it came in file order (PSV) or the pool's alphabetical order (JSON))
    });
    return out;
}

// PROGRESSION pass -- "almost all cast at the bottom, 3-5 + real names at the top":
//   ReplaceCount = the number of seats the CAST (the rookie cohort) occupies. It used to be LOW at the bottom (6) and drop
//   further at the top (4) -> a mostly-REAL grid everywhere (the opposite of what was wanted). Now the cap is HIGH at the bottom
//   (club level = your own generation) and drops as you climb (elite = a few cast members + real names). Capped per tier (bounded
//   by the eligible cast size 28/26/20/14/10/7/6 -> the first invariant still holds): tier 1-2 is fully cast-dominant,
//   tier 4-7 transitions to real-dominant. The difficulty LEVEL is set by the global slider (DDA), not by this split.
int ReplaceCount(int tier, int gridSize) {
    int t = tier < 1 ? 1 : (tier > 7 ? 7 : tier);
    static const int kCap[8] = { 0, /*t1*/24, /*t2*/22, /*t3*/14, /*t4*/10, /*t5*/7, /*t6*/5, /*t7*/4 };
    int n = kCap[t];
    if (n > gridSize) n = gridSize;
    if (n < 0) n = 0;
    return n;
}

// "Freed seats" formalized. For a series with a real field[]:
//   FreedSeatCount = the seats the cast occupies = ReplaceCount(tier, grid).
//   RealFieldCount = the real drivers actually KEPT (top-by-order) = min(fieldSize, grid - freed).
// The cast occupies the HIGHEST-`order` seats (backmarker/midfield) and the real field keeps the lower-`order`
// ones (the best drivers) -- derived purely from the field[]'s `order`, no corpus needed. For series with a
// pinned roster this curve-based field model isn't used; FreedSeatCount/RealFieldCount aren't consulted there.
int FreedSeatCount(const std::string& seriesId, int gridSize) {
    if (gridSize <= 0) return 0;
    const SeriesDef* s = SeriesById(seriesId);
    int tier = s ? s->level : 1;
    return ReplaceCount(tier, gridSize);
}

// the player's own cap (config.json "max_grid"), read at startup. 0 = no cap.
static int g_maxGrid = 0;
void SetMaxGridSize(int n) { g_maxGrid = (n >= 2) ? n : 0; }   // < 2 isn't a race -> ignored
int  MaxGridSize() { return g_maxGrid; }

// THE single source of truth for the grid size. See career_fields.h.
static int GridSizeForClassNameCapped(const std::string& className, bool conTopeJugador) {
    const int libreas = (int)LiveriesForClass(className).size();
    if (libreas <= 0) return 0;                       // class not in the catalog: there's no grid to compose
    int n = market::GridSizeForClass(className);      // the curated value (0 = the class isn't in classes.json)
    if (n <= 0) n = libreas;                          // no data -> the whole catalog, same as before
    if (n > libreas) n = libreas;                     // a car with no livery painted for it never lines up on the grid
    if (conTopeJugador && g_maxGrid > 0 && n > g_maxGrid) n = g_maxGrid;   // the player's cap, BEFORE the floor
    if (n < 2) n = libreas < 2 ? libreas : 2;         // a one-car grid isn't a race
    return n;
}
int GridSizeForClassName(const std::string& className) { return GridSizeForClassNameCapped(className, true); }
int WorldGridSizeForClassName(const std::string& className) { return GridSizeForClassNameCapped(className, false); }

// ...AND THE CAP THE DRIVER POOL ITSELF SETS. See career_fields.h: the actual composition step is asked for the
// class size and its return value is counted -- that's the number of names the XML will actually end up with. It's composed
// WITHOUT multiclass exclusion, on purpose: that exclusion depends on the whole weekend (which classes share the
// track, and in what order), not on the series alone, so folding it in here would make one series' size depend on
// who else is racing. Unknown series or an empty composition -> falls back to the class size, same as before.
// what the pre-generation step actually WROTE into each class's XML, published by whoever wrote it.
// It's NOT cleared by ResetGridSizeCache: the files on disk stay there even after data reloads, and
// that's what AMS2 will actually read. Empty = no pre-generation has run yet -> the pure calculation wins.
static std::map<std::string, int> g_observedXmlNames;

void SetObservedXmlNames(std::map<std::string, int> perClass) { g_observedXmlNames = std::move(perClass); }
int  ObservedXmlNamesCount() { return (int)g_observedXmlNames.size(); }

static const std::map<std::string, int>& XmlNamesPerClass();   // forward declaration: defined next to the startup writer

int GridSizeForSeries(const std::string& seriesId) {
    const SeriesDef* s = SeriesById(seriesId);
    if (!s) return 0;
    const int base = GridSizeForClassName(s->class_name);
    if (base <= 0) return 0;
    int real = (int)ComposeSeasonGrid(seriesId, s->level, /*playerForm=*/0.0, base, /*useHomage=*/false).size();
    // ...AND BOUNDED BY WHAT ITS CLASS'S XML WILL ACTUALLY NAME. Composing this in isolation isn't enough: the file is
    // PER CLASS and the startup step writes it with multiclass exclusion applied, so it can end up with fewer names than
    // this series would compose on its own. It actually happened in three series (harbor_championship_5234 asked for 26 against an XML
    // of 23), and it's exactly the classes that race in TWO series with only one of them inside a group.
    // so the OBSERVED value wins over the calculated one. The table above is composed WITHOUT session context (this layer is
    // pure and knows nothing about seats or the save), but the real XML is written WITH the live world's seat
    // map, which renames drivers and therefore changes who the next class excludes. Measured
    // against a real user save: GT4.xml with 18 names where the pure calculation gave 23. Whatever
    // pre-generation just wrote can't be argued with, so if it exists, it wins.
    const auto obs = g_observedXmlNames.find(s->class_name);
    if (obs != g_observedXmlNames.end() && obs->second > 0)
        return obs->second < real ? obs->second : real;
    const auto& xml = XmlNamesPerClass();
    const auto it = xml.find(s->class_name);
    if (it != xml.end() && it->second > 0 && it->second < real) real = it->second;
    return real > 0 ? real : base;
}

// THE MULTICLASS WEEKEND. See career_fields.h.
static int MeetingClassShareImpl(const std::vector<std::string>& seriesIds, int requestedTotal) {
    const int n = (int)seriesIds.size();
    if (n <= 0) return 0;
    if (n == 1) {
        const int inherited = GridSizeForSeries(seriesIds[0]);
        return requestedTotal >= 2 && requestedTotal <= 60 && requestedTotal < inherited
            ? requestedTotal : inherited;
    }
    // A total that does not leave at least two cars per class is not a valid race: it inherits.
    int total = requestedTotal >= 2 * n && requestedTotal <= 60 ? requestedTotal : 0;
    if (total <= 0) total = MulticlassTotalForSeries(seriesIds[0]);
    if (total <= 0) for (const auto& s : seriesIds) total += GridSizeForSeries(s);   // uncurated: the plain sum (the old behavior)
    int share = total / n;
    // the SHORTEST class's cap governs all of them. Handing out 12 per class when one can only field
    // 2 doesn't produce "12,12,2": the game splits evenly, so it gives 2,2,2. Better to know that here.
    for (const auto& s : seriesIds) { const int c = GridSizeForSeries(s); if (c < share) share = c; }
    // the player's cap is on what's ON TRACK, and in multiclass that's share*n (see
    // MeetingGridSize). The cap is split across the classes so the product never exceeds it. With more classes
    // than cap/2 the split would give fewer than two cars per class: there the floor wins and the total goes over the
    // cap -- that's preferable to fielding a one-car class, and it's a case that doesn't exist in the curated catalog.
    const int cap = MaxGridSize();
    if (cap > 0 && n > 1) { const int per = cap / n; if (share > per) share = per < 2 ? 2 : per; }
    return share > 0 ? share : 0;
}

int MeetingClassShare(const std::vector<std::string>& seriesIds) {
    return MeetingClassShareImpl(seriesIds, 0);
}

int MeetingGridSize(const std::vector<std::string>& seriesIds) {
    const int n = (int)seriesIds.size();
    if (n <= 1) return MeetingClassShare(seriesIds);
    return MeetingClassShare(seriesIds) * n;
}

// the cap of the round's TRACK (contract and reason in career_fields.h) ----------
// The five screens that ask "how many cars are in this round" each repeated this same expression
// on their own; now they do it here, which is also the only place where the circuit cap is applied. If it
// were written by hand again in a sixth place, that place would skip the cap and nobody would notice.
static int RoundBaseShare(const std::vector<std::string>& cita, const std::string& seriesId,
                          int requestedTotal, int* outN) {
    const int n = (int)cita.size() > 1 ? (int)cita.size() : 1;
    *outN = n;
    if (n > 1) return MeetingClassShareImpl(cita, requestedTotal);
    const int inherited = GridSizeForSeries(seriesId);
    return requestedTotal >= 2 && requestedTotal <= 60 && requestedTotal < inherited
        ? requestedTotal : inherited;
}

int RoundClassShare(const std::vector<std::string>& cita, const std::string& seriesId, int trackHash,
                    int requestedTotal) {
    int n = 1;
    int share = RoundBaseShare(cita, seriesId, requestedTotal, &n);
    if (share < 2) return share;            // unresolved series: let the caller decide (its floor of 16)
    const int cap = TrackGridCapacity(trackHash);
    if (cap > 0) {
        const int per = cap / n;            // the part of the track that falls to each class of the meeting
        // the floor of 2 per class takes precedence over the cap, same as in MeetingClassShare: with more classes than
        // slots/2 the distribution would give a one-car race, and that is worse than exceeding the track. It does not exist
        // today in the catalog (the smallest track has 8 slots and no meeting goes past 4 classes).
        if (share > per) share = per < 2 ? 2 : per;
    }
    return share;
}

int RoundGridSize(const std::vector<std::string>& cita, const std::string& seriesId, int trackHash,
                  int requestedTotal) {
    int n = 1;
    const int base = RoundBaseShare(cita, seriesId, requestedTotal, &n);
    if (base < 2) return base;              // ditto: it comes out as is so that the caller applies its floor
    return RoundClassShare(cita, seriesId, trackHash, requestedTotal) * n;
}

// RealFieldCount reports the real drivers ComposeSeasonGrid actually KEEPS (= grid - freed, bounded to
// [0, fieldSize]). It deliberately does NOT apply the floor here: the second invariant compares THIS real value against the
// floor min(8, fieldSize); folding it in here would make that invariant tautological.
int RealFieldCount(const std::string& seriesId, int gridSize) {
    if (gridSize <= 0) return 0;
    int freed = FreedSeatCount(seriesId, gridSize);
    int real = gridSize - freed; if (real < 0) real = 0;
    int fieldSize = (int)FieldForSeries(seriesId).size();
    if (fieldSize > 0 && real > fieldSize) real = fieldSize;   // never more real drivers than actually exist
    return real;
}

// PINNED ROSTERS (series_rosters.psv, binding driver->livery) ----------
static std::vector<PinnedDriver> g_pinned;
// series index -> positions in g_pinned, IN FILE ORDER (= standings order, which is the one that
// numbers the seats). `PinnedRosterForSeries` sweeps the 2,000 rows and returns a copy, and the per-slot seat key
// needs to ask "which livery does seat i carry?" once PER SEAT. Without an index,
// composing the world fingerprint would go from one sweep to a squared sweep.
static std::map<std::string, std::vector<int>> g_pinnedBySeries;

static void ReindexPinned() {
    g_pinnedBySeries.clear();
    for (int i = 0; i < (int)g_pinned.size(); i++) g_pinnedBySeries[g_pinned[i].series_id].push_back(i);
}

bool LoadSeriesRosters(const char* path) {
    ResetGridSizeCache(); ResetGlobalIdentityIndex();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_pinned.clear();
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 9) continue;                       // at least up through livery_name
        if (t[0] == "series_id" || t[0].empty()) continue;
        PinnedDriver p;
        p.series_id = t[0]; p.car_no = t[1]; p.name = CleanNameSpaces(t[2]); p.country = t[3];
        p.skill = atof(t[4].c_str()); p.aggr = atof(t[5].c_str()); p.cons = atof(t[6].c_str());
        p.archetype = t[7]; p.livery_name = t[8];
        p.binding = (t.size() > 9) ? t[9] : std::string();
        p.real = (t.size() > 10) ? (atoi(t[10].c_str()) != 0) : true;
        g_pinned.push_back(p);
    }
    fclose(f);
    ReindexPinned();
    return true;
}

void ReplacePinned(std::vector<PinnedDriver> v) {
    ResetGridSizeCache(); ResetGlobalIdentityIndex(); g_pinned = std::move(v); ReindexPinned(); }

std::vector<PinnedDriver> PinnedRosterForSeries(const std::string& seriesId) {
    std::vector<PinnedDriver> out;                        // file order = standings (do NOT reorder)
    auto it = g_pinnedBySeries.find(seriesId);
    if (it == g_pinnedBySeries.end()) return out;
    out.reserve(it->second.size());
    for (int i : it->second) out.push_back(g_pinned[i]);
    return out;
}

int PinnedRosterCount() { return (int)g_pinned.size(); }

std::string PinnedLiveryAt(const std::string& seriesId, int idx) {
    auto it = g_pinnedBySeries.find(seriesId);
    if (it == g_pinnedBySeries.end() || idx < 0 || idx >= (int)it->second.size()) return std::string();
    return g_pinned[it->second[idx]].livery_name;
}

int PinnedRosterSizeFor(const std::string& seriesId) {
    auto it = g_pinnedBySeries.find(seriesId);
    return it == g_pinnedBySeries.end() ? 0 : (int)it->second.size();
}

// COMPOSITION ----------

// Defending style by the PSV's archetype (AP apex-predator / MT metronome / CH charger / AM gentleman).
static double DefendingForArchetype(const std::string& a) {
    if (a == "AP") return 0.85;
    if (a == "MT") return 0.70;
    if (a == "CH") return 0.62;
    if (a == "AM") return 0.45;
    return 0.80;                                   // GridDriver's default
}

const std::map<std::string, float>& EmptyRivalForm() {
    static const std::map<std::string, float> kEmpty;   // a single stable object -> default parity = base
    return kEmpty;
}

// Real driver->livery pairing (docs/driver_liveries.psv) ----------
static std::map<std::string, std::map<std::string, std::string>> g_driverLivery;   // series_id -> driver -> livery
bool LoadDriverLiveries(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_driverLivery.clear();
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 3 || t[0] == "series_id" || t[0].empty()) continue;
        g_driverLivery[t[0]][CleanNameSpaces(t[1])] = t[2];   // the livery (t[2]) does not: it matches the game byte for byte
    }
    fclose(f);
    return true;
}
void ReplaceDriverLiveries(std::map<std::string, std::map<std::string, std::string>> m) { g_driverLivery = std::move(m); }
std::string DriverLiveryFor(const std::string& seriesId, const std::string& driver) {
    auto s = g_driverLivery.find(seriesId);
    if (s == g_driverLivery.end()) return std::string();
    auto d = s->second.find(CleanNameSpaces(driver));
    return d != s->second.end() ? d->second : std::string();
}
// Sums the sizes of the INNER maps (series -> driver -> livery): counting only g_driverLivery.size()
// would give the number of series with any binding, not the actual number of bindings. Nobody should "simplify" this to .size().
int DriverLiveryCount() {
    int n = 0;
    for (const auto& kv : g_driverLivery) n += (int)kv.second.size();
    return n;
}

// Driver model (genome + affinity + traits) in grid composition ----------
namespace {
// ASCII lowercase, so the family map doesn't depend on how the id happens to be written in the catalog.
std::string LowerAscii(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) o += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
    return o;
}
bool Has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }
bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
} // anon

CarFamily FamilyForClass(const std::string& className) {
    const std::string c = LowerAscii(className);
    // (1) HISTORIC: pre-wing / pre-slick. Goes FIRST because the era matters more than the body shape: an open-wheeler from
    // 1967 and one from 2024 don't drive anything alike, and lumping them into the same family erases exactly the
    // difference the affinity model is meant to capture.
    if (StartsWith(c, "f-vintage") || StartsWith(c, "f-retro") || StartsWith(c, "tc60") ||
        StartsWith(c, "tc70")      || StartsWith(c, "greyham_motors")   || StartsWith(c, "copaclassic") ||
        StartsWith(c, "stanmore_ax671")  || StartsWith(c, "ashwood_motors")  ||
        c == "group a")                                                       return CarFamily::Historic;
    // (2) OPENWHEEL: single-seaters and karts (single seat, no bodywork).
    if (StartsWith(c, "f-") || StartsWith(c, "fe-") || Has(c, "kart"))        return CarFamily::OpenWheel;
    // (3) OVAL: the US stock cars. Ovals are their own discipline (drafting, traffic, barely any real braking).
    if (StartsWith(c, "stock_usa"))                                           return CarFamily::Oval;
    // (4) PROTOTYPE.
    if (Has(c, "pemberton_zel121") || Has(c, "lmp") || Has(c, "dpi") || Has(c, "hypercar") ||
        Has(c, "proto") || c == "group c" || c == "group_c" ||
        c == "p1" || c == "juniper_series_8883" || c == "p2" || c == "p3" || c == "p4")    return CarFamily::Prototype;
    // (5) GT: the GT classes and GT one-make series.
    if (StartsWith(c, "gt") || Has(c, "juniper_trophy") || Has(c, "oldacre_uma401") ||
        StartsWith(c, "g55") || StartsWith(c, "g40"))                         return CarFamily::GT;
    // (6) everything else: touring cars (Brazilian Stock Car, Copa*, Ashmere Motors, TSI, Coldstead Motors...).
    return CarFamily::Touring;
}

TraitBias BiasForArchetype(const std::string& archetype) {
    TraitBias b;
    if (archetype.empty()) return b;             // a real-driver field entry with no archetype -> the pure model, unbiased
    const ArchProfile p = ProfileForArch(archetype);
    const ArchProfile n = ProfileForArch(std::string());   // the neutral "solid" archetype is the reference
    // The bias is the SHIFT relative to neutral, never an absolute value: the level itself is set by talent and
    // the genome, and adding an absolute value would bring back exactly the flat scalar this model exists to kill. It's amplified a
    // little because ApplyBias pushes against the remaining HEADROOM (asymptotically), so a linear shift ends up
    // falling short when applied to a trait that's already high.
    // 26/08 — kGain goes back to 1.0. The 1.25 existed to COMPENSATE for ApplyBias pushing by the slack
    // and falling short; now the bias is added LINEARLY in Compose, so amplifying it would overshoot the rating.
    // An offset of 0.20 relative to `solido` IS 0.20, no more, no less.
    const double kGain = 1.00;
    b.defending       = kGain * (p.def    - n.def);
    b.consistency     = kGain * (p.cons   - n.cons);
    b.avoid_mistakes  = kGain * (p.avoidM - n.avoidM);
    b.avoid_forced    = kGain * (p.avoidF - n.avoidF);
    b.start_reactions = kGain * (p.start  - n.start);
    b.wet_skill       = kGain * (p.wet    - n.wet);
    b.tyre_management = kGain * (p.tyre   - n.tyre);
    b.stamina         = kGain * (p.stam   - n.stam);
    // 25/08 — AGGRESSIVENESS NOW DOES COME FROM THE ARCHETYPE. The previous note said it came authored per
    // driver in the PSV “which is a more specific datum”; the datum NEVER EXISTED — no PSV in the project
    // has an aggressiveness column, so `authoredAggr` never arrived and the bias stayed at zero.
    // Measured on the Highbourne Motors: median 0.200, which is exactly the clip floor. The whole grid
    // stuck to the edge, with no character and not attacking. If one day there is per-driver data, it still takes precedence: it is added
    // on top in ApplyDriverModel, same as consistency.
    b.aggression      = kGain * (p.aggr   - n.aggr);
    // These two go in as an ABSOLUTE TARGET, not a shift: they have no "neutral" to measure
    // against, because they aren't skill, they're race strategy policy (how much you conserve, when you switch to wets).
    // These are what spreads out the pit stops; with them left flat, the whole grid pitted on the same lap.
    // 26/08: defense goes as an ABSOLUTE TARGET (see career_traits.h). The offset is still
    // computed above and TraitsFor ignores it when there is a target; it is kept so as not to touch the neutral.
    // 26/08 — the CENTER of each trait is the NEUTRAL's rating (`solido`), not the model's anchor.
    // It is emitted here because it is the only place that knows the archetype table. See Compose().
    b.consistency_target      = n.cons;
    b.avoid_mistakes_target   = n.avoidM;
    b.avoid_forced_target     = n.avoidF;
    b.start_reactions_target  = n.start;
    b.wet_skill_target        = n.wet;
    b.tyre_management_target  = n.tyre;
    b.stamina_target          = n.stam;
    b.defending_target        = n.def;
    b.fuel_target         = p.fuel;
    b.weather_tyre_target = p.weather;
    return b;
}

double BlueFlagForRole(ClassRole role) {
    // 0.00 for the slow class in a multiclass race (per the official guide's recommendation): it holds its line. With the
    // old default high value, in a real multiclass race the slow class kept moving aside and ended up in the gravel alone.
    if (role == ClassRole::SlowerInMulticlass) return 0.00;
    return 0.95;   // single-class races and the fast class: normal behavior (yield when it's due)
}

// AUTHORED reference values from the PSV: the neutral point a per-driver value shifts the model away from.
constexpr double kAuthoredAggrMid = 0.60;   // GridDriver's default and the PSV's typical value
constexpr double kAuthoredConsMid = 0.85;   // same
constexpr double kAuthoredBiasCap = 0.50;   // an authored value shifts the model, it doesn't dictate it

// 26/08 — A DRIVER'S COMPLETE BIAS, in a single place. Archetype + authored PSV datum +
// the scale cap. It exists as a public function because the suite REPRODUCES this wiring from outside in order to
// check that no piece gets disconnected (test_driver_model_wiring), and while it was loose inside
// ApplyDriverModel every new piece broke that equality and had to be copied into the test. Copying logic into a
// test that exists to watch that same logic watches nothing.
TraitBias BiasForDriver(const std::string& archetype, double authoredAggr, double authoredCons) {
    TraitBias bias = BiasForArchetype(archetype);
    // The AUTHORED per-driver data is more specific than their archetype, so it overrides it (without
    // replacing it: the archetype still sets the character). <=0 = no data -> the model decides alone.
    auto capped = [](double v) { return v < -kAuthoredBiasCap ? -kAuthoredBiasCap : (v > kAuthoredBiasCap ? kAuthoredBiasCap : v); };
    if (authoredAggr > 0.0) bias.aggression  += capped(authoredAggr - kAuthoredAggrMid);
    if (authoredCons > 0.0) bias.consistency += capped(authoredCons - kAuthoredConsMid);
    // 26/08 — THE TOTAL OFFSET CANNOT TAKE THE DRIVER OFF THE SCALE. Archetype and authored
    // datum are ADDED on purpose (the PSV is more specific, but the archetype still marks the
    // character), and added together they overflow: a `cargador` with authored aggressiveness 0.90 asked for 0.32 + 0.30 on
    // a center of 0.50 and landed at 1.017 -> clipped to EXACTLY 1.000, i.e. all the aggressive chargers
    // pinned to the same number. With the earlier asymptotic bias this was not seen because
    // nobody ever reached their rating; now that the offset is linear, it shows.
    // The offset is clipped, NOT the result: clipping the result is exactly what piles people up
    // at the edge. The margins leave room for the trait's own noise, which comes afterwards.
    auto enEscala = [](double centro, double d) {
        const double lo = 0.06 - centro, hi = 0.94 - centro;
        return d < lo ? lo : (d > hi ? hi : d);
    };
    bias.aggression  = enEscala(0.50, bias.aggression);   // its center is the midpoint
    // the real center of consistency is the neutral's rating that BiasForArchetype emits; without an archetype
    // there is no rating and Compose uses its usual anchor (~0.82). The datum is asked, the number is not copied.
    bias.consistency = enEscala(bias.consistency_target > 0.0 ? bias.consistency_target : 0.82,
                                bias.consistency);

    return bias;
}

namespace {

// Fills in one grid driver from the driver model.
//  - identity -> genome (deterministic, no save-file cost)
//  - absolute talent -> affinity by car family -> EFFECTIVE talent in this class
//  - traits -> the factor model, biased by the archetype and by the driver's authored data
// Replaces ApplyAiPersonality on THIS path (that function stays alive for the others). The one thing it doesn't cover
// is reliability, which is assigned to the TEAM: here the archetype's own value is left as a DEFAULT, and
// ApplyCarScalarsToGrid overrides it with the equipment unit's value when the class has that data.
// `identity` = the GENOME key. It is NOT driver_id, and that's not an oversight: the PSV loaders don't mint ids
// (career_customai.h), so in PSV mode `GenomeSeed("")` would give the SAME seed to the whole grid and it would come out
// cloned; and "id if there is one, otherwise the name" breaks PSV<->JSON parity, which is a REAL invariant the test suite
// (test_psv_json_parity: the JSON adapter DOES mint ids, so the two paths would seed distinctly —
// 14,456 divergences measured). The CANONICAL NAME is the only byte-identical identity across both paths.
// Canonical = the one from the data, NEVER the one that ends up on the grid: the tribute swap and the dedup's " 2" suffix
// change `g.name`, and a driver's character can't depend on how they're displayed.
// Once the PSV loaders start minting ids (partially rolled out), this could move to driver_id without breaking parity.
// `baseTalent` = the driver's ABSOLUTE talent in this class (live rating if present, otherwise the authored one).
// `authoredBase` = the AUTHORED skill alone: what ApplyAiPersonality used to receive, and the only thing that
// reliability still hangs off of — it must not move with the driver's live form.
void ApplyDriverModel(GridDriver& g, const std::string& identity, const std::string& archetype,
                      double baseTalent, double authoredBase,
                      CarFamily family, ClassRole role, double authoredAggr, double authoredCons,
                      double arco = 1.0) {
    const uint64_t seed = GenomeSeed(identity);
    const double talent = TalentInFamily(seed, baseTalent, family);

    TraitBias bias = BiasForDriver(archetype, authoredAggr, authoredCons);

    // if this person is in the calibration set, their CRAFT is not drawn — it is measured. It only
    // replaces the ~172 that we truly have measured and that run; the rest comes out byte for byte the same as before.
    double craftMedido = 0.0;
    const double* craftPtr = nullptr;
    { float c = 0.0f;
      if (MeasuredCraftFor(std::string(), identity, c)) { craftMedido = (double)c; craftPtr = &craftMedido; } }
    const TraitVector tv = TraitsFor(seed, talent, bias, craftPtr);
    g.talent = talent;                       // consumed by BuildGridXml to anchor the grid's ace
    // The starting one, before form and affinity move it. `authoredBase` is the authored skill
    // plain; `baseTalent` already carries the form inside, so the reference one is the first.
    g.authored_talent = (authoredBase > 0.0) ? authoredBase : baseTalent;
    g.race_skill = tv.race_skill;            // traits on a TALENT scale; BuildGridXml converts them to XML
    g.qualifying = tv.qualifying;            // its difference from race_skill = the shift toward Saturday (qualifying) pace
    // Aug 9 — racecraft is now CAPPED per discipline (career_traits.h::RacecraftFor). The model still
    // decides who's more aggressive and who defends better; what changes is it no longer strays outside the
    // range where overtaking is possible. See the long RacecraftBounds block: we had 263 drivers
    // with 0.000 aggression and 56% defending above 0.50.
    {
        const RacecraftBounds rb = RacecraftFor(family);
        g.aggression = AggressionInBand(tv.aggression, rb);   // gets capped
        g.defending  = DefendingInBand(tv.defending,  rb);    // gets RESCALED (see career_traits.h)
    }
    g.consistency = tv.consistency;
    g.stamina = tv.stamina;
    g.start_reactions = tv.start_reactions;
    g.wet_skill = tv.wet_skill;
    g.tyre_management = tv.tyre_management;
    g.avoidance_of_mistakes = tv.avoid_mistakes;
    g.avoidance_of_forced_mistakes = tv.avoid_forced;
    // ~~~08/09 (D3, Josh's decision): THE AGE CURVE, EXPLICIT, OVER THE SIX QUALITY CONTROLS, IN
    //  PRODUCTION. `arco` is the multiplier that already scaled the talent (rise x decline, or the anchored arc
    //  of a real driver); the six controls are scaled by `TraitCurveFactor(arco)` = (1 - weight) + weight x arco with
    //  `bal::TRAIT_CURVE_WEIGHT` (0.5; the hook `g_traitCurveMedicion` >= 0 overrides it for measuring). A SINGLE
    //  curve for the seven axes: measured on the corpus, no control peaks later than pace. The
    //  character ones and reliability are not touched.
    if (arco > 0.0 && arco < 1.0) {
        const double f = TraitCurveFactor(arco);
        g.consistency *= f; g.wet_skill *= f; g.tyre_management *= f; g.stamina *= f;
        g.avoidance_of_mistakes *= f; g.avoidance_of_forced_mistakes *= f;
    }
    // Jul 27: no longer flat — the traits model derives them from the archetype (see career_traits.cpp).
    g.fuel_management = tv.fuel_management;                  // fuel management -> pit window
    g.weather_tyre_changes = tv.weather_tyre;                // accuracy of the call to switch to wets -> the lap it happens on
    // Aug 9 — blue flags PINNED per discipline (Curve's ruleset). They were flat at 0.950, which is
    // "the lapped car moves aside instantly". In MULTICLASS the ROLE decides (an LANTERN_MASTERS_7489 lapping a GT doesn't negotiate),
    // so BlueFlagForRole is kept there; in single-class it falls back to the discipline's constant.
    g.blue_flag_conceding = (role == ClassRole::SingleClass) ? RacecraftFor(family).blue
                                                             : BlueFlagForRole(role);
    // reliability DEFAULT: the archetype (how the driver treats their car). It gets OVERWRITTEN afterward by
    // ApplyTeamCarReliability when the driver's equipment unit has data — the spec says the owner of
    // vehicle_reliability is the TEAM. With no team data, this number is what reaches the XML, same as before.
    g.reliability = ReliabilityForArchetype(archetype, authoredBase);
}
} // anon

// The season used to resolve docs/career/team_cars.psv is the one that REPRESENTS the series (its era:
// "1987" for Winston Cup HARBOR_CHALLENGE_8227), not the world clock's year: a period series runs its grid
// from its own era no matter what year it is outside. A non-numeric era ("modern") -> 0, so only '*' rows match,
// which is what every contemporary series carries.
static int SeasonYearForSeries(const SeriesDef* s) {
    if (!s) return 0;
    int year = 0, digits = 0;
    for (char c : s->era) {
        if (c >= '0' && c <= '9') { year = year * 10 + (c - '0'); if (++digits == 4) break; }
        else if (digits > 0) break;
    }
    return digits == 4 ? year : 0;
}

// Resolves the equipment unit against the VANILLA livery_name and leaves the three
// scalars on the GridDriver. This happens HERE, at the close of composition, because RebindGridForSkins rewrites
// livery_name to the installed skin's name: if resolved afterward, "Highstead Racing 15458 " would already be called "Fernando
// Alonso ", its stem would be the driver's name and every car would fall into its own unit. The team's
// spread would disappear SILENTLY, and only for players with driver packs installed — a bug that doesn't reproduce in development.
static void ApplyCarScalarsToGrid(const SeriesDef* s, std::vector<GridDriver>& grid) {
    if (!s) return;
    const int season = SeasonYearForSeries(s);
    ApplyTeamCarScalars(s->id, s->class_name, season, grid);
    // RELIABILITY is also owned by the team, and it lives here for the same reason and with the same key —
    // the VANILLA livery_name. If the unit resolves, the team's data wins and teammates share
    // the number; with no data (class has no row, unit not authored, files missing), each driver keeps
    // whatever their archetype set in ApplyDriverModel. See the comment on ReliabilityForStrength: this does NOT
    // re-center on the grid, unlike the scalars.
    ApplyTeamCarReliability(s->id, s->class_name, season, grid);
}

// Bench switch for precondition C.5 (see career_fields.h). OFF = production intact.
bool g_seedIdResolveOccupant = true;
bool g_benchCohortHomeOnly = false;   // bench of the /E (see career_fields.h); production always false
// CLOSED (27/08) — `g_gridFromSeats` REMOVED, and with it the `rebindFromSeats` bridge.
// The grid is ALWAYS built from the seats when there are any; the static path remains ONLY
// for those who do not have them, which is the period memory. Measured with `--seedlab` BEFORE removing it: the
// rollback reintroduced 310 academy rows above their level and 1 class of 98 where the
// cohort LEADS (0.7.0 data; in f0 they were 326 and 6 of 100), against 0 and 0 on the path through seats.
// That is, the switch was not a safety: it was the pathology that cost the five reverted attempts
// of 09/08, one bool away.
bool g_formPerturbsCapacity = true;   // /ON 12/08. See career_fields.h.
double g_traitCurveMedicion = -1.0;   // ~~~07/09: see career_fields.h (<0 = production TRAIT_CURVE_WEIGHT)
double TraitCurveFactor(double arco) {
    const double peso = g_traitCurveMedicion >= 0.0 ? g_traitCurveMedicion : bal::TRAIT_CURVE_WEIGHT;
    if (peso <= 0.0 || arco <= 0.0 || arco >= 1.0) return 1.0;
    return (1.0 - peso) + peso * arco;
}

// why the last composition by seats left a slot out. See career_fields.h.
static GridDropReport g_drops;
const GridDropReport& LastGridDrops() { return g_drops; }

namespace {
// The id with which a row's ELO is LOOKED UP. With the switch OFF it is the raw one from the data, which is what
// production does today; ON, the shared resolution. A single place for both branches.
std::string RatingIdOf(const FieldDriver& fd)  { return g_seedIdResolveOccupant ? FieldOccupantId(fd)  : fd.id; }
std::string RatingIdOf(const PinnedDriver& pd) { return g_seedIdResolveOccupant ? PinnedOccupantId(pd) : pd.id; }

// — THE PERSON BEHIND AN OCCUPANT, and finally in a single place.
//
// This resolution already existed, but scattered: `pushReal` did it for the field, the pinned block for
// its own, the cast graft for the cohort and the defunct `rebindFromSeats` again for the one that arrived
// four copies with four similar but not identical precedences. With the grid coming from the seats,
// all that arrives is a `driver_id`, so the resolution has to be ONE and be declared.
//
// `talent` = the person's ABSOLUTE capability, or 0 if they have no card anywhere. The 0 is NOT a failure:
// it means “the class curve decides in this car”.
//
// 18/08 — WHO REMAINS AT 0 TODAY, WHICH IS NO LONGER WHO THIS NOTE SAID. The sentence that was here —“it is what
// the rebind does today with the generated tail (its IDENTITY is changed, not its PACE)”— stopped describing
// this path: the generated tail (step 6) DOES carry its own card since this plan. What stays at 0 are
// the LIVERY PACKS (step 5), and on purpose (their `race_skill` is POSITIONAL, not absolute
// capability). Measured over twenty seasons: 160 of 2,044 grid rows, 100% of them from packs.
// The STATIC path no longer crosses identity: the `rebindFromSeats` bridge was removed with the .
struct SeatPerson {
    std::string name, country, archetype, kind;
    double talent = 0.0;                 // 0 = no authored capability -> the class curve decides
    double aggr = 0.0, cons = 0.0;       // 0 = no per-driver data -> the archetype decides
    bool resolved = false;               // false = we do not even have a name: the row cannot be written
    bool cloned = false;                 // the card came from the corpus (RookieProfileTalentCloned), not from authoring
};

// Per-series indices, built ONCE per composition: walking them per seat would be quadratic.
struct SeatPeopleIndex {
    std::map<std::string, const PinnedDriver*> pinned;
    std::map<std::string, const FieldDriver*>  field;
    std::map<std::string, const RosterDriver*> cast;
};

// ===================================================================================================
// 16/09 — WHO THIS ID IS WHEN **ITS** SERIES DOES NOT KNOW IT (the GLOBAL identity index)
// ===================================================================================================
//
// The first two steps of `PersonForOccupant` are LOCAL to the series being composed: the
// pinned roster of that series and its field. And the ids of people who only exist there are an FNV hash of the name
// (`PinnedOccupantId` / `FieldOccupantId`, when the name is not in the pool). The result, measured with
// `--sinnombre` over twenty seasons: as soon as the market moves one of those people to ANOTHER series,
// in the new series nobody knows them — not the pinned roster, nor the field, nor the pool, nor the cohort, nor the
// packs — and their row **cannot be written**. Their seat exists, their car exists, and the car does not show up.
//
// There are two populations, and the big one is not the one it seemed:
//   · **108 of 115 rows at T20 are the AUTHORED RESERVE**: `career_registry` registers everyone who has
//     a card in `f3_authored_profiles.psv` and does not run on any grid today, minting their id with
//     `FieldOccupantId` over a `FieldDriver` without a country. `ReservaLibrePara` seats them, which is exactly what
//     the reserve exists for, and here nobody knew them.
//   · **7 of 115 come from the pinned roster or the field of another series** (Mauricio Gugelmin, from `pinnacle_championship_1503` of
//     period, seated in `cinder_series`; Laurents Horr, from the field of `ember_challenge_7038`, seated in `granite_series_5316`).
//
// WHY IT IS ADDITIVE AND CANNOT BREAK ANYTHING: this is consulted AFTER the usual five steps
// (pinned roster and field of the series, pool, cohort, packs) and before the last one, which only resolves generated
// ones (`gn_*`/`fl_*`, ids that this table never contains). So the only thing that can change is rows that
// are thrown away whole today.
//
// WHY IT REQUIRES A COUNTRY: AMS2 SILENTLY discards a `<country>` that its table does not know and the driver comes out with
// the wrong flag (). Resolving the name without a flag would swap “does not appear” for “appears wrong”, so
// whoever brings no country stays out — on purpose, and counted. The pinned roster and the field always bring a country; the
// corpus brings it from column 9 (see `AuthoredProfile::country`).
struct IdentidadGlobal {
    std::string name, country, archetype;
    double talent = 0.0, aggr = 0.0, cons = 0.0;
    bool real = true;
};
static std::map<std::string, IdentidadGlobal> g_idGlobal;
static bool g_idGlobalHecho = false;

// The public reset lives OUTSIDE this anonymous namespace (further down): in here it would be another function
// distinct from the one career_fields.h declares, and the call would be ambiguous.
static void InvalidarIdentidadGlobal() { g_idGlobal.clear(); g_idGlobalHecho = false; }

static const std::map<std::string, IdentidadGlobal>& IndiceGlobalDeIdentidad() {
    if (g_idGlobalHecho) return g_idGlobal;
    g_idGlobal.clear();
    // (a) The pinned roster and the field of ALL the series. The first one that resolves an id wins: the same person
    //     in two series is ONE, same as `LoadPackDrivers` does with `g_packById`.
    for (const auto& sd : AllSeries()) {
        for (const auto& pd : PinnedRosterForSeries(sd.id)) {
            const std::string id = PinnedOccupantId(pd);
            if (id.empty() || pd.country.empty() || g_idGlobal.count(id)) continue;
            IdentidadGlobal g;
            g.name = pd.name; g.country = pd.country; g.archetype = pd.archetype;
            g.talent = pd.skill; g.aggr = pd.aggr; g.cons = pd.cons; g.real = pd.real;
            g_idGlobal[id] = g;
        }
        for (const auto& fd : FieldForSeries(sd.id)) {
            const std::string id = FieldOccupantId(fd);
            if (id.empty() || fd.country.empty() || g_idGlobal.count(id)) continue;
            // Same rule as step (2) of `PersonForOccupant`: the authored card takes precedence over the PSV
            // curve, and `cons` stays at the sentinel of “no per-driver data”.
            const AuthoredProfile* ap = AuthoredProfileFor(fd.name);
            IdentidadGlobal g;
            g.name = fd.name; g.country = fd.country;
            g.archetype = ap ? ap->archetype : std::string();
            g.talent = ap ? ap->talent : fd.skill;
            g.aggr = fd.aggr; g.cons = kAuthoredConsMid;
            g_idGlobal[id] = g;
        }
    }
    // (b) The AUTHORED RESERVE, with the SAME minting that `career_registry::Load` uses to register it
    //     (a `FieldDriver` with only the name). Replicating it any other way would create the second identity that
    //     the whole exists to prevent.
    for (const auto& kv : AllAuthoredProfiles()) {
        const AuthoredProfile& ap = kv.second;
        if (ap.name.empty() || ap.country.empty()) continue;
        FieldDriver fake; fake.name = ap.name;
        const std::string id = FieldOccupantId(fake);
        if (id.empty() || g_idGlobal.count(id)) continue;
        IdentidadGlobal g;
        g.name = ap.name; g.country = ap.country; g.archetype = ap.archetype;
        g.talent = ap.talent; g.cons = kAuthoredConsMid; g.real = ap.is_real;
        g_idGlobal[id] = g;
    }
    // (c) 17/09 — AND THE 0.6.8 MEMORY, LAST. `docs/career/seat_index_068.psv` froze the
    //     census of who existed when those saves were written, with name and country. It goes LAST because it is
    //     the OLDEST source: any datum of today takes precedence over it (guaranteed by the `count(id)` guard), so
    //     it only ends up registering whoever 0.7.0 removed from the live data and is still seated in a save.
    //
    //     Measured on the real eight-title game save: a single id, `rd_kevin_magnussen`, seated in
    //     Pinnacle_Masters by a signing of the market of that time. Their row was thrown away whole —no step
    //     knew their name anymore— and AMS2 put a generic in their car. With this they come out with their name and
    //     flag. It is not a new pool of people: the scope is that of the table, 212 rows of 9 series.
    //
    //     Talent 0 on purpose, same as the livery packs (step 5 of `PersonForOccupant`): from here comes
    //     the IDENTITY and the pace is set by the curve of their car. If they also keep an authored card, the card
    //     takes precedence, which is the same rule the field applies in (a).
    for (const auto& kv : Identidades068()) {
        const std::string& id = kv.first;
        if (id.empty() || kv.second.second.empty() || g_idGlobal.count(id)) continue;
        const AuthoredProfile* ap = AuthoredProfileFor(kv.second.first);
        IdentidadGlobal g;
        g.name = kv.second.first; g.country = kv.second.second;
        g.archetype = ap ? ap->archetype : std::string();
        g.talent = ap ? ap->talent : 0.0;
        g.cons = kAuthoredConsMid;
        g_idGlobal[id] = g;
    }
    g_idGlobalHecho = true;
    return g_idGlobal;
}

// The seed of a generated driver lives INSIDE its id: `gn_<year>_<seed>`. It is recovered from there so that its
// card comes out of the SAME draw as its rating.
//
// 18/08 -- AND THAT ONLY HOLDS FOR THE `gn_*`. IT IS NOT A GUARANTEE, IT IS A MAJORITY.
// The argument with which this plan was justified is “the same person, the same place on both scales”, and
// it rests entirely on recovering the SAME seed that `CascadeGenerateRookies` used for the rookie's rating
// (career_seats.cpp: `seed = seatFnv1a(career_seed|year|seat)` -> id `gn_<year>_<seed>` -> talent
// by `RookieTalentCloned(seed)`). For those, the card percentile and the rating percentile are THE SAME.
// The `fl_*` are NOT. The per-series tail is called `fl_<series>#<i>` (career_seats.cpp::TailOccupantId) and carries no
// seed at all: what is behind the last '_' contains the '#<i>', fails the digit filter, and ends
// up hashing the WHOLE ID. Their card is still deterministic and stable -- which is what is needed in order
// to store nothing in the game save -- but its percentile does not share a draw with any rating of theirs, because an
// `fl_*` has no seeded rating at all. The only thing it IS correlated with is its name and
// its country, and by accident: `EnsureTailNames` draws them with `seatFnv1a(id)`, which is byte for byte the same
// FNV-1a as the `FieldsFnv1a(id)` down below.
// Closing it would require putting the seed inside the tail's id, and THAT would migrate saves: it would change the card
// of every `fl_*` already seated. It is left NOTED, not fixed.
//
// The FNV-1a is a LOCAL copy in this TU (same algorithm as `career_growth.cpp::GrowthFnv1a`, which lives in
// an anonymous namespace and cannot be linked from here) -- the same pattern already used by seatFnv1a/candFnv1a/
// profFnv1a/memFnv1a: it avoids the ODR of having the symbol repeated in several translation units.
static unsigned long FieldsFnv1a(const std::string& s) {
    unsigned long h = 2166136261ul;
    for (char c : s) { h ^= (unsigned char)c; h *= 16777619ul; }
    return h;
}
}   // namespace

// 16/09 — the reset of the GLOBAL IDENTITY INDEX, visible to the loaders (career_fields.h).
void ResetGlobalIdentityIndex() { InvalidarIdentidadGlobal(); }

// 18/08 — EXPOSED (no longer `static`/anonymous): the harness needs to reproduce the SAME seed that decides the
// cloned card to verify that nobody enters above their ceiling (declared in career_fields.h). It is taken out
// of the anonymous namespace for that reason — inside, even if `static` were removed, it would keep internal linkage and
// career_test.cpp could not call it.
unsigned long SeedFromGeneratedId(const std::string& id) {
    const size_t u = id.rfind('_');
    if (u != std::string::npos && u + 1 < id.size()) {
        const std::string cola = id.substr(u + 1);
        if (!cola.empty() && cola.find_first_not_of("0123456789") == std::string::npos)
            return (unsigned long)strtoul(cola.c_str(), nullptr, 10);
    }
    return FieldsFnv1a(id);
}

namespace {
static SeatPerson PersonForOccupant(const std::string& id, const SeatPeopleIndex& ix) {
    SeatPerson p;
    if (id.empty()) return p;

    // 1) The PINNED roster of this series: the most specific authorship that exists (driver, livery and number
    //    curated by hand for this specific season).
    const auto itp = ix.pinned.find(id);
    if (itp != ix.pinned.end()) {
        const PinnedDriver& pd = *itp->second;
        p.name = pd.name; p.country = pd.country; p.archetype = pd.archetype;
        p.talent = pd.skill; p.aggr = pd.aggr; p.cons = pd.cons;
        p.kind = "real"; p.resolved = true;
        return p;
    }
    // 2) The FIELD of this series. Its authored card takes precedence over the PSV curve (capability belongs to the
    //    person, the curve belongs to the class). `cons` stays at the sentinel of "no per-driver data" — putting
    //    the archetype's here would count it twice, which is the old note of `pushReal`.
    const auto itf = ix.field.find(id);
    if (itf != ix.field.end()) {
        const FieldDriver& fd = *itf->second;
        const AuthoredProfile* ap = AuthoredProfileFor(fd.name);
        p.name = fd.name; p.country = fd.country;
        p.archetype = ap ? ap->archetype : std::string();
        p.talent = ap ? ap->talent : fd.skill;
        p.aggr = fd.aggr; p.cons = kAuthoredConsMid;
        p.kind = "real"; p.resolved = true;
        return p;
    }
    // 3) The global POOL (drivers.json). It is where everyone the market has moved from another series enters.
    if (const PoolDriver* pd = DriverById(id)) {
        const AuthoredProfile* ap = AuthoredProfileFor(pd->name);
        p.name = pd->name; p.country = pd->country;
        p.archetype = ap ? ap->archetype : pd->growth_archetype;
        p.talent = ap ? ap->talent : pd->base_skill;
        p.aggr = pd->base_aggression; p.cons = pd->base_consistency;
        p.kind = (pd->kind == "roster") ? "roster" : "real";
        p.resolved = true;
        return p;
    }
    // 4) The COHORT, which is not in the pool.
    const auto itc = ix.cast.find(id);
    if (itc != ix.cast.end()) {
        const RosterDriver& rd = *itc->second;
        p.name = rd.name; p.country = rd.country; p.archetype = rd.archetype;
        p.talent = rd.base_skill; p.aggr = rd.aggression; p.cons = rd.consistency;
        p.kind = "roster"; p.resolved = true;
        return p;
    }
    // 5) The LIVERY PACKS (): real people, with name and country, who cover the slots that the
    //    curated data does not manage to cover. Talent 0 on purpose — their pack `race_skill` is POSITIONAL
    //    (`stat_source=curve` in 1,981 of 2,330 rows), so the pace is set by the curve of their car. Their
    //    archetype comes from the authored corpus if they have it, and if not from the genome of their name, like everyone.
    if (const PackDriver* pk = PackDriverById(id)) {
        const AuthoredProfile* ap = AuthoredProfileFor(pk->name);
        p.name = pk->name; p.country = pk->country;
        p.archetype = ap ? ap->archetype : std::string();
        if (ap && ap->talent > 0.0) p.talent = ap->talent;   // if they also have a card, their capability decides
        p.cons = kAuthoredConsMid;
        p.kind = "real"; p.resolved = true;
        return p;
    }
    // 5b) 16/09 — THE GLOBAL INDEX: whoever is not from THIS series but is from some series, and the AUTHORED RESERVE.
    //     See the long comment on `IndiceGlobalDeIdentidad`. Additive: the only thing that gets this far is
    //     rows that were previously thrown away whole.
    {
        const std::map<std::string, IdentidadGlobal>& gi = IndiceGlobalDeIdentidad();
        const auto itg = gi.find(id);
        if (itg != gi.end()) {
            const IdentidadGlobal& g = itg->second;
            p.name = g.name; p.country = g.country; p.archetype = g.archetype;
            p.talent = g.talent; p.aggr = g.aggr; p.cons = g.cons;
            p.kind = g.real ? "real" : "roster";
            p.resolved = true;
            return p;
        }
    }
    // 6) THE GENERATED TAIL IS ALSO PEOPLE (`fl_*`, `gn_*`). It has a registered name and, since 18/08,
    //    its OWN card: it is cloned from the authored corpus (the block below). Until that date the talent
    //    stayed at 0 and was set by the car's curve. The STATIC path no longer re-binds occupants (the
    //    `rebindFromSeats` bridge was removed with the ), so there is no other copy of this rule left to touch.
    const std::string nombre = DriverDisplayName(id);
    if (!nombre.empty() && nombre != id) {
        p.name = nombre; p.kind = "roster"; p.resolved = true;
        // (b9874331): the flag travels WITH the name. In 0.6.8 it was set by `rebindFromSeats`, which was
        // removed; without this line the generated tail reached the XML WITHOUT a country and AMS2 SILENTLY discards an empty
        // <country>. The registry knows the generated driver's country; if it does not (legacy save), "".
        p.country = DriverDisplayCountry(id);
        // 18/08 — THEIR CARD COMES FROM THE CORPUS, NOT FROM THEIR SEAT. Without this `p.talent` stayed at 0 and the
        // caller fell to the slot's CURVE point -- the pace of the CAR. Measured: over twenty seasons that was
        // 65% of the rows, and in 41 of 90 categories the grid's ace ended up being one of these, as much as
        // 0.151 above the best driver with a card (the WHOLE spread of cards in an F1 measures 0.11).
        // The seed comes from the id itself, so the number is stable forever: there is nothing to store in the
        // game save nor any save to migrate.
        if (p.talent <= 0.0) {
            const double ficha = RookieProfileTalentCloned(SeedFromGeneratedId(id));
            if (ficha > 0.0) { p.talent = ficha; p.cloned = true; }
        }
        return p;
    }
    return p;   // unresolved: not even a name. The row is not written and is counted.
}
}   // namespace

// defined further down (it needs to call the static composition to get the class CURVE).
// /— A DRIVER'S EFFECTIVE PACE: form PERTURBS capability, it no longer replaces it.
//
// Before, this lived in TWO identical lambdas, one per composition path. And that duplication is exactly what
// left the fix INERT the first time: the static-path one was changed and production composes from seats,
// so nothing changed and the suite stayed green. A single implementation, and both paths call it.
//
// `autorado` is the capability (card talent or class curve); `driverRatings` is what is OBSERVED. Before, the whole
// `RatingQuality(elo)` was returned, and since `SeedAllDrivers` seeds the ELO at the start of the game, the
// authorship was ALWAYS lost — swapped for a seed that had come out of itself. It is the root of the “detached
// ace” (): with pace hanging off the ELO, whoever drives a fast car becomes a better PERSON.
//
// Without a race map the previous behavior is kept (SUBSTITUTION BY ELO), so that callers that
// do not pass it yet do not change result.
// The mean and standard deviation of `RatingQuality` over the LIVE population. It is MEASURED instead of hard-coded because
// the world changes: at the start they are 0.451/0.237 and by twenty seasons they no longer are. It is the ORIGIN scale from
// which talent is projected.
struct EloScale { double media = 0.451, sd = 0.237; bool ok = false; };

static EloScale MeasureEloScale(const std::map<std::string, double>* driverRatings) {
    EloScale e;
    if (!driverRatings || driverRatings->size() < 30) return e;
    double s = 0.0;
    for (const auto& kv : *driverRatings) s += RatingQuality(kv.second);
    e.media = s / driverRatings->size();
    double a = 0.0;
    for (const auto& kv : *driverRatings) { const double d = RatingQuality(kv.second) - e.media; a += d * d; }
    e.sd = std::sqrt(a / driverRatings->size());
    if (e.sd < 1e-4) e.sd = 1e-4;
    e.ok = true;
    return e;
}

// ~~~04/09: `edad` < 0 = unknown -> the usual form cap, bit for bit.
static double EffectivePace(const std::string& id, double autorado,
                            const std::map<std::string, double>* driverRatings,
                            const std::map<std::string, int>* driverRaces,
                            const EloScale& escala, int edad = -1) {
    if (!driverRatings || id.empty()) return autorado;
    const auto it = driverRatings->find(id);
    if (it == driverRatings->end()) return autorado;      // no rating -> the usual curve
    double q = RatingQuality(it->second);                  // 0..1 logistic
    if (q < 0.05) q = 0.05; if (q > 1.0) q = 1.0;
    // 18/08 — THE TWO CUTS ARE DIFFERENT THINGS, and they were fused into a single `return q`.
    //
    // What this comment said —“BENCH SWITCH, and OFF in production”— was FALSE since
    // 12/08: `g_formPerturbsCapacity` is `true` thirty lines above (career_fields.cpp:741). The
    // sentence survived the switch-on and already cost a wrong diagnosis during the grid-order
    // plan, so it is corrected here and in the header of the test that repeated it.
    //
    // And turning the switch OFF did not remove the form: it returned `q`, i.e. the OLD behavior —
    // the WHOLE `RatingQuality(ELO)` replacing the authored capability—, which is exactly the opposite of
    // what the switch's name promises and left without a baseline the “no form” measurement that the next
    // phase needs. In OFF the AUTHORED value is returned: zero perturbation.
    if (!g_formPerturbsCapacity) return autorado;
    // Without a race map `q` IS kept, and that is deliberate: it is the compatibility contract DECLARED
    // in the header above and in career_fields.h (“nullptr = SUBSTITUTION BY ELO, the behavior from before,
    // so that callers that do not pass it yet do not change result”). There are two live ones that do not
    // pass it —the census in `career_canon.cpp` and `career_simrunner.cpp`— and their numbers are written into two
    // specs and test thresholds: changing their result through the back door would be moving an instrument
    // without re-deriving what it feeds. The distinction is the key: `nullptr` means “I do not know how much
    // these people have raced”, and OFF means “I do not want form”.
    if (!driverRaces) return q;
    int carreras = 0;
    const auto ir = driverRaces->find(id);
    if (ir != driverRaces->end() && ir->second > 0) carreras = ir->second;
    const double confianza = (double)carreras / (double)(carreras + bal::FORM_CONFIDENCE_WINDOW);
    // TO THE TALENT SCALE BEFORE SUBTRACTING. Measured, the two magnitudes live in different places
    // (talent 0.859/0.080 · RatingQuality 0.451/0.237), so subtracting them raw gave a fixed bias of 0.41
    // for EVERYONE instead of a per-form deviation. Standardizing the ELO over the live population and
    // projecting it onto (TALENT_MEAN, TALENT_SD) turns the subtraction into what the formula means: “does it perform
    // above or below what its authorship predicts?”.
    const double z = (q - escala.media) / escala.sd;
    const double resultadosEnEscalaTalento = bal::TALENT_MEAN + z * bal::TALENT_SD;
    double delta = (resultadosEnEscalaTalento - autorado) * confianza;
    // ~~~04/09 (, 2nd half): the cap WIDENS with how much growth they have left. At 27 or more,
    //  and with no known age, it is worth exactly the usual one.
    double tope = bal::FORM_RATING_CAP;
    if (edad >= 0) {
        double queda = (1.0 - AgeRisePart(edad, 0)) / (1.0 - bal::AGE_RISE_FLOOR);
        if (queda < 0.0) queda = 0.0;
        if (queda > 1.0) queda = 1.0;
        tope *= 1.0 + bal::FORM_CAP_YOUTH_K * queda;
    }
    if (delta >  tope) delta =  tope;
    if (delta < -tope) delta = -tope;
    double ef = autorado + delta;
    if (ef < 0.05) ef = 0.05; if (ef > 1.0) ef = 1.0;
    return ef;
}

static std::vector<GridDriver> ComposeGridFromSeats(const std::string& seriesId, int tier, int gridSize,
                                                    const std::map<std::string, float>& rivalForm,
                                                    const std::set<std::string>* exclude,
                                                    const std::map<std::string, double>* driverRatings,
                                                    ClassRole role, const std::vector<SeatRow>& seats,
                                                    const std::set<std::string>* skipLiveries,
                                                    const std::map<std::string, int>* driverRaces,
                                                    // 18/08 — the clone enters where it is (career_fields.h).
                                                    const std::map<std::string, double>* driverCurve = nullptr,
                                                    const std::map<std::string, int>* birthYear = nullptr,
                                                    int worldYear = 0);

void ClampGridNoise(std::vector<GridDriver>& grid) {
    // 18/08 — THE GUARD IS `> 0.0`, NOT `>= 0.0`. It is the same one used by the rest of the code that reads this
    // field (ApplyDriverModel writes it with `authoredBase > 0.0`, and the tests count rows “with authorship”
    // with `> 0.0`), but here it was written as `< 0.0` and left an exact 0.0 inside. Today it is unreachable
    // —nobody produces an authored 0.0— and that is why no test saw it; the day it appeared, that 0.0
    // would enter the spread, drag the `lo` floor to zero, inflate the limit to ~0.25 of talent and the
    // clamp would go SILENT for the whole grid, quietly. A boundary discrepancy that is only
    // paid for when it is too late: it is closed now, which costs one line.
    double lo = 2.0, hi = -2.0;
    for (const auto& d : grid) {
        if (d.authored_talent <= 0.0) continue;           // no data: neither touched nor counted
        if (d.authored_talent < lo) lo = d.authored_talent;
        if (d.authored_talent > hi) hi = d.authored_talent;
    }
    const double span = hi - lo;
    if (span <= 0.0) return;                              // authorship all the same: there is no spread to preserve
    const double limite = bal::NOISE_SPAN_FRAC * span;
    for (auto& d : grid) {
        if (d.authored_talent <= 0.0) continue;
        const double delta = d.talent - d.authored_talent;
        if (delta >  limite) d.talent = d.authored_talent + limite;
        if (delta < -limite) d.talent = d.authored_talent - limite;
        if (d.talent < 0.0) d.talent = 0.0;
        if (d.talent > 1.0) d.talent = 1.0;
    }
}

std::vector<GridDriver> ComposeSeasonGrid(const std::string& seriesId, int tier,
                                          double playerForm, int gridSize, bool useHomage,
                                          const std::map<std::string, float>& rivalForm,
                                          const std::set<std::string>* exclude,
                                          const std::map<std::string, double>* driverRatings,
                                          ClassRole role,
                                          const std::vector<SeatRow>* seats,
                                          const std::set<std::string>* skipLiveries,
                                          const std::map<std::string, int>* driverRaces,
                                          const std::map<std::string, double>* driverCurve,
                                          const std::map<std::string, int>* birthYear,
                                          int worldYear) {
    std::vector<GridDriver> grid;
    const SeriesDef* s = SeriesById(seriesId);
    if (!s) return grid;
    // EVERY series that has seats is built from the seats, and the one that does not
    // keeps its static curated grid. And whoever lacks them is exactly the PERIOD MEMORY,
    // which `SeatsForClass` leaves empty on purpose: it is frozen and takes part in neither contracts nor market.
    //
    // 11/08 — THE GUARD WAS `IsModernSeries` AND IT WAS WRONG, and the measurement said so: 32 period classes came out
    // with a cohort, **184 FILL rows** against only 8 of the “2 guests” the user decided on
    // 02/07. is explicit — “the cohort never fills a historic with no data” — and the static path
    // let it in because it calls `RosterForTier` with nothing more. A period series that DOES have a market (`cinder_cup_3102`, GARROWBY
    // 2000) is a live series that runs old cars, not a memory: its grid comes from its seats, and
    // there the cascade is already the right one (field -> pack -> the series' own people, cohort never above
    // its level).
    if (seats && !seats->empty()) {
        auto desdeAsientos = ComposeGridFromSeats(seriesId, tier, gridSize, rivalForm, exclude, driverRatings,
                                                  role, *seats, skipLiveries, driverRaces,
                                                  driverCurve, birthYear, worldYear);
        ClampGridNoise(desdeAsientos);          // the noise cannot measure more than the grid
        return desdeAsientos;
    }
    // The STATIC path is what keeps the SEATLESS series alive: the period memory and the
    // paths with no game save. It NO LONGER crosses by livery — the `rebindFromSeats` bridge was removed when closing the
    // whoever has seats is composed from them, and whoever lacks them has nobody to
    // re-bind. Here only the class CURVE remains, which is what this path always knew how to set.
    // by ID, not by name (see career_fields.h). An empty id never excludes.
    auto excluded = [&](const std::string& id) { return exclude && !id.empty() && exclude->count(id) > 0; };
    std::vector<std::string> liveries = LiveriesForClass(s->class_name);
    // NOBODY ELSE DRIVES YOUR LIVERY -- if there's something to replace it with. The seat crossover only
    // RENAMES, never removes, so the livery you occupy kept showing up in the AI grid with the driver
    // the static pipeline assigned to it: two cars with the same paint, and on top of that AMS2 picks on its own
    // which of the names it drops, and it might not be that one. The intent had been written down for a while
    // (career_seats.cpp: "one car, one driver") but wasn't implemented.
    //
    // It gets REPLACED, not trimmed: the class almost always has more liveries than seats, so the
    // next free one is taken and the grid stays full. Only if removing it would leave the grid short is it kept --
    // better a duplicate than a gap AMS2 would fill with a generic.
    if (skipLiveries && !skipLiveries->empty()) {
        std::vector<std::string> libres;
        libres.reserve(liveries.size());
        for (const auto& ln : liveries) if (!skipLiveries->count(ln)) libres.push_back(ln);
        if ((int)libres.size() >= gridSize) liveries.swap(libres);
    }
    if (gridSize > (int)liveries.size()) gridSize = (int)liveries.size();
    if (gridSize <= 0) return grid;
    const CarFamily family = FamilyForClass(s->class_name);   // Affinity is measured against THIS family

    (void)playerForm;   // no longer used: race_skill is a stable RELATIVE spread; DIFFICULTY is set by the global slider (DDA)

    const EloScale escalaElo = MeasureEloScale(driverRatings);
    auto ratingSkill = [&](const std::string& id, double fallback) -> double {
        // ~~~04/09: the age, so that the form cap knows whether it has a kid or an established driver in front of it.
        int edad = -1;
        if (worldYear > 0) {
            // the year lives in TWO places and both must be checked: the save's map is only written when
            //  the pool does NOT have it (career_calendar.cpp), so for a REAL driver it is almost always
            //  empty. Looking only at the map left this change INERT precisely for the 1,427 in the pool, who are
            //  the ones it is aimed at. It is the same pair that `birthOf` already uses further down in this file.
            int nac = 0;
            if (birthYear) {
                const auto by = birthYear->find(id);
                if (by != birthYear->end() && by->second > 0) nac = by->second;
            }
            if (nac <= 0) nac = BirthYearFor(id);
            if (nac > 0) edad = worldYear - nac;
            if (edad < 14 || edad > 70) edad = -1;
        }
        return EffectivePace(id, fallback, driverRatings, driverRaces, escalaElo, edad);
    };

    std::vector<RosterDriver> ros = RosterForTier(tier);   // fixed order (hot, base_skill): deterministic cast
    // Jul 2 feedback: the cast flagged modern_only does NOT run PERIOD series (the apex lives in the ladder
    // for modern). The filter runs BEFORE both branches (pinned and field) and before the guest cap, so the 2
    // guests from a historic class become the NEXT ones in the cast, not the apex.
    if (!IsModernSeries(seriesId))
        ros.erase(std::remove_if(ros.begin(), ros.end(),
                  [](const RosterDriver& r) { return r.modern_only; }), ros.end());
    // Bench /E: the cohort kept off the grids above its home tier (see career_fields.h).
    if (g_benchCohortHomeOnly && tier > bal::COHORT_HOME_MAX_TIER) ros.clear();
    int N = ReplaceCount(tier, gridSize);

    // the cast's race_skill = STABLE RELATIVE SPREAD (multiplier on the global "Opponent Skill" slider):
    //   1) base_skill = the authored INNATE skill (constant; skill growth was REMOVED on Jul 2 —
    //      the cast's fame is carried by ELO from results). rivalForm remains a legacy hook (empty in
    //      production; the seed_epoch migration clears it), still read for test/plumbing compatibility.
    //   2) NO rubber-banding based on the player's form (that per-XP coupling pulled the apex below the
    //      pack -> Lucía finishing 5th); nor a narrow per-tier clamp (it flattened the order -> a tie at the ceiling).
    // ABSOLUTE DIFFICULTY is set by the global per-race slider (DDA), not this value. Only the ORDER lives here.
    auto castSkill = [&](const RosterDriver& r) {
        double sk = r.base_skill;
        auto it = rivalForm.find(r.id);
        if (it != rivalForm.end()) sk += it->second;                 // legacy hook (empty in production)
        if (sk < 0.05) sk = 0.05; if (sk > 1.0) sk = 1.0;            // safety clamp [0.05, 1.0]
        return sk;
    };
    // `rebindFromSeats` LIVED HERE, THE LIVE WORLD -> GRID BRIDGE. Removed on 27/08 when closing the
    // . It composed from the static files and AFTER that re-bound each car to whoever the live world
    // had seated in it, crossing by LIVERY. It was the rollback of `g_gridFromSeats`, not a live
    // repair: with the switch on (production since 10/08) this function exited on its
    // first line, because the static path is only reached WITHOUT seats and then the map is empty.
    //
    // Why it is removed instead of kept as a safety: measured with `--seedlab`, the rollback gave
    // 310 academy rows above their level and 1 class of 98 where the cohort LEADS, against 0 and 0
    // on the seats path. Two layers deciding who runs is exactly what the existed
    // to kill. If the old behavior is ever needed, it is in git, not in an `if`.
    //
    // What has NOT gone with it: `GridDriver::seat_moved`. Its other producer is the seats layer
    // (`ComposeGridFromSeats`, `g.seat_moved = sr.moved`) and its only consumer is
    // `RepairDriverLiveryPairing` — which is exactly what the asks to remove. It goes with the F, not with this one.

    std::vector<std::string> seenNames;   // dedup if the roster cycles (cast > active drivers): " 2", " 3"... same as AssembleGrid
    auto uniqueName = [&](const std::string& base) {
        int dup = 0; for (const auto& u : seenNames) if (u == base) dup++;
        seenNames.push_back(base);
        return dup > 0 ? base + " " + std::to_string(dup + 1) : base;
    };

    // 17/09 — the PACK driver (docs/career/pack_drivers.psv) filling what is truly left over: neither the
    // pinned roster nor the cohort covers it. It used to live inline only in the modern branch; factored out because
    // today the period branch (right below) also needs it, and this way both read the SAME criterion of "already
    // there" (`ya`) instead of diverging over time. `freeLiv`/`slot` belong to the CALLER: this only consumes.
    auto fillFromPack = [&](std::vector<std::string>& freeLiv, int& slot) {
        for (int k = 0; (int)grid.size() < gridSize && slot < (int)freeLiv.size(); k++) {
            const PackDriver* pk = PackDriverForClassSlot(s->class_name, k);
            if (!pk) break;
            const std::string pid = PackDriverId(*pk);
            if (pid.empty() || excluded(pid)) continue;
            bool ya = false;
            for (const auto& d : grid) if (d.driver_id == pid) { ya = true; break; }
            if (ya) continue;
            const AuthoredProfile* ap = AuthoredProfileFor(pk->name);
            const double base = (ap && ap->talent > 0.0) ? ap->talent : 0.85;
            GridDriver g;
            g.livery_name = freeLiv[slot++];
            g.name = uniqueName(pk->name); g.country = pk->country;
            g.driver_id = pid; g.kind = "real";
            ApplyDriverModel(g, pk->name, ap ? ap->archetype : std::string(), ratingSkill(pid, base), base,
                             family, role, 0.0, kAuthoredConsMid);
            grid.push_back(g);
        }
    };

    // A series with a pinned roster (series_rosters.psv) gets a STABLE driver->livery binding ----
    // Each driver carries THEIR OWN livery from the PSV every round; the cast occupies ONLY free liveries, in
    // deterministic order (catalog x roster), so it also keeps its skin for the whole season.
    std::vector<PinnedDriver> pinned = PinnedRosterForSeries(seriesId);
    if (!pinned.empty()) {
        // 17/09 — PERIOD MEMORY: WITHOUT a guest reserve. `ReplaceCount`/`pinCount` (right below)
        // exist so that the cohort displaces the weakest real drivers in MODERN series with a
        // PARTIAL roster — but for a period memory `castCupo` is ALWAYS 0 (the cohort never enters, a few
        // lines further down), so nobody useful occupied that reserve: it only made the "leftover" and
        // the pack driver (who entered BEFORE it) receive the next free livery IN CATALOG
        // ORDER instead of their own — the driver showed up in ANOTHER's car, and if the catalog ran out
        // earlier, they disappeared from the grid with no trace. Measured in Ivory_Cup (26 pinned/26 slots, tier 4,
        // `ReplaceCount(4,26)=10`): 8 of 26 crossed and 3 vanished (Ian Scheckter, Paddy Driver, Leo
        // Kinnunen). Repeated in Ivory_Trophy (5 crossings) with the same mechanism — see
        // `test_era_memory_pinned_keeps_own_livery`. Here every pinned driver that fits on the grid keeps
        // THEIR OWN livery always — there is no catalog slot to assign, so no crossing is possible — and
        // only if cars are left over (pinned.size() < gridSize) does the PACK driver enter to fill what truly
        // has no curated owner.
        if (IsEraMemorySeries(*s)) {
            std::set<std::string> usedLiv;
            for (size_t pi = 0; pi < pinned.size() && (int)grid.size() < gridSize; ++pi) {
                const PinnedDriver& pd = pinned[pi];
                if (excluded(RatingIdOf(pd))) continue;                // multiclass
                GridDriver g;
                g.livery_name = pd.livery_name;                        // ALWAYS their own: zero distribution by catalog
                g.name = uniqueName(pd.name);
                g.country = pd.country;
                g.driver_id = RatingIdOf(pd); g.kind = "real";
                ApplyDriverModel(g, pd.name, pd.archetype, ratingSkill(RatingIdOf(pd), pd.skill), pd.skill,
                                 family, role, pd.aggr, pd.cons);
                grid.push_back(g);
                usedLiv.insert(pd.livery_name);
            }
            if ((int)grid.size() < gridSize) {                         // cars left over with no curated owner
                std::vector<std::string> freeLiv;
                for (const auto& lv : liveries) if (!usedLiv.count(lv)) freeLiv.push_back(lv);
                int slot = 0;
                fillFromPack(freeLiv, slot);
            }
            ApplyCarScalarsToGrid(s, grid);   // F2: unit of material over the VANILLA name
            ClampGridNoise(grid);             // the noise cannot measure more than the grid
            return grid;
        }
        int pinCount = gridSize - N;                                   // truncate by standings (= file order)
        if (pinCount > (int)pinned.size()) pinCount = (int)pinned.size();
        if (pinCount < 0) pinCount = 0;
        std::set<std::string> usedLiv;
        size_t pi = 0;                                                 // running index over the pinned list (skips excluded)
        int pinFilled = 0;
        for (; pi < pinned.size() && pinFilled < pinCount; ++pi) {
            const PinnedDriver& pd = pinned[pi];
            if (excluded(RatingIdOf(pd))) continue;                    // multiclass: already racing in a faster class this weekend
            GridDriver g;                                              // useHomage doesn't apply to the pinned roster:
            g.livery_name = pd.livery_name;                            // the tribute swap for the real field
            g.name = uniqueName(pd.name);                              // belongs to the cast (RosterDriver.homage)
            g.country = pd.country;
            // Aug 9: the other half — `pd.id` comes in EMPTY (column 2 of the PSV is the CAR NUMBER) and
            // `ratingSkill("")` falls back to the curve. PinnedOccupantId resolves it, but isn't used for the same
            // reason as the field (see the pushReal block): it would expose the cohort above the real drivers.
            g.driver_id = RatingIdOf(pd); g.kind = "real";             // Stable identity for the pinned roster
            // Talent (live rating if present, otherwise authored) feeds into the DRIVER MODEL, and the
            // traits come out of the genome. Authored aggression/consistency data overrides the model.
            ApplyDriverModel(g, pd.name, pd.archetype, ratingSkill(RatingIdOf(pd), pd.skill), pd.skill, family, role, pd.aggr, pd.cons);
            grid.push_back(g);
            usedLiv.insert(pd.livery_name);
            pinFilled++;
        }
        std::vector<std::string> freeLiv;                              // free = catalog minus pinned
        for (const auto& lv : liveries) if (!usedLiv.count(lv)) freeLiv.push_back(lv);
        // The cast occupies ONLY free liveries, and EACH cast driver AT MOST once (ros[i], not
        // ros[i % n]): cycling was producing DUPLICATE rivals (the "Axel Brandt 2/3" artifact). The invariant
        // in ValidateFieldsData (eligible roster >= freed-seat need per tier) guarantees castN <= ros.size(),
        // so this cutoff doesn't change any grid that wasn't already cycling (PSV<->JSON parity).
        // Multiclass: 'slot' only advances on PLACEMENT (skipping an excluded driver doesn't burn a livery -> grid stays full).
        int slot = 0;
        // 11/08 (): the cap of TWO GUESTS of a period memory here too. It was only in the
        // field branch, and period classes with a pinned roster skipped the cap through the other door.
        int castCupo = (IsEraMemorySeries(*s) || IsEventOnlySeries(*s)) ? 0 : (int)ros.size();   // 11/08: cohort ONLY in modern ones (01/10: nor in event-only)
        for (int i = 0; i < (int)ros.size() && castCupo > 0 && (int)grid.size() < gridSize && slot < (int)freeLiv.size(); i++) {
            const RosterDriver& r = ros[i];                            // unique: never recycles the cast
            std::string nm = (useHomage && !r.homage.empty()) ? r.homage : r.name;
            if (excluded(r.id)) continue;                              // multiclass (the tribute changes the name, not the id)
            castCupo--;
            GridDriver g;
            g.livery_name = freeLiv[slot++];
            g.name = uniqueName(nm);
            g.country = r.country;
            g.driver_id = r.id; g.kind = "roster";    // The tribute swap changes name, NOT driver_id
            ApplyDriverModel(g, r.name, r.archetype, ratingSkill(r.id, castSkill(r)), r.base_skill,   // driver-model integration
                             family, role, r.aggression, r.consistency);   // r.name = CANONICAL (not the tribute name)
            grid.push_back(g);
        }
        // (defensive): ONLY if the cast ran out before filling the grid (castN > ros.size(), which the
        // invariant forbids, or the whole cast excluded by multiclass) we extend the REAL field with more
        // pinned drivers by standings (still 'pi') into the remaining free liveries. Never duplicates a driver_id.
        // 11/08: and if capping the cohort leaves the grid short, first the class PACK — the same
        // cascade as the other branch. Only then is it extended with more pinned drivers by standings.
        fillFromPack(freeLiv, slot);
        for (; pi < pinned.size() && (int)grid.size() < gridSize && slot < (int)freeLiv.size(); ++pi) {
            const PinnedDriver& pd = pinned[pi];
            if (excluded(RatingIdOf(pd))) continue;                    // multiclass
            GridDriver g;
            g.livery_name = freeLiv[slot++];
            g.name = uniqueName(pd.name);
            g.country = pd.country;
            g.driver_id = RatingIdOf(pd); g.kind = "real";
            ApplyDriverModel(g, pd.name, pd.archetype, ratingSkill(RatingIdOf(pd), pd.skill), pd.skill,   // driver-model integration
                             family, role, pd.aggr, pd.cons);
            grid.push_back(g);
        }
        ApplyCarScalarsToGrid(s, grid);   // F2: unit of material over the VANILLA name
        ClampGridNoise(grid);             // the noise cannot measure more than the grid
        return grid;
    }

    // fallback: field from the curve (series_fields.psv) + cast graft ----
    // The "freed seats" model: the real field keeps the BEST seats
    // (top-by-order, order 1 = best) and the cast DISPLACES the worst real drivers (the ones with the highest `order`,
    // backmarker/midfield). RealFieldCount(seriesId, gridSize) = how many real drivers are kept;
    // FreedSeatCount = the ones the cast occupies. The livery is assigned by FIXED POSITION (stable across rounds).
    std::vector<FieldDriver> field = FieldForSeries(seriesId);   // ordered by order
    // observed: in PERIOD classes with a complete REAL field (notes "(real ...)"), the cohort only appears
    // as guests: the 2 BEST from your generation (ros is ordered by hot/base_skill, so ros[0..1]).
    // In junior/progression classes (notes "(plausible)"), the cohort still fills out the grid (your rivals).
    {
        int realNotes = 0;
        for (const auto& fd : field) if (fd.note.rfind("(real", 0) == 0) realNotes++;
        (void)realNotes;   // the 2-guests rule stays documented below, but is no longer applied
        // 11/08 () — IN A PERIOD MEMORY THE COHORT IS TWO GUESTS, NEVER THE FILL.
        //
        // The rule above is from 02/07 and is the user's, but it only activated with a MAJORITY of rating notes
        // "(real ...)" — and measured on 11/08 it almost never activates: 32 period classes came out with a cohort and of
        // their 192 rows only **8 were the two guests**; the other 184 were fill. is explicit
        // (“the cohort never fills a historic with no data”) and the user said it in the same words:
        // “why is there a cohort in historic series? there should be no cohort there”.
        //
        // The cap is now applied to EVERY period memory, whatever notes the field has. What is missing
        // to fill is covered first by that class's pack (below) and, if that falls short, the grid is left
        // short ON PURPOSE: says that the lack of a historic roster is “a visible content error”,
        // and covering it with the player's generation is exactly what made it invisible.
        // 11/08 — THE COHORT ONLY IN MODERN SERIES. User's decision, and it repeals that of 02/07 (which in
        // period classes with a full real field wanted “the 2 best of your generation” to appear as
        // guests). Their words today: “no, cohort only in modern ones”. With the 2 guests there were 48 rows left
        // in 24 classes; now there are zero.
        //
        // What fills those slots: the series' field first, and then the class PACK (below). If
        // that falls short, the grid is left short on purpose — says that the lack of a historic roster is
        // “a visible content error”, and covering it with the player's generation is what made it
        // invisible. A short grid is the signal that this class lacks content, not a failure.
        if (IsEraMemorySeries(*s) || IsEventOnlySeries(*s)) N = 0;   // 01/10: event-only, like the era
    }
    int realCount = gridSize - N; if (realCount < 0) realCount = 0;

    // The Pick carries identity (id+kind); the name can mutate (tribute) but the id CANNOT.
    // archetype+base for ABSOLUTE personality (real field -> "" = neutral profile; cast -> its archetype).
    // `canon` = the DATA's name (never the tribute). It's the genome's key: `name` can be the
    // tribute name and even carry the dedup's " 2" suffix, and a driver's character can't depend on that.
    struct Pick { std::string name, country, id, kind, archetype, canon; double skill, aggr, base, cons; };
    std::vector<Pick> picks;
    std::set<int> usedField;                                       // field indices already placed (multiclass dedup)

    // A real driver from the field NO LONGER enters neutral. Until now the TWO places that insert a real driver wrote
    // archetype "" — and BiasForArchetype("") returns a NULL bias, so the game's 1,280 real drivers
    // shared the same character: this is the "drivers are robots" feedback, in two push_backs.
    // With an authored profile, their archetype and talent take over; without one, null bias, i.e. same as before.
    // Pace precedence: live ELO > authored talent (ABSOLUTE) > curve (POSITIONAL, the default that
    // already killed the cast: it would sort by it and discard the talent).
    // The last argument is still 0.85, and NOT ProfileForArch(arch).cons: 0.85 is kAuthoredConsMid, i.e.
    // the sentinel for "no authored consistency PER DRIVER". Putting the archetype's value there would count it TWICE,
    // since BiasForArchetype already applies its own offset (measured: it broke 6 checks).
    auto pushReal = [&](const FieldDriver& fd) {
        const AuthoredProfile* ap = AuthoredProfileFor(fd.name);
        const std::string arch = ap ? ap->archetype : std::string();
        const double base = ap ? ap->talent : fd.skill;
        // Aug 9 — THIS IS WHERE HALF OF THE TWO TALENT SCALES SHOWS UP, and it's left this way ON PURPOSE.
        // `fd.id` comes in EMPTY in PSV fields (only the JSON path mints ids), so `ratingSkill("")`
        // ALWAYS falls back to the static curve, and this driver ends up on a different scale than their rivals in the same
        // car. The correct resolution exists and is exported (career_seats.h::FieldOccupantId).
        // TESTED using it here: with no rating, 740 -> 158, and mixed classes 70 -> 37... but once it finds their
        // ELO, the real drivers fall BELOW the cohort, which is seeded via CastSeedFromBase (1150-2250,
        // with no tier term). Measured: Ivory_Cup_10072 goes from Giaffone/Girolami/Rossi to Vega/Mori/Rocha, and
        // GT3 from Riberas/Davis/Tandy to Morad/Mori/Vega. So this doesn't go in until we decide what the
        // cohort does at the top. See two_talent_scales_in_one_grid.
        picks.push_back({ fd.name, fd.country, RatingIdOf(fd), "real", arch, fd.name,
                          ratingSkill(RatingIdOf(fd), base), fd.aggr, base, 0.85 });
    };

    for (int i = 0, rp = 0; i < (int)field.size() && rp < realCount; i++) {
        if (excluded(RatingIdOf(field[i]))) continue;              // multiclass: already in another, faster class this weekend
        pushReal(field[i]);
        usedField.insert(i); rp++;
    }

    // The cast fills up to gridSize, EACH driver at most once (ros[i], not ros[i % n]): the
    // cycling was producing DUPLICATE rivals. The ValidateFieldsData invariant guarantees the tier's
    // eligible roster covers demand, so in real data it never cycles -> parity intact.
    int castN = gridSize - (int)picks.size();
    // 11/08 (): in a period memory the cap of TWO GUESTS is over those who truly enter, not
    // over those who displace. `N` only bounds how many real drivers the cohort throws out; if the field is shorter than
    // the grid —which is the normal case in period classes— this loop let in the 14 that were needed
    // anyway. Measured: Falcon_Masters came out with 14 academy drivers with the cap above already in place.
    if (IsEraMemorySeries(*s) || IsEventOnlySeries(*s)) castN = 0;   // 11/08: cohort ONLY in modern ones (see above; 01/10: nor event-only)
    for (int i = 0, cp = 0; i < (int)ros.size() && cp < castN; i++) {
        const RosterDriver& r = ros[i];                            // unique: never recycles the cast
        std::string nm = (useHomage && !r.homage.empty()) ? r.homage : r.name;   // tribute changes name, not id
        if (excluded(r.id)) continue;                              // multiclass (by id: the tribute does not move it)
        picks.push_back({ nm, r.country, r.id, "roster", r.archetype, r.name, ratingSkill(r.id, castSkill(r)), r.aggression, r.base_skill, r.consistency }); // FIX 1
        cp++;
    }
    // (defensive): if the cast ran out before reaching gridSize (castN > ros.size(), which the invariant
    // forbids, or the whole cast excluded by multiclass), we extend the REAL field with the next ones by `order`
    // (real drivers the cast would have displaced), skipping ones already used/excluded. At most grid < gridSize.
    for (int i = 0; (int)picks.size() < gridSize && i < (int)field.size(); i++) {
        if (usedField.count(i) || excluded(RatingIdOf(field[i]))) continue;
        pushReal(field[i]);                                        // same rule as the main path
        usedField.insert(i);
    }
    // 11/08 () — AND BEFORE LEAVING IT SHORT, THE CLASS PACK. It is the same cascade the
    // seats path already uses, brought here for the period memories: by capping the cohort at two guests, a
    // 1967 grid was left with the field and nothing else. The pack brings real people of THAT class (PINNACLE_CHALLENGE_4250 17,
    // Group C 14, Onyx Trophy 8637 16, Ashwood Motors 10...), which is infinitely better filler than the modern academy.
    // Identity, not pace, same as on the seats path: their pack `skill` is positional, so
    // it enters with the slot's talent (the curve of its class) and not with a made-up number.
    for (int k = 0; (int)picks.size() < gridSize; k++) {
        const PackDriver* pk = PackDriverForClassSlot(s->class_name, k);
        if (!pk) break;                                            // the pack ran out: short and visible ()
        const std::string pid = PackDriverId(*pk);
        if (pid.empty() || excluded(pid)) continue;
        bool ya = false;
        for (const auto& p : picks) if (p.id == pid) { ya = true; break; }
        if (ya) continue;
        const AuthoredProfile* ap = AuthoredProfileFor(pk->name);
        const double base = (ap && ap->talent > 0.0) ? ap->talent : 0.0;
        picks.push_back({ pk->name, pk->country, pid, "real", ap ? ap->archetype : std::string(), pk->name,
                          ratingSkill(pid, base > 0.0 ? base : 0.85), 0.0, base > 0.0 ? base : 0.85,
                          kAuthoredConsMid });
    }

    // Pass 6: the real field carries its real-life TEAM's livery (driver_liveries.psv) if it has one; cohort +
    // real drivers without a binding take the FREE liveries by position. With no binding loaded -> freeLiv == catalog and
    // fi==i -> identical to the previous one.
    // 23/09 — BUT A LIVERY IS A CAR, AND A CAR IS DRIVEN BY ONE PERSON ONLY. The binding of the
    // FIELD classes is curated by TEAM ("Faircombe Racing " for everyone who raced with the factory BRACKENRIDGE), and here it was
    // accepted repeated: Group A came out with x5 and x5. AMS2 pairs ONE XML entry per livery, so
    // 8 of 25 did not reach the track; same in Group C (Wildmere Motors x3) and Onyx Trophy 8637. The seats path already resolved the
    // clash with “the first wins” (`quiere.emplace`); this one did not. Now each livery has one owner, in this order:
    //   3) the driver the PACK paints in that slot (livery_resolved, col 7) — pack_slot_identity_rule;
    //   2) a binding whose label carries the driver's SURNAME ("Wilddale Racing #5" belongs to Wilddale Racing);
    //   1) any other binding.
    // Tie: the lowest index (field order, which is the curated one). The losers go to the FREE ones like
    // anyone without a binding; a livery is only repeated when the catalog runs out (class with no more cars).
    std::vector<std::string> pickLiv(picks.size());
    std::vector<int> pickPri(picks.size(), 0);
    auto apellido = [](const std::string& nm) {
        const size_t sp = nm.find_last_of(' ');
        return FoldDriverName(sp == std::string::npos ? nm : nm.substr(sp + 1));
    };
    for (int i = 0; i < (int)picks.size(); i++) {
        if (picks[i].kind != "real") continue;                           // field's name = canonical (not tribute)
        const std::string rl = ResolvedLiveryForDriver(s->class_name, picks[i].name);
        if (!rl.empty() && std::find(liveries.begin(), liveries.end(), rl) != liveries.end()) {
            pickLiv[i] = rl; pickPri[i] = 3; continue;
        }
        const std::string bl = DriverLiveryFor(seriesId, picks[i].name);
        if (bl.empty()) continue;
        const std::string ap = apellido(picks[i].name);
        pickLiv[i] = bl;
        pickPri[i] = (!ap.empty() && FoldDriverName(bl).find(ap) != std::string::npos) ? 2 : 1;
    }
    std::map<std::string, int> owner;                                    // livery -> index of the pick that carries it
    for (int i = 0; i < (int)picks.size() && i < gridSize; i++) {
        if (pickLiv[i].empty()) continue;
        auto it = owner.find(pickLiv[i]);
        if (it == owner.end()) { owner[pickLiv[i]] = i; continue; }
        if (pickPri[i] > pickPri[it->second]) { pickLiv[it->second].clear(); it->second = i; }
        else pickLiv[i].clear();                                         // loses: goes to the free ones
    }
    std::set<std::string> boundLiv;
    for (const auto& kv : owner) boundLiv.insert(kv.first);
    std::vector<std::string> freeLiv;                                    // catalog minus the liveries with an owner
    for (const auto& lv : liveries) if (!boundLiv.count(lv)) freeLiv.push_back(lv);
    int fi = 0;
    for (int i = 0; i < (int)picks.size() && i < gridSize; i++) {
        std::string lv;
        if (!pickLiv[i].empty()) lv = pickLiv[i];                        // their real livery (sole owner)
        else if (fi < (int)freeLiv.size()) lv = freeLiv[fi++];           // free by position
        else if (!liveries.empty()) lv = liveries[i % (int)liveries.size()];   // catalog exhausted: repeats
        else break;
        GridDriver g;
        g.livery_name = lv; g.name = uniqueName(picks[i].name); g.country = picks[i].country;
        g.driver_id = picks[i].id; g.kind = picks[i].kind;   // Stable identity
        ApplyDriverModel(g, picks[i].canon, picks[i].archetype, picks[i].skill, picks[i].base,   // (field=neutral; cast=its archetype)
                         family, role, picks[i].aggr, picks[i].cons);   // canon = the DATA's name, not the displayed one
        grid.push_back(g);
    }
    ApplyCarScalarsToGrid(s, grid);   // F2: unit of material over the VANILLA name
    ClampGridNoise(grid);             // the noise cannot measure more than the grid
    return grid;
}

// ===================================================================================================
// — THE GRID FROM THE SEATS
// ===================================================================================================
//
// THE DISTRIBUTION OF ROLES, which is the only truly new thing:
//   · the SEATS provide the PEOPLE and the CAR of each one (the live world had already decided it);
//   · the STATIC composition provides the class CURVE — how much talent corresponds to each car.
//
// Why this way and not "take the person from the seat and their talent from their card": 40% of those who run have no
// card (the per-series generated tail, the live world's rookies). If those were given a made-up number,
// the class spread would stop being the curated one. With the per-car curve, a person with authored capability
// runs with theirs and whoever lacks it inherits that of the slot — which is EXACTLY what the rebind does today
// (“its IDENTITY is changed, not its PACE”) but applied to the whole grid instead of the rows that the
// bridge managed to touch.
//
// 18/08 — THAT DECISION WAS REVERSED. AND THE WARNING IN THE PARAGRAPH ABOVE WAS CORRECT: THAT IS WHY IT STAYS.
//
// WHAT was done. The generated tail (`gn_*`, `fl_*`) is indeed given a number today — not “made up”, but CLONED
// from the corpus of authored cards, at the same percentile that already decided its rating (`PersonForOccupant`,
// step 6, ~line 869; `career_growth.cpp::RookieProfileTalentCloned`).
//
// WHY. Inheriting the slot's curve point was not neutral: it was putting the CAR's pace through the channel of the
// DRIVER's skill, and the car is already expressed separately by the material scalars — it was counted twice.
// Measured in production: over twenty seasons 65% of the grid rows came from the curve, and in 41 of 90
// categories the FASTEST on the grid was one of them, up to 0.151 above the best driver with a
// card, when the WHOLE spread of cards in an F1 measures 0.11. After: 0 of 90 at T0 and at T20, and the worst
// deviation of the ace drops to 0.0140.
//
// AND NOW THE PRICE, WHICH IS EXACTLY THE ONE THE NOTE ANNOUNCED: THE CLASS SPREAD MOVED.
// Clean A/B (same data, same `MedirVara` instrument, same machine; only the plan's three commits
// change). Live/frozen ratio, median of the 90 series:
//
//              T0      T1      T2      T5      T10     T20
//     before   0.98    0.81    0.76    0.72    0.74    0.77
//     now      0.98    0.81    0.79    0.63    0.60    0.53
//
// The live spread of Juniper_Cup at T20 goes from 0.233 to 0.188 (−19%). The drift of the SAME class between
// T1 and T20 rises from p50 12.8% to 30.8%, and the classes that move more than 25% go from 20 to 42. In exchange,
// those that fall outside the envelope (ratio > 1) drop from 14 to 9 at T20.
// The narrowing does not appear until T5, which is exactly when the cloned card starts to be the majority (27.9% of
// the rows at T5, 57.8% at T20): that coincidence is the signature of the cause, not chance. Part of the
// earlier width was the defect itself —curve points with the CAR's range widening what claimed to be
// driver talent— but the note was right about what it said: the spread stopped being the curated one.
// Whoever divides by this yardstick has to read these figures BEFORE, not after.
//
// What this FIXES by construction, without a new rule: the cohort stops appearing above its
// level. Not because it is filtered here, but because the seats layer never seated it there — and now there is a
// single layer deciding who runs instead of two that did not talk to each other.
static std::vector<GridDriver> ComposeGridFromSeats(const std::string& seriesId, int tier, int gridSize,
                                                    const std::map<std::string, float>& rivalForm,
                                                    const std::set<std::string>* exclude,
                                                    const std::map<std::string, double>* driverRatings,
                                                    ClassRole role, const std::vector<SeatRow>& seats,
                                                    const std::set<std::string>* skipLiveries,
                                                    const std::map<std::string, int>* driverRaces,
                                                    const std::map<std::string, double>* driverCurve,
                                                    const std::map<std::string, int>* birthYear,
                                                    int worldYear) {
    std::vector<GridDriver> grid;
    const SeriesDef* s = SeriesById(seriesId);
    if (!s) return grid;
    const CarFamily family = FamilyForClass(s->class_name);

    // (1) THE CURVE. The usual static composition, WITHOUT a seats bridge: only the talent that
    // falls to each car matters, not who drives it. The `skipLiveries` goes the same, so that the curve is computed
    // over the same cars that are going to run.
    const std::vector<GridDriver> curvaGrid = ComposeSeasonGrid(
        seriesId, tier, /*playerForm=*/0.0, gridSize, /*useHomage=*/false, rivalForm,
        /*exclude=*/nullptr, driverRatings, role, /*seats=*/nullptr, skipLiveries);
    std::map<std::string, double> curva;                 // livery -> that car's talent
    std::vector<double> talentos;
    for (const auto& d : curvaGrid) {
        if (!d.livery_name.empty()) curva[d.livery_name] = d.talent;
        talentos.push_back(d.talent);
    }
    // Median, for a car that runs and was not in the curve (the seat and grid catalogs do not
    // necessarily cover exactly the same liveries). Without a curve it is not composed: it is a data failure.
    if (talentos.empty()) return grid;
    std::sort(talentos.begin(), talentos.end());
    const double talentoMediano = talentos[talentos.size() / 2];

    // (2) THE PEOPLE. Indices of this series, once.
    SeatPeopleIndex ix;
    const std::vector<PinnedDriver> pinned = PinnedRosterForSeries(seriesId);
    for (const auto& pd : pinned) { const std::string id = PinnedOccupantId(pd); if (!id.empty()) ix.pinned.emplace(id, &pd); }
    const std::vector<FieldDriver> field = FieldForSeries(seriesId);
    for (const auto& fd : field)  { const std::string id = FieldOccupantId(fd);  if (!id.empty()) ix.field.emplace(id, &fd); }
    // The cohort of ALL tiers: a seat may have someone seated who is not of this level.
    // The vectors are stored: `RosterForTier` returns BY VALUE, so indexing pointers into a temporary
    // of a `for` over the call leaves the whole index dangling.
    std::vector<std::vector<RosterDriver>> castPorTier;
    castPorTier.reserve(7);
    for (int t = 1; t <= 7; t++) castPorTier.push_back(RosterForTier(t));
    for (const auto& v : castPorTier)
        for (const auto& rd : v) if (!rd.id.empty()) ix.cast.emplace(rd.id, &rd);

    const EloScale escalaElo = MeasureEloScale(driverRatings);
    auto ratingSkill = [&](const std::string& id, double fallback) -> double {
        // ~~~04/09: the age, so that the form cap knows whether it has a kid or an established driver in front of it.
        int edad = -1;
        if (worldYear > 0) {
            // the year lives in TWO places and both must be checked: the save's map is only written when
            //  the pool does NOT have it (career_calendar.cpp), so for a REAL driver it is almost always
            //  empty. Looking only at the map left this change INERT precisely for the 1,427 in the pool, who are
            //  the ones it is aimed at. It is the same pair that `birthOf` already uses further down in this file.
            int nac = 0;
            if (birthYear) {
                const auto by = birthYear->find(id);
                if (by != birthYear->end() && by->second > 0) nac = by->second;
            }
            if (nac <= 0) nac = BirthYearFor(id);
            if (nac > 0) edad = worldYear - nac;
            if (edad < 14 || edad > 70) edad = -1;
        }
        return EffectivePace(id, fallback, driverRatings, driverRaces, escalaElo, edad);
    };
    auto excluded = [&](const std::string& id) { return exclude && !id.empty() && exclude->count(id) > 0; };

    std::vector<std::string> seenNames;
    auto uniqueName = [&](const std::string& base) {
        int dup = 0; for (const auto& u : seenNames) if (u == base) dup++;
        seenNames.push_back(base);
        return dup > 0 ? base + " " + std::to_string(dup + 1) : base;
    };

    std::set<std::string> yaEnParrilla;      // one driver, one car: allows two contracts, not two cars
    std::set<std::string> yaUsada;           // one car, one driver
    g_drops = GridDropReport();              // every exclusion is declared (see career_fields.h)
    for (const auto& sr : seats) {
        if ((int)grid.size() >= gridSize) { g_drops.over_size++; continue; }
        if (sr.livery_name.empty() || sr.occupant_id.empty()) { g_drops.unresolved++; continue; }
        if (skipLiveries && skipLiveries->count(sr.livery_name)) { g_drops.player_car++; continue; }
        if (!yaUsada.insert(sr.livery_name).second) { g_drops.dup_livery++; continue; }
        if (excluded(sr.occupant_id)) { g_drops.multiclass++; continue; }    // multiclass , by id
        if (!yaEnParrilla.insert(sr.occupant_id).second) { g_drops.dup_driver++; continue; }
        const SeatPerson p = PersonForOccupant(sr.occupant_id, ix);
        if (!p.resolved) { g_drops.unresolved++; continue; }   // without a name there is no row to write

        const auto ic = curva.find(sr.livery_name);
        const double puntoCurva = (ic != curva.end()) ? ic->second : talentoMediano;
        // The PERSON's capability if they have it; if not, that of the SLOT. See the header.
        // 18/08 — THE CLONE ENTERS WHERE IT IS, NOT AT ITS CEILING.
        // Its card was sampled from the corpus, which are CURRENT levels of mature people: giving it as is to a
        // 19-year-old kid made them enter performing like the adult they were cloned from. The ELO channel already treats that
        // number as a CEILING (agedRatingFor multiplies it by progress); here the same is done, so that the
        // two channels stop disagreeing about what the same number means.
        //
        // 19/08 — THE TWO HALVES OF THE CURVE, NOT ONE. `GrowthProgress` returns ONLY the RISE half
        // (career_growth.cpp: the personal curve when it exists, and `AgeRisePart` when it does not), which is what brings you
        // closer to your ceiling. The other half, `AgeDeclinePart`, is what age takes away past 32 -- the
        // IRREVERSIBLE part of the canonical spec “past 32 you go down no matter what you do”. The
        // ELO channel multiplies by both (career_seats.cpp::agedRatingFor: `GrowthProgress(...) *
        // AgeDeclinePart(...)`), so applying only the rise here left the two channels disagreeing
        // exactly where the comment promised they stopped doing so: a 40-year-old clone with card 0.90 and a saturated
        // curve entered at 0.900 -- their ceiling, at 40 -- while the ELO said 0.842 of the same man.
        // The `ageShift` is a LITERAL 0 in BOTH halves, for the same reason already argued above for the
        // rise: this layer does not know what family the car is, and using different shifts in each half would be
        // to have two scales again inside the same calculation.
        // Without a curve, birth or year -> multiplier 1, i.e. the behavior from before, bit for bit.
        double base = (p.talent > 0.0) ? p.talent : puntoCurva;
        double arco = 1.0;   // ~~~07/09: the same multiplier, stored for the controls (sweep)
        if (p.cloned && driverCurve && birthYear && worldYear > 0) {
            const auto by = birthYear->find(sr.occupant_id);
            if (by != birthYear->end()) {
                const int edad = worldYear - by->second;
                arco = GrowthProgress(*driverCurve, sr.occupant_id, edad, 0) * AgeDeclinePart(edad, 0);
                base *= arco;
            }
        } else if (!p.cloned && birthYear && worldYear > 0) {
            // ~~~04/09 () — A REAL DRIVER'S CARD IS “TODAY”, BUT “TODAY” HAS A DATE.
            //
            // User's decision (18/08, and reaffirmed on 04/09): the curated card says what the driver
            // IS, not how far they can get. Correct -- but the engine treated it as timeless, and so
            // a young real driver NEVER developed: measured over 55 drivers of <=22 years followed
            // twenty seasons, their ELO moves +0.031 on average, which is 0.39 sigma in two decades. And the
            // form cap was not the brake: only 8 of the 55 managed to touch it. There was nothing pushing.
            //
            // Pemberton Sol523's card is written for 1991 and that of a current kid for 2025, so the
            // “today” of each one is the age they had THEN. Anchoring the arc there, the multiplier is
            // EXACTLY 1 at that age -- whoever runs at the age of their card comes out bit for bit as before -- and
            // the young one rises from it while the veteran goes down. It is the opposite of putting them below
            // their place and making them grow: Antonelli enters worth what he is worth and IMPROVES.
            //
            // Without a year in `era` (40% of the corpus) or without a birth year, the ratio is 1 and nothing happens.
            const AuthoredProfile* ap = AuthoredProfileFor(p.name);
            if (ap && ap->era_year > 0) {
                // the year, from the TWO places: the save's map and, failing that, the pool. Looking only at the map
                //  this was left inert for the real drivers, who are exactly the ones it anchors.
                int nac = 0;
                const auto by = birthYear->find(sr.occupant_id);
                if (by != birthYear->end() && by->second > 0) nac = by->second;
                if (nac <= 0) nac = BirthYearFor(sr.occupant_id);
                if (nac > 0) {
                    const int edad    = worldYear    - nac;
                    const int edadRef = ap->era_year - nac;
                    // Absurd ages (corrupt data) are ignored instead of distorting the grid.
                    if (edad >= 14 && edad <= 70 && edadRef >= 14 && edadRef <= 70) {
                        const double arcoHoy = AgeRisePart(edad,    0) * AgeDeclinePart(edad,    0);
                        const double arcoRef = AgeRisePart(edadRef, 0) * AgeDeclinePart(edadRef, 0);
                        if (arcoRef > 1e-6) { arco = arcoHoy / arcoRef; base *= arco; }
                    }
                }
            }
        }
        if (base < 0.05) base = 0.05;
        if (base > 1.00) base = 1.00;

        GridDriver g;
        g.livery_name = sr.livery_name;
        g.name        = uniqueName(p.name);
        g.country     = p.country;
        g.driver_id   = sr.occupant_id;
        g.kind        = p.kind;
        g.seat_moved  = sr.moved;   // it is decided by the seats layer, which is the one that sees both sides
        ApplyDriverModel(g, p.name, p.archetype, ratingSkill(sr.occupant_id, base), base,
                         family, role, p.aggr, p.cons, arco);
        grid.push_back(g);
    }

    ApplyCarScalarsToGrid(s, grid);   // F2: over the VANILLA livery_name, same as the static path
    return grid;
}

// Age / retirement (rival_form's scheduled growth was REMOVED on Jul 2) ----------
// The growth scheme (AdvanceRivalSeason(Aged): deltas toward targets tied to the player's tier
// via archetype anchor_high/low/variable) belonged to the ABSOLUTE difficulty model. Today race_skill is a
// RELATIVE spread on the global slider (XML band [0.85, 1.0]), and the cohort is "the worst generation": already
// strong from day 1 (high authored base_skill + CastSeedFromBase); its evolution is carried by live ELO.
// rival_form remains a legacy hook (always empty in production; the seed_epoch migration clears it).
namespace {
// A deterministic FNV-1a hash of the id -> retirement-age noise 0..RETIRE_AGE_SPREAD (no RNG).
int RetireSpread(const std::string& id) {
    unsigned h = 2166136261u;
    for (char c : id) { h ^= (unsigned char)c; h *= 16777619u; }
    return (int)(h % (unsigned)(bal::RETIRE_AGE_SPREAD + 1));   // 0..RETIRE_AGE_SPREAD
}
} // anon namespace

int DriverAge(int birthYear, int currentYear) {
    if (birthYear <= 0) return -1;                 // no data -> procedural
    if (currentYear < birthYear) return 0;          // defense
    return currentYear - birthYear;
}

// — THE CURVE, SPLIT IN TWO. And the split is not cosmetic: it is the whole of decision 20.6.1.
//
//   RISE      what brings you closer to your ceiling. It is what CIRCUMSTANCE bends — “three years in a bad car really
//             stall you; if a good seat arrives at 26, you rise again”. TIME is lost, not
//             the ceiling, so this half is recoverable by construction.
//   DECLINE   what age takes away past 32. The spec states it explicitly: “the irreversible part is already
//             set by age, so circumstance does not need to be permanent to hurt”.
//
// The product of the two IS `AgeCurveMultiplier` exactly — a test pins it. That is why a world without
// personal growth (or an old save) gives the same number as always: the split changes nothing by itself,
// it only opens the place where it hooks in.
double AgeRisePart(int age, int gtShift) {
    if (age < 0) return 1.0;                                       // unknown -> neutral
    int eff = age - gtShift;
    if (eff < bal::AGE_RISE_FROM) eff = bal::AGE_RISE_FROM;        // below the minimum: same as a rookie
    if (eff >= bal::AGE_PEAK_LO) return 1.0;                       // already arrived: the rise is done
    const double span = (double)(bal::AGE_PEAK_LO - bal::AGE_RISE_FROM);
    const double t = (span <= 0.0) ? 1.0 : (double)(eff - bal::AGE_RISE_FROM) / span;
    return bal::AGE_RISE_FLOOR + t * (1.0 - bal::AGE_RISE_FLOOR);
}

double AgeDeclinePart(int age, int gtShift) {
    if (age < 0) return 1.0;                                       // unknown -> neutral
    int eff = age - gtShift;
    if (eff < bal::AGE_RISE_FROM) eff = bal::AGE_RISE_FROM;
    if (eff <= bal::AGE_PEAK_HI) return 1.0;                       // rise and plateau: takes nothing away yet
    double mult = 1.0;
    int gentleYears = (eff < bal::AGE_DECLINE_KNEE)
                        ? (eff - bal::AGE_PEAK_HI)
                        : (bal::AGE_DECLINE_KNEE - bal::AGE_PEAK_HI);
    mult -= gentleYears * bal::AGE_DECLINE_GENTLE;
    if (eff > bal::AGE_DECLINE_KNEE)
        mult -= (eff - bal::AGE_DECLINE_KNEE) * bal::AGE_DECLINE_STEEP;
    if (mult < bal::AGE_MULT_FLOOR) mult = bal::AGE_MULT_FLOOR;
    return mult;
}

double AgeCurveMultiplier(int age, int gtShift) {
    return AgeRisePart(age, gtShift) * AgeDeclinePart(age, gtShift);
}

bool ShouldRetire(const RosterDriver& d, int currentYear, double agedRating) {
    // 1) authorized (icon with a real year)
    if (d.retire_year > 0) return currentYear >= d.retire_year;
    // 2) procedural — needs a known age
    int age = DriverAge(d.birth_year, currentYear);
    if (age < 0) return false;                                    // no age and no retire_year -> doesn't retire
    // 2a) A HARD CAP, not a criterion. This used to be `age >= RETIRE_AGE_BASE(40) + noise`, and it was the
    // ACTUAL trigger for retirement: simulating eight seasons, Barrichello would leave at 54 and Alonso at
    // 47 without either of them ever crossing their category's bar — Alonso raced two
    // seasons BELOW it and retired anyway just from aging out. We built the performance
    // criterion, and the birthday still ruled.
    //
    // A driver leaves their seat when they no longer meet the bar (ExpireSeatContracts against DemandForClass), and
    // stops existing when they don't meet it in any category they could drop to (CascadeDemoteFreeAgents).
    // This here only prevents the absurd: nobody races at seventy. It should almost never fire; if
    // it fires a lot, the cascade isn't doing its job.
    if (age >= bal::RETIRE_AGE_HARD + d.age_shift + RetireSpread(d.id)) return true;
    // 2b) (REMOVED in that pass) this used to hold "rating below RETIRE_RATING_FLOOR once already a veteran".
    // It was an ABSOLUTE threshold on a scale that now carries the tier anchor baked in, so a
    // karting driver crossed it just by existing, and an elite one never would. What replaces this branch
    // is their class's RELATIVE bar, which is what "no longer meets the level" actually means.
    return false;
}

std::vector<Promotion> PromoteFreedSeats(const std::map<int,int>& retiredByTier,
                                         const std::vector<RosterDriver>& cast,
                                         const std::map<std::string,double>& agedRating) {
    std::vector<Promotion> out;
    std::set<std::string> used;                                  // a driver doesn't take two seats
    for (const auto& kv : retiredByTier) {                       // std::map -> tiers in ascending order
        int toTier = kv.first;
        int seats  = kv.second;
        if (toTier <= 0 || seats <= 0) continue;
        // candidates eligible for THIS tier: tier_max >= toTier, rating >= floor, not already used.
        std::vector<std::pair<std::string,double>> cand;
        for (const auto& r : cast) {
            if (used.count(r.id)) continue;
            if (r.tier_max < toTier) continue;                   // career ceiling doesn't reach
            // -- and its FLOOR. This only looked at the ceiling, and with the cast limited to the cohort
            // (28 drivers with tier_min 1) it didn't show. Once the cast expanded to the whole world it came to
            // light: a free tier-1 seat was being offered to the best-rated driver in the world, so
            // Hamilton would "move up" from F1 to Formula Vee and stay there for eight seasons.
            // A promotion moves upward; downward goes through the demotion cascade, which knows
            // how many steps are reasonable.
            // 13/08 — IT WAS `>` AND IT HAD TO BE `>=`. With the strict one, whoever ALREADY runs at that same level
            // was still a candidate to “promote” to it: it is not a promotion, it is a lateral transfer in disguise.
            // And since it is sorted by rating, the one who took the free slot was always the BEST of the step
            // — i.e. the champion. Measured: Verstappen won three F1 titles in a row and in the third
            // appeared in Keystone_Cup_6095, and six of the HARBOR_CHALLENGE_8227 Cup elite left their category in the first
            // season. Both things were this.
            //
            // The rule was already written RIGHT two files away (`CascadePromoteToOpenSeats`:
            // `if (r.tier_min >= tier) continue;`). Once again the same rule in two places and one of the two
            // copies wrong — the same pattern that left the `ratingSkill` fix inert.
            // It asks about `tier_now` (the HIGHEST slot they hold today) and not `tier_min` (the
            // LOWEST): with `tier_min` a driver with several seats slipped in anyway, because their floor was
            // below the destination even if their ceiling was already there. `tier_now == 0` = holds no live slot
            // (cohort, rookie), and those do get promoted.
            if (r.tier_now > 0 && r.tier_now >= toTier) continue;   // already runs there or above: it is not a promotion
            if (r.tier_min > toTier) continue;                      // their floor is above the destination
            auto it = agedRating.find(r.id);
            double rr = (it == agedRating.end()) ? 0.0 : it->second;
            if (rr < bal::PROMOTE_MIN_RATING) continue;
            cand.push_back({ r.id, rr });
        }
        std::sort(cand.begin(), cand.end(), [](const std::pair<std::string,double>& a,
                                                const std::pair<std::string,double>& b) {
            if (a.second != b.second) return a.second > b.second; // rating desc
            return a.first < b.first;                             // tiebreak by id -> deterministic total order
        });
        for (int i = 0; i < seats && i < (int)cand.size(); i++) {
            out.push_back(Promotion{ cand[i].first, toTier });
            used.insert(cand[i].first);
        }
    }
    return out;
}

double WorldIsRealForClass(int startRealCount, int seasons, double retireReplaceRate) {
    if (startRealCount <= 0) return 0.0;
    if (seasons <= 0) return 1.0;
    double frac = 1.0;
    double keep = 1.0 - retireReplaceRate;
    if (keep < 0.0) keep = 0.0; if (keep > 1.0) keep = 1.0;
    for (int s = 0; s < seasons; s++) frac *= keep;               // deterministic geometric decay
    return frac;
}

bool WriteSeasonGridXml(const std::string& seriesId, const std::vector<GridDriver>& grid, const std::string& customAiDir,
                        int aiPct) {
    const SeriesDef* s = SeriesById(seriesId);
    if (!s) return false;
    std::string stem = XmlStemForClass(s->class_name);
    if (stem.empty()) return false;
    // Wired in on Jul 25: the class's REAL spread, not the club default. With the single-argument
    // overload, all 101 ladder classes shared spreadPct=6.0, which SATURATED the kSkillTransfer curve of the time
    // (extrapolated, last point 5.5% -> 0.30): FloorForSpread returned 0.30 for every class and kClass came out 2.3333
    // identical everywhere, so the writer was inflating the model's spread by ~2.4x (with the MEASURED table
    // from Jul 26 the ceiling sits at 13.28% and no class saturates anymore). Each class's spread comes from
    // docs/career/class_pace_curve.psv (109 classes, covering the 101 that get generated); a class with no row -> defaults.
    EnsureClassPaceLoaded();
    const ClassPace cp = ClassPaceFor(s->class_name);
    // The spread gets SPLIT, not duplicated. The one in class_pace_curve.psv is the TOTAL gap for
    // the class, and it comes from two sources at once: what separates the drivers and what separates the cars. It
    // used to consume it entirely for its race_skill band, and now it also writes its share into the scalars: without
    // this discount, the first grid in play would come out with TWICE the measured spread and get
    // blamed on the wrong scalar. DriverSpreadPct returns the total minus what the car ACTUALLY contributes
    // (already trimmed by the conservative cap), so the sum still reproduces the measured spread.
    const double spreadDriver = DriverSpreadPct(s->id, s->class_name, cp.spread_dry);
    // The MEDIAN shrinks by the SAME proportion: the two PSV numbers describe the same spread (the
    // last and the median relative to the first), and the car also spreads out the midfield, not just the tail.
    // Scaling only the floor would leave the median where it was and warp the class's shape.
    const double shrink = (cp.spread_dry > 0.0) ? (spreadDriver / cp.spread_dry) : 1.0;
    // 15/08: on an OVAL the whole grid goes to race_skill 1.0 (see career_customai.h). The class's family
    // is already known by the traits engine, so no new data is needed.
    const bool isOval = (FamilyForClass(s->class_name) == CarFamily::Oval);
    // 24/08 — THREE THINGS WERE COMPUTED HERE THAT THE WRITER NO LONGER USES: the class talent yardstick
    // (`TalentRefSpanFor`), the discipline (`openWheel`, which chose the Curve band) and the trim
    // `cp.curve_trim`. With the three-nail conversion the width is set by the spread of the CLASS and the
    // extremes, the live grid. The yardstick is still alive outside this path —the `[canon-vara]` diagnostic and
    // the measurement dumps use it—, but it no longer travels to the XML.
    std::string xml = BuildGridXml(grid, spreadDriver, cp.median_dry * shrink, isOval, aiPct);   // 09/09: at the class's slider
    if (!BackupAndWriteAi(customAiDir, stem, xml)) return false;
    // And a copy for each aero-kit VARIANT of the stem (ams2_class_variants.psv). The
    // live class at Northhurst Park is Juniper_Cup_LD, and an oval's is F-USA_*_SO/_SS/_SW: AMS2 looks up the XML by
    // that EXACT token, and with no file the whole grid falls back to generics, silently. Same content by
    // design (same cars, same liveries). Best-effort: the main one is already written, and a variant
    // that fails on disk shouldn't take down the whole class.
    for (const auto& vstem : VariantStemsFor(stem))
        BackupAndWriteAi(customAiDir, vstem, xml);
    // Tracks WHICH drivers we've put in this class's XML. It's the missing half
    // needed to detect, once on track, that AMS2 discarded the override (a livery_name that doesn't match
    // any installed livery) and fell back to its generics — today that happens in complete silence.
    { std::vector<std::string> names; names.reserve(grid.size());
      for (const auto& d : grid) names.push_back(d.name);
      AiVerifyRegisterNames(s->class_name, names); }
    // And it flags the hookups we write BLIND. With packs installed, a livery_name that
    // we don't know which SLOT it belongs to may have been renamed a while ago — and that car would silently lose its
    // entire override without a single symptom. It only speaks up when packs are present: without them, vanilla names are valid.
    { std::vector<std::string> blind;
      int nb = AuditGridLiveryBinding(s->class_name, grid, &blind);
      if (nb > 0) {
          std::string sample;
          for (const auto& b : blind) { if (!sample.empty()) sample += ", "; sample += "'" + b + "'"; }
          UserLog("skins", "%s: %d of %d liveries could not be anchored to a livery slot, so their names are "
                           "written blind (a livery pack may have renamed them; AMS2 drops the whole car's AI "
                           "override when a livery_name does not match). Unanchored: %s",
                  s->class_name.c_str(), nb, (int)grid.size(), sample.c_str());
      } }
    return true;
}

// Multiclass: a className -> map of SPORTSCAR classes (endurance/gt) that CO-EXIST at some meeting (same
// track+day-of-year on the calendar). In career mode only the sportscar world combines into multiclass (everything else is
// single-class due to disjoint scheduling), so only those classes can put the same driver on a grid twice.
static std::map<std::string, std::set<std::string>> BuildClassCoOccurrence() {
    std::map<std::pair<long, int>, std::vector<std::string>> meet;    // (track,doy) -> sportscar series present
    for (const auto& s : AllSeries()) {
        // 23/09 (portal package a69f1ccb): WITHOUT a discipline filter. Until today only "endurance" and "gt" went in
        // because all the curated groups were sportscar; with the portal's championships touring cars coexist
        // (EMBER_TROPHY_10646 KEYSTONE_MASTERS_11138+KEYSTONE_CHALLENGE_11179), stock cars (HARBOR_CHALLENGE_8227 87+92) and single-seaters (The Masters 74/78/83), and the filter left
        // the same person in two cars of the same meeting (rk_lena in KEYSTONE_MASTERS_11138 and KEYSTONE_CHALLENGE_11179; 100 cases measured). The real
        // gate is SameMulticlassGroup, which already leaves out championships that only share a venue.
        for (const auto& cr : CalendarForSeries(s.id)) {
            if (cr.doy <= 0 || cr.track_hash == 0) continue;
            meet[{ cr.track_hash, cr.doy }].push_back(s.id);
        }
    }
    // CURATED multiclass: two classes co-exist (for grid dedup purposes) only if their series are in the
    // SAME curated group AND share circuit+day. Consistent with MeetingClassSet: different championships that
    // share a venue (IVORY_CUP_12573 vs NIMBUS_MASTERS_5890 at Ashwick Ring) do NOT dedup drivers against each other.
    std::map<std::string, std::set<std::string>> co;
    for (const auto& kv : meet) {
        const auto& sids = kv.second;
        for (size_t i = 0; i < sids.size(); ++i)
            for (size_t j = 0; j < sids.size(); ++j) {
                if (i == j) continue;
                if (!SameMulticlassGroup(sids[i], sids[j])) continue;     // curated
                const SeriesDef* a = SeriesById(sids[i]);
                const SeriesDef* b = SeriesById(sids[j]);
                if (a && b && a->class_name != b->class_name) co[a->class_name].insert(b->class_name);
            }
    }
    return co;
}

// the class -> series preference that the app sets before pre-generating. See career_fields.h.
// Global and not a parameter because it is consumed by `BuildGenOrder`, which three different paths already use (the startup
// writer, the grid size and the class role) and all three have to see the SAME plan: if one
// composed with another owner, the number of names in the XML and the one the race asks for would stop matching.
// Same pattern as `SetObservedXmlNames`.
static std::map<std::string, std::string> g_classOwner;

void SetClassOwnerPreference(std::map<std::string, std::string> classToSeries) {
    g_classOwner = std::move(classToSeries);
    ResetGridSizeCache(); ResetGlobalIdentityIndex();   // changing owner changes the grid, so the per-class count expires
}
int ClassOwnerPreferenceCount() { return (int)g_classOwner.size(); }

// A generatable class from the ladder, with the series that writes it. Comes from the SAME sweep and the SAME order
// WriteAllClassGrids uses, pulled out here because a class's ROLE (blue flag) is read from this order and is also
// needed outside the boot path — see ClassRoleForSeries.
namespace {
struct Gen { std::string series_id, stem, cls; int tier; };

// 1) one series per stem (class): the MAIN one (empty branch_of) preferred over a branch (US/EU share a class).
// 2) FASTEST-FIRST order: the driver stays in the fastest class of the weekend. Tiebreak within the same
// tier: PROTOTYPES (Pemberton_Zel121/LMP/DPI/Group C...) before GT (genuinely faster at overall pace),
// then by stem. So Lucía lands in Pemberton_Zel121, not MERROWDOWN_MYR373, when both share a weekend and tier.
std::vector<Gen> BuildGenOrder() {
    std::vector<Gen> gens;
    std::set<std::string> doneStems;
    // THREE passes, not two. Pass 0 is the app's preference — the series being prepared for
    // the next startup —, and only if it says nothing does it fall back to the usual rule (primary, then branch).
    // Without this pass, whoever runs in the branch runs against the other series' grid: measured, 9 of 11
    // shared classes compose different people and Onyx_Cup_4824 differs in 36 drivers.
    for (int pass = 0; pass < 3; pass++) {
        for (const auto& s : AllSeries()) {
            if (IsEventOnlySeries(s)) continue;   // 01/10: no XML of ours: the AI of that class is set by the game
            if (pass == 0) {
                const auto pref = g_classOwner.find(s.class_name);
                if (pref == g_classOwner.end() || pref->second != s.id) continue;
            } else {
                bool isBranch = !s.branch_of.empty();
                if ((pass == 1) == isBranch) continue;                // pass1=primaries, pass2=branches
            }
            std::string stem = XmlStemForClass(s.class_name);
            if (stem.empty() || doneStems.count(stem)) continue;
            if (LiveriesForClass(s.class_name).empty()) continue;
            doneStems.insert(stem);
            gens.push_back({ s.id, stem, s.class_name, s.level });
        }
    }
    auto isProto = [](const std::string& cls) {
        std::string x; for (char c : cls) x += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        return x.find("pemberton_zel121") != std::string::npos || x.find("lmp") != std::string::npos ||
               x.find("dpi") != std::string::npos || x.find("proto") != std::string::npos ||
               x.find("hypercar") != std::string::npos || x.find("group_c") != std::string::npos ||
               x.find("group c") != std::string::npos;
    };
    std::sort(gens.begin(), gens.end(), [&](const Gen& a, const Gen& b) {
        if (a.tier != b.tier) return a.tier > b.tier;
        bool pa = isProto(a.cls), pb = isProto(b.cls);
        if (pa != pb) return pa;                                   // prototype (faster) wins the tiebreak
        return a.stem < b.stem;
    });
    return gens;
}

// A class's ROLE within its weekend, read from the FASTEST-FIRST order plus the
// CURATED co-occurrence map. If any class it shares a weekend with comes BEFORE it in that order, this one is the SLOW one.
// It's the SAME computation at boot and per event: there aren't two derivations that could drift apart.
ClassRole RoleFromOrder(const std::string& className,
                        const std::map<std::string, std::set<std::string>>& coOcc,
                        const std::map<std::string, int>& classOrder) {
    auto it = coOcc.find(className);
    if (it == coOcc.end() || it->second.empty()) return ClassRole::SingleClass;
    ClassRole role = ClassRole::FastestInMulticlass;               // co-exists with others and, for now, takes precedence
    auto mi = classOrder.find(className);
    const int mine = (mi != classOrder.end()) ? mi->second : 0;
    for (const auto& co : it->second) {
        auto oit = classOrder.find(co);
        if (oit != classOrder.end() && oit->second < mine) role = ClassRole::SlowerInMulticlass;
    }
    return role;
}
}  // anon

const SeriesDef* SeriesForClassName(const std::string& className) {
    if (className.empty()) return nullptr;
    const SeriesDef* branch = nullptr;
    for (const auto& s : AllSeries()) {
        if (s.class_name != className) continue;
        if (IsEventOnlySeries(s)) continue;      // 01/10: an event-only series is never the owner of its class
        if (s.branch_of.empty()) return &s;      // the main one wins, same rule as the BATCH generator
        if (!branch) branch = &s;                // ...and if there are only branches, the first one in the catalog
    }
    return branch;
}

void ApplyHomageGridModel(const std::string& className, std::vector<GridDriver>& grid) {
    if (className.empty() || grid.empty()) return;
    ApplyTeamCarScalars("", className, /*season*/0, grid);
    ApplyTeamCarReliability("", className, /*season*/0, grid);
}

ClassRole ClassRoleForSeries(const std::string& seriesId) {
    const SeriesDef* s = SeriesById(seriesId);
    if (!s) return ClassRole::SingleClass;
    const std::map<std::string, std::set<std::string>> coOcc = BuildClassCoOccurrence();
    auto it = coOcc.find(s->class_name);
    if (it == coOcc.end() || it->second.empty()) return ClassRole::SingleClass;   // single-class: doesn't even build the order
    const std::vector<Gen> gens = BuildGenOrder();
    std::map<std::string, int> classOrder;
    for (int i = 0; i < (int)gens.size(); i++) classOrder[gens[i].cls] = i;
    return RoleFromOrder(s->class_name, coOcc, classOrder);
}

// Pass 3 — the BATCH generator (all classes at once; see header). With MULTICLASS dedup (Pass 5):
// it processes classes from FASTEST to SLOWEST, and on every multiclass weekend a driver stays ONLY in the
// fastest class (excluded from the slower ones sharing the weekend). Cohort and real drivers alike.
// HOW MANY NAMES EACH CLASS'S XML ACTUALLY LEAVES.
//
// The XML is PER CLASS and `WriteAllClassGrids` writes it with multiclass EXCLUSION: a driver already placed in
// a faster class on the same weekend isn't repeated in the slower one. That leaves FEWER names than that class
// would compose on its own — measured on disk on Aug 6, GT4.xml had 23 while `harbor_championship_5234` was asking for 26. And since the same
// class is run by TWO series (GT4 = harbor_championship_5234 + onyx_cup_5193), the series not in the group was requesting its full
// count against a file trimmed by the other series' group. Three generics, silently.
//
// This replicates the SAME loop (same order, same accumulation) without writing anything, and it's what bounds
// GridSizeForSeries. It's computed once and invalidated on data reload: composing all 101 classes costs
// about 4ms measured, too much to pay on every query and nothing to pay once.
static std::map<std::string, int> g_xmlNames;
static bool g_xmlNamesReady = false;

void ResetGridSizeCache() { g_xmlNames.clear(); g_xmlNamesReady = false; }

static const std::map<std::string, int>& XmlNamesPerClass() {
    if (g_xmlNamesReady) return g_xmlNames;
    const std::vector<Gen> gens = BuildGenOrder();
    const auto coOcc = BuildClassCoOccurrence();
    std::map<std::string, int> classOrder;
    for (int i = 0; i < (int)gens.size(); i++) classOrder[gens[i].cls] = i;
    std::map<std::string, std::set<std::string>> placed;
    for (const auto& g : gens) {
        std::set<std::string> excl;
        const ClassRole role = RoleFromOrder(g.cls, coOcc, classOrder);
        auto it = coOcc.find(g.cls);
        if (it != coOcc.end())
            for (const auto& co : it->second) {
                auto pit = placed.find(co);
                if (pit != placed.end()) excl.insert(pit->second.begin(), pit->second.end());
            }
        auto grid = ComposeSeasonGrid(g.series_id, g.tier, /*playerForm=*/0.0, GridSizeForClassName(g.cls),
                                      /*useHomage=*/false, EmptyRivalForm(),
                                      excl.empty() ? nullptr : &excl, nullptr, role, nullptr);
        if (grid.empty()) continue;
        std::set<std::string>& ids = placed[g.cls];                   // by ID (see career_fields.h)
        for (const auto& d : grid) if (!d.driver_id.empty()) ids.insert(d.driver_id);
        g_xmlNames[g.cls] = (int)grid.size();
    }
    g_xmlNamesReady = true;
    return g_xmlNames;
}

// the CURRENT deployment plan. See career_fields.h.
std::vector<ClassDeployment> CurrentClassDeployments() {
    std::vector<ClassDeployment> out;
    for (const auto& g : BuildGenOrder()) {
        const auto pref = g_classOwner.find(g.cls);
        const bool porPreferencia = (pref != g_classOwner.end() && pref->second == g.series_id);
        out.push_back(ClassDeployment{ g.cls, g.series_id, porPreferencia ? "next_race" : "main" });
    }
    return out;
}

unsigned long ClassDeploymentStamp() {
    if (g_classOwner.empty()) return 0UL;          // I don't know; see career_fields.h
    unsigned long h = 2166136261UL;
    auto mezcla = [&h](const std::string& s) {
        for (unsigned char c : s) { h ^= c; h *= 16777619UL; }
        h ^= 0x2Fu; h *= 16777619UL;
    };
    for (const auto& cd : CurrentClassDeployments()) { mezcla(cd.class_name); mezcla(cd.owner_series_id); }
    return h ? h : 1UL;                            // 0 is reserved for "I don't know"
}

int WriteAllClassGrids(const std::string& customAiDir,
                       const std::map<std::string, float>& rivalForm, bool useHomage,
                       std::vector<std::pair<std::string, std::vector<GridDriver>>>* out,
                       const std::map<std::string, double>* driverRatings,
                       const std::map<std::string, std::vector<SeatRow>>* seatsBySeries,
                       const std::map<std::string, std::set<std::string>>* playerLiveriesBySeries,
                       const std::map<std::string, int>* driverRaces,
                       const std::map<std::string, double>* driverCurve,
                       const std::map<std::string, int>* birthYear,
                       int worldYear,
                       const CareerProgress* prog) {
    std::vector<Gen> gens = BuildGenOrder();
    auto coOcc = BuildClassCoOccurrence();
    // 3) generates by accumulating: each class excludes drivers already placed in a FASTER class sharing the weekend.
    std::map<std::string, std::set<std::string>> placed;              // className -> driver_id already placed ()
    int written = 0;
    // Each class's position in the FASTEST-FIRST order above. With it, the class's ROLE within
    // its multiclass weekend is a direct lookup: if any class it shares the weekend with comes BEFORE it in that
    // order, this one is the SLOW one (and its blue flag is set to 0 so it doesn't keep pulling aside).
    std::map<std::string, int> classOrder;
    for (int i = 0; i < (int)gens.size(); i++) classOrder[gens[i].cls] = i;
    for (const auto& g : gens) {
        std::set<std::string> excl;
        const ClassRole role = RoleFromOrder(g.cls, coOcc, classOrder);   // the SAME computation as ClassRoleForSeries
        auto it = coOcc.find(g.cls);
        if (it != coOcc.end()) {
            for (const auto& co : it->second) {
                auto pit = placed.find(co);
                if (pit != placed.end()) excl.insert(pit->second.begin(), pit->second.end());
            }
        }
        // the XML writes EXACTLY the real grid size, not the whole livery catalog.
        // With more names than seats, AMS2 picks who races, and that choice isn't ours: the championship
        // standings would change rivals between races. With the two numbers matched, everyone on track is ours.
        int gs = GridSizeForClassName(g.cls);
        // forward driverRatings so the BOOT grids reflect each driver's seeded Elo
        // (same rating->race_skill derivation as the per-event regen paths), not the flat static curve.
        // THE LIVING-WORLD -> GRID BRIDGE, on the path that actually ends up in the XML AMS2 reads. Without
        // this, everything the market moves during the season stays in the model and never reaches the tarmac.
        const std::vector<SeatRow>* asientos = nullptr;
        if (seatsBySeries) {
            const auto sit = seatsBySeries->find(g.series_id);
            if (sit != seatsBySeries->end() && !sit->second.empty()) asientos = &sit->second;
        }
        // the livery the player occupies in THIS series gets replaced by another free one (see ComposeSeasonGrid)
        const std::set<std::string>* mias = nullptr;
        if (playerLiveriesBySeries) {
            const auto mit = playerLiveriesBySeries->find(g.series_id);
            if (mit != playerLiveriesBySeries->end() && !mit->second.empty()) mias = &mit->second;
        }
        // 18/08 — and the AGE of whoever drives, on the path that ENDS UP IN THE XML that AMS2 reads. It is the same
        // trio that `RunWorldSeason` and `ComposeSeasonGridForSave` pass: if the pregen didn't pass it, the world
        // would decide results with one pace and the track file would write another.
        auto grid = ComposeSeasonGrid(g.series_id, g.tier, /*playerForm*/0.0, gs, useHomage, rivalForm,
                                      excl.empty() ? nullptr : &excl, driverRatings, role, asientos, mias,
                                      driverRaces, driverCurve, birthYear, worldYear);
        if (grid.empty()) continue;
        // the ID is registered, which is the identity. And it is registered AFTER composing, so that it
        // picks up the final occupant (the one the living world seated), not the one the static file said.
        std::set<std::string>& ids = placed[g.cls];
        for (const auto& d : grid) if (!d.driver_id.empty()) ids.insert(d.driver_id);
        career::RebindGridForSkins(g.cls, grid, g.series_id);   // "real skins" Task 3: livery_name -> NAME of the installed skin (no-op without overrides). the series, to look up the curated binding
        // 09/09 (): each class is written at the slider it will run at (no save, 0 = the table as is)
        const int aiPct = prog ? EffectiveAiForClass(*prog, g.cls, g.tier) : 0;
        if (WriteSeasonGridXml(g.series_id, grid, customAiDir, aiPct)) {
            written++;
            if (out) out->push_back({ g.series_id, std::move(grid) });
        }
    }
    return written;
}

// Living World: modern/historic split + the active-modern world set ---

bool IsModernSeries(const std::string& seriesId) {
    const SeriesDef* s = SeriesById(seriesId);
    if (!s) return false;                                  // unknown series -> not modern
    if (IsEventOnlySeries(*s)) return false;               // 01/10: event-only: neither modern nor era, outside the world
    // 13/08 — THE TAG DECIDES (user decision: not a per-year cutoff in code, but a specific
    // tag). The year can't tell "old because there is already a new one" from "old because it's the only
    // one of its discipline": with the cutoff at 2015, the 2015 and 2019 F1 were live market and Vettel
    // competed for seats in the current F1; and raising it to 2023 would have frozen Brazilian endurance
    // whole (P4->P3->P2->P1, levels 3 to 6) and MERROWDOWN_MYR373, which have no relay. Without a tag, the previous criterion.
    if (s->market_tag == 1) return true;
    if (s->market_tag == 2) return false;
    // Parse a leading 4-digit year out of era ("2024","1991"…). No parseable year (fictional/timeless
    // Reiza formulas, karts) => modern (spec "fictional / timeless / current => modern ladder").
    const std::string& era = s->era;
    int year = 0, digits = 0;
    for (char c : era) {
        if (c >= '0' && c <= '9') { year = year * 10 + (c - '0'); if (++digits == 4) break; }
        else if (digits > 0) break;                        // year token ended
    }
    if (digits < 4) return true;                           // no full year -> timeless/fictional -> modern
    return year >= bal::MODERN_ERA_CUTOFF;                 // 2020/2021/2024 modern; 1991/2004/2005 historic
}

std::set<std::string> ActiveModernSet(const std::map<std::string, int>& birthYear,
                                      const std::set<std::string>& retired,
                                      int currentYear,
                                      const std::string& seriesFilter) {
    std::set<std::string> out;
    // resolve a driver's birth year: the SaveData override map first, else the pool's own datum (BirthYearFor).
    auto birthOf = [&](const std::string& id) -> int {
        auto it = birthYear.find(id);
        if (it != birthYear.end() && it->second > 0) return it->second;
        return BirthYearFor(id);                            // 0 if unknown -> age unknown -> treated as alive
    };
    // alive = not retired AND (age unknown OR age within the active window). currentYear 0 => age unknown => alive.
    auto isActive = [&](const std::string& id) -> bool {
        if (id.empty()) return false;
        if (retired.find(id) != retired.end()) return false;   // explicitly retired
        int age = DriverAge(birthOf(id), currentYear);          // 1 = unknown
        if (age < 0) return true;                               // unknown age -> alive (don't mis-classify into a drop)
        return age <= bal::ACTIVE_MAX_AGE;                      // older than the retire window -> no longer active
    };
    // collect every driver_id seating in a MODERN-ladder series' field this season, filtered alive/not-retired.
    auto harvestSeries = [&](const std::string& sid) {
        if (!IsModernSeries(sid)) return;
        // reals: PSV fields carry NO id (only the JSON path mints them) -> resolve by name (same as SeedAllDrivers)
        // so the active set is keyed identically to the seeded pool; an unresolved name is skipped by isActive.
        for (const auto& fd : FieldForSeries(sid)) {
            std::string id = fd.id;
            if (id.empty()) id = DriverIdForName(fd.name, fd.country);
            if (isActive(id)) out.insert(id);
        }
        // the recurring cohort (rk_*): the roster seated at this series' tier (the cast that races here)
        const SeriesDef* sd = SeriesById(sid);
        int level = sd ? sd->level : 1;
        for (const auto& r : RosterForTier(level)) if (isActive(r.id)) out.insert(r.id);
    };
    if (!seriesFilter.empty()) {
        harvestSeries(seriesFilter);                            // the screen's single-series filter
    } else {
        for (const auto& s : AllSeries()) harvestSeries(s.id);  // the whole modern world
    }
    return out;
}

std::vector<std::string> ValidateFieldsData() {
    std::vector<std::string> errs;
    for (const auto& d : g_fields) {
        if (!SeriesById(d.series_id)) errs.push_back("orphan field: series '" + d.series_id + "'");
        if (d.name.empty()) errs.push_back("field without name in series '" + d.series_id + "'");
    }
    for (const auto& s : AllSeries()) {
        if (IsEventOnlySeries(s)) continue;              // 01/10: event-only carries no field (no grid is composed)
        std::vector<int> orders;
        for (const auto& d : g_fields) if (d.series_id == s.id) orders.push_back(d.order);
        int c = (int)orders.size();
        if (c < 8) { errs.push_back("series '" + s.id + "' field too small (" + std::to_string(c) + "<8)"); continue; }
        std::sort(orders.begin(), orders.end());          // orders must be contiguous 1..N (the curve assumes it)
        for (int i = 0; i < c; i++) if (orders[i] != i + 1) { errs.push_back("series '" + s.id + "' orders not contiguous 1.." + std::to_string(c)); break; }
    }
    bool covered[8] = { false,false,false,false,false,false,false,false };   // indices 1..7 used; 0 unused
    int hot = 0;
    for (const auto& r : g_roster) {
        if (r.hot) hot++;
        for (int t = r.tier_min; t <= r.tier_max && t <= 7; t++) if (t >= 1) covered[t] = true;
    }
    for (int t = 1; t <= 7; t++) if (!covered[t]) errs.push_back("roster does not cover tier " + std::to_string(t));
    if (hot < 1) errs.push_back("roster has no hot rival");

    // INVARIANT 1 (kills cast duplication): each tier's ELIGIBLE roster must cover the number
    // of freed seats any series in that tier requests. need = the tier's WORST-CASE demand =
    // ReplaceCount(tier, large-grid) (ReplaceCount saturates at the per-tier cap: 6/5/4, so the large
    // grid gives the max). If the eligible roster falls below that, ComposeSeasonGrid would CYCLE the cast
    // (duplicate rivals, the "Axel Brandt 2/3" artifact). With this invariant satisfied, it NEVER cycles.
    for (int t = 1; t <= 7; t++) {
        int need = ReplaceCount(t, 99);                 // large grid -> the tier's cap (6/5/4)
        int eligible = (int)RosterForTier(t).size();
        if (eligible < need)
            errs.push_back("tier " + std::to_string(t) + ": roster eligible (" + std::to_string(eligible) +
                           ") < freed seats needed (" + std::to_string(need) + ") -> cast would duplicate");
    }

    // INVARIANT 2 (the cast doesn't over-displace the real field): every series with a field[] must be able to
    // sustain a FLOOR of real drivers. In the model, the real field occupies the BEST seats (top-by-order)
    // and the cast takes ONLY the freed ones; the cast never truncates the field if the grid fits it (grid >= fieldSize +
    // freed). The floor of real drivers a series can keep is therefore its own fieldSize, and we require
    // it to cover `floor = min(8, fieldSize)` (the same threshold as the field>=8 invariant, expressed in the
    // displacement's vocabulary). If a series with a field declared fewer real drivers than the floor, the cast
    // would have to over-displace/duplicate to fill even a minimal grid -> error. RealFieldCount
    // applied to the grid "that fits" (= fieldSize + freed) materializes the number of real drivers kept (= fieldSize).
    {
        std::set<std::string> seriesWithField;
        for (const auto& d : g_fields) seriesWithField.insert(d.series_id);
        for (const auto& sid : seriesWithField) {
            const SeriesDef* sd = SeriesById(sid);
            if (!sd) continue;                          // orphan already reported above
            // Pass 2b: tier 1-2 (club) is cohort-dominant BY DESIGN -> the cast displaces the real field
            // entirely; the real-driver floor does NOT apply there (the opposite was the old real-dominant model).
            if (sd->level <= 2) continue;
            int fieldSize = (int)FieldForSeries(sid).size();
            if (fieldSize <= 0) continue;
            int floor = fieldSize < 8 ? fieldSize : 8;
            // real drivers kept, applied to the grid that fits the whole field (not truncated by a small grid):
            int realCount = RealFieldCount(sid, fieldSize + FreedSeatCount(sid, fieldSize));
            if (realCount < floor)
                errs.push_back("series '" + sid + "': real drivers kept (" + std::to_string(realCount) +
                               ") < real-field floor (" + std::to_string(floor) + ") -> field too short to sustain the floor");
        }
    }
    // Pinned rosters (series_rosters.psv). Whether livery_name exists in the
    // livery catalog is checked at the TEST level (like the feeder-vs-catalog check): here we don't depend
    // on the catalog. We do check: series exists, no duplicate liveries within the series, skill in (0,1].
    for (size_t i = 0; i < g_pinned.size(); i++) {
        const PinnedDriver& p = g_pinned[i];
        if (!SeriesById(p.series_id)) errs.push_back("orphan pinned: series '" + p.series_id + "'");
        if (p.name.empty()) errs.push_back("pinned without name in series '" + p.series_id + "'");
        if (p.livery_name.empty()) errs.push_back("pinned without livery: '" + p.name + "' (" + p.series_id + ")");
        if (p.skill <= 0.0 || p.skill > 1.0) errs.push_back("pinned skill out of (0,1]: '" + p.name + "' (" + p.series_id + ")");
        for (size_t j = i + 1; j < g_pinned.size(); j++)
            if (g_pinned[j].series_id == p.series_id && g_pinned[j].livery_name == p.livery_name)
                errs.push_back("duplicate pinned livery in '" + p.series_id + "': " + p.livery_name);
    }
    return errs;
}

} // namespace career
