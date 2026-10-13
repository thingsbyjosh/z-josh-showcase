#include "career_state.h"
#include "career_customai.h"   // ClassForVehicle / VehiclesForClass / ResolveClass (catalogue cars)
#include "career_series.h"     // query.times: SeriesById / DiscForSeries (discipline colour of the race)
#include "career_season_ui.h"  // query.tree/garage/market: LadderNodeView/GarageOwnedCarView/MarketClassInfo (declarations)
#include "career_econ.h"       // career::FindEcon / FindEconByClassName (per-category economy, in the engine)
#include "paths.h"             // PathProvider: data/ (psv, ladder, calendar) + userdata/cache (dumps)
#include "theme.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <algorithm>

ImU32 ClassDef::color() const { return Col::Disc(disc.c_str()); }

// vehHash = the class's representative car (from the ladder) for the handoff; phase = ladder level.
std::vector<ClassDef> SAMPLE_CLASSES = {
  {"kart","Karting","KART","touring",0, 591135961, 1}, {"fvee","Formula Vee","ONYX CHALLENGE","single",0, -1091352742, 1},
  {"copa","Copa Clasicos","COPA","touring",0, -790590692, 2}, {"cat","Fellwind","CAT","gt",0, -573173193, 1},
  {"ftrn","Fairham Motors","F-TRN","single",1, 1945290236, 2}, {"grpa","Group A Touring","GRP A","touring",1, -1471541258, 4},
  {"gt4","GT4","GT4","gt",1, 426939550, 5}, {"harbor_masters_2241","Blackstead Motors","F-INT","single",2, -1528927516, 3},
  {"stock","Stock Car","STOCK","touring",2, -1421208401, 4}, {"gt3","GT3","GT3","gt",2, -807368311, 6},
  {"fult","Fairbrook Motors","F-ULT","single",3, 989683008, 6}, {"grpc","Group C","GRP C","endurance",3, 516264716, 5},   // representative car: the class's best-known model
};
// hash = the circuit's catalogue hash for the handoff (g_fixTrack); invented values in this tree. Ashcombe/Junction Bay have no
// catalogue entry -> hash 0 (those events do not force a track; the rest of the event still applies).
std::vector<Track> SAMPLE_TRACKS = {
  {"ashcombe","Ashcombe","NZ",0}, {"bellhaven","Bellhaven","CA",-1079147073}, {"cindermoor","Cindermoor","IE",917405408},
  {"fairwater","Fairwater","AU",-398343602}, {"drummond","Drummond Park","SE",1094976135}, {"ellerby","Ellerby","NO",-1187013283},
  {"glasshouse","Glasshouse","DK",700610474}, {"holloway","Holloway","FI",509989002}, {"ilminster","Ilminster","PT",134146027},
  {"junction","Junction Bay","NL",0}, {"kelvedon","Kelvedon","CH",167701265}, {"lyndhurst","Lyndhurst","AR",1239920270},
  {"marchwood","Marchwood","CL",-246065103}, {"oakhanger","Oakhanger","PL",-230126127}, {"pentridge","Pentridge","JP",876649674},
};
// Sample season (port of the prototype's CALENDAR). One race per day; some days hold 2 competing events.
// {id, name, cls, track, day, round, laps, grid, sprint, prize, hour, weather}  hour 0..23 · weather: 0 clear, 5-8 rain/storm, 12-16 fog, 17 random (full catalogue in career_series.h)
std::vector<Race> SAMPLE_CALENDAR = {
  {"r01","Ashcombe Kart Open","kart","ashcombe",3,1,8,16,true,4500,9,0},
  {"r02","Bellhaven Karting GP","kart","bellhaven",6,2,12,16,false,5200,11,0},
  {"r03","Cindermoor Vee 11h ENDURANCE","fvee","cindermoor",9,3,14,22,false,8000,13,0,660},   // TEST: TIME mode, 11h (660 min)
  {"r04","Cindermoor Classic","copa","cindermoor",9,4,14,20,false,8400,15,0},
  {"r05","Fairwater Vee GP","fvee","fairwater",13,5,16,22,false,9200,10,0},
  {"r06","Fellwind Sprint SP","cat","fairwater",13,6,10,18,true,9800,17,0},
  {"r07","Bellhaven Classics","copa","bellhaven",17,7,18,20,false,10500,12,8},
  {"r08","Drummond Fellwind Trophy","cat","drummond",20,8,16,18,false,12000,14,0},
  {"r09","Ellerby Trainer Series","ftrn","ellerby",24,9,18,24,false,15000,9,0},
  {"r10","Ellerby Touring Masters","grpa","ellerby",24,10,16,20,false,15600,16,0},
  {"r11","Glasshouse GT4 Cup","gt4","glasshouse",28,11,20,26,false,21000,13,0},
  {"r12","Drummond Group A Sprint","grpa","drummond",31,12,12,20,true,16800,18,0},
  {"r13","Holloway Trainer GP","ftrn","holloway",35,13,22,24,false,17500,11,0},
  {"r14","Holloway Endurance Cup","gt4","holloway",35,14,24,28,false,23000,15,12},
  {"r15","Ilminster GT3 Sprint","gt3","ilminster",39,15,26,30,false,38000,14,0},
  {"r16","Junction Bay Inter GP","harbor_masters_2241","junction",42,16,24,24,false,34000,12,0},
  {"r17","Fairwater Stock Round","stock","fairwater",46,17,30,30,false,42000,19,0},
  {"r18","Kelvedon GT3 Trophy","gt3","kelvedon",46,18,28,30,false,41000,16,8},
};
std::vector<CarItem> SAMPLE_GARAGE = {
  {"kart1","Club Kart 2-T","kart",2024,18,42,70},
  {"fvee1","Formula Vee","fvee",2023,30,40,78},
  {"copa1","Copa Classic Early","copa",1995,41,48,66},
  {"cat1","Fellwind Club","cat",2019,40,58,72},
  {"gt41","Halcyon GT4","gt4",2021,70,72,70},
};
// Per-car catalogue (port of the prototype's CARS). cls = SAMPLE_CLASSES id. price = purchase price, rentPct = % of the prize.
// {id, name, cls, year, pwr, grp, bal, price, rentPct}
std::vector<CarItem> SAMPLE_CARS = {
  {"kart_rental","Club Kart 2-T","kart",2024,18,42,70,      0,  0},
  {"kart_gearbox","Gearbox Kart","kart",2024,34,55,64,        9000,  5},
  {"fvee_std","Formula Vee","fvee",2023,30,40,78,         16000,  8},
  {"fvee_plus","Formula Vee Plus","fvee",2023,33,44,80,    21000,  8},
  {"copa_early","Copa Classic Early","copa",1995,41,48,66,        24000,  9},
  {"copa_late","Copa Classic Late","copa",1997,46,50,64,      31000,  9},
  {"cat_club","Fellwind Club","cat",2019,40,58,72,     28000, 10},
  {"cat_trackday","Fellwind Trackday","cat",2020,52,64,70,        39000, 10},
  {"cat_comet","Fellwind Comet","cat",2021,66,70,58,       58000, 12},
  {"ftr_std","Fairham Motors","ftrn",2022,48,56,76,      44000, 11},
  {"ftr_adv","Fairham Motors Mk2","ftrn",2022,60,62,74, 62000, 12},
  {"ga_solstice","Solstice Gr.A","grpa",1991,64,60,62,    71000, 13},
  {"ga_verdant","Verdant Gr.A","grpa",1990,70,58,56,      78000, 13},
  {"ga_amberfall","Amberfall Gr.A","grpa",1992,78,66,60, 96000, 15},
  {"gt4_halcyon","Halcyon GT4","gt4",2021,70,72,70, 120000, 16},
  {"gt4_ravenhurst","Ravenhurst GT4","gt4",2020,73,70,68,         128000, 16},
  {"gt4_meridian","Meridian GT4","gt4",2021,78,74,64,     142000, 18},
  {"fint_std","Blackstead Motors","harbor_masters_2241",2023,80,76,72,    155000, 18},
  {"fint_evo","Blackstead Motors Evo","harbor_masters_2241",2023,86,80,70,188000, 20},
  {"sc_ironmarch","Stock Ironmarch","stock",2024,84,70,60,        168000, 18},
  {"sc_lowtide","Stock Lowtide","stock",2024,84,71,61,       172000, 18},
  {"gt3_halcyon","Halcyon GT3","gt3",2023,88,86,78,    320000, 22},
  {"gt3_copperline","Copperline GT3","gt3",2022,90,84,74,     335000, 22},
  {"gt3_ravenhurst","Ravenhurst Evo GT3","gt3",2023,89,88,76,     342000, 24},
  {"gt3_northgate","Northgate GT3","gt3",2023,91,85,72,     358000, 24},
  {"gc_halcyon","Halcyon GC","grpc",1988,95,80,64,       410000, 26},
  {"gc_sablefield","Sablefield GC","grpc",1989,96,80,64,             425000, 26},   // the top car of the sample endurance class
  {"fu_mk2","Fairbrook Motors Mk2","fult",2024,99,96,78,720000,32},
  {"fu_mk1","Fairbrook Motors Mk1","fult",2020,97,92,80,640000,30},
};
static bool g_vehCatReady = false;

