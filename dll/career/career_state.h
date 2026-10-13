#pragma once
#include <vector>
#include <set>
#include <string>
#include "imgui.h"
#include "entitlements.h"
#include "career_ui.h"   // CareerResultData (overlay->UI bridge)

enum class Screen { Hub, Tree, Calendar, Garage, Vehicle, Results, Season, SeasonStandings, Market, Inbox, Profile, Negotiate,
                    Sponsors, Memories, Championships, Rollover, Tryout, Settings };

struct ClassDef { std::string id, name, shortc, disc; int tier; long vehHash = 0; int phase = 1; ImU32 color() const; }; // tier 0..3; vehHash=real representative car (handoff), phase=ladder phase
struct Track    { std::string id, name, flag; long hash = 0; };   // hash = AMS2's real hash (ams2_track_catalog.csv); 0 = track unavailable -> not forced
struct Race     { std::string id, name, cls, track; int day, round, laps, grid; bool sprint; long prize; int hour = 12; int weather = 0; int durationMin = 0; }; // hour 0..23, weather 0=sunny, durationMin>0 => TIME mode (N min, ignores laps)
// price = purchase cost (once). rentPct = % of the race prize paid when renting (prototype's model).
struct CarItem  { std::string id, name, cls; int year, pwr, grp, bal; long price = 0; int rentPct = 0; int vehHash = 0; int factory = 0; long runValue = 0; };  // vehHash/factory/runValue at the end: don't break SAMPLE_CARS's positional inits. factory=1 -> factory seat (C1: not purchasable, "UNDER CONTRACT"). #6: runValue = ECONOMIC value (class band, CompressPriceToClassBand) -- rent/consumables/insurance/settlement derive from it; price is the PURCHASE one (real collector value, uncompressed). 0 = use price (SAMPLE_CARS/legacy).
struct TreeNode { std::string id; int col; };  // col = column index (visual tier)

// REAL ladder (P3.8): loaded from docs/career_ladder.psv + counts from ams2_car_class.csv
struct LadderNode { std::string id, display, disc; int level; int tier; std::vector<std::string> prereqs, classes; int car_count; std::string dlc, pack; int vehid; };
extern std::vector<LadderNode> LADDER;
void LoadLadder();          // lazy: parses the .psv + counts + saved edits, the first time
void SaveLadderLayout();    // dumps node_id|level (the user's edit) to a local temp file
LadderNode* FindLadder(const std::string& id);

// Per-series economy: C1 moved into the ENGINE (career_econ.h: career::LoadSeriesEcon/FindEcon/
// FindEconByClassName) so it's testable and career_calendar can use it too (prize/entry). ----

// Calendar event (primary flow): class+track+params -> handoff to Custom Race.
// skill/aggression/mistakes 0 = don't force; startHour/weather <0 = don't force.
struct EventConfig { int round; std::string label, cls; int vehid; int track; std::string trackName;
                     int laps; int opponents; int skill; int aggression; int mistakes; int startHour; int weather;
                     int level; std::string clsId; long reward; };  // level=phase(gate), clsId=sample class (CarsByClass), reward=winner's prize
extern std::vector<EventConfig> SEASON;

// Context of the chosen race (calendar/tree -> vehicle screen -> handoff). clsId = sample
// class id (for CarsByClass); vehHash/trackHash = REAL hashes for the Custom Race handoff.
struct PendingRace {
  std::string clsId, className, disc, trackName, raceName, trackFlag, raceId;  // raceId = calendar event id (empty if it's from the tree)
  int  laps = 0, grid = 0, phase = 1, durationMin = 0;   // durationMin>0 => TIME mode
  long reward = 0;                // the event winner's prize
  int  vehHash = 0, trackHash = 0;
  // from the portal: the SINGLE car of the series/event (car rule; 0 = free). CarsByClass filters the pre-race
  // list to that car -> purchase, rental and launch only see that car. AT THE END (positional inits).
  int  ruleVehHash = 0;
};

