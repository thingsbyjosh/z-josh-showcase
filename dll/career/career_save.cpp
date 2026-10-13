#include "career_save.h"
#include "career_difficulty.h"  // career::Clampi + dda::kAiMin/kAiMax (clamps 'classai' + handles migration)
#include "career_data_json.h"   // RegisterGeneratedName (generated drivers get a name too)
#include "career_balance.h"   // bal::REP_TIER_RATING / RATING_ROOKIE_BASE (v1->v2 rating seed)
#include "career_econ.h"      // EconLog — ledger [money] (sponsor refund during the v3 migration)
#include "career_userlog.h"   // (Jul 20) [save] trail — save/load no longer fail silently
#include "career_market.h"    // Variable salary (Jul 21 2026): SalaryFor — heals legacy per_race values on load
#include "career_series.h"    // Variable salary: SeriesById/SeriesDef — the contract's series level
#include "career_calendar.h"  // Variable salary: kStartYear — fixed reference year (no calendar loaded yet)
#include "paths.h"            // Encoding fix (22/07): FOpen/RemoveFile/RenameFile (UTF-8 paths -> wide-char APIs)
#include "json_min.h"         // 0.7.0: profile.json (race pace, ProfileRewardModeSync)
#include "json_write.h"
#include <cerrno>
#include <cstdarg>   // 0.7.0 review: SaveSink (vsnprintf) of the clock stamp
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace career {

// Review 0.7.0 (finding 1): the SAME text that goes to disk also goes to memory, to seal the state of the
// clock (AdvanceSeal) without writing files. The lines below are still `fprintf(f, ...)`: `f` is a
// SaveSink and this overload routes it to the FILE* or to the std::string. That way a new format line is written as
// always and enters the seal on its own; `using ::fprintf` keeps the CRT one at hand for any FILE* in the file.
namespace {
struct SaveSink { FILE* file = nullptr; std::string* mem = nullptr; };
}
using ::fprintf;
static int fprintf(SaveSink* s, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = 0;
    if (s->file) {
        n = vfprintf(s->file, fmt, ap);
    } else {
        va_list cp; va_copy(cp, ap);
        n = vsnprintf(nullptr, 0, fmt, cp);
        va_end(cp);
        if (n > 0) {
            const size_t at = s->mem->size();
            s->mem->resize(at + (size_t)n + 1);
            vsnprintf(&(*s->mem)[at], (size_t)n + 1, fmt, ap);
            s->mem->resize(at + (size_t)n);
        }
    }
    va_end(ap);
    return n;
}

static void WriteCareer(const SaveData& d, SaveSink* f) {
    fprintf(f, "version %d\r\n", kSaveVersion);   // Schema version (read by LoadCareer -> MigrateSave)
    fprintf(f, "credits %ld\r\n", d.prog.credits);
    fprintf(f, "xp_total %ld\r\n", d.prog.xp_total);
    fprintf(f, "xp_toward_next %ld\r\n", d.prog.xp_toward_next);
    fprintf(f, "frontier_phase %d\r\n", d.prog.frontier_phase);
    fprintf(f, "rep %d\r\n", d.prog.rep);
    if (d.prog.titles != 0)
        fprintf(f, "titles %d\r\n", d.prog.titles);   // number of championships (additive; back-compat)
    // The player's live unified rating + provisional race counter. Always written (numeric, no string risk);
    // an older save simply lacks them and loads at default 0.0/0 -> MigrateSave seeds rating from REP.
    fprintf(f, "rating %.4f\r\n", d.prog.rating);
    fprintf(f, "rating_races %d\r\n", d.prog.rating_races);
    // The player's 0..100 EMA. Additive scalar line (mirrors 'rating'); numeric, no
    // string risk. A legacy save lacks it -> loads at default 0.0 -> Settle seeds 70 on first use
    // (the seed lives in Settle, NOT the parser). kSaveVersion stays 2 (no migration step needed).
    fprintf(f, "cleanliness %.4f\r\n", d.prog.cleanliness);
    // (Jul 21) Reward mode (chosen when creating the profile; not the same as difficulty_mode). Additive: an old save
    // without the line loads to 1 = Normal (×1.00) -> economy stays byte-identical to before.
    fprintf(f, "reward_mode %d\r\n",     d.prog.reward_mode);
    fprintf(f, "difficulty_mode %d\r\n", d.prog.difficulty_mode);
    fprintf(f, "custom_difficulty %d\r\n", d.prog.custom_difficulty);   // DM_CUSTOM: the fixed number chosen by hand
    fprintf(f, "ai_difficulty %d\r\n",   d.prog.ai_difficulty);
    fprintf(f, "adaptive_trim %d\r\n",   d.prog.adaptive_trim);
    fprintf(f, "ai_aggression %d\r\n",   d.prog.ai_aggression);   // (additive; Aug 26: a save without the key -> 60 Medium, the new default; an explicit 0 is still Auto)
    fprintf(f, "scale_tyre_wear %d\r\n",  d.prog.scale_tyre_wear ? 1 : 0);   // (additive; old save -> defaults to true)
    fprintf(f, "scale_fuel_usage %d\r\n", d.prog.scale_fuel_usage ? 1 : 0);  //
    // fcy_enabled (0/1) is kept for old DLLs; fcy_mode (0=No/1=Yes/2=Inherited) is the
    // source of truth and comes after deliberately: the loader processes in order, and the mode overrides what's derived from the bool.
    fprintf(f, "fcy_enabled %d\r\n",      d.prog.fcy_mode != 0 ? 1 : 0);
    fprintf(f, "fcy_mode %d\r\n",         d.prog.fcy_mode);
    // Weather mode, and — only if the player has touched the selection — its weights.
    // Write-when-present: a save that never opened the weather section stays byte-identical to the previous format.
    if (d.prog.wx_mode != 0) fprintf(f, "wx_mode %d\r\n", d.prog.wx_mode);
    if (!d.prog.wx_weights.empty()) {
        fprintf(f, "wx_weights");
        for (const auto& kv : d.prog.wx_weights) fprintf(f, " %d:%d", kv.first, kv.second);
        fprintf(f, "\r\n");
    }
    fprintf(f, "inherit_mask %d\r\n",     d.prog.inherit_mask);              // (additive; old save -> 0 = everything AUTO)
    fprintf(f, "calibrated %d\r\n",      d.prog.calibrated ? 1 : 0);
    if (d.stop_rules != StopRules{})   // C1: only if it differs from the defaults (byte-compat + downgrade)
        fprintf(f, "stoprules %d %d %d %d %d\r\n", d.stop_rules.offers ? 1 : 0, d.stop_rules.myRaces ? 1 : 0,
                d.stop_rules.anyRace ? 1 : 0, d.stop_rules.mail ? 1 : 0, d.stop_rules.relation ? 1 : 0);
    fprintf(f, "sponsor_tier %d\r\n", d.sponsor_tier);
    // career_seed is numeric and always written (no string risk, same as 'rating'). A
    // legacy save without this line loads to 0 -> shared-but-deterministic scarcity (accepted). Additive over v2.
    fprintf(f, "seed %lu\r\n", d.career_seed);
    // The player's identity, pipe-delimited (survives spaces in the name). Only written if
    // there's something to persist -> a save without identity stays byte-identical to the previous format (back-compat).
    if (!d.player.player_id.empty() || !d.player.name.empty() || !d.player.nationality.empty() || d.player.number != 0
        || !d.player.surname.empty() || !d.player.sex.empty() || d.player.birth_year != 0)
        fprintf(f, "player %s|%s|%s|%d|%s|%s|%d|%d|%d\r\n",
                d.player.player_id.c_str(), d.player.name.c_str(), d.player.nationality.c_str(), d.player.number,
                d.player.surname.c_str(), d.player.sex.c_str(), d.player.birth_year, d.player.birth_month, d.player.birth_day);
    // last nickname seen in shared memory (Steam/AMS2). When it changes, racesig migrates the championship tables
    // to the new name (merging them): they accumulate by name, and a name change used to split the player into two drivers.
    // Write-when-present: a save without the data stays byte-identical (back-compat).
    if (!d.shm_player_name.empty()) fprintf(f, "shmname %s\r\n", d.shm_player_name.c_str());
    // — THE PURCHASED CAR WAS BEING LOST ON RESTART. class_id can contain a SPACE ("Coldley Motors#<veh>",
    // "Group C#<veh>", "Juniper Trophy", "Group A", "Oldacre Orn227"), and the read used "%127s": it split at the first
    // space, the 2nd field stopped being a number, sscanf returned 1, and the `got >= 2` guard discarded the entire
    // entry, silently. Same defect already fixed in 'classdda' and 'seatocc'.
    // Separated with '|' — only when the id contains spaces. An id without spaces is still written exactly
    // as before (byte for byte), and this isn't cosmetic: an older build still reads those entries
    // the same way it always did. If the format changed for all of them, a downgrade would wipe out every car
    // instead of just the ones that are already being lost today.
    for (const auto& c : d.owned) {
        const bool needsBar = c.class_id.find(' ') != std::string::npos;
        fprintf(f, needsBar ? "owned %s|%d %d %.4f %d %d\r\n" : "owned %s %d %d %.4f %d %d\r\n",
                c.class_id.c_str(), c.vehid, c.races, c.wear, c.insured ? 1 : 0, c.livery);   // 6th field: livery (ordinal; additive)
    }
    // 0.7.0 (car sales): km per unit (car + livery) in an ADDITIVE line, only if there is km. The numbers
    // go first and the key to the end of the line, so a class_id with spaces ("Coldley Motors#...") comes back
    // whole; an earlier build ignores the unknown key and a save without the line loads with 0 km.
    for (const auto& c : d.owned)
        if (c.km > 0.0) fprintf(f, "ownedkm %d %.1f %s\r\n", c.livery, c.km, c.class_id.c_str());
    // same case as 'owned' -- the key is the real class_id, and five catalog classes contain a
    // space. Separated with '|' only when needed, so that an older build keeps reading the
    // entries that were never broken the same way.
    for (const auto& kv : d.class_stats)
        fprintf(f, kv.first.find(' ') == std::string::npos ? "stat %s %d %d %d %d\r\n" : "stat %s|%d %d %d %d\r\n",
                kv.first.c_str(), kv.second.races, kv.second.wins, kv.second.podiums, kv.second.dnf);
    for (const auto& id : d.completed_races)
        fprintf(f, "race %s\r\n", id.c_str());
    for (const auto& id : d.claimed_events)   // Mirror of completed_races
        fprintf(f, "claimedevent %s\r\n", id.c_str());
    // 19/09: what has ALREADY been paid per event (the difference is paid). NEW and additive line: the number first and the id
    // to the end of the line (, an id may carry spaces). An earlier build ignores it; without it, nothing paid.
    for (const auto& kv : d.event_paid)
        if (kv.second > 0) fprintf(f, "evpaid %ld %s\r\n", kv.second, kv.first.c_str());
    for (const auto& id : d.unlocked_gates)
        fprintf(f, "gate %s\r\n", id.c_str());
    // Active contract, pipe-delimited (series_id/team_id can contain spaces -> no %s). Only if it's
    // active -> a save without a contract stays byte-identical to the previous format (back-compat, same as 'player').
    // Multi-contract: one 'contract' line per active contract, with its 'contract_ext'/'deal' right after it (the
    // parser attaches them to the last contract read -> contracts.back()). No contracts -> nothing (back-compat).
    for (const auto& ct : d.contracts) {
        if (!ct.active) continue;
        fprintf(f, "contract %s|%s|%d|%d|%d|%d|%ld|%ld|%d\r\n",
                ct.series_id.c_str(), ct.team_id.c_str(), ct.seat_idx,
                ct.kind, ct.seasons_total, ct.seasons_left,
                ct.sign_bonus, ct.per_race, ct.signed_year);
        if (ct.obj_total || !ct.promote_to.empty() || ct.buy_in || ct.obj_target_pos)
            fprintf(f, "contract_ext %d|%d|%ld|%s|%d\r\n", ct.obj_done, ct.obj_total, ct.buy_in,
                    ct.promote_to.c_str(), ct.obj_target_pos);   // 0.4.0: +obj_target_pos (5th field, additive)
        const DealTerms& dt = ct.accepted_deal;
        int bits = 0;
        for (int i = 0; i < bal::NEGO_DEMAND_COUNT; i++) if (dt.demands[i]) bits |= (1 << i);
        if (!(dt.salaryPct == 50 && bits == 0 && dt.sponsorTierRank == -1))
            fprintf(f, "deal %d|%d|%d\r\n", dt.salaryPct, bits, dt.sponsorTierRank);
        // archetype, additive, write-when-non-default (default 0 = ARCH_SELF_FUNDED_ENTRY -> a
        // save without this line loads to the default via back-compat). Its own key (the ext line above is not
        // extended for this). Attaches to the last contract read (contracts.back()), like the other per-contract lines.
        if (ct.archetype != 0)
            fprintf(f, "contract_arch %d\r\n", ct.archetype);
        // Missed-round counter, additive, write-when-non-zero (default 0 -> a save without this
        // line loads to the default via back-compat). Its own key (the ext line is not extended). Attaches to the last
        // contract read (contracts.back()), like the other per-contract lines. kSaveVersion stays 2.
        if (ct.missed_rounds != 0)
            fprintf(f, "contract_miss %d\r\n", ct.missed_rounds);
        // Exclusivity flag, additive, write-when-true (default false -> back-compat).
        // Its own key; attaches to the last contract read (contracts.back()). kSaveVersion stays 2.
        if (ct.exclusive)
            fprintf(f, "contract_excl 1\r\n");
        // Per-contract premium-charged-year guard, additive, write-when-non-zero
        // (default 0 -> never charged -> back-compat). Its own key; attaches to the last contract read.
        if (ct.premium_year != 0)
            fprintf(f, "contract_prem %d\r\n", ct.premium_year);
    }
    for (const auto& id : d.declined_offers)   // Declined offers (same as the gate set)
        fprintf(f, "declined %s\r\n", id.c_str());
    // Courting (1 line per record, pipe-delimited: team_id/series_id can contain spaces). Only the ones
    // that exist are written -> a save with no courting stays byte-identical to the previous format (back-compat).
    for (const auto& c : d.courting)
        fprintf(f, "courting %s|%s|%d\r\n", c.team_id.c_str(), c.series_id.c_str(), c.first_seen_year);
    // Courting flags (met_rep / tier_locked) as a parallel line per index; only when set non-default.
    for (size_t i = 0; i < d.courting.size(); i++) {
        const auto& c = d.courting[i];
        if (c.met_rep || !c.tier_locked)
            fprintf(f, "courting_ext %zu|%d|%d\r\n", i, c.met_rep ? 1 : 0, c.tier_locked ? 1 : 0);
    }
    for (const auto& id : d.expired_offers)     // Expired offers (same pattern as 'declined'/'gate')
        fprintf(f, "expired %s\r\n", id.c_str());
    // offer_lifecycle: firstSeen per offer (id has no spaces -> %s is enough; absolute day as an integer).
    // Only present entries -> a save with no offers seen stays byte-identical to the previous format (back-compat).
    for (const auto& kv : d.offer_first_seen)
        fprintf(f, kv.first.find(' ') == std::string::npos ? "ofs %s %d\r\n" : "ofs %s|%d\r\n",
                kv.first.c_str(), kv.second);   // separator only if the key contains spaces
    for (const auto& id : d.lapsed_offers)      // offer_lifecycle: offers expired by TTL (same pattern as 'declined'/'expired')
        fprintf(f, "lapsed %s\r\n", id.c_str());
    // rival_form — one delta per cast driver (id has no spaces, %s is enough). Only non-null entries:
    // a save with no growth stays byte-identical to the previous format (back-compat). Bounded (~18 cast members).
    for (const auto& kv : d.rival_form)
        if (kv.second != 0.0f) fprintf(f, kv.first.find(' ') == std::string::npos ? "rivalform %s %.4f\r\n" : "rivalform %s|%.4f\r\n",
                kv.first.c_str(), kv.second);   // separator only if the key contains spaces
    // Per-AI-driver rating (id has no spaces -> %s is enough; double with 4 decimals). Only present entries
    // -> a save with no evolved world stays byte-identical (back-compat). The player's rating lives in prog.rating.
    for (const auto& kv : d.driver_ratings)
        fprintf(f, kv.first.find(' ') == std::string::npos ? "driverrating %s %.4f\r\n" : "driverrating %s|%.4f\r\n",
                kv.first.c_str(), kv.second);   // separator only if the key contains spaces
    // Per-AI-driver races-completed counter (id has no spaces -> %s; integer). Only entries with
    // n>0 -> a save with no evolved world (or predating this field) stays byte-identical (additive back-compat).
    for (const auto& kv : d.driver_races)
        if (kv.second > 0) fprintf(f, kv.first.find(' ') == std::string::npos ? "driverraces %s %d\r\n" : "driverraces %s|%d\r\n",
                kv.first.c_str(), kv.second);   // separator only if the key contains spaces
    // each team's position last year. The POSITION goes first and the key last because the
    // team label HAS SPACES ("Inception Racing"): that way the reader gets the integer with sscanf and keeps
    // the rest of the line as is. Only entries with position > 0 -> byte-identical back-compat.
    for (const auto& kv : d.team_last_rank)
        if (kv.second > 0) fprintf(f, "teamrank %d %s\r\n", kv.second, kv.first.c_str());
    // progress toward the ceiling. Only entries > 0 -> a save without a personal arc stays
    // byte-identical (the absent one is worth the universal rise, which is the behavior as always).
    for (const auto& kv : d.driver_curve)
        if (kv.second > 0.0) fprintf(f, kv.first.find(' ') == std::string::npos ? "drivercurve %s %.4f\r\n" : "drivercurve %s|%.4f\r\n",
                kv.first.c_str(), kv.second);   // separator only if the key contains spaces
    // (Jul 2) Seeding epoch (only if != 0 -> byte-identical back-compat for unmigrated saves).
    if (d.seed_epoch != 0)
        fprintf(f, "seedepoch %d\r\n", d.seed_epoch);
    // mark of seat keys PER SLOT (see career_save.h). Only if != 0 -> byte-identical back-compat.
    if (d.seat_key_epoch != 0)
        fprintf(f, "seatepoch %d\r\n", d.seat_key_epoch);
    // the PRIMARY seat of whoever has several. 'primaryseat <driver_id>|<seat_id>' — it is
    // split at the FIRST '|' because here the driver id goes first and never carries pipes, whereas the
    // seat_id does. Write-when-present: a save without multi-seat stays byte-identical.
    for (const auto& kv : d.primary_seat_by_driver)
        if (!kv.first.empty() && !kv.second.empty())
            fprintf(f, "primaryseat %s|%s\r\n", kv.first.c_str(), kv.second.c_str());
    // fingerprint of the seats the XML files were written with (see career_save.h). Only if != 0 ->
    // a save that never wrote them stays byte-identical to the previous format.
    if (d.grids_stamp != 0)
        fprintf(f, "gridstamp %lu\r\n", d.grids_stamp);
    // and that of the OWNER of each class. Same additivity rule.
    if (d.grids_owner_stamp != 0)
        fprintf(f, "gridowner %lu\r\n", d.grids_owner_stamp);
    // 0.7.0: repair of the skipped multiclass appointments already done. Same additivity rule.
    if (d.meetfix != 0)
        fprintf(f, "meetfix %d\r\n", d.meetfix);
    // 17/09: the inherited empty seats have already been recovered. Same additivity rule.
    if (d.seatfill != 0)
        fprintf(f, "seatfill %d\r\n", d.seatfill);
    // 17/09: the report of the last winter, so that the end-of-season screen tells what really
    // happened. Header with the year and the TWO totals, then one line per each one of the sample. The name goes
    // LAST on its line because it carries spaces; the rest are integers and a word without spaces, so the
    // reading splits by '|' and keeps the rest. All three additive: without a winter none is written.
    if (d.last_rollover.year != 0) {
        fprintf(f, "rollyear %d %d %d\r\n", d.last_rollover.year,
                d.last_rollover.total_retires, d.last_rollover.total_promos);
        // 18/09: the total of signings goes on ITS line, not by lengthening "rollyear": a line that already exists does not
        // change shape (save rule). It goes AFTER "rollyear", which is the one that zeroes the report.
        fprintf(f, "rollmovtot %d\r\n", d.last_rollover.total_moves);
        for (const auto& r : d.last_rollover.retires)
            fprintf(f, "rollret %d|%s|%s\r\n", r.age, r.reason.c_str(), r.name.c_str());
        for (const auto& p : d.last_rollover.promos)
            fprintf(f, "rollpro %d|%d|%s\r\n", p.from_tier, p.to_tier, p.name.c_str());
        for (const auto& m : d.last_rollover.moves)
            fprintf(f, "rollmov %s|%s|%s\r\n", m.from.c_str(), m.to.c_str(), m.name.c_str());
    }
    // Race form — one 'form' line per sample, in order (oldest->newest); only if there are samples
    // -> a save with no form stays byte-identical to the previous format (back-compat). Bounded to N=10.
    for (float q : d.prog.recent_form)
        fprintf(f, "form %.4f\r\n", q);
    for (float g : d.prog.recent_gap)
        fprintf(f, "gap %.4f\r\n", g);
    // A new line — `classdda` is not reused. Its field used to store an offset, and if
    // we wrote an absolute number here, an older build would read 95 as an offset and end up at 120. With a
    // new key, the older build ignores the unknown key (LoadCareer skips ones it doesn't know) and falls back to
    // the global-baseline behavior: a clean fallback instead of an absurd difficulty. kSaveVersion does not bump: the
    // line is additive and its absence is a valid state.
    // The '|' is mandatory: class_ids contain spaces ("Group C", "Coldley Motors", "Juniper Trophy").
    for (const auto& kv : d.prog.class_dda) {
        fprintf(f, "classai %s|%d %d", kv.first.c_str(), kv.second.ai, kv.second.races);
        for (float g : kv.second.gap) fprintf(f, " %.4f", g);
        fprintf(f, "\r\n");
    }
    // ~~~09/09 (, 3): the number and the WET window, on their own line (same shape as 'classai'; the
    //  tail is variadic, so it cannot go as an extra field). Write-when-present: without a rain race run, the save
    //  stays byte-identical. An earlier build ignores the key and stays in dry.
    for (const auto& kv : d.prog.class_dda) {
        if (kv.second.ai_wet <= 0 && kv.second.races_wet <= 0 && kv.second.gap_wet.empty()) continue;
        fprintf(f, "classwet %s|%d %d", kv.first.c_str(), kv.second.ai_wet, kv.second.races_wet);
        for (float g : kv.second.gap_wet) fprintf(f, " %.4f", g);
        fprintf(f, "\r\n");
    }
    // Per-class form feel. A separate line from 'classdda' on purpose: that one
    // ends in a variadic tail of gaps, so adding new fields at the end would break the parser. Same
    // '|' delimiter (the class_id contains spaces). Write-when-present: with no answers, the save stays
    // byte-identical to the previous format (additive back-compat, kSaveVersion untouched).
    for (const auto& kv : d.prog.class_dda)
        if (kv.second.last_feel > 0)
            fprintf(f, "classfeel %s|%d %d\r\n", kv.first.c_str(), kv.second.last_feel, kv.second.feel_streak);
    // Its own line, not another field in 'classai', because that one ends in a
    // variadic tail of samples, and a trailing field would be parsed as a sample -- exactly the reason
    // 'classfeel' already exists separately. Write-when-locked: with no locks, the file stays byte-identical
    // to today's format, and an older build ignores the unknown key. kSaveVersion does not bump.
    for (const auto& kv : d.prog.class_dda)
        if (kv.second.locked)
            fprintf(f, "classlock %s|1\r\n", kv.first.c_str());
    // The master lock. Write-when-true for the same reason.
    if (d.prog.lock_all) fprintf(f, "lock_all 1\r\n");
    // Telemetry consent — "telemetry <enabled> <already_asked>". Written as soon as
    // there's something to say (enabled or already asked); a save that has never seen the question stays
    // byte-identical to the previous format, and an old one loads with both off (it will be asked).
    if (d.prog.telemetry_opt_in || d.prog.telemetry_asked)
        fprintf(f, "telemetry %d %d\r\n", d.prog.telemetry_opt_in ? 1 : 0, d.prog.telemetry_asked ? 1 : 0);
    // nemesis_id — stable driver_id, written only if non-empty (mirrors 'player'/'contract').
    if (!d.nemesis_id.empty())
        fprintf(f, "nemesis %s\r\n", d.nemesis_id.c_str());
    // Seat occupancy — one 'seatocc' line per occupied seat (key|driver_id,
    // pipe-delimited: the key and id can contain characters -> no %s). Only entries with a non-empty id
    // -> a save with no seat state stays byte-identical (additive back-compat). kSaveVersion stays 2.
    for (const auto& kv : d.seat_occupant)
        if (!kv.second.empty())
            fprintf(f, "seatocc %s|%s\r\n", kv.first.c_str(), kv.second.c_str());
    // AI contract start year, only where it differs from the career's start
    // ('seatstart <key>|<year>'). A new career writes not a single line -> byte-identical save, which
    // is this feature's compatibility rule. The expiry is never stored: it's derived from
    // (seat, occupant, start year), so it can never end up orphaned. See career_seats.h.
    for (const auto& kv : d.seat_contract_start)
        if (kv.second > 0)
            fprintf(f, "seatstart %s|%d\r\n", kv.first.c_str(), kv.second);
    // ~~~29/08: consecutive seasons without a seat (idle <id>|<n>). Zero is not written.
    for (const auto& kv : d.seasons_idle) {
        if (kv.second <= 0) continue;
        const auto de = d.idle_from_series.find(kv.first);
        fprintf(f, "idle %s|%d|%s\r\n", kv.first.c_str(), kv.second,
                de == d.idle_from_series.end() ? "" : de->second.c_str());
    }
    // Generated drivers ('gendrv <id>|<name>|<country>|<talent>|<ceiling>'). It's the only
    // living-world data that genuinely needs saving: a made-up driver isn't derived from any table. The
    // birth year doesn't go here — it lives in save.birth_year, which is where the rest of the engine looks for it.
    for (const auto& kv : d.gen_drivers)
        if (!kv.first.empty())
            fprintf(f, "gendrv %s|%s|%s|%.1f|%d\r\n", kv.first.c_str(), kv.second.name.c_str(),
                    kv.second.country.c_str(), kv.second.talent, kv.second.tier_max);
    // Last circuit raced (hash) — the "previous leg" of the A->B travel cost.
    // Numeric, written only if non-zero -> a save with no previous leg (first race) stays byte-identical.
    if (d.last_circuit_hash != 0)
        fprintf(f, "lastcircuit %lu\r\n", d.last_circuit_hash);
    // Head-to-head store — one 'h2h <driver_id> <w> <l>' line per driver with
    // recorded duels (write-when-present: only ids with w|l>0 -> a save with no h2h captured stays byte-identical, back-compat).
    // The id is stable and has no spaces (the inverse of DriverById) -> %s is enough (same idiom as driverrating). Additive
    // over kSaveVersion=2 (no re-bump). Grouped by id on read (last-write-wins).
    for (const auto& kv : d.h2h)
        if (kv.second.w || kv.second.l)
            fprintf(f, kv.first.find(' ') == std::string::npos ? "h2h %s %d %d\r\n" : "h2h %s|%d %d\r\n",
                kv.first.c_str(), kv.second.w, kv.second.l);   // separator only if the key contains spaces
    // Narrative beef store — one line 'beef <driver_id> <level> <last_year>
    // <last_race_idx>' per rivalry still alive (write-when level>0: a decayed-to-0 entry is erased already by
    // BeefDecayAtRollover, so it never reaches here). Same id-safety precedent as h2h/driverrating (%s ok, no
    // spaces). A save with no beef captured stays byte-identical (back-compat).
    for (const auto& kv : d.beef)
        if (kv.second.level > 0)
            fprintf(f, kv.first.find(' ') == std::string::npos ? "beef %s %d %d %d\r\n" : "beef %s|%d %d %d\r\n",
                kv.first.c_str(), kv.second.level, kv.second.last_year, kv.second.last_race_idx);   // separator only if the key contains spaces
    // Press relationship marks (rel_marks) — one line 'relmark <year>|<delta_x1000>|<team>' per mark
    // (pipe-delimited: the team contains spaces -> %s won't do, same idiom as 'mailnarr'/'season'; team goes
    // last so it survives internal pipes). Write-when-non-zero (a delta of 0 wouldn't add anything to the fold, it already
    // decays to this in RelMarksSum). A save with no marks stays byte-identical (back-compat).
    for (const auto& m : d.rel_marks)
        if (m.delta_x1000 != 0)
            fprintf(f, "relmark %d|%d|%s\r\n", m.year, m.delta_x1000, m.team.c_str());
    // Career honors — one line per closed season, pipe-delimited (series_id can contain spaces ->
    // no %s). Only the ones that exist are written -> a save with no honors stays byte-identical to the previous format.
    // team_id is appended as a 5th trailing pipe-field only when non-empty -> a season with
    // no team (legacy / self-funded) keeps writing the byte-identical 4-field line (back-compat); the parser
    // reads the 5th field if present, else "". kSaveVersion stays 2 (purely additive, MigrateSave no-op).
    for (const auto& cs : d.completed_seasons) {
        if (cs.team_id.empty())
            fprintf(f, "season %s|%d|%d|%d\r\n", cs.series_id.c_str(), cs.year, cs.final_pos, cs.rounds);
        else
            fprintf(f, "season %s|%d|%d|%d|%s\r\n", cs.series_id.c_str(), cs.year, cs.final_pos, cs.rounds, cs.team_id.c_str());
    }
    // State of the aging world. Only lines with content are written -> a save with no
    // aged world stays byte-identical to the previous v1 format (additive back-compat).
    for (const auto& kv : d.birth_year)
        if (kv.second != 0) fprintf(f, kv.first.find(' ') == std::string::npos ? "birthyear %s %d\r\n" : "birthyear %s|%d\r\n",
                kv.first.c_str(), kv.second);   // separator only if the key contains spaces
    for (const auto& id : d.retired)
        fprintf(f, "retired %s\r\n", id.c_str());
    for (const auto& id : d.seat_seeded)                     // 23/09: the ELO of its seat, until it races
        fprintf(f, "seatseed %s\r\n", id.c_str());
    if (d.current_world_year != 0)
        fprintf(f, "worldyear %d\r\n", d.current_world_year);
    // Signed sponsors — one line per trait, pipe-delimited (sponsor id may carry spaces -> no %s):
    // "sponsor <id>|<tier>|<trait_kind>|<cap_rounds>|<cap_value>|<rounds_used>|<value_used>". Only written
    // if there are signed sponsors -> a save with no sponsors stays byte-identical to the previous format (back-compat).
    for (const auto& sp : d.signed_sponsors)
        for (size_t i = 0; i < sp.traits.size(); i++) {
            int  ru = i < sp.rounds_used.size() ? sp.rounds_used[i] : 0;
            long vu = i < sp.value_used.size()  ? sp.value_used[i]  : 0;
            fprintf(f, "sponsor %s|%d|%d|%d|%ld|%d|%ld\r\n",
                    sp.id.c_str(), sp.tier, sp.traits[i].kind, sp.traits[i].cap_rounds,
                    sp.traits[i].cap_value, ru, vu);
        }
    // Per-sponsor objective line — pipe-delimited (id may carry spaces / obj_kind is a fixed
    // keyword -> no %s for the id): "sponsor_obj <id>|<obj_kind>|<obj_goal>|<obj_progress>|<paid>|<signed_year>|<tier>".
    // +tier (7th field). The production converter (SponsorDefToSigned) emits empty coverage
    // traits for some sponsors, so no 'sponsor' line is ever written for them — the tier was only
    // carried on that (never-written) line and was LOST on reload (collapsed to 0=Local -> ~45× low Elite payout +
    // corrupt capacity counting). Persisting tier HERE (write-when-present) makes it survive empty-traits sponsors.
    // Written only if the sponsor carries an objective (obj_kind non-empty) -> a save with no objectives (or a
    // legacy save with only 'sponsor' lines) stays byte-identical (back-compat). Additive over kSaveVersion=2 (no re-bump).
    for (const auto& sp : d.signed_sponsors)
        if (!sp.obj_kind.empty())
            fprintf(f, "sponsor_obj %s|%s|%d|%d|%d|%d|%d\r\n",
                    sp.id.c_str(), sp.obj_kind.c_str(), sp.obj_goal, sp.obj_progress,
                    sp.paid ? 1 : 0, sp.signed_year, sp.tier);
    // (Jul 11 2026) Persists sponsor cards + the shop. Additive over kSaveVersion=2 (no re-bump):
    // lines are only written if there are cards -> a save with no cards stays byte-identical (back-compat).
    for (const auto& sc : d.cards)
        fprintf(f, "card %s|%d|%d|%d|%d|%d|%d|%ld\r\n",
                sc.card_id.c_str(), sc.level, sc.slot, sc.races_active,
                sc.races_left, sc.uses_left, sc.clause_miss, sc.invested);
    if (d.card_shop.open) {
        fprintf(f, "cardshop %s|%d|%d|%d\r\n", d.card_shop.race_id.c_str(),
                d.card_shop.seed_base, d.card_shop.rerolls, d.card_shop.bought);
        for (const auto& id : d.card_shop.offers)     fprintf(f, "cardshop_offer %s\r\n", id.c_str());
        for (const auto& id : d.card_shop.bought_ids) fprintf(f, "cardshop_bought %s\r\n", id.c_str());
    }
    // cards already seen in the tab (clamped to the real size: selling/expiry can shrink the card list).
    // Only written if >0 -> a save with no cards (or no visit) stays byte-identical (back-compat).
    if (d.cards_seen > 0)
        fprintf(f, "cardseen %d\r\n",
                d.cards_seen > (int)d.cards.size() ? (int)d.cards.size() : d.cards_seen);
    // the privateer's durable pick — "privpick <series_id>|<veh_hash>|<livery_ord>" (one line per
    // series). Only if there are entries -> byte-identical back-compat.
    for (const auto& kv : d.privateer_pick)
        fprintf(f, "privpick %s|%d|%d\r\n", kv.first.c_str(), kv.second.first, kv.second.second);
    // Legacy progress — "memory <id> <cleared> <last_played_year>" (id has no spaces -> %s is enough).
    // Only entries with cleared>0 -> a save with no memories stays byte-identical to the previous format.
    for (const auto& kv : d.memories_done)
        if (kv.second.cleared > 0)
            fprintf(f, kv.first.find(' ') == std::string::npos ? "memory %s %d %d\r\n" : "memory %s|%d %d\r\n",
                kv.first.c_str(), kv.second.cleared, kv.second.last_played_year);   // separator only if the key contains spaces
    // TIMES log (the Standings tab) — pipe-delimited (class_id/series/track can contain spaces -> no %s).
    // Lines are only written if there are entries -> a save with no races stays byte-identical (back-compat).
    // The post-settle player `rating` snapshot is appended as the trailing 11th
    // pipe-field (append-don't-insert -> the first 10 fields keep their positions so an old loader
    // ignores the tail and the new loader reads parts[10]; a legacy 10-field line parses with rating 0).
    // Additive over kSaveVersion=2 (no re-bump): the field is always written (default 0 sentinel for un-snapshotted entries).
    for (const auto& e : d.race_log) {
        fprintf(f, "racelog %s|%s|%s|%ld|%d|%d|%.3f|%.3f|%d|%d|%d\r\n",
                e.class_id.c_str(), e.series_id.c_str(), e.track.c_str(), e.track_hash,
                e.pos, e.field, e.best_lap, e.total_time, e.year, e.doy, e.rating);
        // P2 (F15): the abandonment goes on a SEPARATE line, right after its 'racelog' and only if there was one. The
        // 'racelog' is not lengthened: the 0.6.8 reader has parts[11] and would stick a field 12 onto the rating. 0.6.8 ignores the
        // unknown key, so going back only loses the DNF (the race is seen as a finish).
        if (!e.finished) fprintf(f, "rldnf 1\r\n");
        // The full race grid, one 'rgrow' line per driver, right after its 'racelog'
        // entry (the loader attaches them to race_log.back()). Pipe-delimited (name/class/car can contain spaces -> no
        // %s; AMS2 names never contain '|'). Only written if there's a grid -> a save with no grids stays back-compat.
        // Fields: pos|name|driver_id|car_class|car_name|laps|best_lap|dnf|is_player|total_time.
        // (Aug 24) 'finished' is the 11th field, additive (append-don't-insert, same pattern as the rating
        // field in racelog): 1 = final result; 0 = still on track when the flag fell (partial laps/total).
        for (const auto& g : e.grid)
            fprintf(f, "rgrow %d|%s|%s|%s|%s|%d|%.3f|%d|%d|%.3f|%d\r\n",
                    g.pos, g.name.c_str(), g.driver_id.c_str(), g.car_class.c_str(), g.car_name.c_str(),
                    g.laps, g.best_lap, g.dnf ? 1 : 0, g.is_player ? 1 : 0, g.total_time, g.finished ? 1 : 0);
    }
    // Generated emails — pipe-delimited (id/payload_key can contain spaces -> no %s).
    // "email <id>|<tag>|<day>|<family>|<read>|<answered>|<choice>|<subtype>|<payload_key>|<variant>|<params>|
    // <from_name>|<from_role_key>|<neg_id>|<year>" (15 fields). Only written if there are emails -> a save with no
    // generated emails stays byte-identical (back-compat). Never the resolved EN/ES body: only id+keys+payload_key
    // (bilingual-via-translation-keys, the renderer re-resolves it). As emails gained more life, the 4 preceding fields
    // in the tail are additive over v2 (no re-bump) — always written (empty/0 in old emails re-saved);
    // params arrives already encoded by MailParamsEncode (no raw pipes -> it never breaks this format). The
    // negotiation redesign added neg_id as the 14th field, additive like the previous ones — "" if the email doesn't belong
    // to a negotiation thread. Fix for emails getting an absurd date across a year boundary (Jul 23 2026): year is the 15th
    // field, additive — the calendar year at the time of sending; 0 in old emails re-saved (unknown:
    // we never invent data we don't have).
    for (const auto& e : d.emails)
        fprintf(f, "email %s|%d|%d|%d|%d|%d|%d|%d|%s|%d|%s|%s|%s|%s|%d\r\n",
                e.id.c_str(), e.tag, e.day, e.family, e.read, e.answered, e.choice, e.subtype,
                e.payload_key.c_str(), e.variant, e.params.c_str(),
                e.from_name.c_str(), e.from_role_key.c_str(), e.neg_id.c_str(), e.year);
    // Negotiation redesign (Jul 20 2026): a negotiation thread. One 'nego' line per thread + one 'negoh' per
    // history entry + one 'withdrawn' per withdrawn offer. Write-when-present (old saves untouched).
    auto maskOf = [](const DealTerms& t) { unsigned m = 0; for (int i = 0; i < bal::NEGO_DEMAND_COUNT; i++) if (t.demands[i]) m |= (1u << i); return m; };
    for (const auto& n : d.negotiations) {
        fprintf(f, "nego %s|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%u|%d|%d|%u|%d|%d|%d|%s|%s|%s|%s|%s|%d|%d|%ld|%ld|%d|%s\r\n",
                n.neg_id.c_str(), n.state, n.round, n.patience, n.patience_base,
                n.respond_day, n.deadline_day, n.reserve_day, n.last_action_day,
                n.want_pct, n.form_pct, n.trial_pct, n.flags,
                maskOf(n.player_terms), n.player_terms.salaryPct, n.player_terms.sponsorTierRank,
                maskOf(n.table_terms), n.table_terms.salaryPct, n.table_terms.sponsorTierRank,
                n.table_is_counter ? 1 : 0,
                n.offer.id.c_str(), n.offer.team.c_str(), n.offer.from.c_str(),
                n.offer.clsId.c_str(), n.offer.scope.c_str(),
                n.offer.contractKind, n.offer.archetype, n.offer.signBonus, n.offer.perRace,
                n.offer.commitment, n.offer.promote_to.c_str());
        for (const auto& h : n.history)
            fprintf(f, "negoh %s|%d|%d|%d|%u|%d|%d|%s\r\n", n.neg_id.c_str(), h.day, h.who, h.kind,
                    h.demands_mask, h.salaryPct, h.sponsorTierRank, h.sponsorCardId.c_str());
    }
    for (const auto& id : d.withdrawn_offers) fprintf(f, "withdrawn %s\r\n", id.c_str());
    // seat-contest observations. Write-when-present -> a save with no contests stays byte-identical.
    for (const auto& w : d.seat_watches) {
        fprintf(f, "seatwatch %s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d\r\n",
                w.series_id.c_str(), w.class_name.c_str(), w.team_label.c_str(),
                w.rival_id.c_str(), w.rival_name.c_str(),
                w.opened_day, w.races_at_open, w.races_window, w.state, w.resolved_day,
                w.races_seen, w.rival_ahead_streak);
        // Sealed candidates, one per line (same pattern as 'negoh' hanging off 'nego'): a name can
        // contain any character, so putting them in a list inside one field would have required escaping them.
        for (const auto& c : w.contenders)
            fprintf(f, "seatwatchc %s|%s|%s|%s\r\n",
                    w.series_id.c_str(), w.team_label.c_str(), c.id.c_str(), c.name.c_str());
    }
    // Sealed keys for the signing window. Write-when-present -> a save with no
    // open window stays byte-identical. Same pipe idiom as 'seatwatch' (team and name contain spaces).
    if (d.window_year > 0) fprintf(f, "winyear %d\r\n", d.window_year);
    for (const auto& w : d.window_seats)
        fprintf(f, "winseat %s|%s|%d|%d|%d|%s|%s|%d%s\r\n", w.series_id.c_str(), w.team_label.c_str(),
                w.state, w.opened_day, w.applied, w.winner_id.c_str(), w.winner_name.c_str(), w.sounded,
                w.seated ? "|1" : "");   // 23/09 (): 9th field only if it is 1 -> a previous save stays identical
    // Narrative memory (from the emails-with-life feature) — write-only-when-non-default (byte-identical back-compat).
    {
        const MailNarrative& n = d.mailnarr;
        if (n.onboard_stage || n.consec_podiums || n.consec_bottom || !n.last_notable_track.empty()
            || n.season_podiums || n.season_wins || n.season_start_credits || n.consec_clean != 0)
            fprintf(f, "mailnarr %d|%d|%d|%s|%d|%d|%ld|%d\r\n",
                    n.onboard_stage, n.consec_podiums, n.consec_bottom, n.last_notable_track.c_str(),
                    n.season_podiums, n.season_wins, n.season_start_credits, n.consec_clean);
    }
    // Pending renewal offers — pipe-delimited (id/team/clsId/promote_to can
    // carry spaces -> no %s). "renewal <id>|<team>|<from>|<clsId>|<scope>|<contractKind>|<archetype>|
    // <signBonus>|<perRace>|<commitment>|<promote_to>". Only written if there are renewals -> a save with
    // none stays byte-identical (back-compat). Additive over v2 (no re-bump). The cosmetic fields
    // (color/title/pitch/why-bars) are not persisted: the renderer re-derives them from the reconstructed OfferDef.
    for (const auto& r : d.pending_renewals)
        fprintf(f, "renewal %s|%s|%s|%s|%s|%d|%d|%ld|%ld|%d|%s\r\n",
                r.id.c_str(), r.team.c_str(), r.from.c_str(), r.clsId.c_str(), r.scope.c_str(),
                r.contractKind, r.archetype, r.signBonus, r.perRace, r.commitment, r.promote_to.c_str());
    // One-off substitution — "subride <series_id>|<seat_idx>|<event_absday>|<fee>|
    // <state>|<team_label>" (pipe-delimited, same idiom as 'cardshop'). Write-only-when state != 0 -> a save
    // with no active/offered substitution stays byte-identical (back-compat). team_label (6th field) goes
    // last because it can contain spaces (never '|'); same idiom as the 'renewal'/'season' line.
    if (d.sub_ride.state != 0)
        fprintf(f, "subride %s|%d|%d|%ld|%d|%s\r\n", d.sub_ride.series_id.c_str(), d.sub_ride.seat_idx,
                d.sub_ride.event_absday, d.sub_ride.fee, d.sub_ride.state, d.sub_ride.team_label.c_str());
}

std::string SerializeCareer(const SaveData& d) {
    std::string out; out.reserve(1 << 18);
    SaveSink sink; sink.mem = &out;
    WriteCareer(d, &sink);
    return out;
}

bool SaveCareer(const SaveData& d, const char* path) {
    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* fp = paths::FOpen(tmp, "wb");
    if (!fp) { UserLog("save", "save FAILED: cannot write %s (errno=%d)", tmp, errno); return false; }
    SaveSink sink; sink.file = fp;
    WriteCareer(d, &sink);
    fclose(fp);
    // Replacement in a single step. Previously the save was deleted and THEN the .tmp was renamed: if the rename
    // failed (or the process died mid-way), no save was left at all. See paths::ReplaceFile.
    if (!paths::ReplaceFileAtomic(tmp, path)) {
        UserLog("save", "save FAILED: cannot replace %s (the previous save is INTACT; the new one stays at .tmp)", path);
        return false;
    }
    return true;
}

// — KEYS WITH SPACES. Several save lines use a catalog class name as their key, and
// five of them contain a space: "Juniper Trophy", "Group A", "Group C", "Oldacre Orn227", and "Coldley Motors". Reading them with
// "%127s" splits them at the first space, the next field stops being a number, sscanf returns 1, and the
// caller's guard discards the entire entry, silently. This is what made a purchased car disappear
// on restart (a player reported this with a Brackenridge Gry837 Coldley Motors), and it also wiped that car's stats.
// This was already fixed before in 'classdda' and 'seatocc'; this generalizes it to the two lines that were still missing it.
//
// Reconstructs the key via the two paths that need to be supported:
//   (a) new form: "<key>|<rest>" -- split at the first '|', same as 'classai' and 'seatocc'. A '|'
//       inside the key is impossible: the catalog is a PSV file and uses it as its separator.
//   (b) recovery of what was already written: published saves carry the old line with the key split apart. The text
//       was never lost -- it just couldn't be read -- so the data is recovered instead of being written off.
//       It's read from the right: the tail is numeric and of known size, and whatever's left in front is the key.
//       Only attempted when the caller says its classic sscanf already failed (`tryRecovery`), so
//       a healthy line never goes through here.
// Returns false if the line doesn't fit either path -> the caller keeps what it already had.
// The single value of a "key <value>" line is everything up to the end of the line,
// spaces included. Reading it with "%127s" truncated it at the first space, and the truncated id no longer matched anything
// (a contested race stops being recorded, an open door closes itself...). Reading to the end is also
// compatible in both directions: it doesn't change what's written, and a value with no spaces reads exactly as
// it always did. It's the same pattern already used by 'cardshop_offer' and 'cardshop_bought'.
static std::string ValorHastaFinDeLinea(const char* line, size_t keyLen) {
    const char* v = line + keyLen;
    while (*v == ' ' || *v == '\t') v++;
    std::string out;
    for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) out += *q;
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
    return out;
}

