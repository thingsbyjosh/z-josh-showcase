#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include "imgui.h"
// Profile data accessors. Deterministic shims over the live rating/REP + career_fields.
// Landed: ProfileWorldRanking now reads the real driver_ratings pool (filtered to the
// active-modern subset) and ProfileRating reads the persisted player `rating` directly (no wins/podiums anchor,
// no kWorld mock). Rivals and the career arc stay seeded/mock for now.
namespace career {

// forward declarations so the re-signatured pure contracts below can take the real
// save types by const-ref without including career_save.h (which would drag career_engine/sponsor/negotiate —
// and ultimately ImGui-adjacent deps — into the pure career_test TU). The full definitions live in career_save.h;
// only the bodies (career_profile.cpp, fed by the overlay/careerproof *Ui bridges) need the complete types.
struct RaceLogEntry;   // career_save.h — the per-race log row (carries the post-settle rating snapshot)
struct H2H;            // career_save.h — { int w, l; } the per-driver head-to-head record vs the player

struct RatingBandView { int idx; const char* key; ImU32 color; std::string es, en; };
struct ProfileRatingInfo {
    int score;                 // unified rating number (display) = prog.rating
    RatingBandView band;       // letter band (S/A..F) via RatingBand
    int next_min;              // min of the next-higher band (0 if at top)
    const char* next_key;      // letter of the next band ("" if at top)
    std::string next_es, next_en;
    int delta = 0;             // the per-race rating delta/trend ("+18 ↑"); 0 = no last race / no movement. No provisional badge.
};
// Player's rating + band, read directly from the persisted unified rating (prog.rating). `lastDelta` = the
// per-race delta from the last RatingSettleReport (surfaced as a trend, not a provisional flag).
ProfileRatingInfo ProfileRating(int playerRating, int lastDelta);

// A world-board row. `band` carries the number+band view (e.g. "1842 · A"). `you` marks the player row.
struct RankRow  { std::string name; int rating; bool you; RatingBandView band; };
// note_key = stable i18n key of the rival note (nearest/second/market/nemesis) for the external UI
// (career_api). The es/en literals stay for the in-game overlay; the key is what the app resolves with t().
struct RivalRow { std::string name, team; int rating; int h2h_w, h2h_l; std::string note_es, note_en; ImU32 color; const char* note_key = ""; };
struct ArcPoint { std::string season; int rating; };

// A single finisher row consumed by the pure h2h accumulation helper. Built at
// the live Settle from the already-classified grid (career_overlay.cpp reuses `cls`) and in career_test from a
// synthetic finishing order. `driver_id` = the stable pool id ("" = generic AI outside the pool -> discarded,
// `pos` = the classified rank (1 = winner); `carClass` = the finisher's class (for the multiclass
// same-class gate — "" disables the gate so a single-class race compares everyone).
struct H2HFinish { std::string driver_id; int pos = 0; std::string carClass; };

// Accumulate the player's win/loss vs each named pool opponent from a
// single race's finishing order. `order` = finishing-order rows (driver_id + pos + carClass); `playerId`/`playerPos`
// identify the player's own row (excluded — no self-h2h); rows with an empty driver_id are discarded (generic
// AI outside the pool). player ahead (playerPos < oppPos) -> h2h[id].w++; player behind -> h2h[id].l++. The
// DNF rule lives at the caller (career_overlay.cpp skips the duels when the player DNF'd, mirroring the rating model
// that excludes DNF — career_overlay.cpp:824); here the math is the pure positional comparison. `playerClass`
// gates the multiclass comparand: when both it and an opponent's carClass are non-empty, only same-class duels count
// (a GT3 isn't "beaten by" an Pemberton_Zel121 ahead in a multiclass race); an empty class on either side disables the gate
// (single-class race -> compare everyone). Mutates `h2h` in place. Pure: no globals, no I/O — exercised by
// career_test::test_rivals_h2h_accum and reused by the live-Settle sweep.
void AccumulateH2H(const std::vector<H2HFinish>& order, const std::string& playerId, int playerPos,
                   std::map<std::string, H2H>& h2h, const std::string& playerClass = std::string());

// World ranking — the real ELO pool. `pool` = the live driver_ratings map (id -> rating);
// `active` = the active-modern subset (ids with a seat in a modern-ladder class this season, alive, not
// retired — see career::ActiveModernSet). Only pool ids present in `active` are rendered; each is mapped to
// its display name via DriverById, given a band via RatingBand, then the player is inserted at `playerRating`
// and the whole list sorted descending. `seriesFilter` (optional) is reserved for the screen's series filter
// (the active set is already filtered upstream; an empty string = no extra filtering here). An empty pool
// renders just the player row (a defensive guard against an empty pool). Pure: no globals, no ImGui state, no I/O.
std::vector<RankRow>  ProfileWorldRanking(const std::map<std::string, double>& pool,
                                          const std::set<std::string>& active,
                                          const std::string& playerName, int playerRating,
                                          const std::string& seriesFilter = std::string());
// 1-based rank of the player in that list.
int                   ProfileMyRank(const std::map<std::string, double>& pool,
                                     const std::set<std::string>& active,
                                     const std::string& playerName, int playerRating,
                                     const std::string& seriesFilter = std::string());
// Rivals / nemesis — real hybrid selection. The 3 cards = RIVAL_PROXIMITY_COUNT (2)
// nearest-ELO neighbors over `activeClasses` (the union of the player's active-contract classes — the proximity
// scope) drawn from `pool` (id->rating), plus 1 market rival passed as (marketRivalId, marketRivalRating,
// marketRivalName) — the seat competitor / ELO-displacement signal. `h2h` (driver_id -> {w,l} vs the
// player) fills each card's record; before any captured race the card reads "—"/0–0; the nemesis is the
// worst-balance captured rival. Selection is deterministic (fnv1a tie-break, no RNG — same inputs => same
// 3 ids/order). Pure: no globals, no ImGui state, no I/O — fed by the overlay/careerproof ProfileRivalsUi()
// bridge (keeps g_save out of screen_profile.cpp) and exercised by career_test::test_rivals_selection.
// NOTE: the body stays mock for now; a later pass flips it to the real selection + h2h read.
std::vector<RivalRow> ProfileRivals(int playerRating,
                                    const std::set<std::string>& activeClasses,
                                    const std::map<std::string, double>& pool,
                                    const std::map<std::string, H2H>& h2h,
                                    const std::string& marketRivalId, int marketRivalRating,
                                    const std::string& marketRivalName);
// Career-arc points — real per-race rating history. Reads `log` (the persisted
// race_log, already date-ordered) for each entry's POST-settle `rating`, OMITS sentinel-0/legacy entries (no
// zero-spike), downsamples to <= bal::ARC_DOWNSAMPLE_POINTS preserving first+last, then appends the
// live "Now"/`playerRating` point last. An empty/all-legacy log returns just the "Now" point (no crash). `es`
// toggles the "Now"/"Ahora" label. Pure: no globals, no ImGui, no I/O — fed by the overlay/careerproof
// ProfileRatingArcUi() bridge and exercised by career_test::test_arc_read.
// NOTE: the body stays mock for now; a later pass flips it to the real race_log read.
std::vector<ArcPoint> ProfileRatingArc(const std::vector<RaceLogEntry>& log, int playerRating, bool es);

} // namespace career
