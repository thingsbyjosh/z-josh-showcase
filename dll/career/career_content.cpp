// (DLC v2) -- see career_content.h. Pure module (no game state): the live
// catalogs arrive via career_series (SetInstalledContent) and here they're just cross-referenced against dlc_products.psv.
#include "career_content.h"
#include "career_econ.h"       // TrackDlcPack, CircuitGeo/AllCircuitGeo (substitution)
#include "career_customai.h"   // ClassForVehicle (per-class override)
#include "paths.h"             // mojibake fix 22/07: FOpen (UTF-8 -> wide)
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <map>
#include <algorithm>
#include <atomic>

namespace career {

static std::vector<DlcProduct> g_products;
static std::map<std::string, std::string> g_trackPackToPid;   // track pack slug -> product_id
static std::map<std::string, std::string> g_classToPid;       // class_name -> product_id
static std::map<std::string, std::string> g_carToPid;         // normalized car name -> product_id
static std::map<std::string, std::string> g_pidToDisplay;     // product_id -> display
static std::set<std::string> g_disabled;                      // product_id with an OFF override (Settings > Content)
static std::set<std::string> g_forcedOn;                      // product_id with an ON override (set by hand)
static std::vector<std::pair<unsigned, std::string>> g_steamRows;   // (appid, product_id) pairs of kind=steam
static std::map<unsigned, bool> g_appOwned;                         // responses from the overlay's sweep
static std::atomic<bool> g_vetoBypass{ false };                     // cmd.debug.contentLog (session; the app forwards it)

// normalized car name (no spaces, lowercase) -- the SAME rule as career_customai::VehicleHashByName,
// so the PSV can write the name exactly as it comes out of the catalog, regardless of spacing.
static std::string NormCar(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == ' ' || c == '\t') continue; o += (char)tolower((unsigned char)c); }
    return o;
}

// local helpers (same style as the career_econ/career_data loaders; no dependencies) --
static std::vector<std::string> SplitPipeKeep(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == '|') { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}
static void StripEolBom(std::string& s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s.erase(0, 3);
}
static DlcProduct& ProductFor(const std::string& pid, const std::string& display) {
    for (auto& p : g_products) if (p.id == pid) return p;
    DlcProduct np; np.id = pid; np.display = display;
    g_products.push_back(np);
    return g_products.back();
}

bool LoadDlcProducts(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_products.clear(); g_trackPackToPid.clear(); g_classToPid.clear(); g_carToPid.clear(); g_pidToDisplay.clear();
    g_steamRows.clear();   // (g_appOwned responses are NOT cleared: they survive a catalog reload)
    char ln[512]; bool header = false;
    while (fgets(ln, sizeof(ln), f)) {
        std::string s(ln); StripEolBom(s);
        if (s.empty() || s[0] == '#') continue;
        if (!header) { header = true; continue; }               // header row key|kind|...
        std::vector<std::string> q = SplitPipeKeep(s);
        if (q.size() < 4 || q[0].empty() || q[2].empty()) continue;
        const std::string &key = q[0], &kind = q[1], &pid = q[2], &disp = q[3];
        if (!disp.empty()) g_pidToDisplay[pid] = disp;
        DlcProduct& p = ProductFor(pid, disp);
        if (kind == "track") { g_trackPackToPid[key] = pid; p.trackSlugs.push_back(key); }
        else if (kind == "class") { g_classToPid[key] = pid; p.classes.push_back(key); }
        else if (kind == "car") { g_carToPid[NormCar(key)] = pid; p.cars.push_back(key); }   // per car
        else if (kind == "steam") {   // the Steam appid that grants the product
            unsigned a = (unsigned)strtoul(key.c_str(), nullptr, 10);
            if (a) g_steamRows.push_back({ a, pid });
        }
    }
    fclose(f);
    return true;
}
int DlcProductCount() { return (int)g_products.size(); }
const std::vector<DlcProduct>& AllDlcProducts() { return g_products; }

