// Types for the career_api v1 contract (query.* payloads).
// Source of truth: the engine's own query implementation
// in dll/career/career_api_queries.cpp. Changes here are protocol changes — touch both sides.

// query.game_state: COARSE game state ($pcars2$). The app polls this to know when AMS2 is at the MENU
// (atMenu = gameState==1 = front-end) before covering it with the takeover screen (avoids freezing the launch behind AMS2's
// lose-focus pause). gameState: 0=exited/loading, 1=menu, 2..6=in-game.
export interface GameStatePayload { gameState: number; sessionState: number; atMenu: boolean;
  // is the game's Shared Memory active? The engine runs inside the game session, so false means the user
  // does NOT have Options>System>Shared Memory="Project CARS 2" enabled -> warn them (without it, races never settle).
  // Optional, to tolerate responses from an older engine build (undefined -> treated as "unknown", no warning).
  shmMapped?: boolean;
  // VR: phase of the race launch. The "Configuring…" cover and the "all set" pill are ImGui
  // drawn over the desktop swapchain -> invisible with a headset on. The app reproduces them on its own screen (which
  // does reach the headset panel) by polling this. 0 = nothing in progress, 1 = configuring, 2 = configured.
  // Optional: an older engine build doesn't send these (undefined -> the app falls back to its own timeout).
  launchPhase?: number;
  cfgFailN?: number;         // values that did NOT land (0 = all good)
  cfgFailGroups?: string;    // affected groups, already translated by the engine
  // is the in-headset panel alive (did the engine's VR path come up)? Decides which of the two panels wins:
  // if this one is rendering, the OpenVR companion steps aside. Can't be inferred from the launch mode alone — launching
  // via SteamVR, the game loads LibOVR if the headset runs through Link, and doesn't load it if it runs through Steam Link.
  // Optional: an older engine build doesn't send this (undefined -> "unknown" -> the companion stays as it is).
  vrPanelInstalled?: boolean;
  // IS THE GRID STALE? true = seat assignments changed since the grid XML files were
  // written, so what's actually on track does NOT match what the paddock says. AMS2 reads CustomAIDrivers at
  // STARTUP and doesn't hot-reload it, and the app runs INSIDE the game, so a new signing doesn't reach the
  // track until AMS2 restarts. The engine derives this by comparing a fingerprint of the world against the one it sealed when
  // it wrote the files, so it can't get stuck at true.
  // Optional: an older engine build doesn't send this (undefined -> no warning).
  gridsStale?: boolean }

export interface CareerStatePayload {
  profile: {
    name: string;
    money: number;
    eloFame: number;
    xp: number;
    week: number;
    year: number;
    dateDoy: number;
  };
  contract: {
    seriesId: string;
    seriesName: string;
    teamName: string;
    raceSeat: boolean;
  } | null;
  flags: {
    inSeason: boolean;
    pendingResults: boolean;
    resultSeq?: number;   // monotonic settlement counter: once launched, the app detects the end of a race
    calibrated?: boolean; // item 5: has the profile already run its calibration race? false -> epic calibration intro (gate)
    simRace?: boolean;    // SIMULATION mode active in the engine (sim flag + no minimize on launch)
    // 05/09: the engine is HOLDING a settlement (the player is already out, the race hasn't finished yet).
    // While it is set, going back to the menu is NOT abandoning: the result is on its way. Optional, to tolerate
    // an older engine (undefined = "we don't know" -> behaves as before).
    settlePending?: boolean;
  };
  // per-tab badges: number of notifications for Market/Inbox/Sponsors/Memories. Optional
  // (an older fixture/mock without this field still type-checks; the shell treats a missing value as 0).
  badges?: {
    market: number;
    inbox: number;
    sponsors: number;
    memories: number;
  };
}

export interface StandingsRow {
  pos: number;
  driverId: string;
  driverName: string;
  teamName: string;
  points: number;
  isPlayer: boolean;
}

export interface StandingsPayload {
  seriesId: string;
  seriesName: string;
  rows: StandingsRow[];
}

// inbox redesign: an instant decision the engine attaches to an email. label is a translation key
// from the engine's string bundle; choice is the integer sent back via cmd.inbox.answer.
export interface InboxAction {
  choice: number;
  label: string;
  kind: 'primary' | 'danger' | 'ghost';
}

export interface InboxMessage {
  id: string;
  week: number;
  year: number;
  from: string;                       // legacy translation key (fallback)
  fromName?: string;                  // raw display string: "Atenea Montero" | team principal | brand
  fromRole?: string;                  // translation key for the role ("inbox_role_manager"...)
  subject: string;
  body: string;                       // translation keys (already resolved to a variant by the engine)
  read: boolean;
  kind: string;                       // kind now includes "manager"
  params?: Record<string, string>;    // app-side {key} interpolation (interpEngine)
  negId?: string;                     // negotiation v2: the negotiation thread this message references, if any
  you?: boolean;                      // negotiation v2: true if the player opened this turn of the thread
  family?: string;                    // inbox redesign: family token ("offer"|"progress"|"nego"|...)
  actions?: InboxAction[];            // pending decisions (absent if none, or already answered)
  resolvedKey?: string;               // translation key for the outcome if answered (mutually exclusive with actions)
  offerId?: string;                   // actionable offer — opens the embedded Negotiate composer
  deadlineWeeks?: number;             // weeks of TTL left on the offer
  // 18/09: days left on the mail offer (TTL, negotiation or market close, whichever comes
  // first); 1 = today is the last day to respond. Only on live, unanswered offer mails.
  expiresInDays?: number;
  // the offer is no longer on the table (deadline passed, its season started, the team withdrew it, or the
  // market closed). The engine stops sending actions/offerId and sends this instead: the screen shows "offer expired"
  // where the button used to be. Without the flag, the email would be left with no buttons and no explanation, which looks broken.
  expired?: boolean;
  // and why it lapsed: "lapsed" (deadline ran out) | "season_started" (you let it slide) | "withdrawn" (the
  // team pulled it) | "window_over" (its signing window already closed: the email is from a previous year).
  // "gone" = closed without you ever deciding (the offer vanished from the market and the year swallowed the
  // specific reason). This used to leave the email completely silent about what happened.
  expiredReason?: 'lapsed' | 'season_started' | 'withdrawn' | 'window_over' | 'gone';
  nav?: string;                       // navigation CTA ("market" = courting a driver -> go to Market;
                                       // "garage" = treasury alert -> go to Garage)
  eventId?: string;                   // 'invite' family, 'event' subtype — opens EventPrerace
  subReady?: boolean;                 // 'invite' family, 'sub' subtype, ACCEPTED and inside the
                                       // arming window — "take the wheel" CTA (computed server-side by query.inbox)
}

export interface InboxPayload {
  messages: InboxMessage[];
}

/** Minimal interface shared by CareerApiClient (real) and MockClient (fixtures). */
// query.content — sellable DLC products, with detection (live catalogs) and a user-facing toggle.
export interface ContentProduct {
  id: string;                       // "premium_track_pack"
  display: string;                  // "Premium Track Pack"
  kind: 'tracks' | 'cars' | 'mixed';
  detected: boolean;                // some product content seen in the live catalogs (raw)
  enabled: boolean;                 // resolved EFFECTIVE state: the override if present, otherwise detection
  // who's in charge of this state. 'auto' = detection (default); 'on'/'off' = the player set it
  // manually. Absent (older engine) reads as 'auto' and the row renders as before (additive contract).
  override?: 'auto' | 'on' | 'off';
  // what Steam says about OWNERSHIP. 'unknown' = still no data (the scan takes a few seconds to
  // resolve after the game starts) -- the view uses it to ask again instead of sticking with the
  // old snapshot, and to explain why a pack is off and can't be turned on.
  owned?: 'owned' | 'not_owned' | 'unknown';
}
export interface ContentPayload { scanned: boolean; products: ContentProduct[] }

// query.events.content — what the player is missing to run each LIVE event. An event doesn't replace
// its circuit (it IS its circuit), so this is used to warn on the card instead of letting them into an
// event that's going to fail. scanned=false or an event missing from the payload => asserts nothing (fail-open).
export interface EventContentClass {
  id: string;                       // series_id of the event's class
  display: string;
  ok: boolean;                      // at least one car of the class is installed
  product: string;                  // display name of the missing product ("" if unknown)
}
export interface EventContentRow {
  id: string;                       // event id
  trackOk: boolean;
  trackProduct: string;
  trackName: string;
  classes: EventContentClass[];
}
export interface EventsContentPayload { scanned: boolean; events: EventContentRow[] }

export interface ApiClientLike {
  status: 'disconnected' | 'connecting' | 'connected';
  query<T>(method: string, params?: object): Promise<T>;
  cmd<T>(method: string, params?: object): Promise<T>;
  on(event: string, cb: (payload: unknown) => void): () => void;
  // explicit WS close (Close frame; disables reconnection). Optional: test mocks don't send this.
  close?(): void;
}

// ===== Profile (query.profile) =====
// Profile (query.profile) ---
// READ contract for the Profile screen. Source of truth for WHAT is shown and in what order:
// dll/career/screen_profile.cpp (overlay). Rating is the engine's ONLY number (ELO and reputation are unified):
// the hero, arc, rivals and ranking are 4 views of the same score. Translatable strings from the engine
// (band name, rival notes, the arc's "Now" label, the "All" filter) travel as
// i18n KEYS (resolved with t()); proper nouns (driver/team/series) travel as literal strings.

export interface ProfileBand {
  letter: string;   // large letter on the hexagon: S/A/B/C/D/E/F
  nameKey: string;  // i18n key for the band name (e.g. "profile.band.elite")
  color: string;    // CSS color for the band (e.g. "var(--accent)")
}

