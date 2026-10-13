#include "career_skins.h"
#include "career_seats.h"      // "real skins" Task 2: LiveryVehicleFor (vanilla livery_name -> vehHash/NN)
#include "career_customai.h"   // "real skins" Task 2: SlugForVehicleEnsured (vehHash -> image slug; guard-load, catalog not loaded at boot)
#include "career_fields.h"     // 27/08: DriverLiveryFor — the CURATED driver->livery binding, which overrides the heuristic
#include "paths.h"             // mojibake fix 2026-07-22: FOpen/ListDir/PathExists (UTF-8 -> wide)
#include <map>
#include <cstdio>
#include <algorithm>

namespace career {
static std::map<std::pair<std::string,int>, std::string> g_skins;      // (slug,id) -> NAME
static std::map<std::pair<std::string,int>, std::string> g_skinPrev;   // (slug,id) -> PREVIEWIMAGE PATH (rel),

void ResetSkinsForTest() { g_skins.clear(); g_skinPrev.clear(); }
int  InstalledSkinCount() { return (int)g_skins.size(); }

// End of the '>' tag RESPECTING quotes (a '>' inside NAME="a > b" doesn't count). npos if none.
static size_t findTagEnd(const std::string& s, size_t p) {
    bool inq = false;
    for (size_t i = p; i < s.size(); i++) {
        char c = s[i];
        if (c == '"') inq = !inq;
        else if (c == '>' && !inq) return i;
    }
    return std::string::npos;
}

// Extracts attr="..." inside [b,e) of the tag; "" if absent. Preserves byte for byte.
// The attribute name must be at a BOUNDARY (previous char = space/tab/nl/'<'): that way "LIVERY="
// doesn't match inside "BASELIVERY=" (same suffix). Keeps searching if a match lands inside another attribute.
static std::string attrVal(const std::string& s, size_t b, size_t e, const char* attr) {
    std::string key = std::string(attr) + "=\"";
    size_t p = b;
    while ((p = s.find(key, p)) != std::string::npos && p < e) {
        char prev = (p > 0) ? s[p - 1] : ' ';
        if (prev == ' ' || prev == '\t' || prev == '\n' || prev == '\r' || prev == '<') {
            size_t vs = p + key.size();
            size_t q = s.find('"', vs);
            if (q == std::string::npos || q > e) return "";
            return s.substr(vs, q - vs);
        }
        p += 1;   // match landed inside a longer attribute (e.g. BASELIVERY) -> keep going
    }
    return "";
}

// Parses an override file: for each <LIVERY_OVERRIDE ...> it pulls LIVERY (int), NAME and -- within its
// block (up to the next <LIVERY_OVERRIDE or the end) -- the <PREVIEWIMAGE PATH="..."> (thumbnail's relative path).
static void parseOverrideFile(const std::string& path, const std::string& slug) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }
    std::string s; s.resize((size_t)sz);
    fread(&s[0], 1, (size_t)sz, f); fclose(f);
    size_t p = 0;
    while ((p = s.find("<LIVERY_OVERRIDE", p)) != std::string::npos) {
        size_t e = findTagEnd(s, p);
        if (e == std::string::npos) break;
        std::string lid = attrVal(s, p, e, "LIVERY");
        std::string nm  = attrVal(s, p, e, "NAME");
        // This override's PREVIEWIMAGE lives between its opening tag and the next <LIVERY_OVERRIDE (or the end).
        size_t nextOv   = s.find("<LIVERY_OVERRIDE", e);
        size_t blockEnd = (nextOv == std::string::npos) ? s.size() : nextOv;
        std::string prev;
        size_t pv = s.find("<PREVIEWIMAGE", e);
        if (pv != std::string::npos && pv < blockEnd) {
            size_t pe = findTagEnd(s, pv);
            if (pe != std::string::npos && pe < blockEnd) prev = attrVal(s, pv, pe, "PATH");
        }
        if (!lid.empty() && !nm.empty()) {
            bool digits = true; for (char c : lid) if (c < '0' || c > '9') { digits = false; break; }
            if (digits) {
                int id = atoi(lid.c_str());
                g_skins[{ slug, id }] = nm;
                if (!prev.empty()) g_skinPrev[{ slug, id }] = prev;
            }
        }
        p = e + 1;
    }
}