std::string DlcProductForTrackPack(const std::string& packSlug) {
    if (packSlug.empty()) return std::string();
    auto it = g_trackPackToPid.find(packSlug);
    return it == g_trackPackToPid.end() ? std::string() : it->second;
}
std::string DlcProductForClass(const std::string& className) {
    auto it = g_classToPid.find(className);
    return it == g_classToPid.end() ? std::string() : it->second;
}
std::string DlcProductForCarName(const std::string& carName) {
    if (carName.empty()) return std::string();
    auto it = g_carToPid.find(NormCar(carName));
    return it == g_carToPid.end() ? std::string() : it->second;
}
std::string DlcProductForVehicle(int vehHash) {
    return DlcProductForCarName(VehicleNameForHash(vehHash));
}
std::string DlcProductDisplay(const std::string& productId) {
    auto it = g_pidToDisplay.find(productId);
    return it == g_pidToDisplay.end() ? std::string() : it->second;
}

// overrides ----
// two DISJOINT sets (explicit off / explicit on); being in neither = AUTO, and AUTO is resolved by
// the detection in DlcProductStates. The disabled set is still the one the content filter queries.
void SetDisabledProducts(const std::set<std::string>& ids) {
    g_disabled = ids;
    for (const auto& id : ids) g_forcedOn.erase(id);           // disjoint: whichever spoke last wins
}
bool ProductDisabled(const std::string& productId) { return g_disabled.count(productId) > 0; }
const std::set<std::string>& DisabledProducts() { return g_disabled; }
void SetForcedOnProducts(const std::set<std::string>& ids) {
    g_forcedOn = ids;
    for (const auto& id : ids) g_disabled.erase(id);
}
const std::set<std::string>& ForcedOnProducts() { return g_forcedOn; }

DlcOverride ProductOverride(const std::string& productId) {
    if (g_disabled.count(productId)) return DlcOverride::Off;
    if (g_forcedOn.count(productId)) return DlcOverride::On;
    return DlcOverride::Auto;
}
void SetProductOverride(const std::string& productId, DlcOverride v) {
    if (productId.empty()) return;
    g_disabled.erase(productId); g_forcedOn.erase(productId);
    if (v == DlcOverride::Off)     g_disabled.insert(productId);
    else if (v == DlcOverride::On) g_forcedOn.insert(productId);
}
void ClearProductOverrides() { g_disabled.clear(); g_forcedOn.clear(); }

bool LoadDlcOverrides(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return true;                       // missing file = no overrides (not an error)
    std::set<std::string> off, on;
    char ln[256];
    while (fgets(ln, sizeof(ln), f)) {
        std::string s(ln); StripEolBom(s);
        if (s.empty() || s[0] == '#') continue;
        std::vector<std::string> q = SplitPipeKeep(s);
        if (q.size() < 2 || q[0].empty()) continue;
        if (q[1] == "off")     off.insert(q[0]);
        else if (q[1] == "on") on.insert(q[0]);   // manually set to ON; an old install doesn't have these rows
    }
    fclose(f);
    for (const auto& id : off) on.erase(id);   // inconsistent file: off wins (never turn on what was asked to be turned off)
    g_disabled = off; g_forcedOn = on;
    return true;
}
bool SaveDlcOverrides(const char* path) {
    // without this, saving would fail SILENTLY on any install where the `cache` directory
    // didn't exist yet (nothing creates it on startup; it only got created as a side effect of whoever saved the ladder layout),
    // so every DLC toggle would reset to ON when the app reopened. That's the root cause of the report.
    if (path) paths::EnsureParentDir(path);
    FILE* f = paths::FOpen(path, "wb");
    if (!f) return false;
    fputs("# dlc_overrides.psv -- eleccion MANUAL del jugador en Ajustes>Contenido. product_id|off o product_id|on.\n"
          "# Un producto sin fila esta en AUTO: lo decide la deteccion del contenido instalado.\n", f);
    for (const auto& id : g_disabled) fprintf(f, "%s|off\n", id.c_str());
    for (const auto& id : g_forcedOn) fprintf(f, "%s|on\n", id.c_str());
    fclose(f);
    return true;
}

