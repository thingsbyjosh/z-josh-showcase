#pragma once
// Public API of the career shell -- WITHOUT dependencies on the internal structs (career_state.h),
// so career_overlay.cpp can include it without type collisions (e.g. its own struct Track).
struct ImGuiIO;
void CareerInit(ImGuiIO& io);     // loads fonts + style (once, right after ImGui_ImplDX11_Init)
void CareerDraw();                // draws the shell full-screen (uses SampleState())
void DrawLoadingCover(int tipIndex);   // item 7: OPAQUE loading cover (logo + rotating tip); the overlay calls it with g_show=0
// the cover's own "what we're setting up" summary. CareerApplyEvent fills it on every handoff
// (Reset + Add per line, text already formatted/translated); DrawLoadingCover paints it under the headline.
// value shows weather as TEXT (wxname_*), never the raw number. Internal line cap; extras are ignored.
void CoverCfgReset();
void CoverCfgAdd(const char* label, const char* value);
// VISUAL notice after loading in Custom Race ("don't touch anything, hit Start"); does NOT block input.
// shmOff (0.5.4) = AMS2's Shared Memory is turned off -> the pill stops saying "all set" and instead says
// the race won't be recorded, with the exact setting to fix it. Takes priority over the verdict-based variant below.
void DrawStartHint(bool shmOff);
// the verdict from the post-apply verification (automatic cfgverify). nFailed=0 -> the notice above
// keeps saying "all set" as usual; >0 -> says WHICH groups didn't land (groups = labels already translated,
// separated by " · ") instead of just asserting the race is ready. The overlay calls it when it closes the apply step.
void StartHintWarn(int nFailed, const char* groups);
// VR 0.6.0: the SAME verdict, readable from outside. In VR the pill above is NOT visible (it draws on the
// desktop swapchain, not on the layers the headset composites), so query.game_state publishes it and the
// app reproduces it on its "race configured" screen -- the warning must not be lost just for wearing a headset.
// Returns nFailed; if groups != nullptr, it stores the pointer to the already-translated groups there (never null).
int StartHintFailInfo(const char** groups);
void  CareerSetFontScale(float s); // shell font scale (live adjustment, e.g. PgUp/PgDn)
float CareerGetFontScale();
void  CareerUiSetScreen(int screenIdx);   // sets the shell's screen by the Screen enum index (for autonomous captures)
void  CareerUiSetupVehLock(int mode);     // demo/capture: vehicle screen with a synthetic PreraceLock (0=free, 1=car+skin contract, 2=fully measured)
void  CareerUiShowTimesRace(int raceIdx); // capture: TIMES tab with a race open (raceIdx>=0 -> grid; -1 -> list)
// Race config for the event to apply in Custom Race (POD, WITHOUT deps on career_state.h).
// Applied on (re-)entering Custom Race (hkSetCarFn -> re-asserted repeatedly for stability over the canonical VMs).
// "Don't force" sentinel: 0 for the integers >0 (laps/opponents/skill/aggression); <0 for hour/weather/mistakes.
struct CareerEventParams {
    int vehHash, trackHash;   // 0 = don't force
    int laps, opponents;      // 0 = don't force
    int skill;                // 0 = don't force (70..120)            -> VM 498 s21
    int aggression;           // 0 = don't force (40/60/80/100)       -> VM 498 s22
    // 19/09: <0 = don't force (bal::kMistNotForced). 0..6 = the MEASURED enum (0 off, 1 x0.5, 2 standard, 3 x2,
    // 4 x3, 5 x4, 6 x5) and the 0 IS WRITTEN: with 0 as the sentinel the heroic door couldn't turn the mistakes off
    // (seen while reviewing the mistakes report in events). The -1 lives in the field itself so that every `CareerEventParams{}` that doesn't set it still stays
    // unforced, just like when the sentinel was the 0 of value initialization.
    int mistakes = -1;        //                                    > VM 498 s28
    int startHour;            // <0 = don't force (0..23)             -> VM 550 s45 (recompute, not reliable yet)
    int weather;              // <0 = don't force (0 clear, 5-8 rain/storm, 12-16 fog, 17 random; catalog in career_series.h); LEGACY single value (debug command 'event'): replicated across all 4 states
    int durationMin;          // >0 = TIME mode with N minutes (s7=0,s8=N,s23/24/25 flags); 0 = LAPS mode
    int startType;            // start type: 0=grid (default), 1=rolling -> VM550 s12 (reverse-engineered Jun 10)
    int phase;                // phase (ladder level) of the class raced; <1 = unknown (default 1) -> settlement
    int oppClass = 0;         // OBSOLETE (no writers left): slot 31 is set by CareerApplyEvent from numClasses (1=same class, 2=multiclass)
    int numClasses = 0;       // 0 = don't force a set; >0 = inject this SET of classes via memory (mode: multiclass if >1, single class if ==1)
    unsigned classes[6] = {0,0,0,0,0,0};   // class hashes (JSON value) for the weekend; classes[0] = typically the player's own. See class_selection_solved.
    // B1: weather is ALWAYS 4 states (career::WeatherSlotsForEvent) -> VM550 s26=4 + s27/s30/s33/s36.
    // Sentinel weather4[0]<0 = "not set": falls back to the legacy 'weather' field above (used by the debug command 'event').
    int weather4[4] = {-1,-1,-1,-1};
    // B4: duration selector. AT THE END of the struct: screen_calendar.cpp's positional init doesn't reach it
    // (stays 0 = don't force), same as the debug 'lemans'/'event' commands and the F3 launches (behavior unchanged).
    int timeProg = 0;         // 0=don't force; 1..11 = s27 index (1=real,2=x2,...,11=x60) -> VM550 s21 (RE 10/06)
    int livery = -1;          // R5: the player's livery id (NN); -1 = don't force (the game's default). AT THE END (safe for positional init).
    // B6 (config-surface, reverse-engineered Jun 11 from the constructor): controls the REST of the Custom Race options
    // confirmed as VM slots. Sentinel <0 = don't force (the game's default). AT THE END of the struct
    // (the positional inits from screen_calendar / 'event' / 'lemans' don't reach them -> they stay -1, inert).
    // Rules/pit/date -> the RACE VM 550 (EvAdd vm=1). Opponent-type/wet -> VM 498 (EvAdd vm=0).
    int mandatoryPit = -1;    // VM550 s14 MandatoryPitStop: mandatory pit stop (0/1)
    int pitMinTyres  = -1;    // VM550 s15 MandatoryPitStopMinTyres: minimum tyres changed in the pit stop (integer)
    int pitMinFuel   = -1;    // VM550 s16 MandatoryPitStopMinFuel: minimum fuel added in the pit stop (integer)
    int refuelling   = -1;    // VM550 s18 PitStopsAllowRefuelling: refuelling allowed in the pit (0/1)
    int formationLap = -1;    // VM550 s19 FormationLap: formation lap (0/1)
    int fcyScheduled = -1;    // VM550 s23 ScheduledFullCourseYellow: scheduled Full Course Yellow (enum 0..5; 0=off). -1 is NO LONGER "don't force" -- CareerApplyEvent writes 0 (off) unless an explicit override >=0 is given
    int dateType     = -1;    // VM550 s42 DateType: 0=predicted,1=current,2=custom,3=?
    int privateQuali = -1;    // VM550 s13 PrivateSession: private session (0/1)
    int oppNumType   = -1;    // VM498 s16 NumOpponentsType: 0=max available,1=custom,2=manual grid
    int aiWetSkill   = -1;    // VM498 s26 OpponentWetSkill: AI skill in the wet (0..200)
    // FIX R5: the event's real date, from the calendar (AT THE END of the struct: the positional init of
    // screen_calendar doesn't reach them -> they stay -1, inert, in other launches). Written to VM550 s47/48/49.
    int dateDay      = -1;    // VM550 s47 day of the month (1..31); <0 = don't force
    int dateMonth    = -1;    // VM550 s48 month (1..12); <0 = don't force
    int dateYear     = -1;    // VM550 s49 year (>1900); <0 = don't force
    // R7 PIT LIMIT (km/h): the global store's "PitSpeedLimit" node (NOT a VM slot); the hook writes it by
    // substitution at the commit. <0 = don't force. AT THE END (screen_calendar's positional init doesn't reach it).
    int pitSpeedLimit = -1;
    // LiveTrack (dynamic track state / rubbering-in): VM550 s45 LiveTrackPresetIdx (was s39 before an AMS2 update shifted memory layout by +6).
    // 1 = "Progressive" (rubber builds up over the session).
    // Jul 22: DEFAULT SET TO -1 (don't force) -- suspected of causing "missing track/environment textures DURING THE RACE":
    //   · LiveTrack IS the track SURFACE state system (rubber/marbles/dust/dry line/puddles), exactly
    //     what was disappearing; and it only affects the race because the "canonical" VM that receives the EvAdd(1,...) calls IS the
    //     RACE session's VM (log [sesscont]: P/Q/R -> R matches VM550), so practice and qualifying
    //     never receive it.
    //   · The default of 1 used to apply to EVERY launch (calendar AND calibration): this field goes AT THE END
    //     of the struct, and screen_calendar/'event''s positional init doesn't reach it, so nothing was ever lowering it.
    //   · The slot's identity is NOT verified after the memory-layout shift: the on-screen check ("LiveTrack
    //     preset: Progressive") was done Jun 16 on s39, BEFORE the Jun 24 shift; s45 is inferred from that +6.
    //     Unlike start hour (the cset 51 debug command) or refuelling (cset 24), it was never confirmed live.
    // Re-enabling it requires VERIFYING s45 in the modal first (the cset 45 1 debug command plus a screenshot). <0 = don't force.
    int liveTrackPreset = -1;
    // CONTRACTS-02 (D-TRIAL-01/02): TRIAL handoff. AT THE END of the struct -> the positional inits of
    // screen_calendar / 'event' / 'lemans' do NOT reach them, so they stay 0/-1 (INERT) in every normal race:
    // only the TRYOUT launch (screen_tryout) sets them. When trialFlag==1, CareerSettleFromShm computes
    // pass/fail = (final position <= trialTopN) -> obj_done/obj_total of the active contract (-> obj_ratio). There's NO
    // bespoke event: it reuses the real-race handoff (CareerApplyEvent + reading $pcars2$).
    int trialFlag    = 0;     // 0 = normal race; 1 = this race is a TRYOUT (computes pass/fail at settlement)
    int trialTopN    = 0;     // target position (absolute Top-N, already resolved from bal::TRIAL_TOPN * grid size); <=0 = not set
    int trialRivalId = -1;    // id of the rival candidate to place on the grid (reserved; -1 = none)
    // CONTRACTS-02 (D-TRIAL): EXPLICIT opponent type. -1 = auto (CareerApplyEvent decides from numClasses:
    // single-class -> "Same class"(1); multi -> "Multiclass"(2)). >=0 = force the raw OpponentsType INDEX
    // (VM498 s31): 0=Identical / 1=Same class / 2=Multiclass. The TRYOUT uses 0 -> a grid of cars IDENTICAL to
    // the player's. AT THE END of the struct -> screen_calendar/'event'/'lemans''s positional init does NOT reach it
    // (stays -1, inert: every normal race still decides by numClasses).
    int oppType = -1;
    // Task 1: per-round session-duration overrides. 0 = use engine fixed default (30 min practice / 15 min quali).
    // AT THE END of the struct -> screen_calendar/'event'/'lemans''s positional init does NOT reach it (stays 0, inert).
    int practiceMin = 0;   // 0 = engine fixed 30 min; >0 = override (capped at 240)
    int qualifyMin  = 0;   // 0 = engine fixed 15 min; >0 = override (capped at 240)
    // NOTE: s19 FormationLap (formation lap) is NEVER managed here -- it's inherited from the user's config
    // (a hard rule since Jun 24). Also, FormationLap=1 breaks the grid spawn. Don't reintroduce a setter for s19.
    // START-TYPE LOCK (s12): -1 = don't lock (every normal race: the start type is set only once, by EvAdd(1,12)).
    // 0/1 = LOCK s12 to this value, RE-ASSERTING it every frame in VM550 until the START commits. Needed
    // when the class's NATIVE default in the game differs from our start type (e.g. Onyx Challenge = rolling start by default):
    // the navigation/CR-init flow reimposes the class default after our write, and without re-asserting it wins at the commit. It's a
    // single scalar (it does NOT rebuild the grid -> no risk of an empty grid). AT THE END of the struct (the positional init
    // doesn't reach it -> -1, inert). Set by calibration (and, in future, contracts/factory seats with a fixed start type).
    int lockStart = -1;
    // MANUAL multi-state weather (events): 0 = AUTO number of states (scaled by duration); 1..4 = FORCE
    // that many states in EVERY session (the author set N explicit slots in weather4 -> all of them are honored,
    // not the random duration-based band). AT THE END of the struct (screen_calendar/'event'/'lemans''s positional init
    // doesn't reach it -> stays 0 = auto in every normal/calendar race, behavior unchanged).
    int weatherSlots = 0;
    // weekend SESSION toggles (reverse-engineered live on Jul 18): the UI's practice/quali on/off lives
    // in the session VM -- s3=ActiveTick (1 on / 0 off) plus its mirror pair s4 (1/-1) and s5 (-1/1). -1 = don't force
    // (the user's config); 0 = session OFF; 1 = session ON. AT THE END of the struct (the positional init doesn't reach them,
    // so they stay -1, inert, in every launch that doesn't set them). See docs/2026-07-18-session-toggle-re-runbook.md.
    int practiceOn = -1;
    int qualifyOn  = -1;
    // MANUAL GRID (multiclass): car count PER CLASS, parallel to classes[] (same order). 0 in a slot
    // = AUTO for that class. If ANY slot is >0, the race enters MANUAL GRID mode: the builder honors
    // these EXACT counts instead of letting the game's own builder split the total evenly across the classes, and NumOpponents becomes
    // (sum of the counts minus 1, since the player counts as one car of their class). 0 everywhere = the usual even split.
    // AT THE END of the struct (screen_calendar/'event'/'lemans''s positional init doesn't reach it -> 0, inert, in every
    // race that doesn't set it). See docs/2026-06-17-multiclass-grid-trials.md and hkBuilder.
    int classCounts[6] = {0,0,0,0,0,0};
    // honor the Settings AUTO/Inherited mask (prog.inherit_mask). 0 = ignore it (calibration,
    // tryouts, special events, debug commands: they always force). 1 = a normal calendar race (honors
    // ALL inherited fields). 2 = the heroic gate (honors everything EXCEPT AI difficulty: beating the AI
    // at its hardest is the whole point). AT THE END of the struct (the positional init doesn't reach it -> 0, inert).
    int honorInherit = 0;
    // hub rev-3 (30/08): hash of the PLAYER's class in a multiclass weekend. The seat discount
    // (one fewer AI slot in your class, because you occupy it) used to apply to classes[0], assuming that
    // index IS your class; a tester measured that the group's LAST class always kept losing the slot,
    // whichever one they raced in. With the explicit hash the builder locates YOUR quota by identity
    // and not by position, which was the fragile assumption. 0 = unknown -> the builder falls back to classes[0] as before.
    unsigned playerClass = 0;
    // 01/10 ([tester], 0.7.1): time progression is AUTHORED by the event (overrides.timeprog). With 1, `timeProg`
    // already carries the author's s27 index and neither the duration selector (ApplyDurationPercent) nor "inherit from the
    // player" (kInhTimeProg) overrides it. 0 = as usual. AT THE END of the struct (positional init does not reach here).
    int timeProgForced = 0;
    // 01/10 (Josh): minute of the start time (VM550 s52, the slot after the hour s51; map of 06/11: s46 Minute
    // 0..59 before the +6). Only counts if startHour >= 0; with the hour forced the minute is ALWAYS written (0 if not
    // requested): before, it kept whatever the game had. AT THE END of the struct.
    int startMinute = 0;
};
void CareerApplyEvent(const CareerEventParams& p);