static bool ClaveConEspacios(const char* line, size_t keyLen, bool intentarRescate,
                             const int* colas, int nColas, std::string& outClave, std::string& outResto) {
    std::string rest;
    { const char* v = line + keyLen;
      while (*v == ' ' || *v == '\t') v++;
      for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q; }

    const size_t bar = rest.find('|');
    if (bar != std::string::npos && bar > 0) {                  // (a)
        outClave = rest.substr(0, bar);
        outResto = rest.substr(bar + 1);
        return true;
    }
    if (!intentarRescate) return false;                          // (b)
    std::vector<std::string> tok;
    { std::string cur;
      for (char ch : rest) { if (ch == ' ' || ch == '\t') { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } } else cur += ch; }
      if (!cur.empty()) tok.push_back(cur); }
    auto esNumero = [](const std::string& s) {
        if (s.empty()) return false;
        char* end = nullptr; strtod(s.c_str(), &end);
        return end && *end == '\0';
    };
    for (int ci = 0; ci < nColas; ci++) {
        const int n = colas[ci];
        if ((int)tok.size() <= n) continue;                      // there has to be something left in front for the key
        bool colaNumerica = true;
        for (int i = (int)tok.size() - n; i < (int)tok.size(); i++) if (!esNumero(tok[i])) { colaNumerica = false; break; }
        if (!colaNumerica) continue;
        outClave.clear();
        for (int i = 0; i < (int)tok.size() - n; i++) { if (i) outClave += ' '; outClave += tok[i]; }
        outResto = rest.substr(outClave.size());
        return true;
    }
    return false;
}