// real ownership via Steam (see career_content.h) ----
const std::vector<std::pair<unsigned, std::string>>& SteamAppidRows() { return g_steamRows; }
void SetSteamAppOwned(unsigned appid, bool owned) { if (appid) g_appOwned[appid] = owned; }
void ClearSteamOwnership() { g_appOwned.clear(); }
void SetOwnershipVetoBypass(bool on) { g_vetoBypass = on; }
bool OwnershipVetoBypass() { return g_vetoBypass; }

Ownership ProductOwnership(const std::string& productId) {
    if (productId.empty()) return Ownership::Unknown;
    bool any = false;                 // does any of the product's appids have a response?
    for (const auto& r : g_steamRows) {
        if (r.second != productId) continue;
        auto it = g_appOwned.find(r.first);
        if (it == g_appOwned.end()) continue;
        if (it->second) return Ownership::Owned;   // owning just ONE is enough (a standalone circuit grants its pack)
        any = true;
    }
    return any ? Ownership::NotOwned : Ownership::Unknown;
}

// 30/08 (design decision): the On override no longer lifts the ownership veto. Manually turning on a
// pack that Steam explicitly says is NOT yours amounts to running unpurchased content, and that's not
// something we offer: ownership is Steam's call, not one of our switches. Note the distinction that keeps
// the switch useful: this only bites on NotOwned (an explicit answer); if there's no scan or the product
// didn't answer, ownership is Unknown and this falls through fail-open here, so someone who has the pack
// and isn't recognized can still force it on. Turning it off by hand is always available (see TrackDisabledByOverride).
// Only exception: SetOwnershipVetoBypass (cmd.debug.contentLog, debug menu).
bool TrackVetoedBySteam(long trackHash) {
    if (g_vetoBypass) return false;
    if (g_appOwned.empty()) return false;                      // no sweep -> fail-open (zero cost)
    std::string pid = DlcProductForTrackPack(TrackDlcPack(trackHash));
    if (pid.empty()) return false;                             // base/unknown track -> never vetoed
    return ProductOwnership(pid) == Ownership::NotOwned;
}
bool VehicleVetoedBySteam(int vehHash) {
    if (g_vetoBypass) return false;
    if (g_appOwned.empty()) return false;
    // Only DECLARED cars (kind=car): their product is unambiguous. A class shared across packs (MERROWDOWN_MYR373,
    // Hypercars) is left fail-open on purpose: vetoing by class would take down cars from packs you actually own.
    std::string pid = DlcProductForVehicle(vehHash);
    if (pid.empty()) return false;
    return ProductOwnership(pid) == Ownership::NotOwned;
}

// effective filter ----
bool TrackDisabledByOverride(long trackHash) {
    if (g_disabled.empty()) return false;
    std::string pid = DlcProductForTrackPack(TrackDlcPack(trackHash));
    return !pid.empty() && g_disabled.count(pid) > 0;
}
bool VehicleDisabledByOverride(int vehHash) {
    if (g_disabled.empty()) return false;
    // the CAR overrides the class. Without this, disabling Highbourne Motors Pt2 wouldn't remove the Eskdale Gry320 from
    // Onyx Cup 12409 (its class is base content, not part of the pack), and disabling Coldstead Motors Pt1 would take down
    // cars from the Highbourne Motors packs that share the Hypercars class.
    std::string carPid = DlcProductForVehicle(vehHash);
    if (!carPid.empty()) return g_disabled.count(carPid) > 0;   // a declared car: its own product decides, and only that
    std::string cls = ClassForVehicle(vehHash);
    if (cls.empty()) return false;             // unknown vehicle: don't take it down (fail-open)
    std::string pid = DlcProductForClass(cls);
    return !pid.empty() && g_disabled.count(pid) > 0;
}