export interface ProfileNextBand {
  gap: number;      // points needed to reach the next band (next_min - score)
  letter: string;   // letter of the next band
  nameKey: string;  // i18n key for the next band's name
}

export interface ProfileRatingHero {
  score: number;                 // persisted unified rating (prog.rating)
  delta: number;                 // delta from the last race; 0 => hidden
  worldRank: number;             // world rank #N
  band: ProfileBand;
  next: ProfileNextBand | null;  // null if already at the top band
}

export interface ProfileArcPoint {
  season: string;  // label under the point: literal "S1".. or i18n key "profile.now" for the latest one
  rating: number;  // rating at that point (fixed Y axis 600..2600, clamped on screen)
}

export interface ProfileRival {
  name: string;      // driver name (literal)
  team: string;      // team (literal)
  rating: number;
  h2hWins: number;
  h2hLosses: number; // wins+losses === 0 => no head-to-heads yet ("—" and a flat bar)
  noteKey: string;   // i18n key for the note (rival/nemesis/market)
  color: string;     // card accent CSS color
}

export interface ProfileFormEntry {
  pos: number; // position finished in or retired in
  finished?: boolean; // false => retirement (P2/F15); absent (old DLL) => arrival
}

export interface ProfilePalmares {
  wins: number;
  podiums: number;
  races: number;
  titles: number; // engine placeholder (to be wired up later)
  xp: number;     // engine placeholder (mock formula)
  age: number;
}

export interface ProfileRankRow {
  rank: number;        // real 1-based world rank (top: 1..10; neighborhood: e.g. 360..364)
  name: string;        // driver name (literal)
  rating: number;
  bandLetter: string;  // band letter shown after the rating
  bandColor: string;   // CSS color the letter is tinted with
  isPlayer: boolean;
  nationality?: string; // country code for the flag; only meaningful on the player's row
}

export interface ProfileWorldRanking {
  top: ProfileRankRow[];          // world top (<= 10)
  neighborhood: ProfileRankRow[]; // window around the player (empty if they're in the top)
  // ("see more"): total = the REAL size of the filtered table (absent on older engine builds -> undefined).
  // `all` = the COMPLETE table; only sent if the query was made with { full: true } (on demand: it's hundreds
  // of rows and shouldn't ride along with the panel's regular polling).
  total?: number;
  all?: ProfileRankRow[];
}

export interface ProfileFilterOption {
  id: string;    // "" => All
  label: string; // series name (literal), or an i18n key for "All"
}

export interface ProfileFilter {
  options: ProfileFilterOption[];
  selectedId: string; // filter the engine actually applied
}

// query.superlicenses: one row per gated championship — Profile shows status plus the two
// progress paths (same composition as the Championship Access panel, aggregated).
export interface SuperlicenseRow {
  champId: string; champDisplay: string; unlocked: boolean;
  qualDisplay: string; qualHasCal: boolean; qualNextDate: string; gateTop: number;
  // tester note 29/08: tier the TEST opens at (all three open at 6 today) -- its class may unlock earlier
  qualLevel?: number;
  hasFeeder: boolean; feederClass: string; feederTop: number;
  feederHasContext: boolean; feederContextPos: number; feederContextSeries: string;
}
export interface SuperlicensesPayload { licenses: SuperlicenseRow[]; }

export interface ProfilePayload {
  playerName: string;
  rating: ProfileRatingHero;
  arc: ProfileArcPoint[];
  arcDelta: number;               // score - arc[0].rating ("+N since T-3")
  rivals: ProfileRival[];         // up to 3
  recentForm: ProfileFormEntry[]; // up to 8, oldest first
  palmares: ProfilePalmares;
  worldRanking: ProfileWorldRanking;
  filter: ProfileFilter;
}


// ===== Calendar (query.calendar) =====
// query.calendar (Calendar / master calendar) ----
// Temporal spine: one card per ROUND of a real series, grouped by date (day-of-year). The engine derives
// everything shown (resolved state, discipline, prize, grid, clashes) the same way screen_calendar.cpp does.
export type CalendarDiscipline = 'single' | 'touring' | 'gt' | 'endurance' | 'entry' | 'offroad';

// Resolved state of the slot (priority: completed>DNF>locked>traveling>clashes>entered>past>next).
// 'traveled' (-bis) = you already raced TODAY at another venue -> you can't be in two places on the same day.
export type CalendarSlotStatus =
  | 'completed' | 'dnf' | 'locked' | 'traveled' | 'invite' | 'clash' | 'enrolled' | 'past' | 'upcoming';

export type CalendarRaceFormat = 'sprint' | 'feature' | 'endurance';

export interface CalendarSlot {
  id: string;                 // stable key = `${seriesId}#${round}`
  seriesId: string;
  seriesName: string;         // series display name (data-driven, shown in UPPERCASE)
  className: string;          // kicker class (data-driven; `_` -> space, uppercased in the view)
  discipline: CalendarDiscipline;
  region: string;
  trackName: string;
  trackHash: number;          // for the circuit thumbnail (gated visual)
  hour: number;               // event time (clash warning: same circuit at a different time does NOT clash)
  trackSlug: string;          // map slug -> <data>/trackmaps/gui/trackmaps3d/<slug>.png
  doy: number;                // 1..366 day-of-year (grouping + auto-scroll)
  date: string;               // "Feb 14" (month + day, formatted by the engine in English)
  year: number;               // real year of the round (current year, or year+1 for the projected tail).
                               // day-of-year alone isn't a unique key across years: always group/sort by (year, doy).
  weekday?: string;           // "Sun".."Sat" (English, 3 letters) from the engine; optional/additive, alongside `date`
  // found from a real bug (5 Aug): days the round OCCUPIES [winLoDoy..winHiDoy], INCLUSIVE, resolved by the engine
  // (SlotOccupiedDays: the series' footprint — a club round is one day, F1 is Fri-Sun, an endurance round is the whole long week). The clash
  // warning uses this instead of comparing raw `doy`: the engine treats overlapping WINDOWS as a clash, not same-day, and the app
  // used to warn only on the exact same day -> clashing rounds went unwarned. Optional: an older engine doesn't send these and the
  // mirror falls back to the single day (the /v1 contract is additive).
  winLoDoy?: number;
  winHiDoy?: number;
  round: number;              // series round -> "R##"
  format: CalendarRaceFormat; // fmt 0/1/2
  // (1 Aug): does this round score at HALF points? That's NOT the same as "is a sprint": a series where EVERY round
  // is a sprint scores in full (there's no long race to compare it against; see RoundPointsScale in the engine).
  // So the engine decides this and sends it resolved, instead of the app trying to infer it from `format` and getting it wrong.
  // Optional: an older engine doesn't send this (undefined -> no warning shown).
  halfPoints?: boolean;
  laps: number;               // 0 when durationMin>0
  durationMin: number;        // >0 -> endurance by TIME
  grid: number;               // grid size (2..26)
  prize: number;              // winner's PRIZE (credits) — NOT a cost
  level: number;              // ladder tier
  audience: number;           // audience 0-100 (bar on the card, red at the cap). Scaled from ELO/reputation.
  status: CalendarSlotStatus; // resolved state for the chip
  clickable: boolean;         // open and not a memory event (launchable)
  enrolled: boolean;          // style: thicker discipline bar / accent border
  conflict: boolean;          // style: amber + a warning glyph (weekend clash)
  multiclass: boolean;        // MULTICLASS badge
  isMemory: boolean;          // era memory: purple MEMORY chip, not launchable here
  gateLocked: boolean;        // "Requires superlicense" badge
  gateQual?: string;          // the series_id of the qualifier that unlocks it (to jump straight to its round)
  // hub CLOSED grid without an active contract -> "By invitation" (not launchable; the seat comes from the market)
  inviteLocked?: boolean;
  // tester suggestion (hub 29/08): the round IS a Superlicense Test -> gold identity on the card
  isQualifier?: boolean;
  // round's track NOT installed (missing DLC). dlcPack = slug of the missing pack ("" unknown).
  dlcLocked?: boolean;
  dlcPack?: string;
  // round whose track was SUBSTITUTED for missing DLC (subtle badge + hover shows the original track).
  // dlcProduct = the sellable product that's missing (also accompanies dlcLocked when known).
  dlcSubbed?: boolean;
  origTrackName?: string;
  dlcProduct?: string;
  historic: boolean;          // era <2000 (ERA filter)
  eraYear?: number;           // 04/10: era year, only in historical rounds -> card skin (ui/eraSkin)
  isAvailable: boolean;       // STATE filter: can be run NOW
  isLocked: boolean;          // STATE filter: locked (gated / future)
  field?: { name: string; slug: string }[];   // item 1: representative cars for the class (hover shows thumbnails)
  // round injected from special_events.json. eventId links to the
  // teaser in that file; tierMin is informational (real gating happens server-side). Optional: an older
  // engine build doesn't emit these and the shell treats them as absent (/v1 contract is additive).
  special?: boolean;
  eventId?: string;
  tierMin?: string;
}

export interface CalendarSeasonResult {
  seriesId: string;
  seriesName: string;
  year: number;
  finalPos: number;           // >=1 position; 0 -> DNF
  champion: string;           // "" if none / it's you
  field: number;
  rounds: number;
}

export interface CalendarCalibration {
  carName: string;            // "Kingsmere Uma713"
  trackName: string;
  trackHash: number;
  trackSlug: string;          // map slug for the circuit
}

export interface CalendarToday {
  doy: number;                // 0 = no date (don't draw the pill)
  date: string;               // "14 Mar" (day + month)
  weekday?: string;           // "Sun".."Sat" (English, 3 letters) from the engine; optional/additive, alongside `date`
  year: number;
}