// PRE-RACE screen lock/preset (vehicle/skin/duration). Filled in by the CALLER (calendar/measurement)
// depending on context -- the screen only APPLIES it, deducing nothing. preset* = the initial selection forced on entry
// (the car/skin the team gives you, or the measurement's fixed config); lock* = the player can't change it
// (dimmed + locked). reasonKey = the badge's TR key ("lock_calibration"/"lock_contract"; "" = no badge).
struct PreraceLock {
  int  presetVehHash = 0;     // car preset on entry (0 = no preset -> the screen's normal default)
  int  presetLivery  = -1;    // preset skin (ORDINAL in CareerVehicleLiveryList; -1 = default)
  int  presetDurPct  = 100;   // preset duration % (only forced if lockDuration)
  int  discountPct   = 0;     // >0 = event's % discount on purchase/rental (Task 6)
  bool lockCar = false, lockSkin = false, lockDuration = false;
  // item 5 (contract): the team PROVIDES the car+skin (random-fixed, chosen at signing). Not the player's
  // property, but it's raced directly like a factory seat (no buy/rent, cost covered). Reuses the pre-race's
  // "factory" path so as not to duplicate economy/UI. presetVehHash/presetLivery carry the specific one.
  bool teamCar = false;
  // v2 (divorce): >0 = there's a privateer "marriage" with preset*; racing ANOTHER identity costs
  // this (REP + credits, charged at settlement). The UI shows it next to the launch button when they diverge.
  int  divorceRep = 0;
  long divorceCr  = 0;
  const char* reasonKey = ""; // badge's TR key; "" = none
  void clear() { *this = PreraceLock{}; }
  bool any() const { return lockCar || lockSkin || lockDuration; }
};

struct CareerState {
  Screen screen = Screen::Hub;
  int  lang = 0;                 // 0=es 1=en
  long credits = 304531;
  int  wins = 3, podiums = 6;
  Tier tier = Tier::SupporterPlus;
  std::set<std::string> owned, completed;
  std::string featured = "gt4";
  // REAL progress (synced by the overlay via CareerSetLiveData; if nobody pushes it, sample values remain)
  long xp_toward_next = 0, xp_threshold = 5000; int frontier_phase = 1, rep = 0, sponsor_tier = 0;
  int  rating = 1150;             // Living World per-driver unified rating (ELO=REP). Pushed by E1 via CareerLiveData; default = RATING_ROOKIE_BASE.
  int  year = 1;                  // current season (the calendar repeats every year)
  int  cur_doy = 0;               // time cursor within the year (0=start; running an event advances it to its doy)
  CareerResultData last_result{};   // last result (Results screen)
  PendingRace pending;              // chosen race (for the vehicle screen)
  PreraceLock prerace;              // vehicle screen preset/lock (set by the caller; default = free)
  std::string vehSel;               // id of the car selected on the vehicle screen
  int  difficulty = 1;              // UI-D Settings: 0=easy 1=medium 2=hard 3=extreme 4=adaptive (default medium)
  std::string negOfferId;           // UI-B: id of the offer under negotiation (Market -> Negotiate); "" = none
  // UI-C transient screen state ---
  std::string mem_sel;              // selected memory id (Memories family tree)
  int  cal_jump_doy = 0;            // if >0: on entering the Calendar, jump to/highlight that date (view-on-calendar / next race)
  std::string champ_sel;            // selected championship series id (Championships left list)
  int  times_sel_race = -1;         // TIMES-01: selected race index in the TIMES tab (-1 = list; >=0 = full grid). Index into RaceLogUiEntries (most recent first).
  int  times_sel_driver = -1;       // TIMES-02 (reserved): driver selected within the grid (-1 = grid; >=0 = lap-by-lap breakdown)
  std::string prof_filter;          // RATING-05 (D-10): Profile world-ranking series filter ("" = All)
  int  tryout_phase = 0;            // 0=brief 1=running 2=result
  std::string tryout_result;        // "pass"|"ok"|"fail" (set when tryout_phase==2)
  int  tryout_attempt = 0;          // deterministic seed for TryoutUiRoll
  double tryout_run_t = 0.0;        // running-phase elapsed seconds
  int  rollover_contract = -1;      // -1=none 0=renew 1=newGoals 2=decline
  CareerEventParams launchEp{};     // full handoff params (set by calendar/tree; triggered by the vehicle screen)
};

// SAMPLE catalogs (defined in career_data.cpp)
extern std::vector<ClassDef> SAMPLE_CLASSES;
extern std::vector<Track>    SAMPLE_TRACKS;
extern std::vector<Race>     SAMPLE_CALENDAR;
extern std::vector<CarItem>  SAMPLE_GARAGE;
extern std::vector<CarItem>  SAMPLE_CARS;     // per-car catalog (price/rentPct), the prototype's model
extern std::vector<TreeNode> SAMPLE_TREE;
const ClassDef* FindClass(const std::string& id);
const Track*    FindTrack(const std::string& id);
// Per-phase AI difficulty for the handoff (scales with phase; tunable). skill 80..108, aggr 40/60/80, mistakes 2->1.
void HandoffAiForPhase(int phase, int& skill, int& aggr, int& mistakes);
// cars of a class (from SAMPLE_CARS); if the class has none, synthesizes one from context.
std::vector<CarItem> CarsByClass(const std::string& clsId, const PendingRace& ctx);
CareerState&    SampleState();   // initial sample state (owned/completed populated)