// LAUNCH wrappers for the external app (defined in screen_calendar.cpp; POD, no career_state.h) ----
// Arm st.pending/launchEp/prerace in the LIVE UI state (SampleState, the one the overlay draws) just like a click
// on the calendar would, so the app's query.prerace / cmd.prerace.* can read it. Return true if they armed successfully.
bool CareerArmRaceLaunch(const char* seriesId, int round);   // false = insufficient balance or the (series, round) doesn't exist
bool CareerArmCalibration();                                 // false = already calibrated
// item 17: arms the contract TRYOUT in the PRE-RACE screen (locked team car, run it directly, DDA style),
// reusing the tryout's params already resolved by TrialUiArm. false = no tryout armed. On launch
// (PreraceLaunch, raceId "tryout"), st.launchEp carries trialFlag=1 -> the settlement produces the verdict (query.tryout).
bool CareerArmTryout();
// arms the EXHIBITION race for a special event (career::SpecialEventById) in the
// live UI state -- WITHOUT a calendar -- so query.prerace/cmd.prerace.* can pick a car and launch (a standalone settlement branch;
// Task 6 applies the bonus via g_curEventId). false = event/class/track couldn't be resolved.
bool CareerArmEvent(const char* eventId, const char* carClassId);
// F6 (Task 4): arms the SUBSTITUTE-DRIVE race on the SAME exhibition chassis (WITHOUT a calendar -> a standalone
// settlement branch), but from the REAL round of the series offered in save.sub_ride (not an event profile). Validates that the
// substitute drive is ACCEPTED (state==2) and that today falls in the window [absday-1, absday]; builds the race from
// CalendarForSeries's round on that absday (track/weather/hour/duration), single-class, car COVERED by the team.
// The settlement branch charges the fee and records the warmth bonus (SubRideSettle). false = couldn't be armed (state/window/round).
// SaveData by reference (forward-declared below): the overlay passes its own g_save; avoids dragging career_save.h into this header.
namespace career { struct SaveData; }
bool SubRideArm(career::SaveData& save);