// series playability ----
bool SeriesPlayable(const SeriesDef& sd) {
    return ClassHasInstalledVehicle(sd.class_name, /*raw=*/false);
}
std::string SeriesMissingProduct(const SeriesDef& sd) {
    if (SeriesPlayable(sd)) return std::string();
    return DlcProductForClass(sd.class_name);
}

// track substitution ----
// VENUE key: the slug up to the first '_' with trailing digits stripped ("redley_bay"->"northhurst_park",
// "thornwick_bay"->"longbourne_ring"). Variants of the same venue share a prefix in the game's catalog.
static std::string VenueKey(const std::string& slug) {
    std::string v = slug.substr(0, slug.find('_'));
    while (!v.empty() && isdigit((unsigned char)v.back())) v.pop_back();
    return v;
}

// 19/09: the usual body, with two optional season filters: specific tracks (hash) and VENUES
// (VenueKey). Without filters it is exactly the choice from before.
static bool ResolveSubFiltered(long missingHash, const std::string& seriesEra, TrackSub& out,
                               const std::set<long>* exHash, const std::set<std::string>* exVenue) {
    CircuitGeo m;
    if (!FindCircuitGeo(missingHash, m)) return false;         // no geo data means no criterion: lock it
    if (m.slug.empty()) return false;
    int refEra = atoi(seriesEra.c_str());
    if (refEra <= 0) refEra = m.eraYear;                       // no series era: falls back to the track's own
    const std::string venue = VenueKey(m.slug);
    // 23/09 (): the substitute's CAPACITY counts. Without it, the IVORY_CUP_12573's Wildham round (36 cars) fell to
    // Wildham RX and came out with 8. A candidate is "short" if fewer cars fit than on the track it replaces and also
    // fewer than kSubShortCap (a track with 30+ slots is not penalized against one with 40: it is plenty for almost everything).
    // With no slot data (0) nothing is asserted: it is not short.
    constexpr int kSubShortCap = 30;
    const int capM = TrackGridCapacity((int)missingHash);
    auto shortOf = [&](long h) {
        const int capC = TrackGridCapacity((int)h);
        return (capM > 0 && capC > 0 && capC < capM && capC < kSubShortCap) ? 1L : 0L;
    };

    long bestSame = 0; long bestSameKey[3] = { 0, 0, 0 };      // (short, |eraDist|, hash) — same venue
    long bestOther = 0; long bestOtherKey[6] = { 0, 0, 0, 0, 0, 0 }; // (short, grade, cont, |era|, country, hash)
    for (const auto& kv : AllCircuitGeo()) {
        long h = kv.first;
        const CircuitGeo& c = kv.second;
        if (h == missingHash || c.slug.empty()) continue;
        if (!TrackInstalled((int)h)) continue;                 // only truly playable candidates
        if (exHash && exHash->count(h)) continue;              // 19/09: the season already runs it (or it already substitutes another round)
        const std::string cv = VenueKey(c.slug);
        if (exVenue && exVenue->count(cv)) continue;           // 19/09: ...or it runs ANOTHER layout of that venue
        long eraDist = labs((long)c.eraYear - (long)refEra);
        // 23/09 (): the type is a requirement ALSO within the venue. "Same venue wins" let through the
        // rallycross layout of Wildham or Northbrook Speedway as the host of an asphalt round: VenueKey merges them
        // ("faircombe" -> "wildham-francorchamps") and the type was not looked at in this branch.
        if (c.type != m.type) continue;                        // REQUIRED type (no karting on a GP date)
        if (cv == venue) {
            long key[3] = { shortOf(h), eraDist, h };
            bool better = !bestSame;
            for (int i = 0; i < 3 && !better; i++) {
                if (key[i] < bestSameKey[i]) better = true;
                else if (key[i] > bestSameKey[i]) break;
            }
            if (better) { bestSame = h; for (int i = 0; i < 3; i++) bestSameKey[i] = key[i]; }
        } else {
            if (c.flags.find("nosub") != std::string::npos) continue;   // monster layouts (Blackworth,
            // Elmmoor Speedway, tourist): they get run if the calendar calls for them, but they don't stand in for a normal round.
            // Order: grade > CONTINENT > era > country > hash. Continent comes before era because a championship
            // that loses a venue moves to another one in the same geographic scope, not to the other hemisphere (a European round
            // used to land on a Brazilian club circuit just because the era matched). Within a continent, era rules.
            long key[6] = { shortOf(h), c.grade == m.grade ? 0 : 1, c.continent == m.continent ? 0 : 1, eraDist,
                            c.country == m.country ? 0 : 1, h };
            bool better = !bestOther;
            for (int i = 0; i < 6 && !better; i++) {
                if (key[i] < bestOtherKey[i]) better = true;
                else if (key[i] > bestOtherKey[i]) break;
            }
            if (better) { bestOther = h; for (int i = 0; i < 6; i++) bestOtherKey[i] = key[i]; }
        }
    }
    // Same venue wins, unless it is SHORT and elsewhere there is one that is not (better another whole track than
    // the same venue with half a grid).
    long pick = bestSame ? bestSame : bestOther;
    if (bestSame && bestOther && bestSameKey[0] == 1 && bestOtherKey[0] == 0) pick = bestOther;
    if (!pick) return false;
    CircuitGeo cg; FindCircuitGeo(pick, cg);
    out.hash = pick; out.name = cg.name;
    out.origHash = missingHash; out.origName = m.name;
    out.productId = DlcProductForTrackPack(TrackDlcPack(missingHash));
    out.productDisplay = DlcProductDisplay(out.productId);
    return true;
}