export interface CalendarCommitments {
  enrolled: number;
  clashes: number;
}

export interface CalendarPayload {
  calibrated: boolean;                       // onboarding gate: false -> calibration race only
  calibration: CalendarCalibration | null;   // present when calibrated=false
  phase: number;                             // frontier_phase (header state + gating)
  today: CalendarToday;
  weeksToNextEvent: number;                  // 0 -> NEXT EVENT button disabled
  commitments: CalendarCommitments;
  slots: CalendarSlot[];
  seasonResults: CalendarSeasonResult[];     // dismissible stack (max 3 shown)
  // silly season: the year's MARKET DATES, on their own track. They don't belong in `slots`
  // because a slot is a RACE (series, round, circuit), and a signing week doesn't have a track.
  // Optional: an older engine doesn't send these (undefined -> the calendar renders as before).
  marketMarks?: CalendarMarketMark[];
}

// ===== Paddock / signing window (query.paddock, cmd.paddock.apply) =====
// silly season. Source of truth: dll/career/career_api_queries.cpp (QueryPaddock). Everything
// is derived from the save and the clock; the only persisted state is the seat's KEY (series|team) and its status.
export type PaddockPhase = 'season' | 'window' | 'decision';
export type PaddockSeatStatus = 'free' | 'sounded' | 'disputed' | 'offered' | 'rejected' | 'renewed' | 'unknown';

export interface PaddockSeat {
  seriesId: string;
  seriesDisplay: string;
  level: number;
  team: string;
  status: PaddockSeatStatus;
  applied: boolean;       // you applied
  soundedYou: boolean;    // the team talked TO YOU (a plain APPROACH doesn't imply this: they may have talked to someone else)
  winnerName: string;     // who won the seat ("" while unresolved)
  occupantName: string;   // who holds it today ("" if vacant)
  expiryYear: number;     // how long the incumbent's contract runs (0 = free agent / unknown)
  perRace: number;        // per-race pay for the OFFERED seat (0 for the rest). Derived, not stored.
  signed: boolean;        // you already have a contract in that series. Derived from the contract, not a seat field.
  daysLeft?: number;      // 18/09: OFFERED unsigned seat: days left (1 = today is the last)
  occupantAge?: number;   // incumbent's age (0 = unknown)
  reqRep?: number;        // the seat's requirement bar (0 = no requirement)
  yourRating?: number;    // your rating, so the requirement renders as a checkmark/cross instead of a bare number
  rel?: number;           // relationship with that team, 0..100 (derived from shared history)
  // the team's sealed SHORTLIST (SeatWatch.contenders). NAMES only: per-candidate state
  // (pre-deal / trial) and your rank on the list don't exist in the engine, so they aren't faked.
  shortlist?: { id: string; name: string }[];
  // WHAT THE SEAT IS LOOKING FOR. `personality` is the team's character as a TOKEN (the screen
  // translates it; the engine never sends pre-baked text), and `asks` are the factors that matter most, each with
  // its weight and YOUR score on it. These come from the same tables that decide the actual contest, so
  // recalibrating a weight updates the screen automatically.
  personality?: PaddockTeamCharacter;
  asks?: { k: PaddockAskFactor; w: number; you: number }[];
  // the team's CAR for this seat (in a multi-car class the seat doesn't mean anything without it).
  // Same deterministic resolver as the offer preview. Absent = older engine build, or a class with no cars installed.
  carName?: string;
  carSlug?: string;
  liveryNN?: number;
}

// The team's character: the six weighted factors, each with what it values.
export type PaddockTeamCharacter =
  | 'meritocratico' | 'cantera_joven' | 'veterano' | 'dinero' | 'patriotico' | 'fiel_marca';
// The eight factors from the candidacy catalog, as tokens.
export type PaddockAskFactor =
  | 'skill' | 'clean' | 'seriesExp' | 'brandAff' | 'rep' | 'natMatch' | 'youth' | 'budget';

export interface PaddockPayload {
  phase: PaddockPhase;
  year: number;
  doy: number;
  targetYear: number;     // the season being signed for RIGHT NOW
  windowYear: number;     // whichever one has the live window locked in (0 = none)
  daysToWindow: number;
  daysToDecision: number;
  // THE DROUGHT — the window passed and you ended up with nothing. It's its own flag rather than another phase,
  // because it isn't a moment on the calendar, it's a state you're in. Not game over: it's the floor.
  drought?: boolean;
  seats: PaddockSeat[];
  // the player's ACTIVE contracts (the "My contracts" sub-tab). Optional: an older
  // engine build doesn't send these.
  contracts?: PaddockContract[];
  // the lane's INDEX: one entry per playable series, with what you need at a glance.
  series?: PaddockSeriesRow[];
  // the world for the REQUESTED series (params.seriesId). Empty if none was requested: the lane draws
  // without it, since walking the seats of all ~115 series just to paint one would mean paying for the whole world.
  world?: PaddockWorldTeam[];
}

export interface PaddockSeriesRow {
  id: string;
  display: string;
  level: number;
  discipline: string;   // raw token (same one the Calendar uses)
  seats: number;        // total seats in the series
  inPlay: number;       // in play during the live window
  yours: number;        // of those, the ones that involve you (you applied, or they approached you)
  expiring: number;     // contracts expiring at year end
  mine: boolean;        // where you race
}

export interface PaddockWorldSeat {
  idx: number;
  occupantName: string;
  occupantAge: number;
  expiryYear: number;
  open: boolean;
}

export interface PaddockWorldTeam {
  team: string;
  windowStatus: string;   // status within the live window ("" = that seat isn't in play)
  mine: boolean;
  seats: PaddockWorldSeat[];
}

export interface PaddockContract {
  seriesId: string;
  seriesDisplay: string;
  team: string;
  seasonsLeft: number;
  perRace: number;
  objTargetPos: number;   // championship position the team is demanding of you (0 = no target)
  fromYear?: number;      // the contract runs from fromYear to untilYear
  untilYear?: number;
  kind?: number;          // career::bal::CONTRACT_*
  currentPos?: number;    // your position TODAY in that championship (0 = no standings yet)
  // your TEAMMATE — the other seat with your same label. "" or absent = your team
  // runs a single car, which is normal outside the big categories, and the sheet shouldn't render that row.
  // comes with their position in the SAME table as yours: a lone name can't be read on its own — "beating your
  // teammate" needs both numbers.
  teammate?: string;
  teammatePos?: number;   // 0 = no standings, or not racing this championship
  // the HEAD-TO-HEAD with them, race by race: how many you've won against them and vice versa. Comes from `save.h2h`,
  // which has been tracking this against every named driver in your class for a while; what was missing
  // was knowing which of them is your teammate. The standings say who's ahead today; this says who's been
  // ahead more often, which is what actually gets counted in the garage.
  teammateW?: number;
  teammateL?: number;
  // what it would cost to leave RIGHT NOW. Comes from the same function that later charges you, so the warning can't
  // lie. 0 = nothing to break.
  breakFee?: number;
  // the internal REPUTATION cost of terminating (positive, floor applied). The engine still sends it,
  // but the warning no longer quotes it: that scale isn't shown as a number on any screen (what's visible is the
  // rating/ELO, which terminating doesn't touch), and promising an invisible number made it look like the
  // penalty wasn't being applied. The consequence the warning promises is the real one: team + sponsors.
  breakRep?: number;
}

// A market date. `days` is how long it lasts (the decision week is 7 days; December's runs to the
// end of the year). Labels arrive as i18n KEYS, not text: the engine doesn't decide the language.
export interface CalendarMarketMark {
  kind: 'window' | 'decision';
  doy: number;          // first day (1-based)
  days: number;         // how many days it spans
  date: string;         // "Feb 15" (month day, like the slots) — the first day, already formatted by the engine
  labelKey: string;     // band label (cal_mark_window / cal_mark_decision)
  dayLabelKey: string;  // PER-DAY label (cal_day_window / cal_day_decision)
}


// ===== Memories (query.memories) =====
// -- Memories / Era Memories (query.memories) --------------------------
// Source of truth: dll/career/screen_memories.cpp (master-detail over the era
// series on the calendar, pre-2000). The engine precomputes the derived states
// (locked, availableThisWeek, per-round status, next round) from the continuous
// clock's cursor; the screen only renders. Does NOT include standings/rewards/laps/positions:
// that's demo logic in the design JSX, unrelated to the engine.

export type EraRoundState = 'open' | 'upcoming' | 'locked';

export interface EraRound {
  round: number;          // r.round -> "R{n}"
  doy: number;            // day of year (ordering / jump-to-calendar)
  date: string;           // career::DoyToDate(doy) -> "14 Mar" ("" if doy<=0)
  trackName: string;      // r.track_name (real name, rendered as-is)
  trackHash: number;      // r.track_hash (circuit map lookup; 0 if none)
  trackSlug: string;      // map slug -> <data>/trackmaps/gui/trackmaps3d/<slug>.png
  state: EraRoundState;   // engine states: open (cursor reached it, not locked), upcoming (future), locked (series locked)
}