// 2026-07-28 -- the override FOLDER isn't always named after the car. AMS2 resolves it by its
// asset name, which sometimes drops the brand prefix (rothbury_z06_gt3r -> oldacre_rothbury_z06_gt3r) or carries
// a variant suffix (formula_v8_g1_m -> formula_v8_g1, the Michelin-shod entry from the HARBOR TROPHY 929 pack).
// We index by folder, so without translating it, SkinNameFor(slug_real, slot) finds nothing and the
// rebind for that car is a SILENT no-op: it writes the vanilla name, which no longer exists once the pack is
// installed, and AMS2 discards the override for the entire car. It's caught by tools/content_coverage.py, which flags
// every folder that doesn't match any car. The file is optional: without it, everything behaves as before.
static std::map<std::string,std::string> g_overrideAlias;

static void loadOverrideAlias(const std::string& path) {
    g_overrideAlias.clear();
    if (path.empty()) return;
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return;
    char ln[512];
    while (fgets(ln, sizeof(ln), f)) {
        std::string s(ln);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        size_t bar = s.find('|');
        if (bar == std::string::npos) continue;
        std::string carpeta = s.substr(0, bar), slug = s.substr(bar + 1);
        if (carpeta.empty() || slug.empty() || carpeta == "carpeta") continue;   // skip the header
        g_overrideAlias[carpeta] = slug;
    }
    fclose(f);
}

int ScanInstalledSkins(const std::string& overridesDir, const std::string& aliasPath) {
    g_skins.clear();
    g_skinPrev.clear();
    loadOverrideAlias(aliasPath);
    // mojibake fix 2026-07-22: ListDir (UTF-8 -> wide); skips "." and ".." by contract
    for (const auto& fe : paths::ListDir(overridesDir + "\\*")) {
        if (!fe.isDir) continue;
        const std::string& carpeta = fe.name;
        // the ACTIVE override is <folder>\<folder>.xml (stem == folder); ignores _dist and per-round variants
        std::string active = overridesDir + "\\" + carpeta + "\\" + carpeta + ".xml";
        if (!paths::PathExists(active)) continue;
        auto it = g_overrideAlias.find(carpeta);
        parseOverrideFile(active, it == g_overrideAlias.end() ? carpeta : it->second);
    }
    return (int)g_skins.size();
}

std::string SkinNameFor(const std::string& vehicleSlug, int liveryId) {
    auto it = g_skins.find({ vehicleSlug, liveryId });
    return it == g_skins.end() ? std::string("") : it->second;
}

// "real skins" Task 4: presence without exposing the name (see comment in career_skins.h).
bool SkinSlotPresent(const std::string& vehicleSlug, int liveryId) {
    return !SkinNameFor(vehicleSlug, liveryId).empty();
}

// TEST-ONLY: direct injection into g_skins (see comment in career_skins.h). NOT for production.
void SetSkinForTest(const std::string& vehicleSlug, int liveryId, const std::string& name,
                    const std::string& previewRel) {
    g_skins[{ vehicleSlug, liveryId }] = name;
    if (!previewRel.empty()) g_skinPrev[{ vehicleSlug, liveryId }] = previewRel;
}

// "real skins" dumps ALL installed slots with their preview_rel (see header). Stable order
// (the map already sorts by (slug,id)). preview_rel is "" if that slot's override doesn't declare <PREVIEWIMAGE>.
std::vector<InstalledSkin> InstalledSkinSlots() {
    std::vector<InstalledSkin> out;
    out.reserve(g_skins.size());
    for (const auto& kv : g_skins) {
        InstalledSkin s;
        s.vehicle_slug = kv.first.first;
        s.livery_id    = kv.first.second;
        s.name         = kv.second;
        auto pit = g_skinPrev.find(kv.first);
        s.preview_rel  = (pit == g_skinPrev.end()) ? std::string() : pit->second;
        out.push_back(std::move(s));
    }
    return out;
}