// 19/09 (player report: Wildton Ring six times in the Thornwick Motors Ax797). With season memory
// (`exclude`) it is tried from most to least demanding: (1) a VENUE the season does not yet visit — the
// first pass of the suite gave Longmere and Pinegate in the same Thornwick Motors, two layouts that
// to the player are the same circuit repeated —; (2) at least one different TRACK; (3) if not even that is left (types
// with few circuits: rallycross, ovals), repeat rather than block the round.
bool ResolveTrackSubstitute(long missingHash, const std::string& seriesEra, TrackSub& out,
                            const std::set<long>* exclude) {
    if (exclude && !exclude->empty()) {
        std::set<std::string> venues;
        for (long h : *exclude) {
            CircuitGeo g;
            if (FindCircuitGeo(h, g) && !g.slug.empty()) venues.insert(VenueKey(g.slug));
        }
        if (ResolveSubFiltered(missingHash, seriesEra, out, exclude, &venues)) return true;
        if (ResolveSubFiltered(missingHash, seriesEra, out, exclude, nullptr)) return true;
    }
    return ResolveSubFiltered(missingHash, seriesEra, out, nullptr, nullptr);
}

// content availability for a special event ----
EventContentState EventContentFor(long trackHash, const std::string& playerSeriesId,
                                  const std::vector<std::string>& eventClasses) {
    EventContentState st;
    if (trackHash != 0 && !TrackInstalled((int)trackHash)) {
        st.trackOk = false;
        st.trackProduct = DlcProductDisplay(DlcProductForTrackPack(TrackDlcPack(trackHash)));
    }
    if (!playerSeriesId.empty()) {
        if (const SeriesDef* psd = SeriesById(playerSeriesId)) {
            if (!SeriesPlayable(*psd)) {
                st.classOk = false;
                st.classProduct = DlcProductDisplay(SeriesMissingProduct(*psd));
            }
        }
    }
    for (const auto& c : eventClasses) {
        if (c == playerSeriesId) continue;                  // the player's was already evaluated above
        const SeriesDef* sd = SeriesById(c);
        if (sd && !SeriesPlayable(*sd)) st.droppedClasses.push_back(c);
    }
    return st;
}

