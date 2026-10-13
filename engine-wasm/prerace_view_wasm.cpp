// prerace_view_wasm.cpp -- the browser host's provider for query.prerace.
//
// PROVISIONAL -- SECOND IMPLEMENTATION OF THE PRE-RACE VIEW.
//
// The authoritative assembly is career_overlay.cpp's SetPreraceViewProvider lambda (it lives in the
// career_api_wiring.inc the overlay includes, with the PreraceUiCompute wrapper it calls), which this
// repository does not publish. What follows is a copy written so the browser build can answer
// query.prerace at all, and copies drift: grep PROVISIONAL to find every one of them. The static_asserts
// below make a change to the view structures themselves loud.
//
// The pre-race screen describes the PENDING RACE: the race the player has just armed from the calendar
// (cmd.calendar.launch) or from an event (cmd.event.arm). The overlay keeps it in the UI's own state
// (SampleState().pending), and PreraceUiCompute's first step is to look at it: with nothing armed it
// returns loaded=true, clsEmpty=true and nothing else, which the app shows as "pick a race".
//
// This host cannot hold a pending race. Arming one is the overlay's executor work (the commands answer
// not_ready here, since no executor is installed), and building the screen for an armed race needs the
// pending race itself, which lives in the UI's own state and is put there only by the overlay's arming
// path. The catalog that screen reads (CarsByClass, FindClass, career_data.cpp) is published now, but
// PreraceUiCompute, which assembles the screen from it, is defined in a UI source this repository does
// not publish, and with nothing armed it would answer the same thing. So the answer is always the
// overlay's own answer for an unarmed race. Of the fields the overlay fills outside PreraceUiCompute,
// the wear table and the two wear toggles come from published functions (RaceLapEquivalent,
// ApplyDurationPercent, WearMultTyre / WearMultFuel, prog.scale_*) and are filled the same way; the
// armed event's paid amount is 0 because no event is armed. The serializer (career_api_queries.cpp's
// QueryPrerace) emits only clsEmpty in this state and is not touched or repeated here.
#include "career_api_queries.h"
#include "career_econ.h"
#include "career_save.h"
#include "career_series.h"
#include <utility>

namespace {

// The guard that makes drift loud, for the same reason wasm_main.cpp guards HubView: PreraceView is a
// plain serialization view, so a field added upstream would ship default-initialized through this demo
// without anything here having to mention it. Size, because C++17 cannot count members; exact for the
// one toolchain this file is built with (wasm32, libc++). When it fires: read the new PreraceView (or
// PreraceCar), fill the new field in MakePreraceView below, THEN update the number. Do not just update
// the number.
static_assert(sizeof(capi::PreraceView) == 384,
              "capi::PreraceView changed upstream. This is a SECOND, provisional copy of the overlay's "
              "pre-race assembly: fill the new/changed field in MakePreraceView below, THEN update this size.");
static_assert(sizeof(capi::PreraceCar) == 84,
              "capi::PreraceCar changed upstream. This is a SECOND, provisional copy of the overlay's "
              "pre-race assembly: fill the new/changed field in MakePreraceView below, THEN update this size.");

capi::CareerStateProvider g_state;

capi::PreraceView MakePreraceView(const std::string& /*carId*/) {
    capi::PreraceView v;
    if (!g_state) return v;
    const capi::CareerStateView st = g_state();
    if (!st.loaded || !st.save) return v;   // loaded=false -> the serializer says not_ready
    const career::SaveData& save = *st.save;

    // PreraceUiCompute with no pending race: loaded, and "pick a race".
    v.loaded = true;
    v.clsEmpty = true;
    v.eventPaid = 0;   // no event armed

    // The wear table, as the overlay builds it around PreraceUiCompute: one entry per duration %, against
    // the full distance of the race. With no race armed the launch distance is the default (0 laps, 0 min).
    v.wearScaleTyre = save.prog.scale_tyre_wear;
    v.wearScaleFuel = save.prog.scale_fuel_usage;
    const int fullEff = career::RaceLapEquivalent(v.launchLaps, v.launchDurationMin);
    for (const int* p = career::kDurationPcts; *p; ++p) {
        int lp = v.launchLaps, dm = v.launchDurationMin;
        career::ApplyDurationPercent(*p, dm, lp);
        const int eff = career::RaceLapEquivalent(lp, dm);
        capi::PreraceView::WearOpt o;
        o.pct = *p;
        o.tyre = career::WearMultTyre(eff, fullEff);
        o.fuel = career::WearMultFuel(eff, fullEff);
        v.wearByPct.push_back(o);
    }
    return v;
}

}  // namespace

namespace wasmhost {

void InstallPreraceView(capi::CareerStateProvider state) {
    g_state = std::move(state);
    capi::SetPreraceViewProvider(&MakePreraceView);
}

}  // namespace wasmhost