// Overlay->UI bridge: the overlay pushes the engine's REAL state (the UI has no dependency on the engine) ----
struct CareerLiveData {
    long credits; long xp_toward_next; long xp_threshold; int frontier_phase; int rep; int sponsor_tier;
    int races, wins, podiums;
    int year = 1, cur_doy = 0;   // P4.3: closes the time loop -- year + calendar cursor (g_calendar) -> keeps Market expiry/year live
    int rating = 1150;           // UI-A: per-driver unified rating (Living World). E1 fills it from g_save.prog.rating.
};
void CareerSetLiveData(const CareerLiveData& d);
struct CareerResultData {
    // (`sponsor` -- the flat per-REP-tier retainer from the v1 system -- removed from the report and from
    //  this bridge; screen_results was already only showing sponsor_payout.)
    bool valid; int pos, field; long prize, entry, costs, credits_delta; int xp_gained, rep_delta;
    bool phase_unlocked; int new_phase;
    long salary = 0;   // C1: factory-seat salary (0 = not applicable; AT THE END: doesn't break positional inits)
    // UI-A Results progression grid (Living World): ELO/REP/EXP/Balance shown before->after. ----
    // ELO+REP derive from the ONE rating; E1 populates rating_before/after, the rest already exist as deltas.
    int  rating_before = 0, rating_after = 0;   // unified rating snapshot (E1); band via career::RatingBand
    int  rep_before = 0,    rep_after = 0;      // engine REP snapshot (tier view); rep_delta already present
    long xp_before = 0,     xp_after = 0;       // xp_gained already present; these give the before->after pair
    long credits_before = 0;                    // credits_delta already present; before = credits_after - delta
    // BUG#2 REAL race times (shm) for the Results panel. <=0 = no data (shows "—"). ----
    // AT THE END of the struct: doesn't break the existing positional inits.
    float best_lap_s   = -1.0f;   // the player's best lap (s) -- g_bestLapCache/mFastestLapTimes
    float total_time_s = -1.0f;   // the player's race clock at the finish line (s) -- mCurrentTime
    float gap_s        = 0.0f;    // PACE gap to the best lap in their class (s); <=0 = the player set the fastest one
    // the thematic fixed sponsor payout for this race (mirror of SettleReport.sponsor_payout).
    // TRAILING defaulted field so the existing positional aggregate inits in careerproof keep compiling
    // unchanged. The ONLY sponsor income line left.
    long sponsor_payout = 0;
    // (D-UI-02): per-race money-breakdown terms mirrored from SettleReport. coverage = the team's
    // covered share of your out-of-pocket (the cyan "Team coverage" +line); result_bonus = the low-type
    // Top10/Top5/win bonus (the gold "Result bonus" +line). TRAILING defaulted (0 = old behavior, line hidden).
    // (travel lands in 04.1-04 -- its field is added when that slice renders its line.)
    long coverage = 0;
    long result_bonus = 0;
    // (ECON-03 / D-UI-03): the per-race insurance deductible (damage_share-scaled, always the
    // player's). Mirrored from SettleReport.deductible. Rendered as the danger "Deductible" (−) MONEY line when
    // >0. TRAILING defaulted (0 = old behavior, line hidden) so the positional careerproof inits keep compiling.
    long deductible = 0;
    // (ECON-07 / D-UI-02): the per-race TRAVEL cost of the A->B leg (country/continent proxy, covered
    // per type). Mirrored from SettleReport.travel. Rendered as the danger "Travel" (−) MONEY line when >0;
    // covered types pay ~0 (line hidden). TRAILING defaulted (0 = old behavior) -- positional inits keep compiling.
    long travel = 0;
};
void CareerSetLastResult(const CareerResultData& r);
void CareerShowResults();   // sets the screen to Results (for the auto-return)
void CareerShowSeasonStandings();   // F3: auto-returns to the season screen (if a season is active)