export interface EraSeries {
  id: string;                     // sd.id (persisted selection key, st.mem_sel)
  era: string;                    // sd.era ("1988")
  year: number;                   // atoi(era) -> color by decade + ordering
  name: string;                   // sd.display (real name, as-is)
  region: string;                 // sd.region
  discipline: string;             // career::DiscForSeries(sd): entry/endurance/touring/single/gt/offroad (raw token)
  level: number;                  // sd.level (required career phase)
  locked: boolean;                // sd.level > frontierPhase
  availableThisWeek: number;      // availNow: number of rounds within ±7 days of the cursor (0 if locked)
  nextRoundNumber: number | null; // featured round (first round with doy>=cursor; if none, the first one); null if there's no calendar
  nextRoundOpen: boolean;         // shows the "Go to calendar" button (open, or available this week)
  rounds: EraRound[];             // the series' calendar, sorted by round
  owned: boolean;                 // Memories: you own a car of the class -> series unlocked
  carPrice: number;               // cheapest buy for the class (heading reads "car from X credits"); 0 if no economy
  discountPct: number;            // active event discount for the class (0-100)
  // ancestor (one per decade, sharing your surname) + historical lore for the series (memory_lore.json).
  ancestorName?: string;          // "Aldo <your surname>"
  // within a generation only the most recent decade is a direct line; the rest are uncles.
  ancestorRelation?: string;      // key: father|grandfather|greatGrandfather|greatGreatGrandfather|uncle|greatUncle|greatGreatUncle|greatGreatGreatUncle -> t('memories.relation.*')
  loreDescEs?: string; loreDescEn?: string;    // historical description (what the series was)
  loreHlEs?: string;  loreHlEn?: string;       // memorable highlight/anecdote
}

export interface MemoriesPayload {
  lineage: string;        // career::CareerIdentityLast() (surname = lineage); "" if none
  frontierPhase: number;  // player's current career phase (for the lock warning)
  giftPending?: boolean;   // F1'67 gift granted and not yet seen -> popup
  giftVehHash?: number;    // gift car (photo)
  giftLivery?: number;     // gift's livery ordinal
  giftClass?: string;      // gift's class
  series: EraSeries[];     // pre-sorted by the engine: year ascending, then display name ascending
}


// ===== Results (query.results) =====
// Results (query.results) ----
// Post-race settlement: event identity + real times (shared memory) + money breakdown
// (the visible lines SUM to the net total) + ELO/reputation/XP/balance progression + championship standings.
// Source of truth for content and its ORDER: dll/career/screen_results.cpp (DrawResults) + results.md.

/** One line of the money breakdown. `key` is an i18n key (results.prize, results.costs, ... or, for
 *  card discount lines, the engine's results_card_disc_* keys). OPTIONAL: INCOME/penalty card lines
 *  don't carry a key, only `label` (brand name + trigger -- dynamic text, not key-translatable).
 *  `amount` carries a sign (negative = deduction). Lines render IN ORDER and sum to `net`. */
export interface ResultsMoneyLine {
  key?: string;
  amount: number;                 // credits (signed)
  tone: 'gold' | 'cyan' | 'danger';
  label?: string;                 // if present, the app renders `label` instead of `t(key)`
}

export interface ResultsMoney {
  lines: ResultsMoneyLine[];
  net: number;                    // net credits (SettleReport.credits_delta)
}

/** Before->after progression for a metric. */
export interface ResultsProgressStat {
  before: number;
  after: number;
  delta: number;
}

/** ELO: besides before/after/delta, the band letter (A..F) and that band's CSS color
 *  (both derived by the engine via career::RatingBand; the app never recomputes them). */
export interface ResultsEloStat extends ResultsProgressStat {
  band: string;                   // "A".."F"
  color: string;                  // CSS color (e.g. "var(--gold)")
}

/** Reputation: the tier's CSS color (market::RepInfoFor); the app never recomputes it. */
export interface ResultsRepStat extends ResultsProgressStat {
  color: string;                  // tier's CSS color
}

export interface ResultsProgression {
  elo: ResultsEloStat;
  rep: ResultsRepStat;
  exp: ResultsProgressStat;       // fixed color (cyan), set by the screen
  balance: ResultsProgressStat;   // fixed color (gold), set by the screen; values in credits
}

export interface ResultsStandingRow {
  name: string;                   // driver's real name
  points: number;
  isPlayer: boolean;              // the player's row (highlighted)
}

export interface ResultsChampionship {
  seriesName: string;
  pointsGained: number;           // points scored THIS race by finishing position (table P[10]=25,18,15,... as in screen_results.cpp); "+N pts" chip
  playerRank: number;             // player's current 1-based rank in the standings
  rows: ResultsStandingRow[];     // top 5 in standings order (points = totals from the real points system)
}

export interface ResultsTiming {
  totalTimeS: number;             // seconds; <=0 => no data ("—")
  bestLapS: number;               // seconds; <=0 => no valid lap ("—")
  gapS: number;                   // pace gap to the class's best lap; <=0.001 => leader (legacy)
  // FINAL gap to your class leader at the FINISH: gapLeaderS = seconds behind the leader (<0 => n/a, or you are the leader);
  // gapLeaderLaps > 0 => lapped by N laps ("+N L"). The screen HIDES this line if you finished 1st (pos===1).
  gapLeaderS: number;
  gapLeaderLaps: number;
}

export interface ResultsPayload {
  pos: number;                    // final position, or the one you retired in
  // P2 (F15): false => retirement; rendered as “DNF (P#)”. Absent (old DLL) => arrival.
  finished?: boolean;
  field: number;                  // grid size
  // Multiclass: when multiclass=true, pos/field are the IN-CLASS values and overall* give the
  // overall result. Absent (older engine build, or a single-class race) => the sub-line isn't rendered.
  multiclass?: boolean;
  overallPos?: number;
  overallField?: number;
  className: string;
  classColor: string;             // CSS color for the class (FindClass(clsId)->color())
  carName: string;
  trackName: string;
  timing: ResultsTiming;
  money: ResultsMoney;
  progression: ResultsProgression;
  championship: ResultsChampionship | null;  // null => standalone race (no championship)
  cardChips?: { text: string; kind: 'fired' | 'idle' | 'expired' | 'lost' }[];
  // Showcase: the REAL sponsor card for the event — the app renders it with <SponsorCardView/>.
  cardShowcase?: { def: CardDefView; level: number; kind: 'fired' | 'idle' | 'expired' | 'lost' }[];
  // the two post-race surfaces. `ask` is the FEEL question (subjective, optional,
  // 5 levels); `offer` is the OBJECTIVE difficulty proposal, which only appears when the safety net trimmed the
  // advantage sample as anomalous. Absent (older engine build) -> neither is rendered.
  feedback?: ResultsFeedback;
}

export interface ResultsFeedback {
  ask: boolean;                 // there's a settled race -> ask the question (consent only governs SENDING it)
  feel: number;                 // 1..5 already answered for this race; 0 = unanswered
  telemetryOptIn: boolean;      // send consent (stated in the card's footer)
  offer: boolean;               // there's a difficulty offer because of an anomalous margin
  gapPct: number;               // measured advantage, as a FRACTION of lap time (+ = the player is faster)
  offerDifficulty: number;      // the one that would apply if accepted
  curDifficulty: number;        // the current one (declining = stay here)
  // (19 Aug): settlement HELD BACK the sample and applied nothing -> not answering leaves the race showing
  // nothing. Absent/false = the step did apply and the offer is only about what fell short.
  held?: boolean;
}

// query.times: lap-time log (TIMES tab; mirrors screen_championships.cpp). One race per
// row with its full grid. Times in SECONDS (<=0 => "—"); date="DD Mon"; pos<=0 => DNF; empty grid
// => pre-feature race (not openable). The app computes leader/gap/fastest-lap the same way DrawTimesDetail does.
export interface TimesGridRow {
  pos: number;                    // dnf/<=0 => "DNF"
  name: string;                   // driver
  className: string;              // raw car_class from shared memory (multiclass)
  classColor: string;             // row's class CSS color
  laps: number;
  bestLapS: number;               // seconds; <=0 => "—"
  totalTimeS: number;             // seconds (sum of laps); <=0 => "—"
  dnf: boolean;
  isPlayer: boolean;
  // false = the driver was still ON TRACK when the settlement was captured (their last lap in progress): laps/total are
  // partial, and with a deficit of d they'll finish d-1 laps down, not d. Absent (older engine build) => treat as false.
  finished?: boolean;
}

export interface TimesRace {
  track: string;
  className: string;              // FindClass(class_id) display name
  classColor: string;             // race's discipline/class color (stripe/dot)
  date: string;                   // "DD Mon" (DoyToDate)
  year: number;
  pos: number;                    // player's position, or the one they retired in
  finished?: boolean;             // P2 (F15): false => retirement (“DNF (P#)”); absent => arrival
  field: number;
  bestLapS: number;               // player's best lap; <=0 => "—"
  totalTimeS: number;             // player's total time; <=0 => "—"
  hasGrid: boolean;               // false => pre-feature race (not openable)
  grid: TimesGridRow[];
}

export interface TimesPayload {
  races: TimesRace[];             // most recent first (RaceLogUiEntries order)
}

// query.tree: progression TREE (discipline x level matrix; mirrors screen_tree.cpp). FLAT list of
// nodes; the app groups them by discipline x level for the matrix and colors disciplines via DISC_META. Navigating to the
// championship on click is a DEFERRED action (cmd.*); vehid is its inert key for now.
export interface TreeRatingTier {
  index: number;   // 0..5 (Unknown..Legend), RepDisplayBand(rep)
  key: string;     // i18n key for the band name ("tree.tier.legend", ...) -> t()
  color: string;   // band CSS color, always via variable ("var(--gold)" / "var(--steel)")
}

export interface TreeNode {
  id: string;                     // node's stable key
  display: string;                // class name (literal, single language; truncated in the card)
  disc: CalendarDiscipline;       // row + color via DISC_META
  level: number;                  // 1..7 (column L1..L7)
  carCount: number;               // 0 => counter hidden
  isDlc: boolean;                 // "DLC" badge
  vehid: number;                  // championship nav key (deferred action)
  locked: boolean;                // level > frontierPhase => dimmed card (cosmetic ONLY)
}

