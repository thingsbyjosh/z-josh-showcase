// garage_view_wasm.cpp -- the browser host's provider for query.garage.
//
// PROVISIONAL -- SECOND IMPLEMENTATION OF THE GARAGE VIEW.
//
// The authoritative assembly is career_overlay.cpp's SetGarageViewProvider lambda (it lives in the
// career_api_wiring.inc the overlay includes, with the garage bridge functions it calls), which this
// repository does not publish. What follows is a copy written so the browser build can answer
// query.garage at all, and copies drift: grep PROVISIONAL to find every one of them. The static_asserts
// below make a change to the view structures themselves loud.
//
// Nothing here is new logic. The contract strip is the overlay's CalUiContractFacets (declared in
// career_season_ui.h, defined only in the private tree), rebuilt from what it is made of:
// ContractCoverageProfile, DeriveOfferPresentation and SeriesById over the save's first active contract.
// The tier is RepDisplayBand + RatingTierColor, as the overlay reads it. The serializer is the engine's
// own QueryGarage (career_api_queries.cpp) and is not touched or repeated here.
//
// The owned cars are the overlay's own resolution, GarageOwnedCars (career_data.cpp), over the save's
// owned list as owned_wasm.cpp reads it -- see OwnedCars below.
#include "career_api_queries.h"
#include "career_econ.h"
#include "career_engine.h"
#include "career_negotiate.h"
#include "career_save.h"
#include "career_season_ui.h"   // GarageOwnedCars
#include "career_series.h"
#include <string>
#include <utility>
#include <vector>

namespace {

// The guard that makes drift loud, for the same reason wasm_main.cpp guards HubView: GarageView is a
// plain serialization view, so a field added upstream would ship default-initialized through this demo
// without anything here having to mention it. Size, because C++17 cannot count members; exact for the
// one toolchain this file is built with (wasm32, libc++). When it fires: read the new GarageView (or
// GarageCarView), fill the new field in MakeGarageView below, THEN update the number. Do not just update
// the number.
static_assert(sizeof(capi::GarageView) == 100,
              "capi::GarageView changed upstream. This is a SECOND, provisional copy of the overlay's "
              "garage assembly: fill the new/changed field in MakeGarageView below, THEN update this size.");
static_assert(sizeof(capi::GarageCarView) == 112,
              "capi::GarageCarView changed upstream. This is a SECOND, provisional copy of the overlay's "
              "garage assembly: fill the new/changed field in MakeGarageView below, THEN update this size.");

capi::CareerStateProvider g_state;

// CalUiContractFacets: the strip describes the FIRST active contract (v1 surfacing), and each facet is
// derived from the same pure profiles the settlement reads, so what the strip promises is what is paid.
capi::GarageContractView ContractFacets(const career::SaveData& save) {
    capi::GarageContractView f;
    for (const auto& c : save.contracts) {
        if (!c.active) continue;
        f.active = true;
        f.seasonsLeft = c.seasons_left; f.seasonsTotal = c.seasons_total;
        f.perRace = c.per_race; f.kind = c.kind;
        f.roleLead = c.accepted_deal.demands[career::NEGO_ROLE_LEAD];
        f.objDone = c.obj_done; f.objTotal = c.obj_total;
        f.objTargetPos = c.obj_target_pos;
        // an early renewal on the table for THIS contract (it waits in the inbox)
        for (const auto& pr : save.pending_renewals)
            if (pr.clsId == c.series_id) { f.renewOnTable = true; break; }
        f.fixesCar = career::ContractCoverageProfile(c.kind, c.archetype).fixes_car;
        f.teamCar  = c.team_car;
        const career::OfferPresentationFacets pf = career::DeriveOfferPresentation(c.kind, c.archetype, &c.accepted_deal);
        f.typeLabel = pf.typeLabel;
        f.covSeatEntry = pf.seatEntryCovered; f.covConsumables = pf.consumablesCovered;
        f.covDamage = pf.damageCovered;       f.covTravel = pf.travelCovered;
        const career::SeriesDef* d = career::SeriesById(c.series_id);
        f.seriesDisp = d ? d->display : c.series_id;
        break;
    }
    return f;
}

// The owned cars, copied as the overlay copies them: each car GarageOwnedCars resolves, with the km of
// that unit (car + livery) off the save, what selling it would pay (CarResaleValue) and, if it cannot be
// sold, why (CarSaleCheck). The overlay passes the car of a launched race not yet settled as the pending
// car; this host cannot launch a race, so there is none.
//
// A new career owns no car and the engine grants none at start, so in the demo as shipped this list is
// empty. test_dispatch.mjs holds it against the hub's owned count, so a car the save holds and this
// resolution drops fails loudly instead of hiding.
std::vector<capi::GarageCarView> OwnedCars(const career::SaveData& save) {
    std::vector<capi::GarageCarView> out;
    for (const career::GarageOwnedCarView& c : career::GarageOwnedCars()) {
        capi::GarageCarView cv;
        cv.id = c.id; cv.name = c.name; cv.classId = c.classId; cv.className = c.className; cv.classShort = c.classShort;
        cv.classColor = (ImU32)c.classColor; cv.year = c.year; cv.pwr = c.pwr; cv.grp = c.grp; cv.bal = c.bal;
        cv.vehHash = c.vehHash; cv.livery = c.livery;
        for (const auto& o : save.owned)
            if (o.class_id == c.id && (o.livery < 0 ? 0 : o.livery) == (c.livery < 0 ? 0 : c.livery)) { cv.km = o.km; break; }
        cv.sellPrice = career::CarResaleValue(c.price, cv.km);
        cv.sellBlock = career::CarSaleBlockToken(career::CarSaleCheck(save, c.id, c.livery, c.price, std::string()));
        out.push_back(std::move(cv));
    }
    return out;
}

capi::GarageView MakeGarageView() {
    capi::GarageView v;
    if (!g_state) return v;
    const capi::CareerStateView st = g_state();
    if (!st.loaded || !st.save) return v;   // loaded=false -> the serializer says not_ready
    const career::SaveData& save = *st.save;
    v.loaded = true;
    v.credits = save.prog.credits;
    const int rep = save.prog.rep;
    v.tierBand = career::RepDisplayBand(rep);
    v.tierColor = (ImU32)career::RatingTierColor(rep);
    v.contract = ContractFacets(save);
    v.cars = OwnedCars(save);
    return v;
}

}  // namespace

namespace wasmhost {

void InstallGarageView(capi::CareerStateProvider state) {
    g_state = std::move(state);
    capi::SetGarageViewProvider(&MakeGarageView);
}

}  // namespace wasmhost