// UI->overlay bridge: car ownership/purchase against the real save (implemented in career_overlay.cpp) ----
bool CareerOwnsCar(const char* carId);                            // does the save own a car with that id?
bool CareerBuyCarId(const char* carId, long price, int vehHash, int livery = -1);  // deducts price + registers it (with the chosen skin) + saves; false if you can't afford it or already own it
int         CareerOwnedCount();                                  // number of owned cars (the garage iterates the REAL save, not SAMPLE_CARS)
const char* CareerOwnedClassId(int i);                           // class_id/id of owned car i ("" if out of range)
int         CareerOwnedVehId(int i);                             // REAL vehHash of owned car i (0 if out of range) -> name+photo in the garage
int         CareerOwnedLivery(int i);                            // saved livery ordinal of owned car i (-1 = default/no choice made)
int         CareerOwnedLiveryForCar(const char* carId);          // saved livery ordinal of the car with that class_id (-1 = not owned/no choice made)
int         CareerOwnedLiveriesForCar(const char* carId, int* out, int maxN);   // Memories 1:1: ALL owned livery ordinals for the car (count written to out; returns the total)
int         CareerSeatLockForSeries(const char* seriesId);       // pre-race lock from a contract: 0=free, 1=car+skin (customer/pay), 2=everything (works)
// sets the chosen race's economics (read by the settlement when it finishes): car, rented?, rentPct,
// winner's prize, the car's value (for run_cost if it's owned), class_id (for per-class stats),
// factorySeat (C1: 1 = factory seat -> Settle with costs/entry at 0 + a salary).
void CareerSetRaceEconomics(const char* carId, int rented, int rentPct, long reward, long carValue, const char* classId, int factorySeat = 0, int raceLaps = 0);   // raceLaps: effective LAPS (spec 2026-07-02 amendment) -> scales the owned car's consumables (<1 -> a reference race)
// sets the id of the calendar event in progress (empty = a tree/generic race); marked completed and persisted at settlement.
void CareerSetRaceId(const char* raceId);
// sets the LAUNCHED duration % -> for events, the settlement divides the reward (credits+rep) by it. Call it in PreraceLaunch.
void CareerSetRaceDurPct(int pct);
// sets the championship's class (the ladder's display name or the internal class_name) -> the overlay auto-writes the
// named grid (Custom AI XML) when the event launches and uses it as the standings' node_id. Call before CareerApplyEvent.
void CareerSetChampClass(const char* classKey);
// pushes the UI the set of races already run (csv of ids) -> real calendar gating (completed/raced).
void CareerSetCompletedRaces(const char* idsCsv);
// D1: the car's real image (a DDS extracted from the game) as a DX11 texture ready for dl->AddImage.
// Returns the ID3D11ShaderResourceView* as a void* (ImTextureID), or nullptr if there's no image
// (vehHash has no slug / folder missing / load failed; cached negatively: won't re-scan).
// livery: -1 = the default (lowest NN in the folder); >=0 ordinal (clamped). outAspect = the real width/height ratio.
void* CareerVehicleImage(int vehHash, int livery /*=-1*/, float* outAspect);
// R5: the car's list of liveries. Scans the SAME folder as CareerVehicleImage and parses the NN from each
// <slug>_livery_NN.dds file, filling outNN in ascending order (SAME order as the image ordinal: ordinal i <-> outNN[i]).
// Returns the number of liveries (0 if there's no slug/folder). maxN caps how much is written to outNN. Cached per vehHash.
int CareerVehicleLiveryList(int vehHash, int* outNN, int maxN);
// R6: the circuit map (a DDS extracted from the game, gui/trackmaps3d/<slug>.dds, RGB32/BGRA8) as a
// DX11 texture ready for dl->AddImage. Returns the ID3D11ShaderResourceView* as a void* (ImTextureID), or nullptr
// if there's no map (trackHash has no slug / file missing / load failed; cached negatively: won't reload).
// outAspect = the real width/height ratio from the header (maps are NOT all square: 10 of them are 1024x512).
void* CareerTrackImage(int trackHash, float* outAspect);
// Country/territory flag (a PNG embedded in the DLL) as a DX11 texture ready for dl->AddImage.
// code = ISO 3166-1 alpha-2 code (e.g. "ES", "PS") or "CAT" for Catalonia. Cached per code.
// Returns nullptr if there's no asset (falls back to the procedural drawing in widgets.cpp).
void* CareerFlagImage(const char* code, float* outAspect);