bool LoadCareer(SaveData& d, const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) { UserLog("save", "no previous save (%s) -> defaults", path); return false; }
    d = SaveData{};               // clean defaults
    int fileVersion = 0;          // 0 = an old save with no "version" line (the line is optional -> back-compat)
    bool sawMailnarr = false;     // Did the file have a 'mailnarr' line?
    // One-shot migration from 'classdda' (offset, old scheme) to 'classai' (absolute
    // number). Accumulated here during the loop and converted afterward (needs prog.ai_difficulty already read).
    // classAiSeen is per-category: a file can carry 'classai' for one class and
    // legacy 'classdda' for another, and a global guard would wrongly discard the migration of the second one.
    std::set<std::string> classAiSeen;
    std::map<std::string, float> legacyDelta;
    std::map<std::string, int> legacyRaces;
    std::map<std::string, std::vector<float>> legacyGap;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char key[64] = {0};
        if (sscanf(line, "%63s", key) != 1) continue;
        if      (!strcmp(key, "version"))        sscanf(line, "%*s %d",  &fileVersion);   // Schema version read
        else if (!strcmp(key, "credits"))        sscanf(line, "%*s %ld", &d.prog.credits);
        else if (!strcmp(key, "xp_total"))       sscanf(line, "%*s %ld", &d.prog.xp_total);
        else if (!strcmp(key, "xp_toward_next")) sscanf(line, "%*s %ld", &d.prog.xp_toward_next);
        else if (!strcmp(key, "frontier_phase")) sscanf(line, "%*s %d",  &d.prog.frontier_phase);
        else if (!strcmp(key, "rep"))            sscanf(line, "%*s %d",  &d.prog.rep);
        else if (!strcmp(key, "titles"))   sscanf(line, "%*s %d", &d.prog.titles);   // number of championships
        else if (!strcmp(key, "rating"))         sscanf(line, "%*s %lf", &d.prog.rating);
        else if (!strcmp(key, "rating_races"))   sscanf(line, "%*s %d",  &d.prog.rating_races);
        else if (!strcmp(key, "cleanliness"))    sscanf(line, "%*s %lf", &d.prog.cleanliness);   // additive; dumb parser, no seed
        else if (!strcmp(key, "reward_mode"))     sscanf(line, "%*s %d", &d.prog.reward_mode);      // (Jul 21) additive; old save -> 1 = Normal
        else if (!strcmp(key, "difficulty_mode")) sscanf(line, "%*s %d", &d.prog.difficulty_mode);
        // additive: older saves don't have the line and keep the struct's default (90)
        else if (!strcmp(key, "custom_difficulty")) sscanf(line, "%*s %d", &d.prog.custom_difficulty);
        else if (!strcmp(key, "ai_difficulty"))   sscanf(line, "%*s %d", &d.prog.ai_difficulty);
        else if (!strcmp(key, "adaptive_trim"))   sscanf(line, "%*s %d", &d.prog.adaptive_trim);
        else if (!strcmp(key, "ai_aggression"))   sscanf(line, "%*s %d", &d.prog.ai_aggression);   //
        else if (!strcmp(key, "scale_tyre_wear"))  { int v=1; sscanf(line, "%*s %d", &v); d.prog.scale_tyre_wear  = (v != 0); }   //
        // fcy_enabled = legacy (migrates to No/Yes mode); fcy_mode comes later in the file and overrides it.
        else if (!strcmp(key, "fcy_enabled"))      { int v=0; sscanf(line, "%*s %d", &v); d.prog.fcy_mode         = (v != 0) ? 1 : 0; }
        else if (!strcmp(key, "fcy_mode"))         { int v=1; sscanf(line, "%*s %d", &v); d.prog.fcy_mode         = (v >= 0 && v <= 2) ? v : 1; }
        else if (!strcmp(key, "inherit_mask"))     { int v=0; sscanf(line, "%*s %d", &v); d.prog.inherit_mask     = v; }          //
        else if (!strcmp(key, "wx_mode"))          { int v=0; sscanf(line, "%*s %d", &v); d.prog.wx_mode          = (v >= 0 && v <= 2) ? v : 0; }   // Weather section
        else if (!strcmp(key, "wx_weights")) {      // "wx_weights <code>:<weight> <code>:<weight> ..."
            d.prog.wx_weights.clear();
            const char* p = strchr(line, ' ');
            while (p) {
                int code = 0, wgt = 0;
                if (sscanf(p, " %d:%d", &code, &wgt) == 2 && code >= 0 && code <= 17 && wgt >= 0)
                    d.prog.wx_weights[code] = wgt;
                p = strchr(p + 1, ' ');
            }
        }
        else if (!strcmp(key, "scale_fuel_usage")) { int v=1; sscanf(line, "%*s %d", &v); d.prog.scale_fuel_usage = (v != 0); }   //

        else if (!strcmp(key, "calibrated"))      { int v=0; sscanf(line, "%*s %d", &v); d.prog.calibrated = (v != 0); }
        else if (!strcmp(key, "stoprules")) {
            int o = 1, m = 1, a = 0, l = 0, r = 0;
            if (sscanf(line, "%*s %d %d %d %d %d", &o, &m, &a, &l, &r) == 5) {
                d.stop_rules.offers = o != 0; d.stop_rules.myRaces = m != 0; d.stop_rules.anyRace = a != 0;
                d.stop_rules.mail = l != 0;   d.stop_rules.relation = r != 0;
            }
        }
        else if (!strcmp(key, "sponsor_tier"))   sscanf(line, "%*s %d",  &d.sponsor_tier);
        else if (!strcmp(key, "seed"))           sscanf(line, "%*s %lu", &d.career_seed);   // legacy save with no 'seed' -> 0

        else if (!strcmp(key, "owned")) {
            char cid[128] = {0}; int vehid = 0, races = 0, ins = 0, liv = -1; float wear = 0;
            int got = sscanf(line, "%*s %127s %d %d %f %d %d", cid, &vehid, &races, &wear, &ins, &liv);
            // the tail is 5 fields (vehid races wear ins liv), or 4 in saves that predate the
            // 'livery' column. If the key contained spaces, `got` comes back < 2 and ClaveConEspacios reconstructs it.
            std::string idConEspacios;
            { static const int cola[] = { 5, 4 };
              std::string tail;
              if (ClaveConEspacios(line, 5 /*"owned"*/, got < 2, cola, 2, idConEspacios, tail)) {
                  const int nums = sscanf(tail.c_str(), "%d %d %f %d %d", &vehid, &races, &wear, &ins, &liv);
                  got = nums >= 1 ? nums + 1 : 0;   // +1: the key counts as the 1st field in the classic form
                  if (!got) idConEspacios.clear();
              } }
            if (got >= 2) {
                // ownership fix: the old GIFT (F1'67) was saved with the bare CLASS ("Falcon_Masters") instead
                // of the per-car format ("Falcon_Masters#<veh>"). The garage resolved it by class (the car showed up),
                // but the pre-race screen matches by exact id (CareerOwnsCar->OwnsClassId) -> the owned car showed up as
                // not owned: greyed out, unselectable, unable to race. Normalizes only that known entry to the per-car id.
                // Deliberately narrow in scope: SAMPLE_CARS car ids are legitimately bare (e.g. "gt3_992"), and
                // the garage resolves them by exact id -> a generic normalization would corrupt them. Idempotent
                // (a save already healthy carries "Falcon_Masters#..." != class -> doesn't match) and doesn't touch the prefix (the
                // series unlock stays intact). Deliberately outside MigrateSave: the bug occurs in saves already on the current version.
                std::string clsId = idConEspacios.empty() ? std::string(cid) : idConEspacios;
                if (clsId == kStarterMemoryClass) clsId = kStarterMemoryCarId;
                career::CarOwnership c{clsId, vehid, races, wear, ins != 0};
                if (got >= 6) c.livery = liv;   // 6th field: livery, optional (old saves -> -1)
                // gift-livery fix: 'livery' is an ordinal (an index into CareerVehicleLiveryList), but the old gift
                // stored the livery NN (50) of the Elmmoor Racing #1 -> as an ordinal that falls out of range (the car has 10 liveries)
                // -> the wrong skin was shown. NN 50 is the car's lowest -> ordinal 0. Heals the legacy value on
                // load (scoped to the gift car; a real ordinal of 50 is impossible on a 10-livery car).
                if (c.vehid == 759059246 && c.livery == 50) c.livery = 0;
                d.owned.push_back(c);
            }
        }
        else if (!strcmp(key, "ownedkm")) {   // 0.7.0: "ownedkm <livery> <km> <class_id to end of line>"
            int liv = -1, idAt = 0; double km = 0.0;
            if (sscanf(line, "%*s %d %lf %n", &liv, &km, &idAt) == 2 && idAt > 0 && km > 0.0) {
                std::string id(line + idAt);
                while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) id.pop_back();
                // the 'owned' lines come earlier in the file: the unit already exists. With repeated units, the
                // first that does not have km yet (they were written in the same order).
                for (auto& c : d.owned)
                    if (c.class_id == id && c.livery == liv && c.km == 0.0) { c.km = km; break; }
            }
        }
        else if (!strcmp(key, "stat")) {
            char cid[128] = {0}; ClassStats st;
            int got = sscanf(line, "%*s %127s %d %d %d %d", cid, &st.races, &st.wins, &st.podiums, &st.dnf);
            // the key is the race's real class_id (career_overlay: class_stats[g_curRaceClassId]),
            // so it contains spaces in the same five classes -- and their races, wins, podiums, and retirements
            // were being lost on every restart along with the car. Tail of 4 fields.
            std::string clave;
            { static const int cola[] = { 4 };
              std::string tail;
              if (ClaveConEspacios(line, 4 /*"stat"*/, got < 2, cola, 1, clave, tail)) {
                  const int nums = sscanf(tail.c_str(), "%d %d %d %d", &st.races, &st.wins, &st.podiums, &st.dnf);
                  got = nums >= 1 ? nums + 1 : 0;
                  if (!got) clave.clear();
              } }
            if (got >= 2) d.class_stats[clave.empty() ? std::string(cid) : clave] = st;
        }
        else if (!strcmp(key, "race")) {
            const std::string rid = ValorHastaFinDeLinea(line, 4);   // the id can contain spaces
            if (!rid.empty()) d.completed_races.insert(rid);
        }
        else if (!strcmp(key, "claimedevent")) {   // mirror of 'race'
            const std::string rid = ValorHastaFinDeLinea(line, 12);   //
            if (!rid.empty()) d.claimed_events.insert(rid);
        }
        else if (!strcmp(key, "evpaid")) {   // 19/09: "evpaid <₡> <id to end of line>" (the difference is paid)
            long paid = 0; int idAt = 0;
            if (sscanf(line, "%*s %ld %n", &paid, &idAt) == 1 && idAt > 0 && paid > 0) {
                const std::string eid = ValorHastaFinDeLinea(line, (size_t)idAt);
                if (!eid.empty()) d.event_paid[eid] = paid;
            }
        }
        else if (!strcmp(key, "gate")) {
            const std::string gid = ValorHastaFinDeLinea(line, 4);   //
            if (!gid.empty()) d.unlocked_gates.insert(gid);
        }
        else if (!strcmp(key, "declined")) {   // declined offer (same pattern as 'gate')
            const std::string oid = ValorHastaFinDeLinea(line, 8);   //
            if (!oid.empty()) d.declined_offers.insert(oid);
        }
        else if (!strcmp(key, "expired")) {    // expired offer (same pattern as 'declined')
            const std::string oid = ValorHastaFinDeLinea(line, 7);   //
            if (!oid.empty()) d.expired_offers.insert(oid);
        }
        else if (!strcmp(key, "ofs")) {   // offer_lifecycle: "ofs <id> <absolute_day>"
            char oid[128] = {0}; int day = 0;
            // When the real time model landed, the absolute day moved from the old scale (year*365+doy, ~739k+) to
            // abs_day (0..~7500 within the horizon). An entry with an old-scale value comes from a save
            // pre-migration; it's discarded (offer_first_seen is transient, it gets rebuilt on the next world
            // tick) so it doesn't break the offer TTL. kAbsDayHealGuard separates both scales with margin.
            const int kAbsDayHealGuard = 100000;
            int got = sscanf(line, "%*s %127s %d", oid, &day);
            std::string clave;   // the key can contain spaces
            { static const int cola[] = { 1 }; std::string tail;
              if (ClaveConEspacios(line, 3, got < 2, cola, 1, clave, tail)) {
                  if (sscanf(tail.c_str(), "%d", &day) == 1) got = 2; else clave.clear();
              } }
            if (got == 2 && day >= 0 && day < kAbsDayHealGuard) {
                const std::string k = clave.empty() ? std::string(oid) : clave;
                if (!k.empty()) d.offer_first_seen[k] = day;
            }
        }
        else if (!strcmp(key, "lapsed")) {   // offer_lifecycle: offer expired by TTL (same pattern as 'declined')
            const std::string oid = ValorHastaFinDeLinea(line, 6);   //
            if (!oid.empty()) d.lapsed_offers.insert(oid);
        }
        else if (!strcmp(key, "rivalform")) {  // "rivalform <driver_id> <delta>"
            char rid[128] = {0}; float delta = 0.0f;
            int got = sscanf(line, "%*s %127s %f", rid, &delta);
            std::string clave;   //
            { static const int cola[] = { 1 }; std::string tail;
              if (ClaveConEspacios(line, 9, got < 2, cola, 1, clave, tail)) {
                  if (sscanf(tail.c_str(), "%f", &delta) == 1) got = 2; else clave.clear();
              } }
            { const std::string k = clave.empty() ? std::string(rid) : clave;
              if (got == 2 && !k.empty()) d.rival_form[k] = delta; }
        }
        else if (!strcmp(key, "driverrating")) {  // "driverrating <driver_id> <rating>"
            char did[128] = {0}; double rv = 0.0;
            int got = sscanf(line, "%*s %127s %lf", did, &rv);
            std::string clave;   //
            { static const int cola[] = { 1 }; std::string tail;
              if (ClaveConEspacios(line, 12, got < 2, cola, 1, clave, tail)) {
                  if (sscanf(tail.c_str(), "%lf", &rv) == 1) got = 2; else clave.clear();
              } }
            { const std::string k = clave.empty() ? std::string(did) : clave;
              if (got == 2 && !k.empty()) d.driver_ratings[k] = rv; }
        }
        else if (!strcmp(key, "driverraces")) {  // "driverraces <driver_id> <n>" (races-completed counter)
            char did[128] = {0}; int n = 0;
            int got = sscanf(line, "%*s %127s %d", did, &n);
            std::string clave;   //
            { static const int cola[] = { 1 }; std::string tail;
              if (ClaveConEspacios(line, 11, got < 2, cola, 1, clave, tail)) {
                  if (sscanf(tail.c_str(), "%d", &n) == 1) got = 2; else clave.clear();
              } }
            { const std::string k = clave.empty() ? std::string(did) : clave;
              if (got == 2 && !k.empty()) d.driver_races[k] = n; }
        }
        else if (!strcmp(key, "drivercurve")) {  // "drivercurve <driver_id> <progreso 0..1>"
            char did[128] = {0}; double pv = 0.0;
            int got = sscanf(line, "%*s %127s %lf", did, &pv);
            std::string clave;   // the same recipe as 'driverraces'
            { static const int cola[] = { 1 }; std::string tail;
              if (ClaveConEspacios(line, 11, got < 2, cola, 1, clave, tail)) {
                  if (sscanf(tail.c_str(), "%lf", &pv) == 1) got = 2; else clave.clear();
              } }
            { const std::string k = clave.empty() ? std::string(did) : clave;
              if (got == 2 && !k.empty() && pv > 0.0) d.driver_curve[k] = pv; }
        }
        else if (!strcmp(key, "teamrank")) {  // "teamrank <rank> <series_id>|<team label, may contain spaces>"
            int puesto = 0; int consumido = 0;
            if (sscanf(line, "%*s %d %n", &puesto, &consumido) == 1 && puesto > 0 && consumido > 0) {
                std::string clave(line + consumido);
                while (!clave.empty() && (clave.back() == '\r' || clave.back() == '\n' ||
                                          clave.back() == ' '  || clave.back() == '\t')) clave.pop_back();
                if (!clave.empty()) d.team_last_rank[clave] = puesto;
            }
        }
        else if (!strcmp(key, "seedepoch")) {    // "seedepoch <n>" (the applied seeding epoch)
            int n = 0;
            if (sscanf(line, "%*s %d", &n) == 1) d.seed_epoch = n;
        }
        else if (!strcmp(key, "seatepoch")) {    // "seatepoch <n>" (seat keys per SLOT)
            int n = 0;
            if (sscanf(line, "%*s %d", &n) == 1) d.seat_key_epoch = n;
        }
        else if (!strcmp(key, "primaryseat")) {  // "primaryseat <driver_id>|<seat_id>"
            const char* v = line + 11;                     // after "primaryseat"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            const size_t cut = rest.find('|');             // the FIRST '|': the seat_id may carry more
            if (cut != std::string::npos && cut > 0 && cut + 1 < rest.size())
                d.primary_seat_by_driver[rest.substr(0, cut)] = rest.substr(cut + 1);
        }
        else if (!strcmp(key, "gridstamp")) {    // "gridstamp <n>": fingerprint of the seats in the written XML files
            unsigned long n = 0;
            if (sscanf(line, "%*s %lu", &n) == 1) d.grids_stamp = n;
        }
        else if (!strcmp(key, "gridowner")) {    // fingerprint of the owner of each class
            unsigned long n = 0;
            if (sscanf(line, "%*s %lu", &n) == 1) d.grids_owner_stamp = n;
        }
        else if (!strcmp(key, "meetfix")) {      // 0.7.0: "meetfix <n>" (repaired multiclass appointment tables)
            int n = 0;
            if (sscanf(line, "%*s %d", &n) == 1) d.meetfix = n;
        }
        else if (!strcmp(key, "seatfill")) {     // 17/09: "seatfill <n>" (inherited empty seats already recovered)
            int n = 0;
            if (sscanf(line, "%*s %d", &n) == 1) d.seatfill = n;
        }
        // 17/09: the report of the last winter. "rollyear <anyo> <totRetiradas> <totAscensos>" and then
        // "rollret <edad>|<motivo>|<nombre>" / "rollpro <desdeNivel>|<hastaNivel>|<nombre>". The name is deliberately
        // the last thing on the line: it is the only field with spaces.
        else if (!strcmp(key, "rollyear")) {
            int y = 0, tr = 0, tp = 0, tm = 0;
            // (the 4th integer was only written by builds from the morning of 18/09; the total now goes in "rollmovtot")
            if (sscanf(line, "%*s %d %d %d %d", &y, &tr, &tp, &tm) >= 3) {
                d.last_rollover.year = y;
                d.last_rollover.total_retires = tr;
                d.last_rollover.total_promos = tp;
                d.last_rollover.total_moves = tm;
                d.last_rollover.retires.clear();
                d.last_rollover.promos.clear();
                d.last_rollover.moves.clear();
            }
        }
        else if (!strcmp(key, "rollret")) {
            const std::string v = ValorHastaFinDeLinea(line, 7);
            const size_t b1 = v.find('|'), b2 = (b1 == std::string::npos) ? b1 : v.find('|', b1 + 1);
            if (b2 != std::string::npos) {
                SaveData::RolloverSaved::Retire r;
                r.age = atoi(v.substr(0, b1).c_str());
                r.reason = v.substr(b1 + 1, b2 - b1 - 1);
                r.name = v.substr(b2 + 1);
                if (!r.name.empty()) d.last_rollover.retires.push_back(std::move(r));
            }
        }
        else if (!strcmp(key, "rollmovtot")) {   // 18/09: total of signings of the last winter
            int tot = 0;
            if (sscanf(line, "%*s %d", &tot) == 1) d.last_rollover.total_moves = tot;
        }
        else if (!strcmp(key, "rollmov")) {   // 18/09: "rollmov <de>|<a>|<nombre>" (the name, last)
            const std::string v = ValorHastaFinDeLinea(line, 7);
            const size_t b1 = v.find('|'), b2 = (b1 == std::string::npos) ? b1 : v.find('|', b1 + 1);
            if (b2 != std::string::npos) {
                SaveData::RolloverSaved::Move m;
                m.from = v.substr(0, b1);
                m.to = v.substr(b1 + 1, b2 - b1 - 1);
                m.name = v.substr(b2 + 1);
                if (!m.name.empty()) d.last_rollover.moves.push_back(std::move(m));
            }
        }
        else if (!strcmp(key, "rollpro")) {
            const std::string v = ValorHastaFinDeLinea(line, 7);
            const size_t b1 = v.find('|'), b2 = (b1 == std::string::npos) ? b1 : v.find('|', b1 + 1);
            if (b2 != std::string::npos) {
                SaveData::RolloverSaved::Promo p;
                p.from_tier = atoi(v.substr(0, b1).c_str());
                p.to_tier = atoi(v.substr(b1 + 1, b2 - b1 - 1).c_str());
                p.name = v.substr(b2 + 1);
                if (!p.name.empty()) d.last_rollover.promos.push_back(std::move(p));
            }
        }
        else if (!strcmp(key, "form")) {       // "form <quality>" (one sample of the race-form window, in order)
            float q = 0.0f;
            if (sscanf(line, "%*s %f", &q) == 1) d.prog.recent_form.push_back(q);
        }
        else if (!strcmp(key, "gap")) { float g=0; if (sscanf(line, "%*s %f", &g) == 1) d.prog.recent_gap.push_back(g); }
        else if (!strcmp(key, "classwet")) {
            // ~~~09/09 (, 3): "classwet <class_id>|<ai_wet> <races_wet> [gap...]", twin of 'classai'.
            const char* v = line + 8;                       // after "classwet"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            const size_t bar = rest.find('|');
            if (bar != std::string::npos) {
                const std::string cid = rest.substr(0, bar);
                int nAi = 0, nR = 0, consumed = 0;
                if (!cid.empty() && sscanf(rest.c_str() + bar + 1, "%d %d%n", &nAi, &nR, &consumed) == 2) {
                    career::ClassDdaState& st = d.prog.class_dda[cid];   // operator[]: keeps what 'classai' set
                    st.ai_wet = nAi ? career::Clampi(nAi, career::dda::kAiMin, career::dda::kAiMax) : 0;
                    st.races_wet = nR;
                    const char* p = rest.c_str() + bar + 1 + consumed;
                    std::vector<float> gap;
                    float g = 0.0f; int used = 0;
                    while (sscanf(p, " %f%n", &g, &used) == 1) { gap.push_back(g); p += used; }
                    st.gap_wet = gap;
                }
            }
        }
        else if (!strcmp(key, "classai")) {
            // "classai <class_id>|<ai> <races> [gap...]" — the class_id can contain spaces,
            // so it's split at the first '|', same as 'classdda' and 'seatocc'.
            const char* v = line + 7;                       // after "classai"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            const size_t bar = rest.find('|');
            if (bar != std::string::npos) {
                const std::string cid = rest.substr(0, bar);
                int nAi = 0, nR = 0, consumed = 0;
                if (!cid.empty() && sscanf(rest.c_str() + bar + 1, "%d %d%n", &nAi, &nR, &consumed) == 2) {
                    // Uses operator[] instead of overwriting the whole entry -- if
                    // 'classfeel' for this same class was already parsed (or arrives later, order isn't
                    // guaranteed), this preserves last_feel/feel_streak instead of overwriting them with a
                    // freshly-constructed entry (last_feel=0, feel_streak=0).
                    career::ClassDdaState& st = d.prog.class_dda[cid];
                    // 0 is the "no number yet" sentinel (career_engine.h) and
                    // it is not clamped -- clamping would turn it into kAiMin (70) and the sentinel wouldn't survive a
                    // save/load/save cycle.
                    st.ai = nAi ? career::Clampi(nAi, career::dda::kAiMin, career::dda::kAiMax) : 0;
                    st.races = nR;
                    const char* p = rest.c_str() + bar + 1 + consumed;
                    std::vector<float> gap;
                    float g = 0.0f; int used = 0;
                    while (sscanf(p, " %f%n", &g, &used) == 1) { gap.push_back(g); p += used; }
                    st.gap = gap;
                    classAiSeen.insert(cid);   // Per-class guard, not global
                }
            }
        }
        else if (!strcmp(key, "classdda")) {
            // Legacy from the old scheme (which stored an offset over the global baseline). It is not
            // applied here: it accumulates in local maps and gets converted to `classai` after the loop, once
            // prog.ai_difficulty is already read, without depending on the order of the file's lines. If
            // this same class already carried 'classai' (classAiSeen, a per-category guard:
            // a global boolean would over-discard if the file mixes 'classai' for one class and legacy 'classdda'
            // for another), this entry's migration is ignored so it isn't applied twice.
            // Same parsing as the legacy scheme: "classdda <class_id>|<delta> <races> [gap...]", class_id
            // delimited by '|' (can contain spaces: "Group C", "Oldacre Orn227"...), same as 'seatocc'.
            const char* v = line + 8;                       // after "classdda"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            size_t bar = rest.find('|');
            if (bar != std::string::npos) {
                std::string cid = rest.substr(0, bar);
                const char* p = rest.c_str() + bar + 1;
                float dl = 0.0f; int nr = 0, consumed = 0;
                if (!cid.empty() && sscanf(p, "%f %d%n", &dl, &nr, &consumed) == 2) {
                    std::vector<float> gap;
                    const char* g0 = p + consumed;
                    float g = 0.0f; int adv = 0;
                    while (sscanf(g0, " %f%n", &g, &adv) == 1) { gap.push_back(g); g0 += adv; }
                    legacyDelta[cid] = dl; legacyRaces[cid] = nr; legacyGap[cid] = gap;
                }
            } else {
                // Backward compatibility: the old format "classdda <id> <delta> <races> [gap...]" (with no
                // '|') was reliably parseable when the id had no spaces (the case that
                // "worked" before the '|' was introduced) — supporting it again is trivial and free, so
                // it's kept as a fallback instead of dropping the line. It goes into the same pair of local maps: it's migrated
                // the same as the '|' branch.
                char cid[128] = {0}; float dl = 0.0f; int nr = 0, consumed = 0;
                if (sscanf(rest.c_str(), "%127s %f %d%n", cid, &dl, &nr, &consumed) == 3 && cid[0]) {
                    std::vector<float> gap;
                    const char* g0 = rest.c_str() + consumed;   // fix: the legacy branch must also read the gap tail
                    float g = 0.0f; int adv = 0;
                    while (sscanf(g0, " %f%n", &g, &adv) == 1) { gap.push_back(g); g0 += adv; }
                    legacyDelta[cid] = dl; legacyRaces[cid] = nr; legacyGap[cid] = gap;
                }
            }
        }
        else if (!strcmp(key, "classfeel")) {
            // "classfeel <class_id>|<feel> <streak>". Same split at the first '|'
            // as 'classdda' (the class_id can contain spaces). It can arrive before its 'classdda' if the
            // save was hand-edited: operator[] creates the entry with delta/races at zero, which is correct
            // (a class with a recorded feel and no settled races doesn't move anything on its own).
            const char* v = line + 9;                       // after "classfeel"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            size_t bar = rest.find('|');
            if (bar != std::string::npos) {
                std::string cid = rest.substr(0, bar);
                int fe = 0, st = 0;
                if (!cid.empty() && sscanf(rest.c_str() + bar + 1, "%d %d", &fe, &st) == 2 && fe >= 1 && fe <= 5) {
                    career::ClassDdaState& s = d.prog.class_dda[cid];
                    s.last_feel = fe; s.feel_streak = st;
                }
            }
        }
        else if (!strcmp(key, "classlock")) {
            // "classlock <class_id>|<0|1>". Like 'classfeel', it uses operator[] to avoid depending
            // on line order: the format doesn't guarantee 'classai' comes first.
            const char* v = line + 9;                       // after "classlock"
            while (*v == ' ') v++;
            std::string rest = v;
            while (!rest.empty() && (rest.back() == '\r' || rest.back() == '\n')) rest.pop_back();
            const size_t bar = rest.find('|');
            if (bar != std::string::npos) {
                const std::string cid = rest.substr(0, bar);
                int on = 0;
                if (!cid.empty() && sscanf(rest.c_str() + bar + 1, "%d", &on) == 1)
                    d.prog.class_dda[cid].locked = (on != 0);
            }
        }
        else if (!strcmp(key, "lock_all")) { int v = 0; sscanf(line, "%*s %d", &v); d.prog.lock_all = (v != 0); }
        else if (!strcmp(key, "telemetry")) {
            // "telemetry <enabled> [already_asked]". The second field is optional: a single-value
            // line could only have been written by a version where enabling it was, by definition, an
            // explicit answer -> already-asked is assumed. Total absence of the line = neither enabled nor asked.
            int on = 0, asked = -1;
            int n = sscanf(line, "%*s %d %d", &on, &asked);
            if (n >= 1) {
                d.prog.telemetry_opt_in = (on != 0);
                d.prog.telemetry_asked  = (n >= 2) ? (asked != 0) : true;
            }
        }
        else if (!strcmp(key, "nemesis")) {    // "nemesis <driver_id>" (same pattern as 'gate')
            const std::string nid = ValorHastaFinDeLinea(line, 7);   //
            if (!nid.empty()) d.nemesis_id = nid;
        }
        else if (!strcmp(key, "birthyear")) {   // "birthyear <driver_id> <year>" (id has no spaces -> %s)
            char bid[128] = {0}; int by = 0;
            int got = sscanf(line, "%*s %127s %d", bid, &by);
            std::string clave;   //
            { static const int cola[] = { 1 }; std::string tail;
              if (ClaveConEspacios(line, 9, got < 2, cola, 1, clave, tail)) {
                  if (sscanf(tail.c_str(), "%d", &by) == 1) got = 2; else clave.clear();
              } }
            { const std::string k = clave.empty() ? std::string(bid) : clave;
              if (got == 2 && !k.empty()) d.birth_year[k] = by; }
        }
        else if (!strcmp(key, "retired")) {     // "retired <driver_id>" (same pattern as 'gate')
            const std::string rid = ValorHastaFinDeLinea(line, 7);   //
            if (!rid.empty()) d.retired.insert(rid);
        }
        else if (!strcmp(key, "seatseed")) {    // 23/09: "seatseed <driver_id>" (same pattern as 'retired')
            const std::string sid = ValorHastaFinDeLinea(line, 8);
            if (!sid.empty()) d.seat_seeded.insert(sid);
        }
        else if (!strcmp(key, "worldyear")) sscanf(line, "%*s %d", &d.current_world_year);
        else if (!strcmp(key, "seatocc")) {
            // "seatocc <key>|<driver_id>" (key/id can contain special characters -> split at '|').
            // Absence -> empty map (back-compat). Attaches key->driver_id in d.seat_occupant.
            const char* v = line + 7;                       // after "seatocc"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            // — SPLIT AT THE LAST '|', NOT THE FIRST. The per-slot seat key IS
            // "<serie>|<veh>|<librea>": splitting at the first one, the line
            // "seatocc onyx_cup_4824|12345|7|rd_hamilton" was read as key "onyx_cup_4824" and occupant "123457rd_
            // hamilton" — the whole save silently corrupted. The driver id never carries '|', so
            // the last one is the right separator, and with a legacy key (no pipes) the last IS the first:
            // old saves are read exactly the same.
            const size_t cut = rest.rfind('|');
            if (cut != std::string::npos && cut > 0 && cut + 1 < rest.size())
                d.seat_occupant[rest.substr(0, cut)] = rest.substr(cut + 1);
        }
        else if (!strcmp(key, "seatstart")) {
            // "seatstart <key>|<year>". Absence -> empty map -> all seats are read
            // as occupied since the career started (back-compat with any older save).
            const char* v = line + 9;                      // after "seatstart"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            const size_t cut = rest.rfind('|');            // the LAST '|' (see 'seatocc')
            if (cut != std::string::npos && cut > 0) {
                const int yr = atoi(rest.c_str() + cut + 1);
                if (yr > 0) d.seat_contract_start[rest.substr(0, cut)] = yr;
            }
        }
        else if (!strcmp(key, "idle")) {
            // ~~~29/08: "idle <id>|<temporadas>". Absence -> empty map -> an earlier save is read the same.
            const char* v = line + 4;
            while (*v == ' ') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            const size_t cut = rest.rfind('|');
            if (cut != std::string::npos && cut > 0) {
                // "idle <id>|<n>|<serie>": the LAST | separates the series, the previous one the counter.
                const std::string serie = rest.substr(cut + 1);
                const std::string izq = rest.substr(0, cut);
                const size_t cut2 = izq.rfind('|');
                if (cut2 != std::string::npos && cut2 > 0) {
                    const int nIdle = atoi(izq.c_str() + cut2 + 1);
                    if (nIdle > 0) {
                        d.seasons_idle[izq.substr(0, cut2)] = nIdle;
                        if (!serie.empty()) d.idle_from_series[izq.substr(0, cut2)] = serie;
                    }
                }
            }
        }
        else if (!strcmp(key, "gendrv")) {
            // "gendrv <id>|<name>|<country>|<talent>|<ceiling>". Absence -> empty map -> a save
            // predating this feature reads exactly as before (back-compat).
            const char* v = line + 6;                      // after "gendrv"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[5]; int pi = 0;
            for (char ch : rest) { if (ch == '|') { if (pi < 4) pi++; } else if (pi < 5) parts[pi] += ch; }
            if (!parts[0].empty() && !parts[1].empty()) {
                SaveData::GenDriver g;
                g.name = parts[1]; g.country = parts[2];
                g.talent = atof(parts[3].c_str());
                g.tier_max = atoi(parts[4].c_str());
                if (g.tier_max < 1) g.tier_max = 1; if (g.tier_max > 7) g.tier_max = 7;
                d.gen_drivers[parts[0]] = g;
                // ...and its name to the engine's registry, or the many places that resolve
                // id->name would show the player a raw "gn_2025_1160919266".
                // along with its COUNTRY (the save carries it in parts[2]) — without it, re-attaching the grid
                // left the previous occupant's flag showing when this name was set.
                RegisterGeneratedName(parts[0], g.name, g.country);
            }
        }
        else if (!strcmp(key, "lastcircuit")) sscanf(line, "%*s %lu", &d.last_circuit_hash);
        else if (!strcmp(key, "h2h")) {   // "h2h <driver_id> <w> <l>"
            // id has no spaces (the inverse of DriverById) -> %s is safe (same precedent as driverrating). Defensive:
            // sscanf==3 + non-empty id; a short/malformed line -> ignored, no crash. Group-by-id (one
            // entry per id; the last one wins, like driver_ratings). No lines -> empty map (back-compat).
            char did[128] = {0}; int w = 0, l = 0;
            int got = sscanf(line, "%*s %127s %d %d", did, &w, &l);
            std::string clave;   //
            { static const int cola[] = { 2 }; std::string tail;
              if (ClaveConEspacios(line, 3, got < 3, cola, 1, clave, tail)) {
                  if (sscanf(tail.c_str(), "%d %d", &w, &l) == 2) got = 3; else clave.clear();
              } }
            { const std::string k = clave.empty() ? std::string(did) : clave;
              if (got == 3 && !k.empty()) { d.h2h[k].w = w; d.h2h[k].l = l; } }
        }
        else if (!strcmp(key, "beef")) {   // "beef <driver_id> <level> <last_year> <last_race_idx>"
            // id has no spaces (same precedent as h2h/driverrating) -> %s is safe. Defensive: sscanf==4 + non-empty
            // id; a short/malformed line -> ignored (no crash). Group-by-id (last-write-wins). No
            // lines -> empty map (back-compat).
            char bid[128] = {0}; int level = 0, lastYear = 0, lastRaceIdx = -1;
            int got = sscanf(line, "%*s %127s %d %d %d", bid, &level, &lastYear, &lastRaceIdx);
            std::string clave;   //
            { static const int cola[] = { 3 }; std::string tail;
              if (ClaveConEspacios(line, 4, got < 4, cola, 1, clave, tail)) {
                  if (sscanf(tail.c_str(), "%d %d %d", &level, &lastYear, &lastRaceIdx) == 3) got = 4; else clave.clear();
              } }
            { const std::string k = clave.empty() ? std::string(bid) : clave;
              if (got == 4 && !k.empty()) {
                  Beef& be = d.beef[k];
                  be.level = level; be.last_year = lastYear; be.last_race_idx = lastRaceIdx;
              } }
        }
        else if (!strcmp(key, "relmark")) {
            // Press relationship marks: "relmark <year>|<delta_x1000>|<team>" (team goes last, it can
            // contain spaces -> split at '|' like 'mailnarr'/'season', the %s used for h2h won't do here). A
            // short/malformed line (missing the year field) -> ignored, no crash. Multiple marks per team are accepted
            // (RelMarksSum sums them all, decayed).
            const char* v = line + 7;                       // after "relmark" (strlen("relmark")=7)
            while (*v == ' ' || *v == '\t') v++;
            std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[3]; int pi = 0;               // [0]=year [1]=delta_x1000 [2]=team
            for (char c : rest) { if (c == '|') { if (pi < 2) pi++; } else if (pi < 3) parts[pi] += c; }
            if (!parts[0].empty()) {
                RelMark m;
                m.year        = atoi(parts[0].c_str());
                m.delta_x1000 = atoi(parts[1].c_str());
                m.team        = parts[2];
                d.rel_marks.push_back(m);
            }
        }
        else if (!strcmp(key, "memory")) {   // "memory <id> <cleared> <last_played_year>"
            char mid[128] = {0}; int cleared = 0, year = 0;
            int got = sscanf(line, "%*s %127s %d %d", mid, &cleared, &year);
            std::string clave;   // the memory_id can contain spaces
            { static const int cola[] = { 2, 1 }; std::string tail;
              if (ClaveConEspacios(line, 6, got < 2, cola, 2, clave, tail)) {
                  const int nums = sscanf(tail.c_str(), "%d %d", &cleared, &year);
                  if (nums >= 1) got = nums + 1; else clave.clear();
              } }
            { const std::string k = clave.empty() ? std::string(mid) : clave;
              if (got >= 2 && !k.empty()) {
                  MemoryProgress mp; mp.cleared = cleared; mp.last_played_year = year;
                  d.memories_done[k] = mp;
              } }
        }
        else if (!strcmp(key, "racelog")) {   // times log — pipe: class|series|track|hash|pos|field|best|total|year|doy[|rating]
            // The trailing 11th field is the post-settle rating snapshot (appended,
            // append-don't-insert). parts[] sized to 11 + the pipe index advanced up to 10; a legacy 10-field line leaves
            // parts[10] empty -> e.rating == 0 (sentinel) and the first 10 fields are never garbled.
            const char* v = line + 7; while (*v == ' ' || *v == '\t') v++;
            std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[11]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 10) pi++; } else if (pi < 11) parts[pi] += c; }
            RaceLogEntry e;
            e.class_id = parts[0]; e.series_id = parts[1]; e.track = parts[2];
            e.track_hash = atol(parts[3].c_str());
            e.pos = atoi(parts[4].c_str()); e.field = atoi(parts[5].c_str());
            e.best_lap = (float)atof(parts[6].c_str()); e.total_time = (float)atof(parts[7].c_str());
            e.year = atoi(parts[8].c_str()); e.doy = atoi(parts[9].c_str());
            e.rating = parts[10].empty() ? 0 : atoi(parts[10].c_str());   // legacy 10-field line -> rating 0
            d.race_log.push_back(e);
        }
        else if (!strcmp(key, "rldnf")) {   // P2 (F15): the 'racelog' just before was an abandonment
            // It attaches to the LAST 'racelog' read, same as 'rgrow'. Orphan (without a prior racelog) -> ignored.
            // Without the line, the race is a finish: that is how saves from before and those from 0.6.8 load.
            int dnf = 1; sscanf(line, "%*s %d", &dnf);
            if (!d.race_log.empty() && dnf != 0) d.race_log.back().finished = false;
        }
        else if (!strcmp(key, "rgrow")) {   // row of the full race grid — pipe: pos|name|driver_id|car_class|car_name|laps|best|dnf|is_player|total
            // Attached to the last 'racelog' read (they're always written right after it). An orphaned 'rgrow'
            // (no preceding racelog, malformed save) is ignored without crashing. Name/class/car can contain spaces.
            if (!d.race_log.empty()) {
                const char* v = line + 5; while (*v == ' ' || *v == '\t') v++;   // after "rgrow"
                std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
                std::string parts[11]; int pi = 0;
                for (char c : rest) { if (c == '|') { if (pi < 10) pi++; } else if (pi < 11) parts[pi] += c; }
                RaceGridRow g;
                g.pos = atoi(parts[0].c_str()); g.name = parts[1]; g.driver_id = parts[2];
                g.car_class = parts[3]; g.car_name = parts[4]; g.laps = atoi(parts[5].c_str());
                g.best_lap = (float)atof(parts[6].c_str()); g.dnf = atoi(parts[7].c_str()) != 0;
                g.is_player = atoi(parts[8].c_str()) != 0; g.total_time = (float)atof(parts[9].c_str());
                // field 11 is additive. A legacy line (10 fields) -> false, deliberately: those captures were
                // taken at the flag with half the grid still on track, so their 1-lap deficit was almost
                // always "finishing their last lap", not being lapped — never claim "+1 L" without having measured it.
                g.finished = parts[10].empty() ? false : (atoi(parts[10].c_str()) != 0);
                d.race_log.back().grid.push_back(g);
            }
        }
        else if (!strcmp(key, "email")) {
            // Generated emails, extended over time by the emails-with-life feature and the negotiation redesign, plus a
            // fix for emails getting an absurd date across a year boundary (Jul 23 2026):
            // "email <id>|<tag>|<day>|<family>|<read>|<answered>|<choice>|<subtype>|<payload_key>|<variant>|
            // <params>|<from_name>|<from_role_key>|<neg_id>|<year>" (15 fields). Everything after "email " is split at
            // '|' (id/payload_key/from_name can contain spaces -> no %s). A short/malformed line -> empty/0
            // parts, no crash (the pipe index is capped at 14; unknown keys fall through the if/else; a
            // malformed payload_key re-resolves to a translation placeholder, it never executes). The 4 fields added by the emails-with-life feature
            // plus neg_id and year are additive: an old 9/13/14-field line leaves the tails empty ->
            // variant=0, params/from_name/from_role_key/neg_id="", year=0 (struct defaults, back-compat).
            const char* v = line + 5;                       // after "email" (strlen("email")=5; the while loop skips the space)
            while (*v == ' ' || *v == '\t') v++;            // skips the separator
            std::string rest;                               // trims trailing CR/LF
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[15]; int pi = 0;              // 15 fields (year fix: +year)
            for (char c : rest) { if (c == '|') { if (pi < 14) pi++; } else if (pi < 15) parts[pi] += c; }
            if (!parts[0].empty()) {                        // no id -> invalid line, ignore
                GeneratedEmail e;
                e.id          = parts[0];
                e.tag         = atoi(parts[1].c_str());
                e.day         = atoi(parts[2].c_str());
                e.family      = atoi(parts[3].c_str());
                e.read        = atoi(parts[4].c_str());
                e.answered    = atoi(parts[5].c_str());
                e.choice      = atoi(parts[6].c_str());
                e.subtype     = atoi(parts[7].c_str());
                e.payload_key = parts[8];                   // can contain spaces (survives the pipe split)
                e.variant     = atoi(parts[9].c_str());     // "" (old save) -> 0
                e.params      = parts[10];                  // encoded; "" -> no params
                e.from_name   = parts[11];
                e.from_role_key = parts[12];
                e.neg_id      = parts[13];                  // "" (13-field save) -> email not part of a thread
                e.year        = atoi(parts[14].c_str());    // "" (14-field save, before the fix) -> 0 = unknown year
                d.emails.push_back(e);
            }
        }
        else if (!strcmp(key, "nego")) {
            // Negotiation redesign (Jul 20 2026): "nego <neg_id>|<state>|<round>|<patience>|
            // <patience_base>|<respond_day>|<deadline_day>|<reserve_day>|<last_action_day>|<want_pct>|
            // <form_pct>|<trial_pct>|<flags>|<player_mask>|<player_salaryPct>|<player_sponsorRank>|
            // <table_mask>|<table_salaryPct>|<table_sponsorRank>|<table_is_counter>|<offer.id>|<offer.team>|
            // <offer.from>|<offer.clsId>|<offer.scope>|<offer.contractKind>|<offer.archetype>|
            // <offer.signBonus>|<offer.perRace>|<offer.commitment>|<offer.promote_to>" (31 fields).
            const char* v = line + 4; while (*v == ' ' || *v == '\t') v++;
            std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[31]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 30) pi++; } else if (pi < 31) parts[pi] += c; }
            if (!parts[0].empty()) {
                Negotiation n;
                auto termsOf = [](unsigned m, int sal, int sp) { DealTerms t;
                    for (int i = 0; i < bal::NEGO_DEMAND_COUNT; i++) t.demands[i] = (m >> i) & 1u;
                    t.salaryPct = sal; t.sponsorTierRank = sp; return t; };
                n.neg_id = parts[0];
                n.state = atoi(parts[1].c_str()); n.round = atoi(parts[2].c_str());
                n.patience = atoi(parts[3].c_str()); n.patience_base = atoi(parts[4].c_str());
                n.respond_day = atoi(parts[5].c_str()); n.deadline_day = atoi(parts[6].c_str());
                n.reserve_day = atoi(parts[7].c_str()); n.last_action_day = atoi(parts[8].c_str());
                n.want_pct = atoi(parts[9].c_str()); n.form_pct = atoi(parts[10].c_str());
                n.trial_pct = atoi(parts[11].c_str()); n.flags = atoi(parts[12].c_str());
                n.player_terms = termsOf((unsigned)strtoul(parts[13].c_str(), nullptr, 10), atoi(parts[14].c_str()), atoi(parts[15].c_str()));
                n.table_terms  = termsOf((unsigned)strtoul(parts[16].c_str(), nullptr, 10), atoi(parts[17].c_str()), atoi(parts[18].c_str()));
                n.table_is_counter = atoi(parts[19].c_str()) != 0;
                n.offer.id = parts[20]; n.offer.direct = true;
                n.offer.team = parts[21]; n.offer.from = parts[22];
                n.offer.clsId = parts[23]; n.offer.scope = parts[24];
                n.offer.contractKind = atoi(parts[25].c_str()); n.offer.archetype = atoi(parts[26].c_str());
                n.offer.signBonus = atol(parts[27].c_str()); n.offer.perRace = atol(parts[28].c_str());
                n.offer.commitment = atoi(parts[29].c_str()); n.offer.promote_to = parts[30];
                d.negotiations.push_back(n);
            }
        }
        else if (!strcmp(key, "negoh")) {
            // "negoh <neg_id>|<day>|<who>|<kind>|<demands_mask>|<salaryPct>|<sponsorTierRank>|
            // <sponsorCardId>" (8 fields). Attaches to the thread with that neg_id (the 'nego' line always precedes
            // its 'negoh' lines when writing); orphaned (neg_id not found) -> ignored without crashing.
            const char* v = line + 5; while (*v == ' ' || *v == '\t') v++;
            std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[8]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 7) pi++; } else if (pi < 8) parts[pi] += c; }
            if (!parts[0].empty())
                for (auto& n : d.negotiations) if (n.neg_id == parts[0]) {
                    NegoRoundLog h;
                    h.day = atoi(parts[1].c_str()); h.who = atoi(parts[2].c_str()); h.kind = atoi(parts[3].c_str());
                    h.demands_mask = (unsigned)strtoul(parts[4].c_str(), nullptr, 10);
                    h.salaryPct = atoi(parts[5].c_str()); h.sponsorTierRank = atoi(parts[6].c_str());
                    h.sponsorCardId = parts[7];
                    n.history.push_back(h); break;
                }
        }
        else if (!strcmp(key, "seatwatch")) {
            // "seatwatch <series_id>|<class_name>|<team_label>|<rival_id>|<rival_name>|<opened_day>|
            // <races_at_open>|<races_window>|<state>|<resolved_day>|<races_seen>|<rival_ahead_streak>" (12
            // fields). Same pipe idiom as 'nego'. The last 2 are additive: a 10-field line (the first
            // version of the observation, with no streak) leaves parts[10..11] empty -> atoi("")=0, which is exactly
            // "no race has been recorded yet". No series means no seat -> discarded without crashing.
            const char* v = line + 9;                       // after "seatwatch" (strlen=9)
            while (*v == ' ' || *v == '\t') v++;
            std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[12]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 11) pi++; } else if (pi < 12) parts[pi] += c; }
            if (!parts[0].empty()) {
                SeatWatch w;
                w.series_id = parts[0]; w.class_name = parts[1]; w.team_label = parts[2];
                w.rival_id = parts[3];  w.rival_name = parts[4];
                w.opened_day    = atoi(parts[5].c_str());
                w.races_at_open = atoi(parts[6].c_str());
                w.races_window  = atoi(parts[7].c_str());
                w.state         = atoi(parts[8].c_str());
                w.resolved_day  = atoi(parts[9].c_str());
                w.races_seen         = atoi(parts[10].c_str());   // additive: a 10-field line -> 0
                w.rival_ahead_streak = atoi(parts[11].c_str());
                d.seat_watches.push_back(w);
            }
        }
        else if (!strcmp(key, "winseat")) {
            // "winseat <series_id>|<team_label>|<state>|<opened_day>|<applied>|
            // <winner_id>|<winner_name>|<sounded>" (8 fields). The last ones are additive: a 5-field line (a seat
            // still unresolved) leaves them empty, which is exactly "nobody has landed it yet". No series or
            // no team means no key -> discarded without crashing, same as 'seatwatch'.
            // the 8th field (sounded) was added at the end, so a 7-field save reads it as 0 -- "nobody
            // approached you", which is exactly what was true before approaches existed.
            const char* v = line + 7;                       // after "winseat" (strlen=7)
            while (*v == ' ' || *v == '\t') v++;
            std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[9]; int pi = 0;   // 23/09 (): 9th field `seated`, additive (absent = 0)
            for (char c : rest) { if (c == '|') { if (pi < 8) pi++; } else if (pi < 9) parts[pi] += c; }
            if (!parts[0].empty() && !parts[1].empty()) {
                WindowSeat w;
                w.series_id  = parts[0]; w.team_label = parts[1];
                w.state      = atoi(parts[2].c_str());
                w.opened_day = atoi(parts[3].c_str());
                w.applied    = atoi(parts[4].c_str());
                w.winner_id  = parts[5]; w.winner_name = parts[6];
                w.sounded    = atoi(parts[7].c_str());
                w.seated     = atoi(parts[8].c_str());
                d.window_seats.push_back(w);
            }
        }
        else if (!strcmp(key, "winyear")) sscanf(line, "%*s %d", &d.window_year);
        else if (!strcmp(key, "seatwatchc")) {
            // "seatwatchc <series_id>|<team_label>|<id>|<name>" (4 fields). Attaches to the
            // observation for that seat (the 'seatwatch' line always precedes its 'seatwatchc' lines when
            // writing); orphaned (seat not found) -> ignored without crashing, same as 'negoh'.
            const char* v = line + 10;                      // after "seatwatchc" (strlen=10)
            while (*v == ' ' || *v == '\t') v++;
            std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[4]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 3) pi++; } else if (pi < 4) parts[pi] += c; }
            if (!parts[0].empty() && !parts[2].empty())
                for (auto& w : d.seat_watches)
                    if (w.series_id == parts[0] && w.team_label == parts[1]) {
                        SeatContender sc; sc.id = parts[2]; sc.name = parts[3];
                        w.contenders.push_back(sc); break;
                    }
        }
        else if (!strcmp(key, "withdrawn")) {   // "withdrawn <offer_id>" (same pattern as 'declined')
            const std::string buf = ValorHastaFinDeLinea(line, 9);   //
            if (!buf.empty()) d.withdrawn_offers.insert(buf);
        }
        else if (!strcmp(key, "mailnarr")) {
            // "mailnarr <onboard_stage>|<consec_podiums>|<consec_bottom>|
            // <last_notable_track>|<season_podiums>|<season_wins>|<season_start_credits>|<consec_clean>" (8
            // fields; the 8th was added later). Same pipe idiom as 'email'; an absent line (old save) -> d.mailnarr
            // stays at the struct's defaults. A legacy 7-field line (before the 8th was added) -> parts[7] stays empty ->
            // atoi("") = 0 (back-compat: consec_clean starts at 0).
            sawMailnarr = true;
            const char* v = line + 8;                       // after "mailnarr" (strlen("mailnarr")=8)
            while (*v == ' ' || *v == '\t') v++;
            std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[8]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 7) pi++; } else if (pi < 8) parts[pi] += c; }
            d.mailnarr.onboard_stage        = atoi(parts[0].c_str());
            d.mailnarr.consec_podiums       = atoi(parts[1].c_str());
            d.mailnarr.consec_bottom        = atoi(parts[2].c_str());
            d.mailnarr.last_notable_track   = parts[3];
            d.mailnarr.season_podiums       = atoi(parts[4].c_str());
            d.mailnarr.season_wins          = atoi(parts[5].c_str());
            d.mailnarr.season_start_credits = atol(parts[6].c_str());
            d.mailnarr.consec_clean         = atoi(parts[7].c_str());
        }
        else if (!strcmp(key, "renewal")) {
            // "renewal <id>|<team>|<from>|<clsId>|<scope>|<contractKind>|
            // <archetype>|<signBonus>|<perRace>|<commitment>|<promote_to>" (11 fields). Same pipe loader as
            // 'email'/'contract': everything after "renewal " split at '|'. A short/malformed line -> empty/0 parts,
            // no crash. Reconstructs the carrying market::OfferDef (the cosmetic fields stay at their default;
            // the renderer re-derives them). No lines -> pending_renewals empty (back-compat).
            const char* v = line + 7;                       // after "renewal" (strlen=7; the while loop skips the space)
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[11]; int pi = 0;              // 11 fields
            for (char c : rest) { if (c == '|') { if (pi < 10) pi++; } else if (pi < 11) parts[pi] += c; }
            if (!parts[0].empty()) {                        // no id -> invalid line, ignore
                market::OfferDef r;
                r.id           = parts[0];
                r.direct       = true;                      // a renewal is always direct (you get re-signed)
                r.team         = parts[1];
                r.from         = parts[2];
                r.clsId        = parts[3];
                r.scope        = parts[4];
                r.contractKind = atoi(parts[5].c_str());
                r.archetype    = atoi(parts[6].c_str());
                r.signBonus    = atol(parts[7].c_str());
                r.perRace      = atol(parts[8].c_str());
                r.commitment   = atoi(parts[9].c_str());
                r.promote_to   = parts[10];                 // can contain spaces (survives the pipe split)
                d.pending_renewals.push_back(r);
            }
        }
        else if (!strcmp(key, "subride")) {
            // "subride <series_id>|<seat_idx>|<event_absday>|<fee>|<state>|<team_label>"
            // (same pipe idiom as 'cardshop'; series_id has no spaces but it's still split at '|' for consistency).
            // team_label (6th field) is tolerant: 5-field saves leave it empty (back-compat); it can
            // contain spaces (never '|'), which is why it goes last and is read as the rest of the segment.
            const char* v = line + 7;                       // after "subride" (strlen("subride")=7)
            while (*v == ' ' || *v == '\t') v++;
            std::string rest; for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[6]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 5) pi++; } else if (pi < 6) parts[pi] += c; }
            d.sub_ride.series_id     = parts[0];
            d.sub_ride.seat_idx      = atoi(parts[1].c_str());
            d.sub_ride.event_absday  = atoi(parts[2].c_str());
            d.sub_ride.fee           = atol(parts[3].c_str());
            d.sub_ride.state         = atoi(parts[4].c_str());
            d.sub_ride.team_label    = parts[5];
        }
        else if (!strcmp(key, "season")) {
            // "season <series_id>|<year>|<final_pos>|<rounds>[|<team_id>]". Same as 'courting': we take
            // everything after "season " and split at '|' (the series_id can contain spaces -> no %s).
            // <team_id> is an optional 5th field (only present if driven for a team);
            // a legacy 4-field line parses identically (team_id -> "", back-compat). It's the last field ->
            // any remaining '|' fall inside it (it survives pipes/spaces, like 'promote_to').
            const char* v = line + 6;                       // after "season"
            while (*v == ' ' || *v == '\t') v++;            // skips the separator
            std::string rest;                               // trims trailing CR/LF
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[5]; int pi = 0;               // [0]=series_id [1]=year [2]=final_pos [3]=rounds [4]=team_id
            for (char c : rest) { if (c == '|') { if (pi < 4) pi++; } else if (pi < 5) parts[pi] += c; }
            if (!parts[0].empty()) {
                CompletedSeason cs;
                cs.series_id = parts[0];
                cs.year      = atoi(parts[1].c_str());
                cs.final_pos = atoi(parts[2].c_str());
                cs.rounds    = atoi(parts[3].c_str());
                cs.team_id   = parts[4];                     // "" if the legacy line didn't carry the 5th field (back-compat)
                d.completed_seasons.push_back(cs);
            }
        }
        else if (!strcmp(key, "sponsor")) {
            // "sponsor <id>|<tier>|<kind>|<cap_rounds>|<cap_value>|<rounds_used>|<value_used>" (one per trait).
            const char* v = line + 7;                       // after "sponsor"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[7]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 6) pi++; } else if (pi < 7) parts[pi] += c; }
            if (!parts[0].empty()) {
                std::string id = parts[0];
                SignedSponsor* sp = nullptr;                // groups the trait under its sponsor (by id)
                for (auto& e : d.signed_sponsors) if (e.id == id) { sp = &e; break; }
                if (!sp) { d.signed_sponsors.push_back(SignedSponsor{}); sp = &d.signed_sponsors.back(); sp->id = id; }
                sp->tier = atoi(parts[1].c_str());
                SponsorTrait tr;
                tr.kind       = atoi(parts[2].c_str());
                tr.cap_rounds = atoi(parts[3].c_str());
                tr.cap_value  = atol(parts[4].c_str());
                sp->traits.push_back(tr);
                sp->rounds_used.push_back(atoi(parts[5].c_str()));
                sp->value_used.push_back(atol(parts[6].c_str()));
            }
        }
        else if (!strcmp(key, "sponsor_obj")) {
            // "sponsor_obj <id>|<obj_kind>|<obj_goal>|<obj_progress>|<paid>|<signed_year>|<tier>".
            // +tier (7th, optional). Groups by id on the same SignedSponsor as the
            // 'sponsor' lines (same group-by-id idiom): if the id already exists (because a 'sponsor' line
            // came before), it fills in its objective fields; otherwise it creates the SignedSponsor with just id+objective (a
            // sponsor with no coverage traits is valid — and it's the shape the production converter emits).
            const char* v = line + 11;                      // after "sponsor_obj"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[7]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 6) pi++; } else if (pi < 7) parts[pi] += c; }
            if (!parts[0].empty()) {
                std::string id = parts[0];
                SignedSponsor* sp = nullptr;                // groups the objective under its sponsor (by id)
                for (auto& e : d.signed_sponsors) if (e.id == id) { sp = &e; break; }
                if (!sp) { d.signed_sponsors.push_back(SignedSponsor{}); sp = &d.signed_sponsors.back(); sp->id = id; }
                sp->obj_kind     = parts[1];
                sp->obj_goal     = atoi(parts[2].c_str());
                sp->obj_progress = atoi(parts[3].c_str());
                sp->paid         = atoi(parts[4].c_str()) != 0;
                sp->signed_year  = atoi(parts[5].c_str());
                // tier survives even when no 'sponsor' line was written (an empty-traits production sponsor).
                // Legacy 6-field line -> parts[6] empty -> tier untouched (still set from any preceding 'sponsor'
                // line, else the SignedSponsor default 0) -> back-compat preserved.
                if (!parts[6].empty()) sp->tier = atoi(parts[6].c_str());
            }
        }
        else if (!strcmp(key, "card")) {
            // (Jul 11 2026) "card <id>|<level>|<slot>|<races_active>|<races_left>|<uses_left>|<clause_miss>|<invested>".
            // Pipe-delimited (card_id has no spaces -> %s is enough). Additive over kSaveVersion=2 (no re-bump):
            // a save with no cards loads to clean defaults.
            const char* v = line + 5;                            // after "card "
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string p[8]; int pi = 0;
            for (char ch : rest) { if (ch == '|') { if (pi < 7) pi++; } else if (pi < 8) p[pi] += ch; }
            if (!p[0].empty()) {
                SignedCard sc; sc.card_id = p[0];
                sc.level        = atoi(p[1].c_str());
                sc.slot         = atoi(p[2].c_str());
                sc.races_active = atoi(p[3].c_str());
                sc.races_left   = atoi(p[4].c_str());
                sc.uses_left    = atoi(p[5].c_str());
                sc.clause_miss  = atoi(p[6].c_str());
                sc.invested     = atol(p[7].c_str());
                if (sc.level < 1) sc.level = 1;
                d.cards.push_back(sc);
            }
        }
        else if (!strcmp(key, "cardshop")) {
            // "cardshop <race_id>|<seed_base>|<rerolls>|<bought>" (only if open==true).
            // Pipe-delimited (race_id has no spaces -> %s is enough).
            const char* v = line + 9;                            // after "cardshop "
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string p[4]; int pi = 0;
            for (char ch : rest) { if (ch == '|') { if (pi < 3) pi++; } else if (pi < 4) p[pi] += ch; }
            d.card_shop.open = true;
            d.card_shop.race_id   = p[0];
            d.card_shop.seed_base = atoi(p[1].c_str());
            d.card_shop.rerolls   = atoi(p[2].c_str());
            d.card_shop.bought    = atoi(p[3].c_str());
        }
        else if (!strcmp(key, "cardshop_offer")) {
            std::string rest;
            for (const char* q = line + 15; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            d.card_shop.offers.push_back(rest);
        }
        else if (!strcmp(key, "cardshop_bought")) {
            std::string rest;
            for (const char* q = line + 16; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            d.card_shop.bought_ids.push_back(rest);
        }
        else if (!strcmp(key, "cardseen")) {   // "cardseen <n>" (cards already seen in the tab)
            d.cards_seen = atoi(line + 9);
            if (d.cards_seen < 0) d.cards_seen = 0;
        }
        else if (!strcmp(key, "privpick")) {   // "privpick <series_id>|<veh_hash>|<livery_ord>"
            const char* v = line + 9;                            // after "privpick "
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string p[3]; int pi = 0;
            for (char ch : rest) { if (ch == '|') { if (pi < 2) pi++; } else if (pi < 3) p[pi] += ch; }
            if (!p[0].empty())
                d.privateer_pick[p[0]] = { atoi(p[1].c_str()), atoi(p[2].c_str()) };
        }
        else if (!strcmp(key, "courting")) {
            // "courting <team_id>|<series_id>|<first_seen_year>". Same as 'player'/'contract': we take
            // everything after "courting " and split at '|' (the first two fields can contain spaces).
            const char* v = line + 8;                       // after "courting"
            while (*v == ' ' || *v == '\t') v++;            // skips the separator
            std::string rest;                               // trims trailing CR/LF
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[3]; int pi = 0;               // [0]=team_id [1]=series_id [2]=first_seen_year
            for (char c : rest) { if (c == '|') { if (pi < 2) pi++; } else if (pi < 3) parts[pi] += c; }
            if (!parts[0].empty() || !parts[1].empty()) {
                Courting ct;
                ct.team_id         = parts[0];
                ct.series_id       = parts[1];
                ct.first_seen_year = atoi(parts[2].c_str());
                d.courting.push_back(ct);
            }
        }
        else if (!strcmp(key, "contract")) {
            // "contract <series_id>|<team_id>|<seat_idx>|<kind>|<seasons_total>|<seasons_left>|<sign_bonus>|<per_race>|<signed_year>".
            // Same as 'player': we take everything after "contract " and split at '|' (the first two fields
            // can contain spaces). Only written if active -> reading it implies an active contract.
            const char* v = line + 8;                       // after "contract"
            while (*v == ' ' || *v == '\t') v++;            // skips the separator
            std::string rest;                               // trims trailing CR/LF
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[9]; int pi = 0;               // 9 fields
            for (char c : rest) { if (c == '|') { if (pi < 8) pi++; } else if (pi < 9) parts[pi] += c; }
            Contract ct;
            ct.series_id     = parts[0];
            ct.team_id       = parts[1];
            ct.seat_idx      = atoi(parts[2].c_str());
            ct.kind          = atoi(parts[3].c_str());
            ct.seasons_total = atoi(parts[4].c_str());
            ct.seasons_left  = atoi(parts[5].c_str());
            ct.sign_bonus    = atol(parts[6].c_str());
            ct.per_race      = atol(parts[7].c_str());
            ct.signed_year   = atoi(parts[8].c_str());
            ct.active        = !ct.series_id.empty();       // the line only exists if there was an active contract
            d.contracts.push_back(ct);                      // multi-contract: each 'contract' line adds one
        }
        else if (!strcmp(key, "contract_ext")) {
            // "contract_ext <obj_done>|<obj_total>|<buy_in>|<promote_to>[|<obj_target_pos>]". Loaded after
            // contract (file order preserved: SaveCareer writes contract then contract_ext). promote_to may hold
            // spaces. 0.4.0: 5th field obj_target_pos, additive — an old save (4 fields) loads 0 (no objective).
            const char* v = line + 12;                      // after "contract_ext"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[5]; int pi = 0;
            for (char c : rest) { if (c == '|') { if (pi < 4) pi++; } else if (pi < 5) parts[pi] += c; }
            if (!d.contracts.empty()) {                     // attaches to the last contract read
                Contract& bc = d.contracts.back();
                bc.obj_done   = atoi(parts[0].c_str());
                bc.obj_total  = atoi(parts[1].c_str());
                bc.buy_in     = atol(parts[2].c_str());
                bc.promote_to = parts[3];
                bc.obj_target_pos = atoi(parts[4].c_str());   // "" (old save) -> 0
            }
        }
        else if (!strcmp(key, "contract_arch")) {
            // The "<key> <archetype>" line. Its own additive key (does not extend the ext line).
            // defensive atoi from line+13 (= the key's strlen); attaches to the last contract read.
            // Absence -> the contract stays at its default 0 (back-compat). Tolerant of truncated/legacy saves.
            if (!d.contracts.empty())
                d.contracts.back().archetype = atoi(line + 13);
        }
        else if (!strcmp(key, "contract_miss")) {
            // The "contract_miss <missed_rounds>" line. Its own additive key (does not extend
            // the ext line). Defensive atoi from line+13 (= strlen "contract_miss" = 13); attaches to the last
            // contract read. Absence -> the contract stays at its default 0 (back-compat). Tolerant of legacy saves.
            if (!d.contracts.empty())
                d.contracts.back().missed_rounds = atoi(line + 13);
        }
        else if (!strcmp(key, "contract_excl")) {
            // The "contract_excl <0/1>" line. Its own additive key; attaches to the last
            // contract read. Absence -> exclusive=false (back-compat). Defensive atoi from line+13.
            if (!d.contracts.empty())
                d.contracts.back().exclusive = (atoi(line + 13) != 0);
        }
        else if (!strcmp(key, "contract_prem")) {
            // The "contract_prem <premium_year>" line. Its own additive key; attaches to the
            // last contract read. Absence -> premium_year=0 (back-compat). Defensive atoi from line+13.
            if (!d.contracts.empty())
                d.contracts.back().premium_year = atoi(line + 13);
        }
        else if (!strcmp(key, "deal")) {
            // "deal <salaryPct>|<demandBits>|<sponsorTierRank>". Loaded onto the active contract;
            // a missing line leaves the neutral default (back-compat).
            int salaryPct = 50, bits = 0, rank = -1;
            if (sscanf(line, "%*s %d|%d|%d", &salaryPct, &bits, &rank) == 3 && !d.contracts.empty()) {
                DealTerms& dt = d.contracts.back().accepted_deal;
                dt.salaryPct = salaryPct;
                dt.sponsorTierRank = rank;
                for (int i = 0; i < bal::NEGO_DEMAND_COUNT; i++)
                    dt.demands[i] = (bits & (1 << i)) != 0;
            }
        }
        else if (!strcmp(key, "courting_ext")) {
            // "courting_ext <index>|<met_rep>|<tier_locked>". Applies to d.courting[index] (written after
            // all 'courting' lines, so the vector is fully populated). Out-of-range index -> ignore.
            int idx = -1, mr = 0, tl = 1;
            if (sscanf(line + 12, " %d|%d|%d", &idx, &mr, &tl) >= 1 &&
                idx >= 0 && (size_t)idx < d.courting.size()) {
                d.courting[idx].met_rep     = (mr != 0);
                d.courting[idx].tier_locked = (tl != 0);
            }
        }
        else if (!strcmp(key, "shmname")) {   // the nickname can contain spaces -> the rest of the line, entire
            const char* v = line + 7;                       // after "shmname"
            while (*v == ' ' || *v == '\t') v++;
            std::string rest;
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            d.shm_player_name = rest;
        }
        else if (!strcmp(key, "player")) {
            // "player <player_id>|<name>|<nationality>|<number>". We take everything after "player " and
            // split at '|' (we don't use %s: the name can contain spaces). Missing fields -> "".
            const char* v = line + 6;                       // after "player"
            while (*v == ' ' || *v == '\t') v++;            // skips the separator
            std::string rest;                               // trims trailing CR/LF
            for (const char* q = v; *q && *q != '\r' && *q != '\n'; q++) rest += *q;
            std::string parts[9]; int pi = 0;               // [0]id [1]name [2]nat [3]num [4]surname [5]sex [6]by [7]bm [8]bd
            for (char c : rest) { if (c == '|') { if (pi < 8) pi++; } else if (pi < 9) parts[pi] += c; }
            d.player.player_id   = parts[0];
            d.player.name        = parts[1];
            d.player.nationality = parts[2];
            d.player.number      = atoi(parts[3].c_str());
            d.player.surname     = parts[4];                // onboarding: old saves (4 fields) -> "" / 0 (back-compat)
            d.player.sex         = parts[5];
            d.player.birth_year  = atoi(parts[6].c_str());
            d.player.birth_month = atoi(parts[7].c_str());
            d.player.birth_day   = atoi(parts[8].c_str());
        }
    }
    // One-time migration. Done here, not inside the loop, because it needs
    // prog.ai_difficulty, and the file's line order isn't guaranteed by the format.
    // NOT "exact and lossless" in every case -- it rounds the saved float delta
    // (up to 0.5 pt) and only clamps base+delta, whereas the old scheme clamped base+trim+delta all at
    // once; with a non-zero trim and a base near the edge of the domain there can be a couple points of
    // drift versus the old behavior. It's the best approximation without re-simulating the race, not a
    // bit-for-bit round-trip.
    // If this category already carries `classai` (classAiSeen), its legacy value is ignored (it isn't migrated twice;
    // the guard is per-class, not a whole-file boolean).
    int migrated = 0;
    for (const auto& kv : legacyDelta) {
        if (classAiSeen.count(kv.first)) continue;
        int races = 0;
        { auto it = legacyRaces.find(kv.first); if (it != legacyRaces.end()) races = it->second; }
        // With races == 0, the old scheme ignored the saved delta and seeded from
        // the community baseline (ClassDeltaEffectiveFor) -- migrating that delta to a concrete number would manufacture a difficulty
        // of its own where the design says "no number yet, the community sets it". The 0 sentinel is left as-is.
        career::ClassDdaState& st = d.prog.class_dda[kv.first];   // operator[]: preserves last_feel/feel_streak
                                                                   // if 'classfeel' for this class already arrived
        st.races = races;
        st.ai = (races == 0) ? 0
                              : career::Clampi(d.prog.ai_difficulty + (int)lround(kv.second),
                                                career::dda::kAiMin, career::dda::kAiMax);
        { auto it = legacyGap.find(kv.first); if (it != legacyGap.end()) st.gap = it->second; }
        migrated++;
    }
    if (migrated > 0) {
        // "the effective difficulty of every category is preserved" was overpromising:
        // the comment two lines above already acknowledges it isn't exact and lossless
        // in every case (delta rounding, a different clamp order). This log is for support, and it's
        // what gets read in a conversation with a real user: it should say the same thing as the code, no more.
        UserLog("save", "migrated %d per-class difficulty entries from the layered format to absolute numbers "
                        "(base %d + offset -> number, races==0 kept as 0/community-seeded); each category's "
                        "effective difficulty carries over as closely as the saved data allows",
                migrated, d.prog.ai_difficulty);
        // After migrating, prog.ai_difficulty kept the old baseline until the
        // first valid race -- meanwhile the Settings screen was already rendering ClassGeneralNow() live
        // (career_overlay.cpp), so the save on disk was out of sync starting from the first save
        // after the migration (and with it, the downstream readers: the player/AI ELO and the aiBase
        // used by telemetry along the !valid path, and the simrunner). Recalculating it here with the same function
        // the Settings screen uses keeps the save consistent right away, without waiting for the player to race.
        std::vector<int> ais;
        for (const auto& kv : d.prog.class_dda)
            if (kv.second.races > 0 && kv.second.ai > 0) ais.push_back(kv.second.ai);
        d.prog.ai_difficulty = career::ClassGeneralFrom(ais, d.prog.ai_difficulty);
    }
    // Legacy/pre-mailnarr saves (with no 'mailnarr' line) end up with season_start_credits=0
    // until the first rollover -> the first season close would report the ENTIRE balance as {net}. Seeds
    // the snapshot in pre-mailnarr saves so the first close doesn't report the entire balance.
    if (!sawMailnarr) d.mailnarr.season_start_credits = d.prog.credits;
    fclose(f);
    UserLog("save", "save loaded v%d (%s)", fileVersion, path);   // audit note, 07/20
    MigrateSave(d, fileVersion);   // forward-compatible seam (v1->v2 migrates 'calibrated'); leaves d at kSaveVersion
    // Seeds/repairs the player's rating after loading. A career created via the profile slots
    // Play gate (profileSlots.ts writes only profile.json, without going through cmd.profile.create, which seeds
    // the rating) used to start from a blank SaveData{} -> rating 0 -> the 1st race did the ELO update from 0 and PERSISTED
    // ~32 (0 + K_PROVISIONAL) — a real case on Jul 19: "32nd in the world ranking, far from the pack". <=0 = not
    // yet seeded (clean seed); 0<rating<900 = corrupted — the model's legitimate floor is around ~930 (10
    // provisional losses from 1250) — it's re-seeded while preserving rating_races.
    if (d.prog.rating <= 0.0) { d.prog.rating = bal::RATING_PLAYER_SEED; d.prog.rating_races = 0; }
    // (Aug 5) The second branch looked only at the value ("<900 = corrupted") and that was wiping out EARNED ratings. The
    // assumption above -- "the legitimate floor is around ~930, 10 provisional losses from 1250" -- holds for
    // the first 10 races and no more: measured with the career simulator, a player who finishes
    // 18th of 27 on average drops to 389 over 75 races, and that isn't corruption, it's their level. With the
    // value-based threshold, every load reset them to 1250: progression didn't exist, and the failure was invisible on top of it because
    // the number always looked healthy.
    //
    // What actually distinguishes corruption from "you're doing badly" isn't the value, it's the value against the races
    // raced. The real case that motivated this had a rating of ~32 after one race: starting from 0 and applying an
    // ELO update. Within the provisional window you can't legitimately drop below 900; past it, you can, a lot.
    else if (d.prog.rating < 900.0 && d.prog.rating_races <= bal::K_PROVISIONAL_RACES)
        d.prog.rating = bal::RATING_PLAYER_SEED;
    // (Jul 21 2026) Re-deriving the salary of active salaried contracts to the variable model
    // (SalaryFor) is now a one-shot legacy step: only for saves with fileVersion < kSaveVersion (v4). It migrates the
    // old flat figure once; then SaveCareer stamps v4 and the heal never runs again -> per_race stays frozen at the
    // value fixed at signing (OfferTermsCtx). Without this gate the heal ran on every load with inputs that DIFFER from
    // the offer (worldYear=kStartYear vs live; current rating vs at signing), and the figure would DRIFT after reloading.
    if (fileVersion < kSaveVersion) {
        for (auto& c : d.contracts) {
            if (!c.active) continue;
            if (ContractCoverageProfile(c.kind, c.archetype).salary_dir <= 0) continue;   // not salaried -> nothing
            const SeriesDef* sd = SeriesById(c.series_id);
            if (!sd) continue;   // series catalog not loaded yet -> don't degrade (don't write a made-up level=1); preserve per_race (the legacy save keeps its own figure instead of an invented one)
            int level = sd->level;
            // fixed reference worldYear (kStartYear): LoadCareer doesn't have the calendar loaded yet (it lives in another file),
            // and the team band is relative to the field -> a stable reference year is enough and makes the migration deterministic.
            c.per_race = SalaryFor(c.series_id, c.team_id, level, d.prog.rating, c.accepted_deal.salaryPct, d, kStartYear);
        }
    }
    return true;
}