export interface TreePayload {
  frontierPhase: number;          // player's phase: level headers above the current phase are dimmed
  rep: number;                    // raw reputation (not shown; only the band is)
  ratingTier: TreeRatingTier;     // top-right badge
  nodes: TreeNode[];              // order from the source data table; the app builds the discipline x level matrix from it
}

// query.settings: SETTINGS, Difficulty section (mirrors screen_settings.cpp). v1 = Difficulty ONLY (the
// assists/AI/sim/interface sections from the prototype are EXAMPLES; they don't exist in the engine). 100% READ-ONLY:
// selecting a card / ±trim are DEFERRED actions (cmd.settings.*). nameKey/descKey resolved via t().
export type DifficultyMode = 'easy' | 'medium' | 'hard' | 'extreme' | 'adaptive' | 'custom';

export interface SettingsDiffCard {
  mode: DifficultyMode;    // card selected when mode === difficulty.mode
  // 'custom' = difficulty FIXED by hand: it has no meter (meter 0) and its number is difficulty.customAi
  nameKey: string;         // i18n key -> t(nameKey) (diff_easy | diff_medium | ...)
  descKey: string;         // i18n key -> t(descKey) (diff_easy_d | ...)
  meter: number;           // 1..5 lit bars on the meter
  color: string;           // level's CSS color (var(--green|cyan|amber|accent|purple))
  isAuto: boolean;         // Adaptive only (AUTO badge + trim row)
}

// per-class difficulty, revision 2: one row per category with history, for the number's hover.
// `delta` is no longer its own layer (that model was retired: it oscillated) -- it's how far `difficulty` sits from
// the save's OVERALL value, so the number can be explained. `seeded` = the community set this number and
// you haven't raced that category yet. No shape change: same payload as the previous revision.
export interface SettingsDiffClass {
  classId: string;
  // panel redesign (19 Aug): identity for DISPLAY. `classId` is still the key (controls and the save
  // key off it); these two exist because a column of raw ids doesn't read as a list of categories.
  // Optional: an older engine build doesn't send them and the row falls back to classId and the accent color.
  className?: string;
  classColor?: string;
  difficulty: number;
  delta: number;
  races: number;
  seeded: boolean;
  locked?: boolean;    // per-class difficulty lock: this category's OWN flag; the effective value is lockAll || locked
  // 09/09 (, 4): visible saturation (+1 = at the ceiling and you are still faster, -1 = at the floor and you are still
  // slower, 0 = nothing) and the category's wet number (0 = not yet available). Optional: an older engine does not send them.
  saturation?: number;
  wetDifficulty?: number;
  racesWet?: number;
}

export interface SettingsDifficulty {
  mode: DifficultyMode;    // active mode (highlighted card)
  trim: number;            // manual adaptive trim (-5..+5)
  trimMin: number;         // 5 (bound to enforce once the write command ships)
  trimMax: number;         // +5 (bound to enforce once the write command ships)
  cards: SettingsDiffCard[]; // 6 cards, fixed engine order (4 bands + adaptive + fixed by hand)
  // Per-class difficulty rows are ordered by races-run descending. Deliberately optional: an older engine build doesn't send this and the hover isn't rendered.
  classes?: SettingsDiffClass[];
  lockAll?: boolean;   // The master difficulty lock. Optional: an older engine build doesn't send this and no locks are rendered.
  // the REAL AI difficulty number for ADAPTIVE mode (the AMS2 slider). aiDifficulty = the base value learned by the adaptive system;
  // aiEffective = clamp(base + trim, aiMin, aiMax) = the value actually SET in the race. Informational only in Adaptive mode.
  aiDifficulty: number;
  aiEffective: number;
  aiMin: number;
  aiMax: number;
  // 'custom' mode (difficulty FIXED by hand): the slider number that ALWAYS races as-is, unmoved by the
  // category level, the trim, or class/track offsets. Same range as aiMin..aiMax. Sent via
  // cmd.settings.setCustomAi { value }, and it's sent even when a different mode is active (it's a saved preference).
  // Absent = an engine build from before this card existed -> the screen doesn't render it.
  customAi?: number;
}

export interface SettingsPayload {
  difficulty: SettingsDifficulty;
  // AI aggression. value: 0 = auto (scales by career phase) | 40/60/80/100 = fixed override.
  aggression?: { value: number };
  // toggles for automatic scaling of Gameplay multipliers (wear / consumption).
  gameplayScaling?: { tyre: boolean; fuel: boolean };
  // FCY/safety car — legacy 0/1 (older engine build; "Inherited" arrives as true).
  fcyOn?: boolean;
  // FCY tri-state: 0=No, 1=Yes, 2=Inherited. Absent = older engine build (derive it from fcyOn).
  fcyMode?: number;
  // WEATHER. mode 0 = random (generated over the full catalog), 1 = inherited
  // from your Custom Race (the mod doesn't write the slots), 2 = rotation over YOUR selection. weights is indexed by the
  // AMS2 catalog code (the same one used by wxname_*), and a 0 means "that weather never occurs"; defaults are the factory
  // weights, so the UI can show where it started from. Absent (older engine build) -> the block isn't rendered.
  weather?: { mode: number; weights: Record<string, number>; defaults: Record<string, number> };
  // 21/07: race PACE (reward mode). Chosen when creating the save, in the Play window, and since
  // 0.7.0 changed in Settings with cmd.settings.setReward { mode: 'easy'|'normal'|'hard' }. It is NOT `difficulty`
  // (that's the on-track AI). mode: 0=easy 1=normal 2=hard; mult = money/reputation/XP multiplier.
  // Absent on an older engine build -> the row isn't rendered.
  reward?: { mode: number; mult: number };
  // AUTO/Inherited per field. true = INHERITED: the mod does NOT write that part of the config when
  // launching calendar races, and whatever the player has set in their Custom Race applies instead. Changed via
  // cmd.settings.setInherit { field, inherit } with these same tokens. Absent (older engine build) -> not rendered.
  inherit?: SettingsInherit;
  // Race telemetry submission consent. telemetryAsked=false => the question hasn't been
  // asked yet: the app requests consent the first time, next to the Play button. Absent (older engine build)
  // -> no toggle, no dialog.
  telemetryOptIn?: boolean;
  telemetryAsked?: boolean;
  // (2 Aug): the level the CALIBRATION race will run at, and whether that race already happened. While
  // calibrated is false the player can choose aiSeed (cmd.settings.setSeed); afterwards the number is learned by
  // the adaptive loop and the control disappears. Absent (older engine build) -> the screen uses the recommended 80.
  aiSeed?: number;
  calibrated?: boolean;
}

// stable contract tokens (same order as the engine's kInh* bits).
export interface SettingsInherit {
  weather: boolean;    // REMOVED (engine v5): always arrives as false and can't be set — weather is now inherited
                       // via the Settings > Weather mode (cmd.settings.setWeather {mode:1}). It stays in the
                       // contract because the engine's bits are POSITIONAL; Settings no longer renders this row.
  hour: boolean;       // start time
  timeProg: boolean;   // time progression (time-of-day acceleration)
  start: boolean;      // start type (standing/rolling)
  pit: boolean;        // pit rules (mandatory stop, refueling, minimums, pit speed limit)
  ai: boolean;         // AI difficulty (skill/aggression/mistakes/wet-weather)
  sessions: boolean;   // practice/qualifying: duration and on/off
}

// (multi-profile): query.profiles. Each profile is an independent career (its own save in the engine).
// mode: "career" (default) | "sandbox" (everything unlocked + 10M credits on creation).
export interface ProfileEntry {
  slug: string;
  name: string;
  mode: 'career' | 'sandbox' | string;
}
export interface ProfilesPayload {
  active: string;             // active profile's slug
  profiles: ProfileEntry[];
}

// query.garage: GARAGE (mirrors screen_garage.cpp). Owned cars + active-contract strip +
// rating band + credits. 100% READ-ONLY (no selling: cars are "Collectible"; buying lives in
// the vehicle/pre-race screen). The app derives ownedCount/classCount from cars[]. The DDS thumbnail isn't portable -> a ghost placeholder.
export interface GarageCar {
  id: string;
  name: string;
  classId: string;
  className: string;      // "" if the class is unknown (the app renders "—")
  classShort: string;
  classColor: string;     // discipline CSS color (#hex/var)
  year: number;
  stats: { pwr: number; grp: number; bal: number };   // 0..100
  vehHash: number;        // image key (ghost placeholder in the app)
  livery: number;         // ordinal (-1 = default)
  slug: string;           // car PNG slug (same as pre-race) — "" if unknown
  // 0.7.0 (car selling): accumulated km, what you get for selling it, and why it can't be sold ('ok' = it can).
  // Optional: an older engine does not send them and the card is left without the sale row.
  km?: number;
  sellPrice?: number;
  sellBlock?: GarageSellBlock;
}
export type GarageSellBlock = 'ok' | 'pending_race' | 'contract' | 'gift' | 'no_value' | 'not_owned';
export interface GarageSellResult { ok: boolean; carId: string; reason: GarageSellBlock; paid: number; }
export interface GarageContract {
  series: string;
  seasonsLeft: number;
  seasonsTotal: number;   // "Season N/M": N = seasonsTotal - seasonsLeft + 1 (clamp 1..total)
  selfFunded: boolean;    // true -> con_money_self ; false -> "+Cr {perRace}/race"
  perRace: number;
  roleLead: boolean;      // con_role_lead / con_role_team
  objDone: number;
  objTotal: number;
  objTargetPos?: number;  // season target (added in 0.4.0): "finish Top N", required by the team (0/absent = no target)
  renewOnTable?: boolean; // silly season (0.4.0): an early renewal waiting in the inbox -> chip on the strip
  fixesCar: boolean;      // chip car_fixed_by_team + teamCar
  teamCar: string;
  typeLabel: number;      // 0..5 -> t(`ctype_${typeLabel}`)
  coverage: { seatEntry: boolean; consumables: boolean; damage: boolean; travel: boolean };
}
export interface GaragePayload {
  credits: number;
  tier: { band: number; token: string; color: string };   // token -> t(`garage.tier.${token}`)
  contract: GarageContract | null;                          // null => no active contract
  cars: GarageCar[];                                        // order from the save (ownership)
}

