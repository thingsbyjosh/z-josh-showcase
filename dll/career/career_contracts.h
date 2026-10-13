#pragma once
// career_contracts — the world told in CONTRACTS.
//
// WHAT IT IS. A derived VIEW of seat occupancy, not a new record. is explicit: a
// contract "is not necessarily a duplicated record", and duplicating it would mean being able to desynchronize it from the
// seat — the same trap that expiry already avoids, which is also derived.
//
// WHY IT IS NEEDED. Today the world is read as a map `seat key -> driver id`, and with that you cannot
// formulate even the most basic question: **what is this driver's MAIN seat?**
// Measured over year 1: **192 AI drivers occupy two or more seats** and none has a main one, so
// "their category", "their team" and "which series the market can kick them out of" have no answer. The career
// arc of and the cascade of hang off that answer.
//
// WHAT IT DOES **NOT** DO. It does not rewrite the market. Building the series from contracts and
// The composition FROM SEATS is the ****, and it is where this is really consumed; here the vocabulary is delivered
// and the rule of who rules. Nor does it validate secondaries by calendar (it asks for compatibility of
// dates: that needs the calendar of both series and arrives with E).
//
// Pure: reads the save and the loaded tables, does not write except where stated. Deterministic.
#include <string>
#include <vector>
#include "career_save.h"

namespace career {

// — an ACTIVE contract, derived. `expiry_year` comes from (seat, occupant, start year) just as
// in the rest of the engine: it is never stored twice.
struct SeatContract {
    std::string driver_id;
    std::string seat_id;        // the SLOT key of the
    std::string series_id;
    std::string class_name;
    std::string team_label;
    int tier = 0;
    int seat_idx = -1;          // index within the class (diag/compat; the identity is seat_id)
    int start_year = 0;
    int expiry_year = 0;
    bool primary = false;
};

// All the active contracts of the world, sorted by (driver_id, seat_id). It walks AllSeries() and resolves
// the occupancy of each class, so it costs what a sweep of the world costs: do not call it in a loop.
std::vector<SeatContract> WorldContracts(const SaveData& save);

// The contracts of ONE driver. Also a world sweep; for several drivers, use WorldContracts once.
std::vector<SeatContract> ContractsForDriver(const SaveData& save, const std::string& driverId);

// — the MAIN seat. `save.primary_seat_by_driver` rules if it has it; if not, it is derived with
// "" if the driver occupies no seat.
std::string PrimarySeatFor(const SaveData& save, const std::string& driverId);

// — derives and PERSISTS the main seat of everyone who has two or more seats and does not have it fixed.
// The criterion, in this order: (1) highest tier compatible with their affinity, (2) oldest contract,
// (3) lower `seat_id`, which is what makes it deterministic. A driver with a single seat is NOT written:
// deriving it is trivial and storing it would be state that can go stale.
// It frees nothing. point 5: incoherent secondaries are released at the next rollover, not on
// load — opening an old save cannot suddenly change all its grids.
int DerivePrimarySeats(SaveData& save);

// — can this contract be secondary alongside that main one? Same family (or allowed crossover)
// and at most one tier step. Does NOT check the calendar yet ().
bool SecondaryAllowed(const SeatContract& primary, const SeatContract& secondary);

// Diagnostic of the main/secondary split over a save.
struct ContractCensus {
    int drivers = 0;            // drivers with at least one contract
    int contracts = 0;
    int multi_seat = 0;         // with two or more
    int primaries_persisted = 0;// with a main one written in the save
    int secondaries = 0;
    int secondaries_illegal = 0;// secondaries that TODAY would not meet it
    int max_seats = 0;
    std::string max_seats_driver;
};
ContractCensus TakeContractCensus(const SaveData& save);

}   // namespace career