std::string RebindLiveryName(const std::string& className, const std::string& vanillaName) {
    int vh = 0, nn = -1;
    if (!LiveryVehicleFor(className, vanillaName, vh, nn)) return vanillaName;   // not mappable -> left untouched
    // SlugForVehicleEnsured (NOT SlugForVehicle): guard-loads the vehicle catalog from paths::Data
    // if it isn't loaded yet. At BOOT (PregenAllAi) the catalog is NOT loaded (SeasonUiEnsureData loads
    // LoadLiveryVehicleMap but NOT LoadVehicleCatalog -- that one is lazy-loaded on UI queries). Without Ensured, in
    // production SlugForVehicle would always return "" and the rebind would be a permanent no-op.
    std::string slug = SlugForVehicleEnsured(vh);
    if (slug.empty()) return vanillaName;
    // 2026-07-28 -- LiveryVehicleFor returns the slot in CATALOG numbering, and g_skins is indexed in
    // GAME numbering, which runs one higher (see GameLiveryIdFromCatalogNN). Without converting, this
    // was resolving the PREVIOUS slot: with a pack installed the grid came out one livery off, and
    // slots with no neighbor simply found nothing and wrote the vanilla name, which no longer exists once
    // the pack is installed -- and then AMS2 silently discards the override for the ENTIRE car.
    std::string skin = SkinNameFor(slug, GameLiveryIdFromCatalogNN(nn));
    return skin.empty() ? vanillaName : skin;                                    // installed skin -> NAME
}

// 2026-08-09 -- A DRIVER'S CAR IS THE ONE THAT CARRIES THEIR NAME ----
//
// A player's report after testing in-game: "in F1 2025 I've seen several drivers mismatched, Tsunoda in
// Fairfield_Motors, Piastri in a Stonefield Motors, Hamilton in a Greystead Motors." Measured against their XML: 19 of 20 rows crossed in
// Juniper_Cup, and five other classes with the same problem.
//
// And it's NOT the per-slot rebind, which resolves correctly. It's that there are TWO independent curation passes over the same
// slot, and only one of them shows up on track:
//   . ours ties each driver to a VANILLA livery -- series_rosters.psv says `Max Verstappen ->
//     "Dundale Racing #6"`, and this class's vanilla teams are fictional (there's no Redmoor Racing 12424);
//   . the real-liveries pack REPAINTS that same slot with ANOTHER identity (the slot vanilla
//     called "Dundale Racing #6" the pack calls "Yuki Tsunoda").
// We translate slot->name correctly and the result is still Verstappen in Tsunoda's car.
//
// The rule is the only coherent one that exists: if a car's label NAMES a driver from THIS grid,
// the car is THEIRS. There's no alternative -- the pack doesn't ship a Stonefield Motors painted "Verstappen", so the
// paint's name IS the car's identity.
//
// Applied as a PERMUTATION: the set of liveries written doesn't change, so no paint gets duplicated
// and no car is left without a driver; only the crossed ones get uncrossed.
//
// AND WHAT THIS is **NOT**: it's not "every driver returns to their real car forever". The tie to the
// VANILLA livery exists for two reasons that still hold -- you can play WITHOUT any livery packs (in which case
// the labels are teams, not people, and this does nothing), and those seats WILL move: the market
// signs drivers, someone retires, new people come in. That's why only the AUTHORED curation gets repaired: a row whose
// occupant has been decided by the LIVING WORLD arrives with `seat_moved` set (marked by the seats layer) and stays
// exactly as is, paint and all, out of the shuffle. The day the market moves Verstappen
// to another team, that signing shows; without this guard the uncross would send them back to their old car and hide it.
//
// Deliberately conservative, and all four guards matter:
//   . matches by LAST NAME (last token of >=4 letters, no accents), never by first name;
//   . only when the pairing is UNIQUE IN BOTH DIRECTIONS. Without this, Harbor_Masters_8186 ("Thornton Racing 6930") would tie
//     all 22 cars to the driver surnamed Brackenridge Cor144 -- there the label is the MODEL, not an identity;
//   . a seat the market has moved (`seat_moved`) is untouchable, see above;
//   . whoever has no pairing KEEPS THEIR LIVERY if nobody claims it. Handing out the leftovers blindly
//     used to displace rows that were already fine (measured: Ivory_Cup_10072 5 crossed -> 6, Onyx_Cup_10277 2 -> 5).
//
// Measured against the user's XML with this version: +48 drivers in their own car (Juniper_Cup
// 0 -> 19 of 20 -- the 20th is a cohort driver with no car of their own, which is correct), Ivory_Cup
// 0 -> 14, Lantern_Trophy 14 -> 18, Ivory_Cup_10072 9 -> 13. And ZERO liveries moved among the ones that already matched
// (Pinnacle_Challenge_10113 31/32 and _2023 31/31 before and after): where there's nothing to fix, nothing gets touched.

