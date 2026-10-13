// career_contracts.cpp — the world's contracts view ().
#include "career_contracts.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "career_series.h"
#include "career_calendar.h"   // kStartYear: the "since forever" of a seat with no saved start year
#include "career_fields.h"     // FamilyForClass / CarFamily
#include "career_seats.h"      // SeatsForClass / SeatKey / ContractExpiryYear / SeatContractStartYear
#include "career_registry.h"   // the driver's affinity comes from the identity catalog ()
#include "career_balance.h"

namespace career {

namespace {

// CarFamily (career_traits.h) and canon::Family () enumerate THE SAME thing in the SAME order, and that
// coincidence is a tacit contract between two files that do not know each other. It is crossed here, in a single place,
// and the harness checks it: the day someone adds a family to one of the two, a test fails and not a
// grid. will unify the two enums; until then, this.
bool SameFamily(canon::Family a, CarFamily b) { return (int)a == (int)b; }

// The driver's affinity, or "unknown". Without a loaded registry compatibility cannot be judged, and in that
// case the right answer is "yes" —do not block for lack of data— not "no".
bool DriverFamily(const std::string& driverId, canon::Family& out) {
    const canon::DriverIdentity* di = registry::ById(driverId);
    if (!di || !di->affinity_known) return false;
    out = di->primary_affinity;
    return true;
}

}   // namespace

std::vector<SeatContract> WorldContracts(const SaveData& save) {
    std::vector<SeatContract> out;
    // A seat_id can only have ONE contract (SeatDefinition 1 -- 0..1 active DriverContract). Today
    // there are 9 seats that share a slot with another, and without this guard two contracts would come out with the same
    // physical address —possibly of two drivers— and the main/secondary split would over-count. The
    // seat census already keeps the first; both have to count the same or they are no good for comparing.
    std::set<std::string> vistos;
    for (const auto& sd : AllSeries()) {
        std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (int i = 0; i < (int)seats.size(); i++) {
            const Seat& s = seats[i];
            if (s.open || s.occupant_id.empty() || s.occupant_id == std::string(kSeatOpen)) continue;
            if (!vistos.insert(SeatKey(sd.id, i)).second) continue;
            SeatContract c;
            c.driver_id  = s.occupant_id;
            c.seat_id    = SeatKey(sd.id, i);
            c.series_id  = sd.id;
            c.class_name = sd.class_name;
            c.team_label = s.team_label;
            c.tier       = sd.level;
            c.seat_idx   = i;
            c.start_year = SeatContractStartYear(save, c.seat_id);
            c.expiry_year = ContractExpiryYear(c.seat_id, c.driver_id, c.start_year);
            out.push_back(c);
        }
    }
    std::sort(out.begin(), out.end(), [](const SeatContract& a, const SeatContract& b) {
        if (a.driver_id != b.driver_id) return a.driver_id < b.driver_id;
        return a.seat_id < b.seat_id;
    });

    // The main one: whatever the save says, and if it does not say, the derivation of over what is in front.
    for (size_t i = 0; i < out.size(); ) {
        size_t j = i;
        while (j < out.size() && out[j].driver_id == out[i].driver_id) j++;
        auto fijado = save.primary_seat_by_driver.find(out[i].driver_id);
        size_t elegido = out.size();
        if (fijado != save.primary_seat_by_driver.end())
            for (size_t k = i; k < j; k++) if (out[k].seat_id == fijado->second) { elegido = k; break; }
        if (elegido == out.size()) {
            canon::Family fam = canon::Family::OpenWheel;
            const bool sabeFam = DriverFamily(out[i].driver_id, fam);
            for (size_t k = i; k < j; k++) {
                if (elegido == out.size()) { elegido = k; continue; }
                const SeatContract& a = out[elegido];
                const SeatContract& b = out[k];
                // (1) compatible with its affinity beats incompatible; (2) higher tier; (3) older;
                // (4) lower seat_id. The order matters: it asks for the highest category AMONG THEIRS, not the
                // highest period — a GT driver does not "move up" by being put in a single-seater.
                const bool aFit = !sabeFam || SameFamily(fam, FamilyForClass(a.class_name));
                const bool bFit = !sabeFam || SameFamily(fam, FamilyForClass(b.class_name));
                if (aFit != bFit)               { if (bFit) elegido = k; continue; }
                if (a.tier != b.tier)           { if (b.tier > a.tier) elegido = k; continue; }
                const int aStart = a.start_year ? a.start_year : kStartYear;
                const int bStart = b.start_year ? b.start_year : kStartYear;
                if (aStart != bStart)           { if (bStart < aStart) elegido = k; continue; }
                if (b.seat_id < a.seat_id)      elegido = k;
            }
        }
        if (elegido < out.size()) out[elegido].primary = true;
        i = j;
    }
    return out;
}

std::vector<SeatContract> ContractsForDriver(const SaveData& save, const std::string& driverId) {
    std::vector<SeatContract> out;
    if (driverId.empty()) return out;
    for (const auto& c : WorldContracts(save)) if (c.driver_id == driverId) out.push_back(c);
    return out;
}

std::string PrimarySeatFor(const SaveData& save, const std::string& driverId) {
    auto it = save.primary_seat_by_driver.find(driverId);
    if (it != save.primary_seat_by_driver.end()) return it->second;
    for (const auto& c : ContractsForDriver(save, driverId)) if (c.primary) return c.seat_id;
    return std::string();
}

int DerivePrimarySeats(SaveData& save) {
    const std::vector<SeatContract> todos = WorldContracts(save);
    std::map<std::string, int> cuantos;
    for (const auto& c : todos) cuantos[c.driver_id]++;
    int escritos = 0;
    for (const auto& c : todos) {
        if (!c.primary) continue;
        if (cuantos[c.driver_id] < 2) continue;                       // with only one, deriving it is trivial
        if (save.primary_seat_by_driver.count(c.driver_id)) continue; // already fixed: the save rules
        save.primary_seat_by_driver[c.driver_id] = c.seat_id;
        escritos++;
    }
    return escritos;
}

bool SecondaryAllowed(const SeatContract& primary, const SeatContract& secondary) {
    if (primary.driver_id != secondary.driver_id) return false;
    if (primary.seat_id == secondary.seat_id)     return false;
    // "a driver never appears twice in the same event". The same series is the trivial case.
    if (primary.series_id == secondary.series_id) return false;
    // at most one tier step, except an authored exception (which does not exist today).
    if (std::abs(primary.tier - secondary.tier) > 1) return false;
    // same family, or an explicitly allowed crossover. Today there is no crossover table, so
    // the rule is the strict one — and let the census show how many of today's secondaries would not meet it.
    return FamilyForClass(primary.class_name) == FamilyForClass(secondary.class_name);
}

ContractCensus TakeContractCensus(const SaveData& save) {
    ContractCensus c;
    const std::vector<SeatContract> todos = WorldContracts(save);
    c.contracts = (int)todos.size();
    for (size_t i = 0; i < todos.size(); ) {
        size_t j = i;
        while (j < todos.size() && todos[j].driver_id == todos[i].driver_id) j++;
        const int n = (int)(j - i);
        c.drivers++;
        if (n > 1) c.multi_seat++;
        if (n > c.max_seats) { c.max_seats = n; c.max_seats_driver = todos[i].driver_id; }
        if (save.primary_seat_by_driver.count(todos[i].driver_id)) c.primaries_persisted++;
        const SeatContract* prim = nullptr;
        for (size_t k = i; k < j; k++) if (todos[k].primary) { prim = &todos[k]; break; }
        for (size_t k = i; k < j; k++) {
            if (todos[k].primary) continue;
            c.secondaries++;
            if (prim && !SecondaryAllowed(*prim, todos[k])) c.secondaries_illegal++;
        }
        i = j;
    }
    return c;
}

}   // namespace career