// ===== Sponsor Cards (query.cards) =====
// Sponsor Cards (query.cards); engine: career_cards.* ----
export interface CardBiText { en: string; es: string; }
export type CardRarityKey = 'local' | 'regional' | 'national' | 'international' | 'elite';
export interface CardDefView {
  id: string; name: string;
  tag: CardBiText;
  rarity: number;                    // 0..4 = local..elite
  rarityKey: CardRarityKey;
  life: { k: 'perm' | 'races' | 'uses'; n: number };
  trigger: string;                   // 'passive'|'win'|'podium'|'top8'|'clean'|'damagefree'|'tyrewear'|'fueluse'|'endur'
  fx: { hook: string; v: number };   // hooks: see the header for the full list
  fxExtra?: { hook: string; v: number }[];   // catalog v2: 0-2 EXTRA effects, same vocabulary
  clause: { kind: 'dnf_fine' | 'notop10_lose'; fine: number; races: number } | null;
  special: { id: string; es: string; en: string } | null;
  // v2/v3: magnitude of the main effect at N2/N3 (0.7.0, ; optional for old fixtures)
  upgrade: { after: number; cost: number; levels: number; v2?: number; v3?: number } | null;
  cost: number;                      // credits price in the shop
}
export interface CardUpgradeNextView { toLevel: number; cost: number; need: number; have: number; ready: boolean; }
export interface SignedCardView {
  def: CardDefView;
  level: number;
  slot: number;                      // 1 = bench; 0..5 active slot
  racesActive: number; racesLeft: number; usesLeft: number; clauseMiss: number;
  invested: number; sellValue: number;
  fxV: number;                       // effective magnitude at the current level
  upgradeNext: CardUpgradeNextView | null;
}
export interface CardShopView {
  raceId: string; rerolls: number; bought: number;
  maxBuys: number; offerCount: number; rerollCost: number;
  offers: CardDefView[];             // already carries the full definition + cost
  boughtIds: string[];
}
export interface CardsPayload {
  signed: SignedCardView[];
  shop: CardShopView | null;         // null = no pending shop offer
  slotsUnlocked: number;             // 2..6 depending on career phase
  phase: number;
  money: number;                     // current credits (for visual gating)
}

// query.market: DRIVER MARKET (mirrors screen_market.cpp). Reputation + stats + contract banner +
// dynamic offers. Composite strings from the engine (title/lockReason/expectation) travel as an {en, es} pair
// (the app picks by locale). Sign/decline/negotiate are write commands (cmd.*). Empty offers (rookie) are NOT an error.
export interface MarketTierDot { color: string; filled: boolean; }
export interface MarketNextTier { tierKey: string; min: number; gap: number; }
export interface MarketReputation {
  score: number; tierIdx: number; pct: number;
  tierKey: string; tierColor: string;
  next: MarketNextTier | null;
  dots: MarketTierDot[];
}
export interface MarketStats { wins: number; podiums: number; races: number; }
export interface MarketContract {
  count: number;
  teamName?: string; seriesName?: string; kind?: number;   // count===1
  seriesNames?: string[];                                    // count>1
}
export interface MarketOfferWhy { rating: number; form: number; rel: number; }
// team-to-team relationships of the offering team (reputation graph). kind: family|ally|rival|bitter.
export interface MarketOfferRelEdge { team: string; kind: string; }
export interface MarketOfferRel {
  allies: MarketOfferRelEdge[];
  rivals: MarketOfferRelEdge[];
  pathways: { team: string; prestige: number }[];
}
export interface MarketOfferIncludes {
  typeLabel: number; seatEntry: boolean; consumables: boolean; damage: boolean; travel: boolean;
}
export interface MarketOffer {
  id: string;
  category: 'direct' | 'open';
  state: number;             // 0 undecided / 1 accepted / 2 declined
  held: boolean;
  direct: boolean;
  team: string;              // raw; "seat_privateer_entry" -> t(team)
  from: string;
  title: string; titleEn: string;
  teamColor: string;
  className: string; clsId: string;
  discColor: string;
  scope: string;             // token -> t("scope_"+scope)
  commitment: number;
  contractKind: number;      // 0..4 -> t("market.ckind."+n)
  contractKindIcon: string; contractKindColor: string;
  requiresTryout: boolean;
  canAfford: boolean;
  reqRep: number;
  displacement: boolean;
  buyIn: number;
  expectationKey: string;    // "market.expect.*"
  expectation: string; expectationEn: string;
  pitchKey: string; pitchPrestige: boolean;
  why: MarketOfferWhy;
  signBonus: number; perRace: number; seatName: string;
  includes: MarketOfferIncludes;
  lockReason: string; lockReasonEn: string;
  // item 5 (contract): preview of the FIXED car+skin the team assigns (works/customer). fixesCar=false -> the
  // player chooses (privateer): no preview. teamCarSlug + teamLiveryNN -> a real thumbnail of the assigned livery.
  fixesCar?: boolean; teamCarName?: string; teamCarSlug?: string; teamLiveryNN?: number;
  conflicts: string[];
  // academy offer: narrative archetype of the offer, plus the real parent driver if it comes from an affiliated academy ("" if not).
  archetype: number;
  parentTeam: string;
  // team-to-team relationships of the offering team (graph). Optional (absent on older offers/fixtures).
  rel?: MarketOfferRel;
  // offer_lifecycle: weeks remaining until the offer expires. Absent on older offers/fixtures.
  weeksLeft?: number;
  // 18/09: days until it can no longer be signed (TTL, negotiation or market close); 1 = today is the last.
  daysLeft?: number;
  // negotiation v2: can this offer be negotiated? plus the thread's state/deadline if one exists (mirrors NegoThreadState).
  negotiable?: boolean; negState?: number; negRespondDays?: number;
  // who you beat for the seat. Empty/absent when the seat wasn't contested (there's always room at your level and below,
  // or there was nobody else in the running). rivalId is the roster id, rivalName is the display name.
  rivalId?: string; rivalName?: string;
}
export const ARCH_ACADEMY_PROMOTION = 9;   // academy promotion offer

// the reason isn't generic. Each `why` bar has a different NEUTRAL baseline -> comparing raw magnitudes always
// biases toward "form" (form=70 is the default with no data; rel=50 means "no relationship"; rating is a logistic curve centered ~50).
// So we compare the EXCESS over each baseline and pick the largest; if nothing stands out, it falls back to "they see potential in you" (avoiding
// the false "your recent form won them over" on a rookie's very first offers). Pure and deterministic.
export function whyTheyWantKey(o: MarketOffer): string {
  if (o.archetype === ARCH_ACADEMY_PROMOTION) return 'market.why.promotion';
  const { rating, form, rel } = o.why;
  const cand: Array<[number, string]> = [
    [rel - 50, 'market.why.relationship'],   // baseline 50 = no relationship (REL_BASE)
    [form - 70, 'market.why.form'],           // baseline 70 = neutral with no results yet (MK_FORM_NEUTRAL)
    [rating - 55, 'market.why.pace'],         // baseline ~55 = the middle band of the rating logistic curve
  ];
  cand.sort((a, b) => b[0] - a[0]);
  if (cand[0][0] < 8) return 'market.why.potential';   // nothing stands out above its baseline -> "they see potential in you"
  return cand[0][1];
}
// query.hub: HOME (mirrors screen_hub.cpp). Driver card + next race + badges + lifecycle.
export interface HubPayload {
  identity: { first: string; last: string };
  reputation: {
    score: number; tierIdx: number; pct: number; tierKey: string; tierColor: string;
    next: { tierKey: string; min: number; gap: number } | null;
    followers: number;
  };
  stats: { wins: number; podiums: number; races: number };
  nextRace: { series: string; track: string; date: string; weekday?: string; round: number; doy: number } | null;
  badges: { directOffers: number; unread: number };
  // breakFee/contractTeams: the engine still sends them (the TOTAL cost of breaking every contract, plus
  // the active team count), but as of 19 Aug the app doesn't use them: the hub's "break contract" button was removed by
  // user decision ("it broke everything at once and caused confusion") — terminating a contract now lives ONLY in Paddock -> My contracts.
  lifecycle: { enrollmentCount: number; contractActive: boolean; contractCount?: number; contractSeries?: string; breakFee?: number; contractTeams?: string[]; cursorDate: string; weekday?: string; year: number };
  progress: { ownedCount: number; completedRaces: number };
  // item 16c: unified rating (ELO) + delta + phase progression (XP) + latest results.
  rating?: { elo: number; delta: number; xpToward: number; xpThreshold: number; phase: number };
  // P2 (F15): explicit `finished` (false = retirement, `pos` = where you got out). Absent => arrival.
  recentResults?: { pos: number; field: number; track: string; finished?: boolean }[];
}