// Migration hook. Called by LoadCareer after parsing the file, with the version read (fromVersion;
// 0 = an old save with no version line). v2 introduces a non-additive step: 'calibrated' has a default of false
// (correct for new careers), but an existing v1 save already has progress -> it must end up calibrated=true so
// it isn't gated back into onboarding. MigrateSave detects this and sets the correct value. For future non-additive changes
// (a renamed field / changed semantics), add a step here `if (fromVersion < N) { ... }` before the
// later ones (old->new cascade). Returns true if the save ended up at kSaveVersion.
bool MigrateSave(SaveData& d, int fromVersion) {
    if (fromVersion >= kSaveVersion) return true;   // already up to date (or a save with no old schema, which loads additively)
    UserLog("save", "save migrated v%d -> v%d", fromVersion, kSaveVersion);   // audit note, 07/20

    // future migration steps, cascading ---
    // v1 -> v2: a pre-rating save has prog.rating == 0.0. Seed it from the old REP so an in-progress
    // career keeps its standing instead of resetting to rookie. Map the old REP DISPLAY band (0..5) onto the
    // bottom of the matching rating tier (bal::REP_TIER_RATING), floored at RATING_ROOKIE_BASE. rating_races
    // is left at 0 -> the migrated player re-enters a provisional window (high K), which quickly re-settles
    // the seeded rating against real results. Additive + idempotent (only fires when rating is still 0.0).
    if (fromVersion < 2 && d.prog.rating == 0.0) {
        int band = RepDisplayBand(d.prog.rep);                 // 0..5 from the old engine REP scale
        if (band < 0) band = 0; if (band > 5) band = 5;
        double seed = bal::REP_TIER_RATING[band];
        if (seed < bal::RATING_ROOKIE_BASE) seed = bal::RATING_ROOKIE_BASE;
        d.prog.rating = seed;
    }
    // v2: SaveData.signed_sponsors is purely additive (new optional 'sponsor' lines; the parser ignores
    // unknown keys and a save without sponsors simply has none -> signed_sponsors defaults to empty). It rides
    // on the existing v2 schema, so there is nothing to transform here — kept as a note for the seam.
    // v2: birth_year/retired/current_world_year are additive — a save without them loads them to their
    // clean defaults (empty map/set, 0). There is nothing to transform; the engine re-seeds birth_year from
    // the roster/JSON the first time it ages (RolloverYearReport). It rides on v2 (no dedicated step).
    // v2: memories_done ('memory' lines, write-only-when cleared>0) is additive — a save without
    // memories loads it to an empty map (back-compat), and a legacy v1/version-less save falls through cleanly. No dedicated step.
    // migration steps, cascading ---
    // v1 -> v2: the 'calibrated' field didn't exist. A career WITH progress was already "underway" and must not
    // be re-gated into the calibration race -> mark it calibrated. A fresh career (with no progress) stays
    // calibrated=false (default) and enters onboarding.
    if (fromVersion < 2)
        d.prog.calibrated = (d.prog.xp_total > 0 || !d.completed_races.empty() || d.prog.frontier_phase > 1);
    // v3 (Jul 11 2026): objective-based sponsors are retired — the card system replaces them.
    // The player loses no value: the acquisition cost of each signed sponsor's tier is refunded.
    if (fromVersion < 3 && !d.signed_sponsors.empty()) {
        long refund = 0;
        for (const auto& sp : d.signed_sponsors) {
            int t = sp.tier; if (t < 0) t = 0; if (t > 4) t = 4;
            d.prog.credits += bal::SPONSOR_ACQ_COST[t];
            refund += bal::SPONSOR_ACQ_COST[t];
        }
        EconLog("migrate_sponsor_refund", refund, d.prog.credits);   //
        d.signed_sponsors.clear();
    }
    // v4 -> v5. Weather had two controls for the same decision: the "Inherited" row of
    // (the kInhWeather bit) and wx_mode=1. Worse, the bit won: with it set, choosing Random or Manual selection did
    // nothing, and it also erased the authored weather of a historic round, which the weather feature promises
    // to always respect. The bit is retired and its intent is translated to the control that remains. wx_mode is only overridden
    // if it's at its default (0): whoever already chose a mode explicitly takes precedence over the old bit.
    if (fromVersion < 5 && (d.prog.inherit_mask & kInhWeather)) {
        if (d.prog.wx_mode == 0) d.prog.wx_mode = 1;   // 1 = inherited from your Custom Race
        d.prog.inherit_mask &= ~kInhWeather;
        UserLog("save", "migrated inherited weather -> wx_mode=%d (kInhWeather retired)", d.prog.wx_mode);
    }
    return true;
}