std::vector<CarItem> CarsByClass(const std::string& clsId, const PendingRace& ctx){
  std::vector<CarItem> out;
  // price/rent from the PER-SERIES ECONOMY (docs/ams2_series_econ.psv) when present;
  // otherwise a per-phase fallback. rentPct = % of the CAR'S VALUE -> also works for heritage events (prize=0).
  int ph = ctx.phase<1?1:(ctx.phase>7?7:ctx.phase);
  static const long PRICE[7] = { 16000, 44000, 130000, 200000, 410000, 550000, 990000 };
  static const int  RENT[7]  = { 8, 11, 18, 20, 26, 30, 36 };
  long price; int rent;
  static bool econReady = false;
  if (!econReady) econReady = career::LoadSeriesEcon(paths::Data("ams2_series_econ.psv").c_str());
  static bool vehPriceReady = false;   // per-car price (override): loaded ONCE (absent = everything falls back to base+jitter)
  if (!vehPriceReady) { career::LoadVehiclePrices(paths::Data("ams2_vehicle_prices.psv").c_str()); vehPriceReady = true; }
  // clsId can be a ladder node_id (legacy tree: "gt3") or a catalogue class_name (calendar flow:
  // "GT3" and the like). The class_name->row join is the PSV's 'classes' column (this lookup used to
  // fail ALWAYS in the live flow and send everything to the per-level fallback).
  const career::SeriesEcon* ec = career::FindEcon(clsId);
  if (!ec) ec = career::FindEconByClassName(clsId);
  // Special events: there is NEVER a factory/contract seat -- EVERY car can be bought or
  // rented (at a discount), a chance to add it to the collection. The flag is cleared BEFORE
  // price/rent are set, so a class with a "factory" economy quotes a real price (ec->comprar or the per-phase fallback)
  // instead of 0; the event's discount (st.prerace.discountPct) is applied later, in the prerace.
  bool isEvent = ctx.raceId.rfind("event_", 0) == 0;
  int factory = (!isEvent && ec && ec->model == "factory") ? 1 : 0;
  if (factory){
    price = 0; rent = 0;   // factory seat -- cannot be bought or rented ("BY CONTRACT"; salary paid at settlement)
  } else if (ec && ec->comprar > 0){
    price = ec->comprar;
    rent  = (int)(100.0 * (double)ec->alquiler / (double)ec->comprar + 0.5);
    if (rent < 1) rent = 1;
  } else { price = PRICE[ph-1]; rent = RENT[ph-1]; }
  int s = 40 + ph*7; if (s>99) s=99;

  // 1) CATALOGUE CARS, resolved through the class's representative vehHash (ams2_vehclass.psv).
  if (!g_vehCatReady) g_vehCatReady = career::LoadVehClass(paths::Data("ams2_vehclass.psv").c_str());
  std::string cat = career::ClassForVehicle(ctx.vehHash);          // vehHash -> catalogue class_name
  if (cat.empty()) cat = career::ResolveClass(ctx.className);       // fallback: by name/display
  // with a car rule (ctx.ruleVehHash) the list is that single car (fail-open to the whole class if it
  // is not installed or not in the class). This filters EVERYTHING the player sees: cards, buy, rent, launch.
  std::vector<career::VehicleEntry> vehs = cat.empty() ? std::vector<career::VehicleEntry>()
                                                       : career::VehiclesForRace(cat, ctx.ruleVehHash);
  if (!vehs.empty()){
    for (auto& v : vehs){
      CarItem c;
      c.id = clsId + "#" + std::to_string(v.vehHash);   // stable per-car id (the ownership key)
      c.name = v.name; c.vehHash = v.vehHash; c.cls = clsId;
      c.year = 2024; c.pwr = s; c.grp = s>4?s-4:s; c.bal = 70;
      // price: the per-car override if there is one (and the car IS for sale, not factory); otherwise class base+jitter.
      // Collector cars are wanted, but their running costs need not be collector-priced:
      // price = the uncompressed value (buying a classic IS a collector's purchase); runValue = the value compressed
      // into the class's band -- EVERYTHING it costs to RACE it (rent/consumables/insurance/settlement) comes from that.
      long vp = factory ? 0 : career::VehiclePrice(v.vehHash);
      c.price    = vp > 0 ? vp : career::JitterPrice(price, v.vehHash);
      c.runValue = vp > 0 ? career::CompressPriceToClassBand(vp, price) : c.price;
      c.rentPct = rent; c.factory = factory;
      out.push_back(c);
    }
    return out;
  }

  // 2) Fallback: the prototype's curated sample cars (classes present in SAMPLE_CARS).
  for (auto& c : SAMPLE_CARS) if (c.cls == clsId) out.push_back(c);
  if (out.empty()){
    // 3) nothing at all: synthesize one representative car from the context.
    CarItem c; c.id = clsId + "_spec"; c.name = ctx.className.empty()? clsId : ctx.className;
    c.cls = clsId; c.year = 2024; c.pwr = s; c.grp = s>4?s-4:s; c.bal = 70; c.vehHash = ctx.vehHash;
    long vp = factory ? 0 : career::VehiclePrice(ctx.vehHash);
    c.price    = vp > 0 ? vp : price;
    c.runValue = vp > 0 ? career::CompressPriceToClassBand(vp, price) : c.price;   // collector-value split, as above
    c.rentPct = rent; c.factory = factory;
    out.push_back(c);
  }
  return out;
}
std::vector<TreeNode> SAMPLE_TREE = {
  {"kart",0},{"fvee",1},{"ftrn",2},{"harbor_masters_2241",3},{"fult",4},
  {"copa",1},{"grpa",2},{"stock",3},{"cat",1},{"gt4",2},{"gt3",3},{"grpc",4},
};
const ClassDef* FindClass(const std::string& id){ for (auto& c:SAMPLE_CLASSES) if (c.id==id) return &c; return nullptr; }
const Track*    FindTrack(const std::string& id){ for (auto& t:SAMPLE_TRACKS) if (t.id==id) return &t; return nullptr; }
// AI difficulty per phase (HandoffAiForPhase): MOVED to career_ai_handoff.cpp -- this file does not
// enter the test harness build (it pulls in the UI layer), and a test already covers it. See there for why.
CareerState& SampleState(){
  static CareerState s; static bool init=false;
  if (!init){ s.owned={"kart","fvee","copa","cat","gt4"}; s.completed={"r01","r02","r04","r06"}; s.rating=1480; init=true; }
  return s;
}