// query.negotiate: contract NEGOTIATION (screen_negotiate.cpp), parametrized by {offerId}. found=false ->
// the app returns to the Market. negotiation v2: the evaluation lives in the engine (NegoEvaluate); the app only shows the
// agent's band (patienceBand/budgetLo/budgetHi) and the thread's state (negState/negRound/table/history) exactly
// as the engine serves them. Demand/tier text arrives as {en, es} pairs.
export interface NegoDemandRow { demandId: number; en: string; es: string; descEn: string; descEs: string; cost: number; positive: boolean }
export interface NegoSalaryPkg { pct: number; cost: number }
// cardId: card_id of the sponsor card behind this chip ("" for
// the "None offered" chip). Returned as-is in terms.sponsorCardId on acceptance -- identifies the card being GIVEN UP
// for the engine to consume (MarketUiAccept removes it from the wallet if the signing succeeds).
export interface NegoSponsorChip { tierRank: number; name: string; tierEn: string; tierEs: string; color: string; delta: number; cardId: string }
// negotiation v2: the thread's current terms table (the player's proposal, or the engine's counteroffer).
export interface NegoTableView { demands: boolean[]; salaryPct: number; sponsorTierRank: number }
// negotiation v2: one history row for the thread (one entry per turn).
export interface NegoHistoryRow { day: number; who: number; kind: number; demandsMask: number; salaryPct: number }
// Thread states (mirrors the engine's NegoThreadState)
export const NEG_WAITING = 0, NEG_COUNTERED = 1, NEG_ACCEPTED_PENDING = 2, NEG_ULTIMATUM = 3;
export const NEG_SIGNED = 10, NEG_WITHDRAWN = 11, NEG_PLAYER_WALKED = 12;
export interface NegotiatePayload {
  found: boolean;
  offerId: string;
  teamName?: string; teamColor?: string;
  rating?: number; relationship?: number;
  startBonus?: number; startPerRace?: number; startDurationSeasons?: number; startProtectionNone?: boolean;
  negotiationLevel?: number; competing?: number;
  credits?: number;
  demands?: NegoDemandRow[];
  salaryPackages?: NegoSalaryPkg[];
  sponsorChips?: NegoSponsorChip[];
  initialSalaryPct?: number; initialSponsorTierRank?: number;
  // negotiation v2: server-authoritative negotiation thread (NegoEvaluate lives in the engine).
  negotiable?: boolean;
  negId?: string;
  negState?: number;
  negRound?: number;
  patienceBand?: number;
  budgetLo?: number; budgetHi?: number;
  tableIsCounter?: boolean;
  table?: NegoTableView;
  respondInDays?: number;
  deadlineWeeks?: number;
  deadlineDays?: number;              // 18/09: days until the offer can no longer be signed; 1 = last day
  history?: NegoHistoryRow[];
}

// query.season: CHAMPIONSHIPS master-detail (screen_season.cpp), parametrized by {seriesId}. Rail (3 sections)
// + detail that changes by kind (1=enrolled grid / 2=superlicense access / 3=info) + end-of-season bonus modal.
export interface SeasonRailItem {
  seriesId: string; display: string; classShort: string; className: string;
  discColor: string; section: number; gateLocked: boolean; level: number; selected: boolean;
  playerPos: number; myPoints: number; roundIdx: number; totalRounds: number; field: number; done: boolean;
}
// time/duration/sessions travel along for the calendar of a championship you're NOT racing yet — there, what's
// being decided is whether it fits you, and a 24h round is not the same commitment as a sprint. laps XOR durationMin.
export interface SeasonRoundCol { round: number; trackName: string; date: string; doy: number;
                                  hour: number; laps: number; durationMin: number; practiceMin: number; qualifyMin: number }
export interface SeasonRoundCell { round: number; pos: number; points: number; dnf: boolean; ran: boolean }
export interface SeasonStandRow { rank: number; name: string; isPlayer: boolean; total: number; cells: SeasonRoundCell[] }
export interface SeasonDetail {
  kind: number;   // 0=none 1=enrolled 2=access 3=info
  seriesDisplay: string; roundIdx: number; totalRounds: number; seasonYear: number; myPos: number; myPts: number;
  pointsScale: number[]; rounds: SeasonRoundCol[]; standings: SeasonStandRow[]; nextRaceDoy: number;
  qualDisplay: string; qualFirstDate: string; qualHasCal: boolean;
  hasFeeder: boolean; feederClass: string; feederTop: number; feederHasContext: boolean; feederContextPos: number; feederContextSeries: string;
}
export interface SeasonPayload {
  playerName: string; selectedSeriesId: string;
  rail: SeasonRailItem[];
  detail: SeasonDetail;
  bonusShown: boolean; bonusPlayerPos: number; bonusField: number; bonusMoney: number; bonusXp: number; bonusRep: number;
}

// query.prerace: PRE-RACE / vehicle selection (screen_vehicle.cpp), param {carId?}. Reads the armed state
// (st.pending/launchEp/prerace from SampleState). Duration % and livery are chosen client-side and passed to
// cmd.prerace.launch. Car/track images never cross the wire (placeholder by vehHash+NN). clsEmpty -> "pick a race".
export interface PreraceCar {
  id: string; name: string; classShort: string;
  year: number; pwr: number; grp: number; bal: number;
  price: number; rentPct: number; vehHash: number; factory: boolean; owned: boolean; discColor: string;
  slug: string;   // image slug -> <data>/vehimages/gui/vehicleimages/vehicleimages_<slug>/<slug>_livery_NN.png
}
export interface PreracePayload {
  clsEmpty: boolean;
  raceName: string; className: string; discColor: string;
  trackName: string; trackHash: number; laps: number; durationMin: number; grid: number; reward: number;
  // 19/09: special event armed -> credits ALREADY collected in it. A better finish pays the DIFFERENCE up to what
  // it is worth; an equal or worse one, nothing. Optional: an old engine does not send it (and 0 = you have not collected anything yet).
  eventPaid?: number;
  reasonKey: string; lockCar: boolean; lockSkin: boolean; lockDuration: boolean;
  presetVehHash: number; presetLivery: number; presetDurPct: number;
  // v2: >0 = "locked in" with a preset THIS season; changing identity mid-season costs this
  // (warning shown next to Launch). Renting doesn't cost reputation: the privateer rule punishes switching, not renting.
  divorceRep: number; divorceCr: number;
  ratingTierLabel: string; ratingTierColor: string; credits: number; visual: boolean;
  seriesId: string; freeEntry: boolean; raceLaps: number;
  // tire wear and fuel consumption the mod will apply at EACH duration %, computed by the engine with the
  // same functions it uses when writing them into the game. Optional: an older engine build doesn't send these and the screen
  // simply doesn't render the line. `wearScale*` off = that axis isn't scaled, and the player's own setting is respected.
  wearByPct?: { pct: number; tyre: number; fuel: number }[];
  wearScaleTyre?: boolean; wearScaleFuel?: boolean;
  isMemory?: boolean;             // Memories: an era series -> buy-to-own (no renting)
  cars: PreraceCar[];
  selCarId: string; selVehHash: number; selIsOwned: boolean; selFactory: boolean;
  liveryNN: number[]; liveryCount: number; ownedLiveryOrdinal: number;
  ownedLiveries?: number[];       // Memories, one livery per unit: livery ordinals owned for the selected car (car+skin units)
  cost: { tyres: number; fuel: number; fluids: number; insurance: number; total: number };
  entryFee: number; inscDue: number; travel: number; coverAmt: number; rentCost: number;
  outlayRent: number; outlayOwn: number; totalRent: number; totalOwn: number; factorySalary: number;
  canBuy: boolean; canRent: boolean; canRaceOwn: boolean; seatOk: boolean;
  launchLaps: number; launchDurationMin: number;
  // item 1: REAL specs for the selected car (AMS2's VehicleDetails). Empty strings = unavailable ->
  // the card falls back to the PWR/GRP/BAL bars. Optional (an older fixture/mock without the field still type-checks).
  specs?: { drivetrain: string; enginePos: string; engineType: string; topSpeed: string; horsepower: string; weight: string; torque: string };
}

// query.tryout: trial-as-race TRYOUT (screen_tryout.cpp). Armed from the Market (an offer with requiresTryout) and
// polled while it runs (hasResult flips to true on settlement). BINARY verdict: pass = pos<=topN.
export interface TryoutStake { key: string; en: string; es: string; color: string }
export interface TryoutPayload {
  armed: boolean;
  clsId: string; className: string; discColor: string;
  trackName: string; trackHash: number; team: string; topN: number;
  targetEn: string; targetEs: string; standardEn: string; standardEs: string;
  stakes: TryoutStake[];
  hasResult: boolean; pass: boolean; pos: number; field: number; resultTopN: number;
}

// query.rollover: end-of-season review (screen_rollover.cpp). NOTE: retirements/promotions/marketMoves plus
// yourTeam/yourClass/nextYear are still STUBS in the engine today; renewal + insurance are LIVE data. Confirm is still a stub.
export interface RolloverRetire { name: string; team: string; age: number; reasonEn: string; reasonEs: string }
export interface RolloverPromo { name: string; fromEn: string; fromEs: string; toEn: string; toEs: string }
export interface RolloverMove { driver: string; from: string; to: string; color: string }
export interface RolloverPayload {
  nextYear: number;
  retirements: RolloverRetire[];
  promotions: RolloverPromo[];
  marketMoves: RolloverMove[];
  /** 18/09: how many signings there were in the winter (marketMoves is the sample). Old engine: absent. */
  movesTotal?: number;
  yourTeam: string; yourClass: string;
  hasRenewal: boolean; renewState: string; renewPerRace: number; renewSeries: string; renewOfferId: string;
  insurancePremium: number; cleanliness: number;
  // 17/09: the winter moves a hundred-odd people and the screen fits eight, so the engine sends a
  // SAMPLE in the lists and the TOTAL separately. reportYear 0 = this save has not gone through a winter yet,
  // and then the lists come back empty on purpose (they used to bring two names hand-written in the engine).
  retiresTotal: number; promosTotal: number; reportYear: number;
}