// Decay a year without a fresh duel. An entry whose last_year is older than fromYear
// (the year that's rolling over) loses 1 level; an entry that DID get a duel this year (last_year == fromYear)
// is untouched. Entries that land at level<=0 are erased outright (a decayed-out rivalry leaves no trace, and
// SaveCareer's write-when-level>0 guard would drop them anyway). Pure map mutation — no calendar/prog reads.
void BeefDecayAtRollover(SaveData& d, int fromYear) {
    for (auto it = d.beef.begin(); it != d.beef.end(); ) {
        if (it->second.last_year < fromYear) it->second.level--;
        if (it->second.level <= 0) it = d.beef.erase(it);
        else ++it;
    }
}

// Purge rel_marks entries whose age exceeds 3 (they weigh 0 in RelMarksSum anyway).
// Pure save-state mutation; called from RolloverYearReport with fromYear = cc.year BEFORE cc.year++.
void RelMarksPurge(SaveData& d, int fromYear) {
    d.rel_marks.erase(std::remove_if(d.rel_marks.begin(), d.rel_marks.end(),
        [fromYear](const RelMark& m) { return fromYear - m.year > 3; }), d.rel_marks.end());
}

bool OwnsClassId(const SaveData& d, const std::string& classId) {
    for (const auto& c : d.owned) if (c.class_id == classId) return true;
    return false;
}