// per-product status (query.content) ----
// DETECTION is strict (no fail-open here), and when the product declares specific cars, it goes CAR BY CAR.
// The previous status logic used the blocking helpers, which answer "yes" when there's no live catalog, or when the
// class isn't in the catalog: on the DLC screen that showed up as "you own every pack". And even with a
// populated catalog it still lied on shared classes (owning the Yarrowby Tor384 or the Stanmore Sol246 from the Highbourne Motors
// packs would mark Coldstead Motors Pt1 as detected, because they share the Hypercars class).
// With no populated catalog, nothing is asserted: everything comes back NOT detected, and query.content already sets scanned=false so
// the app can say it hasn't been able to check yet. The actual blocking logic is still fail-open, which is a separate decision.
std::vector<DlcProductState> DlcProductStates() {
    std::vector<DlcProductState> out;
    const bool scanned = (InstalledTrackCount() > 0 || InstalledVehicleCount() > 0);
    std::set<std::string> packsDetected;
    if (InstalledTrackCount() > 0)
        for (const auto& kv : AllCircuitGeo()) {
            std::string pk = TrackDlcPack(kv.first);
            if (!pk.empty() && TrackInstalledRaw((int)kv.first)) packsDetected.insert(pk);
        }
    for (const auto& p : g_products) {
        DlcProductState st; st.id = p.id; st.display = p.display;
        const bool hasCars = !p.classes.empty() || !p.cars.empty();
        st.kind = (!p.trackSlugs.empty() && hasCars) ? "mixed" : (!p.trackSlugs.empty() ? "tracks" : "cars");
        bool det = false;
        if (scanned) {
            for (const auto& s : p.trackSlugs) if (packsDetected.count(s)) { det = true; break; }
            // declared cars -> THEY decide (the class would otherwise include cars from other packs)
            if (!det && !p.cars.empty()) {
                for (const auto& c : p.cars) {
                    int vh = VehicleHashByName(c);
                    if (vh && VehicleInstalledRaw(vh)) { det = true; break; }
                }
            } else if (!det) {
                for (const auto& c : p.classes) if (ClassHasInstalledVehicleStrict(c)) { det = true; break; }
            }
        }
        st.detected = det;
        // AUTO = whatever detection says. With no live catalog (scanned=false) nothing is asserted and everything is left
        // passing, which is the module's FAIL-OPEN rule, qualified by the app's "I haven't been able to
        // check yet" notice; if everything were switched off here, a first launch would look like an install with no DLC at all.
        // Steam OWNERSHIP tightens AUTO -- AMS2 lists DLC content even when it isn't owned
        // (the files ship to everyone), so "detected" isn't enough: NotOwned switches the product off under AUTO
        // even without a scan (Steam is authoritative). Unknown asserts nothing. The player's On/Off override still wins.
        st.owned = ProductOwnership(p.id);
        st.ovr = ProductOverride(p.id);
        // 30/08 (design decision): the manual switch CANNOT turn on a pack Steam considers NOT
        // OWNED. Turning it on would tell the mod "treat this content as if it were mine" -- a lever to
        // run what hasn't been bought, and that's not something we offer: ownership is Steam's call, not a toggle.
        // The legitimate escape is kept fully intact, because a broken detection's case is NOT NotOwned but
        // Unknown (no data means nothing is asserted) and there the On still takes over: exactly what someone
        // who does have the pack and isn't recognized needs. Turning it off by hand, on the other hand, is always possible.
        // Only exception: SetOwnershipVetoBypass (cmd.debug.contentLog, debug menu).
        const bool ownershipDenies = (st.owned == Ownership::NotOwned) && !g_vetoBypass;
        st.enabled = ownershipDenies             ? false
                   : st.ovr == DlcOverride::On   ? true
                   : st.ovr == DlcOverride::Off  ? false
                   : (scanned ? det : true);
        out.push_back(st);
    }
    return out;
}

} // namespace career