// query.race_config: config of the LAST launch (a DEBUG tool). The engine snapshots it inside
// CareerApplyEvent after resolving AUTO/Inherited and before writing anything: it's "what the race would have been
// configured with." Used by the SIMULATION mode's coverage, where there's no game to look at.
// ENGINE SENTINELS, left untranslated (what's being audited is which slot gets written, and with what value): <0 — and 0 in the
// "0 = don't force" fields (laps/durationMin/opponents/skill/aggression/timeProg/numClasses; 19/09 mistakes no longer:
// its 0 is "off" and is written, the sentinel is -1)— means
// the mod does NOT write that slot and the player's own Custom Race setting stands. `inherited` says which fields are like
// that by a deliberate Settings choice. With no launch this session, the engine responds not_ready.
export interface RaceConfigPayload {
  sim: boolean;                    // the captured launch was simulated (nothing reached the game)
  raceId: string;                  // "series_id#round" ("tryout"/"" for tryouts/standalone races)
  seriesName: string; trackName: string;   // "" = not resolvable from the catalog
  vehHash: number; trackHash: number; livery: number;
  laps: number; durationMin: number;       // laps XOR durationMin
  opponents: number; oppType: number; numClasses: number;
  ai: { skill: number; aggression: number; mistakes: number; wetSkill: number };
  startHour: number; timeProg: number;     // timeProg 1..4 = real/x2/x5/x10
  weather: number[];               // 4 states (AMS2 catalog, see eventHelpers); -1 = not set
  weatherSlots: number;            // 0 = number of AUTO states by duration; 1..4 = forced
  startType: number;               // 0 grid / 1 launched
  formationLap: number;            // ALWAYS -1 (this launch slot is never touched)
  pit: { mandatory: number; minTyres: number; minFuel: number; refuelling: number; speedLimit: number };
  fcy: number;                     // REAL Settings value: 1/0 = Yes/No forced, -1 = Inherited (not touched)
  sessions: { practiceOn: number; qualifyOn: number; practiceMin: number; qualifyMin: number };
  date: { day: number; month: number; year: number };
  phase: number; trial: boolean; trialTopN: number;
  // wear/consumption: NOT part of the handoff, but GLOBAL game options the mod writes at launch time
  // (scaled to the race length) and restores when it ends. mult 0 = not scaled -> your own setting stands
  // globally; idx = the game's target index (lower = more wear); orig = your original value, restored afterward.
  // armed=false = this launch doesn't write them (simulation: nothing actually runs).
  wear: {
    armed: boolean; laps: number;
    tyre: { scale: boolean; mult: number; idx: number; orig: number };
    fuel: { scale: boolean; mult: number; idx: number; orig: number };
  };
  honorInherit: number;            // 0 forces everything / 1 calendar / 2 heroic gate (doesn't inherit AI)
  inherited: { weather: boolean; hour: boolean; timeProg: boolean; start: boolean; pit: boolean; ai: boolean; sessions: boolean };
}

// query.identity: career-creation GATE (screen_identity.cpp). needed=true -> the app renders ONLY this screen
// (no topbar/tabs). g_save.player.* fields, raw (0/"" = unfilled). The country catalog is app-owned.
export interface IdentityPayload {
  needed: boolean;
  amsName: string;                 // prefilled from shared memory ("" if none)
  startYear: number;               // age anchor (kStartYear)
  first: string; last: string; sex: string; nationality: string;
  birthYear: number; birthMonth: number; birthDay: number;
  age: number;
}

export interface MarketPayload {
  reputation: MarketReputation;
  stats: MarketStats;
  contract: MarketContract | null;
  seekingYouCount: number;
  openSeatsCount: number;
  playerRating: number;      // gate: repOk = direct || playerRating >= reqRep
  offers: MarketOffer[];     // order: directs (not declined) first, then opens by reqRep ascending
  // seats WATCHING you. NOT offers yet — you came close to being the top candidate, and
  // the team decides within racesLeft races. Kept as a separate list so these don't read as seats already won.
  // Absent on older payloads/fixtures.
  watching?: MarketWatch[];
  // the market runs on a SCHEDULE: outside the signing window there are no offers and nothing gets signed. `daysToWindow`
  // turns an empty screen into a dated wait instead of something that looks broken.
  // Absent on older payloads -> assumed open, matching the previous behavior.
  marketOpen?: boolean;
  daysToWindow?: number;
}

export interface MarketWatch {
  seriesId: string;
  seriesName: string;
  teamName: string;
  rivalName: string;   // "" if there was no one else in the running
  racesLeft: number;   // races remaining IN THAT CLASS before they decide
}

// cmd.skins.status: catalog of recommended skin resources (packs OR standalone
// liveries) + which ones are DETECTED as installed in the game. Source: docs/skins/recommended_skins.json,
// read by the engine (CmdSkinsStatus in career_api_queries.cpp), enriched with live detection via overrides.
// Invoked via client.query<SkinsStatus>('cmd.skins.status') — the engine dispatches by name (kind is
// orthogonal to that), so the mock serves it from FIXTURES like any other read. Unit = RESOURCE: a pack with 1
// entry is presented as a single livery. detected/detected_entries come from scanning the installed overrides.
export interface SkinPack {
  slug: string;
  title: string;
  url: string;                 // its lantern_series_8801 page (the INSTALL button opens this)
  author: string;
  series: string[];            // class_name(s) it covers (e.g. ["Onyx_Cup_4824","Pemberton_Zel121"])
  season_year: string;         // "2023" | "1987-1991" | "various" | ...
  entries_total: number;       // number of liveries in the resource (1 = standalone livery; >1 = pack)
  detected: boolean;           // >=1 entry with the skin actually installed
  detected_entries: number;    // number of the resource's entries detected
  sample_vehicle: string;      // first entry -> car preview in the app
  sample_livery_id: number;    // livery_id of the first entry (-1 if none)
}
export interface SkinsStatus {
  ok: boolean;
  installed_overrides: number; // total number of (slug, livery_id) slots with a skin installed
  packs: SkinPack[];
}

// cmd.skins.installed: EVERY skin slot actually installed in the game (the ground
// truth on disk, catalog or not), with the relative path of its PREVIEWIMAGE. The app passes these to
// bridge.provision.syncOverridePreviews to convert DDS to PNG in the background and show the real skin.
export interface InstalledSlot {
  slug: string;
  livery_id: number;
  name: string;
  preview_rel: string;    // relative path of the override's <PREVIEWIMAGE PATH> ("" if the override doesn't declare one)
}
export interface InstalledSkins { ok: boolean; slots: InstalledSlot[] }

// noShows (clock with stops): ENROLLED rounds that would fall behind the stop, i.e. those that
// would count as a no-show if you advance up to there. Sent by query.time.preview / cmd.time.advance
// inside TimeAdvancePayload (below).
export interface TimeJumpNoShow {
  seriesId: string;
  className: string;
  seriesName: string;
  round: number;
  trackName: string;
  date: string;
  weekday: string;
  year: number;
  absDay: number;
}

// query.time.preview / cmd.time.advance / cmd.time.rules (Task C1, spec ): the single clock with stops.
// It is the ONLY time-advance contract of the app: the old "jump" pair (preview + execution per day
// target, with its double-confirmation dialog) died in T6, because the popup IS the review and confirming
// the same thing twice added nothing.
// Params evaluated by the engine IN THIS ORDER (ResolveTimeTarget, pinned by test): { seriesId } -> until the
// next event of that series; { untilAbsDay } -> until EXACTLY that day (used by "Stop here"; negative =
// bad_params, 0 = no cap); { scope: 'available'|'enrolled' } -> until the next available or
// enrolled event; {} (no params) -> until the next stop according to the active rules.
// resolved:false = no reachable target; the app must not offer to advance, only to close.
export interface TimeStopRules {
  offers: boolean;
  myRaces: boolean;
  anyRace: boolean;
  mail: boolean;
  relation: boolean;
}
export interface TimeDay {
  absDay: number;
  year: number;
  doy: number;
  date: string;
  weekday: string;
}
export type TimeEventKind =
  | 'race_mine' | 'race_other' | 'mail' | 'offer' | 'offer_expired' | 'relation'
  | 'decision' | 'rollover' | 'year_end' | 'cap' | 'until' | 'noshow'
  // review 0.7.0 (): eve of the close with offered unsigned seats (stops under the offers rule) and
  // the day they are lost (red line, does not stop)
  | 'seats_last_day' | 'seats_lost'
  // 18/09: eve of a normal market offer expiring (stops under the offers rule)
  | 'offer_last_day';
export interface TimeDayEvent extends TimeDay {
  kind: TimeEventKind;
  label: string;
  params: Record<string, string>;
  seriesId: string;
  stops: boolean;
}
export interface TimeAdvancePayload {
  ok: boolean;
  resolved: boolean;
  executed: boolean;
  today: TimeDay;
  stop: TimeDay & { reason: TimeEventKind | '' };
  days: number;
  crossedYear: boolean;
  crossed: TimeDayEvent[];
  noShows: TimeJumpNoShow[];
  rules: TimeStopRules;
}
export interface TimeAdvanceParams {
  untilAbsDay?: number;
  seriesId?: string;
  scope?: 'available' | 'enrolled';
  // review 0.7.0: only resolves the target, without simulating; of the payload only `resolved` matters (the Rollover CTA)
  resolveOnly?: boolean;
}