std::string ClassPrefix(const std::string& id) {
    auto h = id.find('#');
    return h == std::string::npos ? id : id.substr(0, h);
}

bool OwnsCarOfClass(const SaveData& d, const std::string& className) {
    for (const auto& c : d.owned) if (ClassPrefix(c.class_id) == className) return true;
    return false;
}

std::set<std::string> OwnedClassSet(const SaveData& d) {
    std::set<std::string> s;
    for (const auto& c : d.owned) s.insert(ClassPrefix(c.class_id));   // prefix: matches both a purchase (class#veh) and a gift
    return s;
}

const char* const kStarterMemoryClass = "Falcon_Masters";
const char* const kStarterMemoryCarId = "Falcon_Masters#759059246";

bool GrantStarterMemoryIfDue(SaveData& save) {
    if (save.completed_races.size() < 3) return false;             // not yet the 3rd race
    if (save.claimed_events.count("gift_f167")) return false;      // already claimed (idempotent across reloads)
    if (!OwnsCarOfClass(save, kStarterMemoryClass)) {   // prefix: don't re-gift if the player already has a '67 car
        CarOwnership c; c.class_id = kStarterMemoryCarId; c.vehid = 759059246; c.races = 0; c.wear = 0.0f; c.insured = true; c.livery = 0;   // Highley Racing #1: livery = ordinal (index into CareerVehicleLiveryList), not the NN. NN 50 is the car's lowest => ordinal 0.
        save.owned.push_back(c);
    }
    save.claimed_events.insert("gift_f167");
    return true;
}