// Real ladder ----------------
std::vector<LadderNode> LADDER;
static std::vector<std::string> splitStr(const std::string& s, char d){
  std::vector<std::string> r; size_t i=0;
  while (true){ size_t j=s.find(d,i); if(j==std::string::npos){ r.push_back(s.substr(i)); break; } r.push_back(s.substr(i,j-i)); i=j+1; }
  return r;
}
static int tierIdx(const std::string& t){ if(t=="rookie")return 0; if(t=="semipro")return 1; if(t=="pro")return 2; if(t=="elite")return 3; return 0; }
// Test season (5 rounds). vehid = representative id; track = the circuit's catalogue hash.
// {round, label, cls, vehid, track, trackName, laps, opponents, skill, aggression, mistakes, startHour, weather, level}
// startHour = -1 (do not force): the hour lives in the master datetime (the session copy is a mirror the game re-syncs);
// forcing it needs the datetime field itself. weather: the value applies (the native summary's icon is cached).
// level = the class's ladder phase -> economy (buy/rent price) and settlement.
std::vector<EventConfig> SEASON = {
  {1, "Ronda 1 - Escuela",      "Fairham Motors",       1945290236,  -515639840, "Holloway Club",   8, 12, 85, 40, 2, 11, 0, 2, "ftrn", 15000},
  {2, "Ronda 2 - GT4",          "GT4",                   426939550,   509989002,  "Holloway GP",  10, 16, 88, 40, 2, 13, 0, 3, "gt4",  21000},
  {3, "Ronda 3 - One-Make Cup",  "Halcyon One-Make Cup",  1562178984,  964424965, "Ilminster North", 12, 20, 92, 60, 2, 12, 0, 3, "gt4",  22000},
  {4, "Ronda 4 - GT3",          "GT3",                  -807368311,  134146027,  "Ilminster",       14, 24, 98, 80, 1, 16, 0, 4, "gt3",  38000},
  {5, "Ronda 5 - GT3 (final)",  "GT3",                  -807368311,  509989002,  "Holloway GP",  16, 26,105, 80, 1, 18, 0, 4, "gt3",  41000},
};
void LoadLadder(){
  if (!LADDER.empty()) return;
  // car counts per class_name
  std::map<std::string,int> cc;
  if (FILE* f=paths::FOpen(paths::UserData("cache\\ams2_car_class.csv"),"rb")){
    char ln[512]; bool first=true;
    while (fgets(ln,sizeof(ln),f)){ if(first){first=false;continue;} std::string s(ln);
      while(!s.empty() && (s.back()=='\n'||s.back()=='\r')) s.pop_back();
      auto p=splitStr(s,','); if(p.size()>=3) cc[p[2]]++; }
    fclose(f);
  }
  // ladder psv
  if (FILE* f=paths::FOpen(paths::Data("career_ladder.psv"),"rb")){
    char ln[1024]; bool header=false;
    while (fgets(ln,sizeof(ln),f)){ std::string s(ln);
      while(!s.empty() && (s.back()=='\n'||s.back()=='\r')) s.pop_back();
      if (s.empty() || s[0]=='#') continue;
      if (!header){ header=true; continue; }   // skip the header row
      auto p=splitStr(s,'|'); if(p.size()<7) continue;
      LadderNode n; n.id=p[0]; n.display=p[1]; n.disc=p[2];
      n.level=atoi(p[3].c_str()); if(n.level<1)n.level=1; if(n.level>7)n.level=7;
      n.tier=tierIdx(p[4]);
      if(!p[5].empty()) n.prereqs=splitStr(p[5],',');
      n.classes=splitStr(p[6],','); n.car_count=0;
      for (auto& c:n.classes) n.car_count += cc.count(c)?cc[c]:0;
      if(p.size()>8) n.dlc=p[8];     // base|dlc
      if(p.size()>9) n.pack=p[9];    // DLC pack name
      n.vehid=0; if(p.size()>10) n.vehid=atoi(p[10].c_str());  // representative veh hash (handoff on RACE)
      LADDER.push_back(n);
    }
    fclose(f);
  }
  // user edits (drag): node_id|level (overrides the base level)
  if (FILE* f=paths::FOpen(paths::UserData("cache\\ams2_ladder_layout.psv"),"rb")){
    char ln[256];
    while (fgets(ln,sizeof(ln),f)){ std::string s(ln);
      while(!s.empty() && (s.back()=='\n'||s.back()=='\r')) s.pop_back();
      if (s.empty() || s[0]=='#') continue;
      auto p=splitStr(s,'|'); if(p.size()<2) continue;
      if (LadderNode* n=FindLadder(p[0])){ int lv=atoi(p[1].c_str()); if(lv>=1&&lv<=7) n->level=lv; }
    }
    fclose(f);
  }
}
LadderNode* FindLadder(const std::string& id){ for(auto&n:LADDER) if(n.id==id) return &n; return nullptr; }
void SaveLadderLayout(){
  std::string layoutPath = paths::UserData("cache\\ams2_ladder_layout.psv"); paths::EnsureParentDir(layoutPath);
  if (FILE* f=paths::FOpen(layoutPath,"wb")){
    fputs("# ediciones de nivel del usuario (drag). node_id|level\r\n",f);
    for (auto& n:LADDER){ char ln[160]; snprintf(ln,sizeof(ln),"%s|%d\r\n",n.id.c_str(),n.level); fputs(ln,f); }
    fclose(f);
  }
}