// Folds UTF-8 to lowercase ASCII: "Hulkenberg" with and without the umlaut have to yield the same token, because the
// driver's name and the paint's name come from different files and only one of them carries the diaeresis.
// Separators (and any multibyte character outside Latin-1) turn into a space, i.e. they end a token.
static std::string FoldAsciiLower(const std::string& s) {
    // 64 slots = U+00C0..U+00FF (second byte 0x80..0xBF of the 0xC3 xx sequence). ' ' = no ASCII base.
    static const char* kBase = "aaaaaaaceeeeiiii" "dnooooo ouuuuy s" "aaaaaaaceeeeiiii" "dnooooo ouuuuy y";
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == 0xC3 && i + 1 < s.size()) {
            const char b = kBase[(unsigned char)s[++i] & 0x3F];
            o += (b == ' ') ? ' ' : b;
        } else if (c >= 'A' && c <= 'Z')                        o += (char)(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += (char)c;
        else                                                     o += ' ';   // everything else: ends the token
    }
    return o;
}

static std::vector<std::string> FoldedTokens(const std::string& s) {
    std::vector<std::string> t;
    std::string cur;
    const std::string f = FoldAsciiLower(s);
    for (char c : f) {
        if (c == ' ') { if (!cur.empty()) { t.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) t.push_back(cur);
    return t;
}

// Last name = last token of >=4 letters that isn't a number. The length cutoff excludes suffixes
// ("David Musial Jr" -> musial) and digit ones, the race numbers that sometimes hang off a name.
static std::string SurnameToken(const std::string& name) {
    const std::vector<std::string> t = FoldedTokens(name);
    for (size_t k = t.size(); k-- > 0; ) {
        if (t[k].size() < 4) continue;
        bool digits = true;
        for (char c : t[k]) if (c < '0' || c > '9') { digits = false; break; }
        if (!digits) return t[k];
    }
    return std::string();
}

void RepairDriverLiveryPairing(std::vector<GridDriver>& grid, const std::vector<bool>* intocable) {
    const int n = (int)grid.size();
    if (n < 2) return;
    // 27/08 () — an UNTOUCHABLE row neither offers its paint nor loses it, same as `seat_moved`.
    // Who is one and why, in the header: filled by `RebindGridForSkins`.
    auto quieto = [&](int j) {
        if (grid[j].seat_moved) return true;
        return intocable && j < (int)intocable->size() && (*intocable)[j];
    };
    std::vector<std::string> sur(n);
    for (int i = 0; i < n; i++) sur[i] = SurnameToken(grid[i].name);

    std::vector<int> owner(n, -1);      // label j -> driver it names (if it names exactly ONE)
    std::vector<int> named(n, 0);       // driver i -> how many labels name them
    for (int j = 0; j < n; j++) {
        if (quieto(j)) continue;                        // market or curated pairing: doesn't offer its paint
        const std::vector<std::string> lt = FoldedTokens(grid[j].livery_name);
        int hit = -1, hits = 0;
        for (int i = 0; i < n; i++) {
            if (sur[i].empty() || quieto(i)) continue;   // nor lose it: it can't be claimed
            if (std::find(lt.begin(), lt.end(), sur[i]) == lt.end()) continue;
            hits++; hit = i;
        }
        if (hits == 1) { owner[j] = hit; named[hit]++; }
    }

    std::vector<std::string> out(n);
    std::vector<char> fixed(n, 0);
    auto aceptada = [&](int j) { return owner[j] >= 0 && named[owner[j]] == 1; };
    int accepted = 0;
    for (int i = 0; i < n; i++) {                   // market and curated pairing: pinned where they are
        if (!quieto(i)) continue;
        out[i] = grid[i].livery_name; fixed[i] = 1;
    }
    for (int j = 0; j < n; j++) {
        if (!aceptada(j)) continue;
        out[owner[j]] = grid[j].livery_name;
        fixed[owner[j]] = 1;
        accepted++;
    }
    if (accepted == 0) return;                      // no label identifies anyone: nothing is touched

    std::vector<std::string> sobran;                // liveries that haven't been tied to their owner
    for (int j = 0; j < n; j++) if (!aceptada(j) && !quieto(j)) sobran.push_back(grid[j].livery_name);
    for (int i = 0; i < n; i++) {                   // 1) no pairing but their livery is still free -> they keep it
        if (fixed[i]) continue;
        auto it = std::find(sobran.begin(), sobran.end(), grid[i].livery_name);
        if (it == sobran.end()) continue;
        out[i] = *it; fixed[i] = 1; sobran.erase(it);
    }
    size_t k = 0;                                   // 2) the rest, from the leftovers in order (bijection intact)
    for (int i = 0; i < n; i++) if (!fixed[i] && k < sobran.size()) out[i] = sobran[k++];

    for (int i = 0; i < n; i++) grid[i].livery_name = out[i];
}

// "real skins" Task 3: applies the rebind to an entire grid, in-place. Only touches livery_name (see header).
// 2026-08-09: and right after, uncrosses driver<->car (see the long block above). It runs AFTER the rebind on
// purpose: the identity that must be respected is the installed pack's, which is what gets read when writing.
// 28/08 () — NO LONGER UNCROSSES. This function translates the vanilla name to the installed pack's and
// nothing more, which is what's asked («limit `career_skins` to resolving name/preview»).
//
// Until today it chained `RepairDriverLiveryPairing`, which read the LABEL when writing the XML and permuted
// the grid. That broke rule 4 of —«the runtime doesn't read a name to decide anything»— and I
// defended it as unavoidable because the player installs the pack. **That was false**: we only support
// the RECOMMENDED packs, which are ours and versioned, and the SLOT links the vanilla to the pack.
// Measured: of the 136 slots installed on the machine, the 106 that are in the recommended corpus
// match it EXACTLY. So who drives which car can be fully curated in advance.
//
// And once curated, what the uncrossing kept moving was ERROR MEASUREMENT, not contribution: from 58
// moves it dropped to 31, then to 11 on fixing the catalog->game offset, and to 2 on deriving the
// bindings from the label (`tools/gen_driver_liveries_from_labels.py`). And those last 2 were ITS OWN:
// it put Leonardo de Souza in `Longbourne Racing 1014 - R.Souza`, which is a different Souza — it matches only on
// surname and doesn't look at the initial.
//
// `RepairDriverLiveryPairing` stays exported and still has an owner: it's the CONTENT VALIDATOR the
// phase asks for. The suite's `[faseF-mueve]` diagnostic uses it to call out, by name, every row
// where our data puts someone in another driver's car. It must report ZERO.
void RebindGridForSkins(const std::string& className, std::vector<GridDriver>& grid,
                        const std::string& seriesId) {
    (void)seriesId;   // the uncrossing bound consumed it; kept in the signature for the validator
    for (auto& d : grid) d.livery_name = RebindLiveryName(className, d.livery_name);
}

// cc8Eelp0 -- the pairing we CANNOT resolve by slot.
//
// The packs' real mechanism is by numbered SLOT: they don't add liveries, they overwrite slot NN of a
// car and change its label. That's why the stable identity is (vehHash, NN) and the name is what's volatile...
// and the name is the ONLY thing CustomAIDrivers's schema accepts. The only defense possible is reasoning in
// slots internally and translating to a name AT WRITE TIME -- which is what RebindLiveryName does.
//
// But that translation has a blind spot: if LiveryVehicleFor doesn't know which (vehHash, NN) a
// vanilla livery_name corresponds to, RebindLiveryName returns the name AS IS, unable to check anything. With a pack
// installed on that car, that name may have stopped existing a while ago, and AMS2 will discard the whole
// car's override without a word. This COUNTS those cases: they're exactly the pairings we're writing
// blindly, and the number is the distance left before we can reason entirely in slots.
//
// It has no opinion if no override is installed at all: without packs, vanilla names are correct by definition
// and warning would just be noise. Returns how many drivers COULDN'T be anchored to a slot.
int AuditGridLiveryBinding(const std::string& className, const std::vector<GridDriver>& grid,
                           std::vector<std::string>* unresolved) {
    if (g_skins.empty()) return 0;            // with no packs installed there's nothing that could have been renamed
    int blind = 0;
    for (const auto& d : grid) {
        if (d.livery_name.empty()) continue;
        int vh = 0, nn = -1;
        if (LiveryVehicleFor(className, d.livery_name, vh, nn)) continue;   // anchored to a slot: checkable
        blind++;
        if (unresolved && unresolved->size() < 6) unresolved->push_back(d.livery_name);
    }
    return blind;
}
} // namespace career