bool StarterGiftPending(const SaveData& s) {
    return s.claimed_events.count("gift_f167") && !s.claimed_events.count("gift_f167_seen");
}

void AckStarterGift(SaveData& s) {
    s.claimed_events.insert("gift_f167_seen");
}

// 19/09: see career_save.h. The difference with what was already paid is paid, and what was paid only goes up.
long EventClaimCredits(SaveData& save, const std::string& eventId, long earned) {
    if (eventId.empty() || earned <= 0) return 0;
    long& pagado = save.event_paid[eventId];
    const long paga = earned > pagado ? earned - pagado : 0;
    if (earned > pagado) pagado = earned;
    return paga;
}

// Flat CSV: names from shared memory can contain commas, so name and class
// go in quotes with internal quotes doubled (CSV convention). best_lap <= 0 = no valid lap -> the cell is
// left empty, so the downstream reducer doesn't mistake a -1 for a real time.
std::string FormatRaceGridCsv(const std::vector<RaceGridRow>& grid) {
    auto quoted = [](const std::string& s) {
        std::string q = "\"";
        for (char c : s) { if (c == '"') q += '"'; q += c; }
        q += '"';
        return q;
    };
    std::string out = "pos,name,car_class,laps,best_lap,dnf,is_player\r\n";
    char buf[32];
    for (const RaceGridRow& r : grid) {
        out += std::to_string(r.pos);
        out += ','; out += quoted(r.name);
        out += ','; out += quoted(r.car_class);
        out += ','; out += std::to_string(r.laps);
        out += ',';
        if (r.best_lap > 0.0f) { snprintf(buf, sizeof(buf), "%.3f", r.best_lap); out += buf; }
        out += ','; out += (r.dnf ? '1' : '0');
        out += ','; out += (r.is_player ? '1' : '0');
        out += "\r\n";
    }
    return out;
}