// Per-series economy (data-driven) ----------------
// A SECOND CALENDAR USED TO LIVE HERE, AND IT WAS DEAD. `LoadCalendar()`/`CALENDAR` read
// docs/ams2_calendar.psv into a vector NOBODY consulted: not one call anywhere in the engine. And it was not
// just extra code: the file contradicted the live calendar (one series appeared there with a different
// round list from the one it actually runs), so
// anyone who read it took away the wrong data. The REAL calendar comes in through
// docs/series_calendar.psv -> docs/career/seasons/*.json (career_dataload / career_data_json), which
// REPLACES the engine's globals. The .psv is kept only for the offline comparison tools
// (tools/build_preset_comparison*.py), and its header says so.

// the per-series economy table now lives in the ENGINE: career_econ.cpp
// (career::LoadSeriesEcon / FindEcon / FindEconByClassName). Only CarsByClass's lazy load remains here.

// Host->UI bridge: the host pushes the engine's REAL state into the sample CareerState ----
void CareerSetLiveData(const CareerLiveData& d){
  CareerState& s = SampleState();
  s.credits = d.credits; s.xp_toward_next = d.xp_toward_next; s.xp_threshold = d.xp_threshold;
  s.frontier_phase = d.frontier_phase; s.rep = d.rep; s.sponsor_tier = d.sponsor_tier; s.rating = d.rating;
  s.wins = d.wins; s.podiums = d.podiums;
  s.year = d.year; s.cur_doy = d.cur_doy;   // live time loop (Market reads st.year/st.cur_doy)
}
void CareerSetLastResult(const CareerResultData& r){ SampleState().last_result = r; }