// 0.7.0 — race pace in profile.json (see career_save.h). The file is written by the app (when creating the
// slot in the Play window) and by the engine (cmd.profile.create and, from now on, Settings), so when rewriting it
// EVERYTHING it carries is preserved: the serializer copies each member as is and only changes "difficulty".
static void WriteJsonValue(jsonw::Writer& w, const jsonm::Value& v) {
    switch (v.type) {
    case jsonm::Type::Null:   w.Null(); break;
    case jsonm::Type::Bool:   w.B(v.b); break;
    case jsonm::Type::Number: w.N(v.num); break;
    case jsonm::Type::String: w.S(v.str); break;
    case jsonm::Type::Array:
        w.BeginArray();
        for (const auto& e : v.arr) { if (e) WriteJsonValue(w, *e); else w.Null(); }
        w.EndArray();
        break;
    case jsonm::Type::Object:
        w.BeginObject();
        for (const auto& kv : v.obj) { w.Key(kv.first.c_str()); if (kv.second) WriteJsonValue(w, *kv.second); else w.Null(); }
        w.EndObject();
        break;
    }
}

int ProfileRewardModeRead(const std::string& profileJsonPath) {
    auto pj = jsonm::ParseFile(profileJsonPath.c_str());
    if (!pj || !pj->isObject()) return -1;
    return RewardModeFromToken(pj->memberString("difficulty"));
}

bool ProfileRewardModeSync(const std::string& profileJsonPath, int rewardMode) {
    auto pj = jsonm::ParseFile(profileJsonPath.c_str());
    if (!pj || !pj->isObject()) return true;   // without a readable profile.json nobody seeds over the save
    const std::string want = RewardModeToken(rewardMode);
    if (pj->memberString("difficulty") == want) return true;
    auto tok = std::make_shared<jsonm::Value>();
    tok->type = jsonm::Type::String; tok->str = want;
    pj->obj["difficulty"] = tok;
    jsonw::Writer w; WriteJsonValue(w, *pj);
    const std::string tmp = profileJsonPath + ".tmp";
    FILE* f = paths::FOpen(tmp, "wb");
    if (!f) { UserLog("save", "profile.json NOT updated: cannot write %s (errno=%d)", tmp.c_str(), errno); return false; }
    const std::string body = w.Str() + "\n";
    const bool wrote = fwrite(body.data(), 1, body.size(), f) == body.size();
    const bool closed = fclose(f) == 0;
    if (!wrote || !closed || !paths::ReplaceFileAtomic(tmp, profileJsonPath)) {
        paths::RemoveFile(tmp);
        UserLog("save", "profile.json NOT updated: replace failed for %s", profileJsonPath.c_str());
        return false;
    }
    return true;
}

} // namespace career