// query.results: display name + colour of a class by id (wraps FindClass for the host's provider,
// which cannot include career_state.h because of the 'struct Track' clash). Declared in career_season_ui.h.
namespace career {
bool ClassDisplayColor(const std::string& classId, std::string& outName, unsigned int& outColor){
    const ::ClassDef* cd = ::FindClass(classId);
    if (!cd) return false;
    outName  = cd->name;
    outColor = (unsigned int)cd->color();
    return true;
}
// lock panel: the identity of a row PER CLASS, so the list reads as
// categories and not as a column of raw ids. The keys that arrive here are the ones the difficulty loop uses,
// i.e. CATALOGUE class_names, which are NOT in SAMPLE_CLASSES -> ClassDisplayColor
// fails on them. Cascade, from the most specific to the one that never fails:
//   1) the ladder class (ClassDisplayColor): its curated name and its discipline colour.
//   2) the SERIES that runs that class: its display is readable and its discipline gives the colour. It is the
//      same rule RaceListColor uses for a race's colour strip.
//   3) the raw id + accent: a row is never left without a name, which is what the previous version did.
// ALWAYS returns something. There is no variant that fails on purpose: a row without an identity is exactly the defect
// this redesign set out to fix.
void ClassRowIdentity(const std::string& classId, std::string& outName, unsigned int& outColor){
    if (ClassDisplayColor(classId, outName, outColor)) return;
    for (const auto& sd : AllSeries()) {
        if (sd.class_name != classId) continue;
        outName  = sd.display.empty() ? classId : sd.display;
        outColor = (unsigned int)Col::Disc(DiscForSeries(&sd).c_str());
        return;
    }
    outName  = classId;
    outColor = (unsigned int)Col::accent;
}
// query.times: same rule as RaceColor (screen_championships.cpp): the series' discipline, otherwise
// the class colour, otherwise accent. Called by the host's provider (which cannot include career_state.h).
unsigned int RaceListColor(const std::string& seriesId, const std::string& classId){
    if (!seriesId.empty()){ const SeriesDef* sd = SeriesById(seriesId); if (sd) return (unsigned int)Col::Disc(DiscForSeries(sd).c_str()); }
    const ::ClassDef* cd = ::FindClass(classId); if (cd) return (unsigned int)cd->color();
    return (unsigned int)Col::accent;
}
// query.times: the class colour of one row (multiclass) -- ResolveClass(car_class)->FindClass->color; if
// it does not resolve, the fallback (the race's colour). Mirrors DrawTimesDetail's rowCol.
unsigned int RowClassColor(const std::string& carClass, unsigned int fallback){
    std::string cid = ResolveClass(carClass);
    if (!cid.empty()){ const ::ClassDef* cd = ::FindClass(cid); if (cd) return (unsigned int)cd->color(); }
    return fallback;
}
// query.tree: the REAL ladder (LoadLadder()+LADDER) as a flat view. career_data.cpp is the TU that defines
// ::LADDER and includes career_state.h; the host cannot touch ::LADDER (the 'struct Track' clash). Level
// clamped 1..7 as in screen_tree.cpp; isDlc = (dlc=="dlc"). tier/prereqs/pack are not exposed (unused).
std::vector<LadderNodeView> LadderNodesView(){
    LoadLadder();   // idempotent
    std::vector<LadderNodeView> out; out.reserve(LADDER.size());
    for (const ::LadderNode& n : LADDER){
        LadderNodeView v;
        v.id = n.id; v.display = n.display; v.disc = n.disc;
        v.level = n.level < 1 ? 1 : (n.level > 7 ? 7 : n.level);
        v.carCount = n.car_count; v.isDlc = (n.dlc == "dlc"); v.vehid = n.vehid;
        out.push_back(std::move(v));
    }
    return out;
}
// query.garage: owned cars RESOLVED (a copy of DrawGarage's loop). The host cannot touch
// SAMPLE_CARS/FindClass/CarsByClass (career_state.h) -> here. Resolution: 1) exact id in SAMPLE_CARS;
// 2) "cls#veh"/"cls_spec" -> CarsByClass synthesizes and the exact vehHash is picked (or the first one).
std::vector<GarageOwnedCarView> GarageOwnedCars(){
    std::vector<GarageOwnedCarView> out;
    int n = CareerOwnedCount();
    for (int oi = 0; oi < n; oi++){
        std::string cid = CareerOwnedClassId(oi); if (cid.empty()) continue;
        int vid = CareerOwnedVehId(oi);
        CarItem chosen; bool have = false; std::string clsId;
        const CarItem* found = nullptr; for (auto& c : SAMPLE_CARS) if (c.id == cid){ found = &c; break; }
        if (found){ chosen = *found; have = true; clsId = found->cls; }
        else {
            clsId = cid; size_t h = clsId.find('#'); if (h != std::string::npos) clsId = clsId.substr(0, h);
            size_t sp = clsId.rfind("_spec"); if (sp != std::string::npos) clsId = clsId.substr(0, sp);
            PendingRace ctx; ctx.clsId = clsId; ctx.vehHash = vid;
            const ClassDef* cd = ::FindClass(clsId); if (cd){ ctx.className = cd->name; ctx.disc = cd->disc; }
            std::vector<CarItem> syn = CarsByClass(clsId, ctx);
            if (vid) for (auto& c : syn) if (c.vehHash == vid){ chosen = c; have = true; break; }
            if (!have && !syn.empty()){ chosen = syn[0]; have = true; }
            if (have && !chosen.cls.empty()) clsId = chosen.cls;
        }
        if (!have) continue;
        GarageOwnedCarView r;
        r.id = cid; r.name = chosen.name; r.classId = clsId;
        r.vehHash = chosen.vehHash ? chosen.vehHash : vid; r.livery = CareerOwnedLivery(oi);
        r.year = chosen.year; r.pwr = chosen.pwr; r.grp = chosen.grp; r.bal = chosen.bal;
        r.price = chosen.factory ? 0 : chosen.price;   // resale base (a factory car has no price)
        const ClassDef* c = ::FindClass(clsId);
        if (c){ r.className = c->name; r.classShort = c->shortc; r.classColor = (unsigned)c->color(); }
        else  { r.classColor = (unsigned)Col::accent; }
        out.push_back(std::move(r));
    }
    return out;
}
// query.market: the DISCIPLINE colour of an offer's class (Col::Disc of disc, NOT the class colour).
bool MarketClassInfo(const std::string& classId, std::string& outName, unsigned int& outDiscColor){
    const ::ClassDef* cd = ::FindClass(classId);
    if (!cd) return false;
    outName = cd->name;
    outDiscColor = (unsigned int)Col::Disc(cd->disc.c_str());
    return true;
}
} // namespace career
void CareerShowResults(){ SampleState().screen = Screen::Results; }
void CareerShowSeasonStandings(){ SampleState().screen = Screen::SeasonStandings; }
// the host pushes the races already run (csv "r01,r07,...") -> replaces st.completed (the calendar's real gating)
void CareerSetCompletedRaces(const char* idsCsv){
  CareerState& s = SampleState(); s.completed.clear();
  if (!idsCsv || !*idsCsv) return;
  for (auto& id : splitStr(idsCsv, ',')) if (!id.empty()) s.completed.insert(id);
}
