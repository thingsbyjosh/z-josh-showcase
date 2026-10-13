// App shell: TopBar with its tabs + router.
// Home wired up; the rest of the screens are ported later (placeholder for now).
import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, CareerStatePayload, IdentityPayload, GameStatePayload, CardsPayload, InstalledSkins, SettingsPayload } from '../api/contracts';
import { autoSwitchEnabled, simRaceEnabled, launchMode, isVrLaunch, lowRamMode, contentLogVerbose, setContentLogVerbose, telemetryChoice, setTelemetryChoice,
  uiScale, setUiScale, UI_SCALES, vrOverlayEnabled } from '../ui/prefs';
import { flushTelemetryQueue } from '../telemetry/queue';
import { postRaceTelemetry, setModVersion } from '../telemetry/raceFeedback';
import { machineHash } from '../account/accountStore';
import { useGamepadNav } from '../game/useGamepadNav';
import { saveLocale, type AppConfig } from '../config/appConfig';
import { useQuery } from '../api/useQuery';
import { useGameSession, pushEntitlements, closeAppGracefully, shouldTakeoverDesktop } from '../game/useGameSession';
import { shouldOfferGridRestart } from '../game/gameSession';
import { shouldShowResults, shouldGiveUpAsAbandoned, GAME_GONE_TICKS } from '../game/resultsGate';   // Results waits for the main menu   // the "restart the game" bar is decided separately
import { onAccountTierChange } from '../account/accountEvents';
import { useUpdater } from '../updater/useUpdater';
import { syncSpecialEvents } from '../events/syncSpecialEvents';
import { loadSpecialEvents } from '../events/eventsStore';
import { eventIsListed } from '../events/eventHelpers';
import { useTutorial, activeTourFor } from '../tutorial/useTutorial';
import { resetTutorial } from '../tutorial/seen';
import { UpdateModal } from './UpdateModal';
import { LocalePicker } from './LocalePicker';
import { normalizeLocale, type Locale } from '../i18n';
import { getBridge } from './bridge';
import { surfaceApp, toggleWindowMode, useWindowMode } from './windowMode';
import { useVrOverlay } from '../vr/useVrOverlay';
import { useVrPointer } from '../vr/useVrPointer';
import { useVrRacePanel } from '../vr/useVrRacePanel';
import { ScrollRail } from '../ui/ScrollRail';   // side scroll arrows (VR: there's no mouse wheel)
import { supportLog } from '../support/supportLog';
import { Display, Kicker, Mono } from '../ui/components';
import { DebugSimWorld } from './DebugSimWorld';   // simulate the world up to a given date (debug menu)
import { BrandLogo } from '../ui/BrandLogo';
import { RaceConfigPanel } from '../ui/RaceConfigPanel';
import { Identity } from '../screens/Identity';
import { Home } from '../screens/Home';
import { Season } from '../screens/Season';
import { Prerace } from '../screens/Prerace';
import { Rollover } from '../screens/Rollover';
import { Results } from '../screens/Results';
import { Inbox } from '../screens/Inbox';
import { Profile } from '../screens/Profile';
import { Calendar } from '../screens/Calendar';
import { Memories } from '../screens/Memories';
import { Times } from '../screens/Times';
import { Tree } from '../screens/Tree';
import { Settings } from '../screens/Settings';
import { Liveries } from '../screens/Liveries';
import { Garage } from '../screens/Garage';
import { Sponsors } from '../screens/Sponsors';
import { CardShop } from '../screens/CardShop';
import { Paddock } from '../screens/Paddock';
import { Tryout } from '../screens/Tryout';
import { ConnectionGate } from '../screens/ConnectionGate';
import { VrLaunch } from '../screens/VrLaunch';
import { Calibration } from '../screens/Calibration';
import { EventPrerace } from '../screens/EventPrerace';
import { EventsScreen } from '../screens/EventsScreen';
import type { SpecialEvent } from '../events/eventsClient';
// Results.tsx exists (already ported) but it's a post-session screen, not a tab -> it's wired into the race-session flow.

export type TabKey =
  | 'hub' | 'tree' | 'calendar' | 'championships' | 'times' | 'market' | 'paddock'
  | 'sponsors' | 'memories' | 'profile' | 'inbox' | 'garage' | 'settings'
  | 'liveries';

// "Race" (formerly Calendar) moves up to 2nd position (right after the Hub) -- it's the central action.
// Skins (liveries) PROMOTED to a main tab (used to live inside Settings), next to the Garage.
// 13/09: event to request going to a tab from outside the shell (see the listener next to goTab).
export const GOTO_TAB_EVENT = 'zta:goto-tab';
const TABS: TabKey[] = [
  'hub', 'calendar', 'tree', 'championships', 'times', 'paddock',
  'sponsors', 'memories', 'profile', 'inbox', 'garage', 'liveries', 'settings',
];
// 17/09 (Josh's decision) — SETTINGS LEAVES THE ROW and becomes a gear next to the credits.
// It stays in TABS because it is a destination like the others for ROUTES, keyboard shortcuts and badges;
// the only thing that changes is that it no longer spends a slot in a row that was already short. And it is the natural candidate:
// it is not visited in a loop like the rest, and a gear is recognized without reading it.
const TABS_EN_FILA: TabKey[] = TABS.filter((k) => k !== 'settings');

function fmtCredits(n: number): string {
  return `₡ ${n.toLocaleString('en-US')}`;
}

// Per-tab badge counter, driven by the engine's payload. Absent -> 0.
function badgeCount(b: CareerStatePayload['badges'] | undefined, k: TabKey): number {
  if (!b) return 0;
  // the OFFERS badge hangs off the Paddock: the Market is no longer its own tab, its offers live inside
  return k === 'paddock' ? b.market : k === 'inbox' ? b.inbox : k === 'sponsors' ? b.sponsors : k === 'memories' ? b.memories : 0;
}

export function AppShell({ client, config }: { client: ApiClientLike; config: AppConfig }) {
  const { t, i18n } = useTranslation();
  const [tab, setTab] = useState<TabKey>('hub');
  // Badges with "seen" semantics: ENTERING a tab turns its badge off;
  // it only comes back if the engine's counter changes while you're elsewhere. Purely
  // presentational -- the engine is still the source of truth for the count.
  const [seenBadges, setSeenBadges] = useState<Partial<Record<TabKey, number>>>({});
  // 17/09 — the tab row scrolls and is driven with two arrows. `tabsOver` is the only thing that needs to be
  // known (is there anything left on each side?) and it is recomputed on the scroll itself and on resize.
  // 18/09 (Josh: “the garage looks cut in half, it should let me use the right arrow”).
  // The row's node goes in STATE, not in a useRef. The row does NOT exist when the shell mounts —first
  // comes the connection gate, and with the race launched it unmounts—, so the mount effect read
  // an empty ref, never got to observe anything and `tabsOver` stayed at {false,false}: both arrows
  // off forever and the edge tab cut off with no way to see it. With the node in state, the
  // effect runs when the row really appears (and again if it remounts).
  const [tabsEl, setTabsEl] = useState<HTMLDivElement | null>(null);
  const [tabsOver, setTabsOver] = useState({ left: false, right: false });
  const syncTabsArrows = () => {
    if (!tabsEl) return;
    const max = tabsEl.scrollWidth - tabsEl.clientWidth;
    setTabsOver({ left: tabsEl.scrollLeft > 1, right: tabsEl.scrollLeft < max - 1 });
  };
  // It advances the visible width MINUS a bit, so that the clipping still leaves a half tab
  // after pressing: that is what says “there is still more” without needing another indicator. The last step
  // is clamped by the browser to the end, so the last tab ends up whole and the arrow switches off.
  const scrollTabs = (dir: number) => {
    if (tabsEl) tabsEl.scrollBy({ left: dir * Math.max(140, tabsEl.clientWidth - 110), behavior: 'smooth' });
  };
  useEffect(() => {
    syncTabsArrows();
    if (!tabsEl || typeof ResizeObserver === 'undefined') return;
    const ro = new ResizeObserver(syncTabsArrows);
    ro.observe(tabsEl);
    // The row's box does NOT change when what is inside grows (flex:1 stretches it to the whole space), so
    // each tab is watched too: a late-arriving font or a language change widen them
    // without moving the container, and without this the new overflow did not turn the arrow on.
    for (const tab of Array.from(tabsEl.children)) ro.observe(tab);
    return () => ro.disconnect();
  }, [tabsEl, i18n.language]);
  const session = useGameSession(config, client);
  // The in-headset panel starts on PLAY, not when the app opens: with the headset off, OpenVR init fails, and
  // that used to leave you without a panel for the rest of the session. 'idle' = nothing launched or attached yet, and
  // in flat mode there's no headset to peek into (the mode selector still decides that).
  useVrOverlay(client, session.state.phase !== 'idle' && isVrLaunch(launchMode()));   // 'dialog' doesn't arm VR
  useVrPointer(client);   // native VR panel: controller pointer -> a real click (no-op without a vr.pointer event)
  const cs = useQuery<CareerStatePayload>(client, 'query.career_state');
  const idq = useQuery<IdentityPayload>(client, 'query.identity');
  const profile = cs.data?.profile;
  // connection GATE. Without the game attached, the app shows ONLY Play/Reconnect (no tabs or
  // menus). It's hoisted up here (previously computed right before the gate) because the shop-reopening effect
  // further down needs to know whether we're already connected. client.status='connected' also covers the browser
  // mock (on() is a no-op there).
  const gameConnected = session.state.phase === 'connected' || session.state.phase === 'safe_mode' || client.status === 'connected';
  // the topbar's money/badges (query.career_state) used to only refresh on specific events
  // (settle, profile, rollover...) -- buying CARDS (or a car/skin/market item) left the balance STALE, and the
  // pre-race gate looked like it was lying ("Need 14,100 credits" with an old 18k still on screen; the engine had actually deducted correctly).
  // A gentle poll while connected: any credit change reflects on its own within a few seconds.
  // useQuery keeps the previous data during a refetch -> no flicker in the topbar or screens.
  useEffect(() => {
    if (!gameConnected) return;
    const t = setInterval(() => { void cs.refetch(); }, 4000);
    return () => clearInterval(t);
  }, [gameConnected, cs.refetch]);
  // "getting paid doesn't show up instantly" (a tester report): on top of the poll, every COMPLETED command
  // ('cmd_done' on the client) refreshes career_state immediately -- terminating a contract, buying, or signing shows in the
  // balance without waiting out the poll interval. The poll above remains a safety net for money the
  // engine moves without a command in between (a race settling, for instance).
  useEffect(() => client.on('cmd_done', () => { void cs.refetch(); }), [client, cs.refetch]);
  // app startup + game state transitions, into the support log. They happen
  // many times WITHOUT the engine (game closed/crashed) -> supportLog falls back to the Rust side so the trail never breaks.
  useEffect(() => {
    // startup banner with the OS's and the UI's language (encoding/locale diagnostics)
    void getBridge().then((b) => b.app.version()).then((v) => {
      // the mod's version gets stamped onto race telemetry from here, since it's the only place that
      // knows it (the engine builds the payload without knowing which build it is). See setModVersion.
      setModVersion(v);
      supportLog(null, `app started v${v} (os lang=${navigator.language}, ui lang=${i18n.language})`);
    }).catch(() => {});
  }, []);  // eslint-disable-line react-hooks/exhaustive-deps
  const prevPhase = useRef(session.state.phase);
  useEffect(() => {
    if (prevPhase.current === session.state.phase) return;
    supportLog(client, `game state: ${prevPhase.current} -> ${session.state.phase}`);
    prevPhase.current = session.state.phase;
  }, [session.state.phase, client]);
  // app language -> engine (bilingual loading cover + ALL SET pill). Covers both connecting AND switching
  // language live (i18n.language is reactive); best-effort, the engine falls back to EN if it never arrives.
  useEffect(() => {
    if (!gameConnected) return;
    // Localization: the engine receives the CODE. `lang` is also sent so that an old DLL
    // (which only reads that field) keeps understanding English/Spanish instead of going mute.
    void client.cmd('cmd.app.lang', {
      code: i18n.language,
      lang: i18n.language === 'es' ? 1 : 0,
    }).catch(() => {});
  }, [gameConnected, i18n.language, client]);
  // Race flow (Calendar -> Prerace -> launch -> results). raceFlow covers the tab's contents.
  // 'launched' = CareerApplyEvent already fired -> flags.resultSeq gets POLLED; once it increments (race settled)
  // -> 'results' (query.results). Switching tabs cancels the flow.
  const [raceFlow, setRaceFlow] = useState<null | 'prerace' | 'launched' | 'results' | 'shop'>(null);
  const [rolloverOpen, setRolloverOpen] = useState(false);   // end-of-season takeover (year advanced)
  const winMode = useWindowMode();   // so the window/fullscreen button shows what it's about to do now
  const [debugOpen, setDebugOpen] = useState(false);         // debug mini-menu (Ctrl+Shift+D)
  // the simulator lives in its own panel rather than as just another preset, because it isn't "apply this":
  // it's a question with a date attached, and its answer is a timeline that needs to be readable.
  const [simWorldOpen, setSimWorldOpen] = useState(false);
  const [contentLog, setContentLog] = useState(contentLogVerbose);   // debug menu switch (cmd.debug.contentLog)
  const [identityEdit, setIdentityEdit] = useState(false);   // post-creation editor for the driver profile (from Settings)
  const [refreshNonce, setRefreshNonce] = useState(0);       // debug save presets: force a remount of ALL screens (re-query)
  const [tutorialNonce, setTutorialNonce] = useState(0);     // tutorial: replaying from Settings re-evaluates the hook
  // T147 (5): Settings/Rollover request their OWN query and draw no anchor while it loads; with that
  // slow query, the hook's fixed margin was not enough and the tour fired lame. Each screen signals the
  // FIRST time its query settles (onDataSettled); until then the hook arms nothing for that tab.
  const [settingsReady, setSettingsReady] = useState(false);
  const [rolloverReady, setRolloverReady] = useState(false);
  const [tryoutOffer, setTryoutOffer] = useState<string | null>(null);      // tryout in progress (offerId) -> verdict shown in Results
  const [eventArmed, setEventArmed] = useState<SpecialEvent | null>(null);  // special event armed from the hub -> EventPrerace instead of the normal Prerace
  const [eventsListOpen, setEventsListOpen] = useState(false);              // "See all" -> the EventsScreen grid, fullscreen over the tab
  // same source as the hub's carousel (Home.tsx) -- reloaded here so the CTA
  // "Open event" from an email (onOpenEvent) can resolve the eventId by id WITHOUT going through the carousel.
  const [specialEvents, setSpecialEvents] = useState<SpecialEvent[]>([]);
  // notice that SHARED MEMORY is off. The engine runs inside the game session, so shmMapped===false means
  // the user doesn't have the game's Shared Memory option enabled -> without it, races can never be settled.
  // Dismissed per session (not on every launch). undefined (older DLL / mock) -> no warning shown.
  const [shmMissing, setShmMissing] = useState(false);
  const [shmWarnDismissed, setShmWarnDismissed] = useState(false);
  // THE GRID IS STALE: the transfer window has moved seats, and the grid XML the game read
  // at startup no longer matches the world state. There's no hot-reloading it, so the only way out is to
  // restart the game -- and since the app lives INSIDE the game, that happens every time you play, not just occasionally.
  // It offers to do it for the player instead of asking them to do it by hand.
  const [gridsStale, setGridsStale] = useState(false);
  const [gridsWarnDismissed, setGridsWarnDismissed] = useState(false);
  const [pendingNegotiate, setPendingNegotiate] = useState<string | null>(null); // open Negotiate in the Market after passing a tryout
  // update check on startup. The modal is rendered further down, alongside the rest of the
  // shell's overlays; dismissed per session (updateDismissed) and never shown over a launched race.
  const updater = useUpdater();
  const [updateDismissed, setUpdateDismissed] = useState(false);
  useEffect(() => { void updater.check(); }, []);
  // special-events sync on startup (fetch /v1/events -> a local special_events.json).
  // Best-effort: the engine will read them on the NEXT game launch (one-shot load per process).
  // F6 (Task 5): and loads special_events.json for the onOpenEvent lookup (same eventIsListed filter
  // as the hub carousel). refreshNonce as a dep: rereads after a tier change (Patreon)
  // that rewrites the file with a new payload (the same trigger that forces the screens to remount).
  // ("Open Event does nothing"): the read used to happen ONCE on mount, and the sync would rewrite the file
  // AFTERWARD -- if the sync landed late (first run, slow network), the list stayed empty for the whole
  // session, and the email's CTA silently no-opped. Now: it reads whatever's on disk RIGHT AWAY (still works
  // offline) and RE-READS once the sync finishes, whichever one wins the race.
  useEffect(() => {
    if (!config.paths.data) return;
    let alive = true;
    const load = () => loadSpecialEvents(config.paths.data)
      .then((f) => { if (alive && f) setSpecialEvents(f.events.filter((e) => eventIsListed(e))); })
      .catch(() => {});
    void load();
    void syncSpecialEvents().then(load).catch(() => {});
    return () => { alive = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [config.paths.data, refreshNonce]);
  // On (re)connecting to Patreon and CHANGING tier: refreshes everything that depends on the subscription, WITHOUT restarting the app.
  // Re-downloads the events (special_events.json now WITH a payload if you upgraded tier), re-pushes the entitlements to the
  // engine, and remounts the screens (refreshNonce) so the carousel/calendar re-read the freshly written file.
  useEffect(() => {
    const off = onAccountTierChange(() => {
      void syncSpecialEvents().then((r) => { if (r === 'saved') setRefreshNonce((n) => n + 1); }).catch(() => {});
      void pushEntitlements(client).catch(() => {});
    });
    return off;
  }, [client]);
  // SIMULATION MODE: the preference is chosen on the Play gate (locally, before connecting);
  // here it's SYNCED to the engine on every (re)connection. flags.simRace (query.career_state) mirrors the engine's
  // current state: it paints the SIM chip in the topbar and stops the app from minimizing on launch (there's nothing to drive).
  const simRace = cs.data?.flags?.simRace === true;
  // the in-headset panel during the race cycle (launch + the return in Results). Returns the
  // stage to render while the race is being set up; null when nothing is launching or there's no VR.
  const vrLaunch = useVrRacePanel(client, raceFlow, { vr: isVrLaunch(launchMode()), simRace });   //
  useEffect(() => {
    if (!gameConnected) return;
    void client.cmd('cmd.debug.simRace', { enabled: simRaceEnabled() }).then(() => { void cs.refetch(); }).catch(() => {});
    // Experimental Low RAM Mode: chosen on the Play window (local preference); synced to the
    // engine on every (re)connection. Best-effort: an older engine without the command just ignores it (doesn't break the connection).
    void client.cmd('cmd.debug.lowRam', { enabled: lowRamMode() }).catch(() => {});
    // The screens already requested their data on mount (the children's effects run first): with the switch
    // ON they must be remounted so they do not keep the computation from before the resend.
    void client.cmd('cmd.debug.contentLog', { enabled: contentLogVerbose() })
      .then(() => { if (contentLogVerbose()) setRefreshNonce((n) => n + 1); }).catch(() => {});
    // telemetry consent gets answered on the Play window (with no engine attached yet),
    // so the answer lives locally and gets synced here. It's only sent once it's actually been answered: null (not yet
    // asked) must NOT write anything to the save, or it would record a "no" the player never actually gave.
    // And the reverse: if there's no local answer but the SAVE says it was already asked (reinstalling the app wipes
    // localStorage, not the save), it's recovered from there instead of asking again about something already answered.
    { const choice = telemetryChoice();
      if (choice !== null) void client.cmd('cmd.settings.setTelemetry', { on: choice }).catch(() => {});
      else void client.query<SettingsPayload>('query.settings')
        .then((s) => { if (s?.telemetryAsked) setTelemetryChoice(s.telemetryOptIn === true); })
        .catch(() => {});
      // It also drains the queue of races that couldn't be sent (no network, backend down). With
      // consent WITHDRAWN the queue is deleted instead of sent: doing otherwise would mean sending it behind the back of
      // a decision already made. Best-effort and in the background; connecting doesn't wait on this.
      void flushTelemetryQueue(postRaceTelemetry, machineHash, choice === true, new Date().toISOString())
        .then((r) => { if (r.sent || r.dropped) console.info('[telemetry] cola:', r); })
        .catch(() => {}); }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [gameConnected]);
  // on connecting, asks for the installed skin slots (cmd.skins.installed) and converts their
  // preview images (DDS) to PNG in the BACKGROUND (doesn't block the UI). If it converts anything, it bumps refreshNonce -> the
  // screens re-list the overrides folder and do the vanilla->skin swap without restarting. Best-effort:
  // older engine (without the query), no game, or no skins -> silently ignored.
  useEffect(() => {
    if (!gameConnected) return;
    let live = true;
    void (async () => {
      try {
        const inst = await client.query<InstalledSkins>('cmd.skins.installed');
        if (!live || !inst?.slots?.length) return;
        const b = await getBridge();
        const res = await b.provision.syncOverridePreviews(config.paths.gameInstall, config.paths.data, inst.slots);
        if (live && res.converted > 0) setRefreshNonce((n) => n + 1);
      } catch { /* best-effort: the app falls back to the vanilla livery */ }
    })();
    return () => { live = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [gameConnected]);
  // 19/09 (report: “the card shop gets lost if I leave Results through a tab”). The engine opens the
  // shop after a class podium, but the app only went through it from Results' “← Back”: a
  // tab did goTab, which sets raceFlow to null and skipped it, and the Sponsors badge does not count it on
  // purpose. Now ANY exit from Results asks query.cards first: with a shop pending it is entered
  // (it is one more step of the post-race flow, as when reopening it at startup) and on closing it continues to the
  // tab that had been requested; without a shop, straight to that tab.
  const [shopExitTab, setShopExitTab] = useState<TabKey>('calendar');
  const enterTab = (k: TabKey) => { setTab(k); setRaceFlow(null); setTryoutOffer(null); setEventArmed(null); setEventsListOpen(false);
    setShopExitTab('calendar');
    supportLog(client, `screen ${k}`);   // navigation breadcrumb (via the engine when connected, Rust fallback otherwise)
  };
  const leaveResults = (k: TabKey) => {
    const leave = () => enterTab(k);
    void client.query<CardsPayload>('query.cards')
      .then((d) => { if (d.shop != null) { setShopExitTab(k); setRaceFlow('shop'); } else leave(); })
      .catch(leave);   // with no answer, do not leave the player on a screen it cannot render
  };
  const goTab = (k: TabKey) => {
    if (raceFlow === 'results' && !tryoutOffer) { leaveResults(k); return; }
    enterTab(k);
  };
  // 13/09: navigation requested from OUTSIDE the shell (e.g. the stale-assets warning, which lives in App.tsx).
  // The app does not navigate by URL -- a tab is state of this component --, so a `location.hash` does
  // nothing: it is requested with this event. A ref so the listener, registered once, always calls the current goTab.
  const goTabRef = useRef(goTab);
  goTabRef.current = goTab;
  useEffect(() => {
    const onGoto = (e: Event) => {
      const k = (e as CustomEvent<string>).detail;
      if (TABS.includes(k as TabKey)) goTabRef.current(k as TabKey);
    };
    window.addEventListener(GOTO_TAB_EVENT, onGoto);
    return () => window.removeEventListener(GOTO_TAB_EVENT, onGoto);
  }, []);
  // GAMEPAD navigation. The bumpers (LB/RB) move across the shell's visible tabs (not during the
  // race flow, where the app is minimized). Directional focus + A/B is handled globally by the hook.
  const stepTab = (delta: number) => {
    if (raceFlow) return;
    const i = TABS.indexOf(tab);
    const base = i < 0 ? 0 : i;
    const next = (base + delta + TABS.length) % TABS.length;
    goTab(TABS[next]);
  };
  // (useGamepadNav is mounted further down, once raceFlow and atFrontEnd exist: the focus gate needs them)
  // a contract TRYOUT uses the NORMAL race flow. It arms the tryout (engine: the pre-race car is the
  // locked team's car) and enters the pre-race screen; the verdict is resolved on settle and shown in Results.
  const startTryout = (offerId: string) => {
    void client.cmd('cmd.tryout.arm', { offerId }).then(() => { setTryoutOffer(offerId); setEventArmed(null); setRaceFlow('prerace'); }).catch(() => {});
  };

  // END-OF-RACE DETECTION. In the "launched" state it pins a resultSeq baseline and polls query.career_state;
  // once resultSeq passes the baseline -> there's a NEW result -> Results is shown.
  const launchSeq = useRef<number | null>(null);
  const resultSeq = cs.data?.flags?.resultSeq ?? 0;
  const resultSeqRef = useRef(0);   // a FRESH read of resultSeq inside the interval (the closure would otherwise capture the one from setup)
  resultSeqRef.current = resultSeq;
  // 05/09: does the engine have a settlement ON HOLD? Same ref trick: the interval's closure captures the
  // value from setup, and this is exactly what changes while the interval runs.
  const settlePending = cs.data?.flags?.settlePending ?? false;
  const settlePendingRef = useRef(false);
  settlePendingRef.current = settlePending;
  const sawRaceStart = useRef(false);   // the race left the menu at least once (avoids a false abandon while still on the loading menu)
  // is the game at its MAIN MENU? Finishing the race and exiting the race are two separate steps, and Results
  // waits for the second one (see game/resultsGate.ts). gameGone = the game stopped responding: an emergency exit
  // so the result isn't held hostage if the game closed or crashed right at the finish.
  const [atFrontEnd, setAtFrontEnd] = useState(false);
  const [gameGone, setGameGone] = useState(false);
  const goneTicks = useRef(0);
  const atMenuStreak = useRef(0);       // consecutive "at the menu" polls (debounce)
  // 27/09: the controller only navigates with the window focused. The exception is the headset panel, which shows the
  // app without the window having system focus: in a VR mode with an armed panel it is allowed as long as you are not
  // driving (race launched and AMS2 out of the main menu), which is exactly when the panel hides.
  useGamepadNav({
    onPrevTab: () => stepTab(-1),
    onNextTab: () => stepTab(1),
    allowUnfocused: () => isVrLaunch(launchMode()) && vrOverlayEnabled() && (raceFlow !== 'launched' || atFrontEnd),
  });
  useEffect(() => {
    if (raceFlow !== 'launched') { launchSeq.current = null; sawRaceStart.current = false; atMenuStreak.current = 0; return; }
    launchSeq.current = resultSeq;
    sawRaceStart.current = false;
    atMenuStreak.current = 0;
    setAtFrontEnd(false); setGameGone(false); goneTicks.current = 0;   // never inherit the previous race's state
    let cancel = false;
    const iv = setInterval(() => {
      void cs.refetch();
      // ABANDONMENT. The app used to get STUCK on "waiting for result" if you left the race for the
      // menu/Custom Race without finishing it (no settle -> resultSeq never advances). We detect: (1) the race
      // STARTED (left the menu), (2) RETURNED to the menu for 2 consecutive polls, (3) resultSeq did NOT advance -> abandonment -> go back
      // to the hub WITHOUT Results (and give focus back to the app). If there was a result, the effect below (resultSeq>baseline)
      // wins and goes to Results; the resultSeq<=baseline guard prevents false abandonment during the race window.
      void client.query<GameStatePayload>('query.game_state').then((gs) => {
        if (cancel) return;
        goneTicks.current = 0; setGameGone(false);
        setAtFrontEnd(gs.atMenu);   // the gate into Results
        if (!gs.atMenu) { sawRaceStart.current = true; atMenuStreak.current = 0; return; }
        atMenuStreak.current += 1;
        if (launchSeq.current != null && shouldGiveUpAsAbandoned({
              sawRaceStart: sawRaceStart.current,
              atMenuStreak: atMenuStreak.current,
              resultAdvanced: resultSeqRef.current > launchSeq.current,
              settlePending: settlePendingRef.current,
            })) {
          cancel = true;
          setRaceFlow(null);
          setTryoutOffer(null);   // abandoning a tryout doesn't leave its verdict hanging either
          if (shouldTakeoverDesktop(launchMode(), autoSwitchEnabled(), vrOverlayEnabled())) void getBridge().then((b) => surfaceApp(b).catch(() => {}));
        }
      }).catch(() => {
        // the game stops responding. A single blip doesn't count; several in a row means it closed or crashed, and then
        // there's no race left to exit, and the result shouldn't stay buried.
        if (cancel) return;
        goneTicks.current += 1;
        if (goneTicks.current >= GAME_GONE_TICKS) setGameGone(true);
      });
    }, 3000);
    return () => { cancel = true; clearInterval(iv); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [raceFlow]);
  // A race can be SETTLED while the player is still inside the game: crossing the finish line isn't the same as leaving the
  // race -- there's one more step. Jumping straight to Results at the finish line made people forget that step and
  // leave the game hanging mid-session. Now it waits for the main menu, the same rule the
  // headset panel already followed. Meanwhile, the waiting screen says so (raceSettled, below).
  const resultReady = raceFlow === 'launched' && launchSeq.current != null && resultSeq > launchSeq.current;
  useEffect(() => {
    if (shouldShowResults({ resultReady, atFrontEnd, simRace, gameGone })) setRaceFlow('results');
  }, [resultReady, atFrontEnd, simRace, gameGone]);

  // auto-switch focus: launching a race makes the app step aside (game in front, ready to drive); when it
  // ends (Results), the app comes back to the front, covering the game. The DLL grants AllowSetForegroundWindow on settle.
  useEffect(() => {
    if (!shouldTakeoverDesktop(launchMode(), autoSwitchEnabled(), vrOverlayEnabled())) return;   // MANUAL (toggle in Settings) or VR with the panel armed -> the user/overlay manages focus (VR without the panel behaves like flat mode)
    if (simRace) return;                // SIMULATION: there's nothing to drive -> the app doesn't step aside or fight for focus
    if (raceFlow === 'launched') {
      void getBridge().then((b) => b.window.minimize().catch((e) => supportLog(client, `autoswitch: minimize FAILED (${String(e)})`)));
    } else if (raceFlow === 'results') {
      // don't cover the game on settle: there's still an "exit the race" step in the game. Wait for it to RETURN to the menu.
      let cancel = false;
      void (async () => {
        const b = await getBridge();
        for (let i = 0; i < 150 && !cancel; i++) {   // ~5 min of margin
          try { const gs = await client.query<GameStatePayload>('query.game_state'); if (gs.atMenu) break; } catch { /* */ }
          await new Promise((r) => setTimeout(r, 2000));
        }
        if (!cancel) await surfaceApp(b).catch((e) => supportLog(client, `autoswitch: surface FAILED (${String(e)})`));
      })();
      return () => { cancel = true; };
    }
  }, [raceFlow, client, simRace]);

  // debug mini-menu (save presets). Global keyboard listener.
  // The shortcut is Ctrl+Shift+D, not a locale-specific character: comparing by the literal key character means a US/UK/DE/FR keyboard has
  // no key that can produce it -> the menu was unreachable for any tester outside Spain.
  // The old character-based shortcut stays as an alias (costs nothing to keep), but the key combo is what's documented.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const combo = e.ctrlKey && e.shiftKey && !e.altKey && (e.key === 'D' || e.key === 'd');
      const legacy = !e.ctrlKey && !e.altKey && (e.key === 'ñ' || e.key === 'Ñ');
      if ((combo || legacy) && !(e.target instanceof HTMLInputElement) && !(e.target instanceof HTMLTextAreaElement)) {
        e.preventDefault(); setDebugOpen((v) => !v);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  // scale shortcuts, the same ones the browser uses (Ctrl + / Ctrl - / Ctrl 0). Besides being what people
  // already expect, they're the EMERGENCY EXIT: if a large scale leaves the Settings buttons awkward to reach,
  // Ctrl+0 snaps back to 100% without having to reach them. It goes through the same steps Settings offers.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!e.ctrlKey || e.altKey) return;
      const step = e.key === '+' || e.key === '=' ? 1 : e.key === '-' ? -1 : 0;
      if (e.key === '0') { e.preventDefault(); setUiScale(100); return; }
      if (step === 0) return;
      e.preventDefault();
      const steps: number[] = [...UI_SCALES];
      const i = steps.indexOf(uiScale());
      const next = i < 0 ? 100 : steps[Math.min(Math.max(i + step, 0), steps.length - 1)];
      setUiScale(next);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  // lazy polling (every 30s) of Shared Memory. Only matters when we're NOT in a launched race
  // (during the race the app is minimized). shmMapped===false -> the user needs to enable the option in the game.
  // Closed loop: an old DLL doesn't return the field (undefined) -> we never warn by accident.
  useEffect(() => {
    if (raceFlow) return;
    let cancel = false;
    // gridsStale is requested on purpose: the engine only computes that fingerprint (expensive, the whole distribution) if it is asked
    const check = () => void client.query<GameStatePayload>('query.game_state', { gridsStale: true })
      .then((gs) => {
        if (cancel) return;
        if (gs.shmMapped !== undefined) setShmMissing(gs.shmMapped === false);
        // the same probe doubles as the stale-grid check: it's the same coarse state, and it doesn't
        // deserve its own query. undefined (older engine) -> no warning.
        if (gs.gridsStale !== undefined) setGridsStale(gs.gridsStale === true);
      })
      .catch(() => {});
    check();
    const iv = setInterval(check, 30000);
    return () => { cancel = true; clearInterval(iv); };
  }, [client, raceFlow]);

  // reopens a pending sponsor-card shop on load. If the app was closed with a shop left unresolved
  // (the player quit before "Continue to hub"), reconnecting reopens it on its own instead of leaving it buried
  // in the Sponsors tab. A SINGLE check per launch (shopChecked avoids repeating it on every reconnection), and
  // only if there isn't already another race flow in progress; a network failure doesn't block startup (silent catch).
  const shopChecked = useRef(false);
  useEffect(() => {
    if (!gameConnected || shopChecked.current) return;
    shopChecked.current = true;
    if (raceFlow !== null) return;
    void client.query<CardsPayload>('query.cards')
      .then((data) => { if (data.shop != null) setRaceFlow((cur) => (cur === null ? 'shop' : cur)); })
      .catch(() => {});
  }, [gameConnected, raceFlow, client]);

  // "Seen" badges: while a tab is active (and there's no takeover), its current count gets
  // recorded as seen -> the badge turns off on entry and only comes back if the engine changes it
  // while you're on another tab.
  const liveBadges = cs.data?.badges;
  useEffect(() => {
    if (raceFlow !== null) return;                       // a takeover doesn't count as "seeing" the tab
    const cur = badgeCount(liveBadges, tab);
    setSeenBadges((s) => (s[tab] === cur ? s : { ...s, [tab]: cur }));
  }, [tab, raceFlow, liveBadges]);
  // a DROP in the counter is never news -- it's the echo of your own actions (reading/answering
  // emails, signing an offer) arriving via the poll AFTER you've left the tab; with "seen" pinned to the
  // old value, the badge would relight with the decreased number (6, 8...). A drop silently syncs "seen"
  // for ANY tab; rendering only lights up when live > seen (an actual increase).
  useEffect(() => {
    if (!liveBadges) return;
    setSeenBadges((s) => {
      let changed = false;
      const next = { ...s };
      for (const k of TABS) {
        const cur = badgeCount(liveBadges, k);
        if (cur < (s[k] ?? 0)) { next[k] = cur; changed = true; }
      }
      return changed ? next : s;
    });
  }, [liveBadges]);

  // the debug save preset does a FULL RESET of the save (money, phase, unlocks, world). On top of refetching the topbar,
  // bumping the nonce remounts ALL screens so their queries (tree/calendar/...) re-read the new state.
  const applyDebug = (preset: number) => { setDebugOpen(false); void client.cmd('cmd.debug.setState', { preset }).then(() => { void cs.refetch(); void idq.refetch(); setRefreshNonce((n) => n + 1); }).catch(() => {}); };
  // Without an engine the command is lost and nothing happens: the connection effect resends it from the pref.
  const toggleContentLog = () => {
    const next = !contentLog;
    setContentLogVerbose(next); setContentLog(next);
    void client.cmd('cmd.debug.contentLog', { enabled: next }).then(() => { void cs.refetch(); setRefreshNonce((n) => n + 1); }).catch(() => {});
  };

  // 01/10: changing the language ALSO saves it. Before, it only changed i18next and on reopening the app the
  // language from the first-run wizard came back (see saveLocale). Saving is best-effort: if the disk fails,
  // the language changes anyway in this session and the support log says so.
  const pickLocale = (l: Locale) => {
    void i18n.changeLanguage(l);
    saveLocale(config, l).catch((e) => supportLog(client, `locale: could not save '${l}' (${String(e)})`));
  };

  // clicking ✕ with the game OPEN asks whether to close it too (yes/no; the
  // backdrop cancels). With the game already closed, it closes directly as always. closeProcess = Stop-Process.
  const [closeAsk, setCloseAsk] = useState(false);
  const requestClose = () => void (async () => {
    const b = await getBridge();
    const running = await b.game.isRunning('AMS2AVX.exe').catch(() => false);
    if (running) setCloseAsk(true);
    else await closeAppGracefully(client);
  })();
  const doClose = (alsoGame: boolean) => void (async () => {
    setCloseAsk(false);
    supportLog(client, alsoGame ? 'closing: app + AMS2' : 'closing: app only');
    if (alsoGame) { try { const b = await getBridge(); await b.game.closeProcess('AMS2AVX.exe'); } catch { /* best-effort */ } }
    await closeAppGracefully(client);
  })();

  // Tutorial: a first-visit tour per screen. The gates (connection/profile/calibration/
  // editor) veto it by passing null; everything else is decided by activeTourFor, mirroring the render order below.
  // GOTCHA on a first run: idq/cs start out undefined (in flight), and !undefined?.needed / !==false
  // evaluate to a false "ready" -> requires RESOLVED data (!= null) before arming the tour, otherwise the provisional
  // shell/hub fires the tour and the real gate destroys it the moment needed:true lands, marking it as seen.
  const tutorialReady = gameConnected && idq.data != null && !idq.data.needed
    && cs.data != null && cs.data.flags?.calibrated !== false && !identityEdit;
  // T147 (5): only Settings and Rollover request their own slow query after the gate above; the rest of the
  // tabs already have their data (cs/idq) resolved by the time tutorialReady is true, so their margin
  // is still the fixed MOUNT_DELAY_MS.
  const tourScreenReady = tab === 'settings' ? settingsReady : rolloverOpen ? rolloverReady : true;
  useTutorial(
    tutorialReady ? activeTourFor({ tab, raceFlow, rolloverOpen, eventsListOpen, tryout: tryoutOffer != null }) : null,
    tutorialNonce,
    tourScreenReady,
  );

  // connection GATE. Without the game attached, the app shows ONLY Play/Reconnect (no tabs or
  // menus). Goes FIRST (identity/calendar/... need the connection alive). safe_mode (connected, but an unsupported build)
  // passes through to the shell with actions disabled. (gameConnected is computed above, alongside the rest of the hooks.)
  if (!gameConnected) {
    return (
      <>
        <ConnectionGate session={session} everConnected={session.everConnected} userdata={config.paths.userdata} />
        {!updateDismissed && session.state.phase !== 'compat_blocked' &&
          (updater.status === 'available' || updater.status === 'downloading' || updater.status === 'restarting') && (
            <UpdateModal u={updater} onDismiss={() => setUpdateDismissed(true)} />
          )}
      </>
    );
  }

  // creation GATE: if the save exists but the profile is blank, ONLY the Identity screen is drawn (no topbar/tabs).
  // On submit it refetches identity (needed->false, enters the shell) + career_state (starting credits/name).
  if (idq.data?.needed) {
    return <Identity client={client} onDone={() => { void idq.refetch(); void cs.refetch(); }} />;
  }

  // calibration GATE. A freshly-created profile (unmeasured) sees ONLY the manager's epic intro (no
  // tabs) until launching the calibration race. Accepting enters the race flow (raceFlow) -- which falls back to the
  // shell and shows a locked pre-race screen; once it settles, flags.calibrated flips to true and the gate disappears.
  // explicit `=== false`: a mock/fixture without the field (undefined) does NOT block browser-based dev.
  if (cs.data?.flags?.calibrated === false && raceFlow === null) {
    // onSkip marks calibrated=true in the engine (cmd.calendar.skipCalibration) -> leaves
    // adaptive difficulty at its default seed (unmeasured); the refetch clears this gate. The catch is NOT silent:
    // an old DLL without the command responds bad_method, and that failure needs to be visible (it used to be a silent "does nothing").
    return <Calibration client={client} onEnterPrerace={() => { setEventArmed(null); setRaceFlow('prerace'); }}
      onSkip={() => {
        void client.cmd('cmd.calendar.skipCalibration', {})
          .then(() => { void cs.refetch(); })
          .catch((e) => supportLog(client, `calibration: skipCalibration FAILED (old DLL injected?) (${String(e)})`));
      }} />;
  }

  // post-creation profile EDITOR (same screen, edit=true). Opened from Settings; a NON-destructive submit
  // (the engine only re-seeds on a fresh profile). Closing it refetches identity/career_state (name in the topbar/standings).
  if (identityEdit) {
    return <Identity client={client} edit onCancel={() => setIdentityEdit(false)}
      onDone={() => { setIdentityEdit(false); void idq.refetch(); void cs.refetch(); }} />;
  }

  // end-of-season takeover (no topbar). onNegotiate leads to the Market (renewal).
  if (rolloverOpen) {
    return <Rollover client={client} onDone={() => { setRolloverOpen(false); void cs.refetch(); }} onNegotiate={() => { setRolloverOpen(false); setTab('market'); }}
      onDataSettled={() => setRolloverReady(true)} />;
  }

  return (
    <div style={{ height: '100%', display: 'flex', flexDirection: 'column', position: 'relative' }}>
      <div className="app-bg" />
      <div className="topbar" role="banner">
        <div className="brand">
          <div className="mark" />
          {/* 18/09 (Josh) — without the “AMS2 CAREER” label next to it: the logo already says what it is. */}
          <BrandLogo height={24} />
        </div>
        {raceFlow !== 'launched' && (
        <div className="tabsbar" style={{ marginLeft: 20 }}>
          <button className="tabs-arrow" aria-label={t('common.prev')} disabled={!tabsOver.left}
                  onClick={() => scrollTabs(-1)} tabIndex={-1}>{'‹'}</button>
        <div className="tabs" ref={setTabsEl} onScroll={syncTabsArrows} role="navigation" aria-label={t('app.title')} data-tour="shell.tabs">
          {TABS_EN_FILA.map((k) => {
            // Per-tab notification badges: Market/Sponsors/Memories with
            // "seen" semantics, only lighting up on an actual RISE above what was seen (a drop
            // is just your own action arriving late via the poll, and it's synced silently).
            // MAIL is different -- its engine counter is UNREAD-ONLY,
            // so it's shown as-is, like any mail client: reading clears it, with no
            // "seen" gate (a gate would hide genuinely-unread mail after leaving the tab mid-read).
            const live = badgeCount(cs.data?.badges, k);
            const n = k === 'inbox' ? live : (live > (seenBadges[k] ?? 0) ? live : 0);
            // the badge lives OUTSIDE the <button> (the .tab's diagonal clip-path was cutting it off).
            // A relatively-positioned wrapper with no clip -> the badge pokes out of the corner intact; pointerEvents:none
            // so clicks still land on the button.
            return (
              <span key={k} style={{ position: 'relative', display: 'inline-block' }}>
                <button className={'tab' + (tab === k && !raceFlow ? ' active' : '')} onClick={() => goTab(k)}>
                  {t(`nav.${k}`)}
                </button>
                {n > 0 && (
                  <span aria-label={`${n}`} style={{ position: 'absolute', top: -5, right: -4, minWidth: 15, height: 15, padding: '0 4px', borderRadius: 8, background: 'var(--accent)', color: 'var(--accent-ink)', fontSize: 9, fontWeight: 700, lineHeight: '15px', textAlign: 'center', boxSizing: 'border-box', zIndex: 1, pointerEvents: 'none' }}>
                    {n > 9 ? '9+' : n}
                  </span>
                )}
              </span>
            );
          })}
        </div>
          <button className="tabs-arrow" aria-label={t('common.next')} disabled={!tabsOver.right}
                  onClick={() => scrollTabs(1)} tabIndex={-1}>{'›'}</button>
          {/* Settings, now outside the row. It goes here and not in the right-hand block so it stays on the side
              of NAVIGATION —it is a destination, not an indicator— and so the row butts against the credits. */}
          <button data-nav-skip className={'tab tab-icon' + (tab === 'settings' && !raceFlow ? ' active' : '')}
                  aria-label={t('nav.settings')} title={t('nav.settings')}
                  onClick={() => goTab('settings')}>
            {/* A drawing and not the “⚙” character: that glyph is painted tiny inside its box, so raising
                the font size was barely noticeable. With an SVG the size is what is asked for, exactly. */}
            <svg viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="currentColor"
                 strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
              <circle cx="12" cy="12" r="3.2" />
              <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09a1.65 1.65 0 0 0-1.08-1.51 1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09a1.65 1.65 0 0 0 1.51-1.08 1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V11a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z" />
            </svg>
          </button>
        </div>
        )}
        {/* once the race is ALREADY LAUNCHED, the game is in control and the only legitimate action is "Back".
            The tabs are HIDDEN (not just disabled): seeing them invites navigating to a screen that the flow
            is about to cover the moment the result lands. The rest of the topbar (credits/language/window) stays. */}
        <div className="row gap12" style={{ alignItems: 'center' }}>
          {/* persistent reminder of SIMULATION MODE -- race results are fabricated */}
          {simRace && (
            <span className="mono" title={t('simrace.hint')} style={{ fontSize: 9, fontWeight: 700, color: 'var(--amber)', border: '1px solid var(--amber)', borderRadius: 3, padding: '2px 6px', letterSpacing: '0.08em' }}>
              {t('simrace.chip')}
            </span>
          )}
          <span className="credits-pill" data-tour="shell.credits">{fmtCredits(profile?.money ?? 0)}</span>
          <LocalePicker value={normalizeLocale(i18n.language)} onPick={pickLocale} label={t('tour.shell.lang.t')} />
          {/* Window controls: minimize, window/fullscreen, close. The AUTO/MANUAL focus toggle lives in Settings.
              The middle one TOGGLES and the choice is SAVED: auto-switch (end of race, returning from alt-tab)
              brings the app back in that same mode instead of always maximizing it. */}
          <span className="row" style={{ gap: 2, paddingLeft: 4, borderLeft: '1px solid var(--line)' }} data-tour="shell.win">
            {/* 27/09 (audit): the window controls are NOT controller destinations (data-nav-skip). From the bottom
                right button, “right” ended on the ✕ that closes the app. With the mouse they stay the same. */}
            <button className="btn btn-sm btn-ghost" data-nav-skip title={t('common.minimize')} onClick={() => void getBridge().then((b) => b.window.minimize())}>—</button>
            <button className="btn btn-sm btn-ghost" data-nav-skip title={t(winMode === 'fullscreen' ? 'common.windowed' : 'common.fullscreen')} onClick={() => void toggleWindowMode()}>{winMode === 'fullscreen' ? '❐' : '⛶'}</button>
            {/* a CLEAN close -- notifies the engine (visual quiesce), closes the socket with a Close frame, and only then the window.
                with the game open, it asks first whether to close it too. */}
            <button className="btn btn-sm btn-ghost" data-nav-skip title={t('common.close')} onClick={requestClose}>✕</button>
          </span>
          <span className="col" style={{ lineHeight: 1.1, alignItems: 'flex-end', paddingLeft: 6, borderLeft: '1px solid var(--line)' }} data-tour="shell.rep">
            <span style={{ fontWeight: 700, fontSize: 14, whiteSpace: 'nowrap' }}>{profile?.name ?? t('common.player')}</span>
            <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
              {t('common.rating')} {profile?.eloFame ?? '—'}
            </Mono>
          </span>
        </div>
      </div>

      {/* persistent notice that SHARED MEMORY is off -- without it the mod can't read results or settle
          races. Dismissible per session; includes the exact instructions to enable it in AMS2.
          30/09 (Josh: “a Dismiss floating under the name”): own position + zIndex. `.app-bg` is a
          fixed background with z-index 0 and is painted ON TOP of everything that is not positioned, so the whole notice was
          covered and only the button peeked out, which is positioned. The notice has never been seen since it has existed. */}
      {shmMissing && !shmWarnDismissed && (
        <div role="alert" className="row between" style={{ position: 'relative', zIndex: 2, alignItems: 'center', gap: 16, padding: '9px 16px', background: 'color-mix(in oklab, var(--amber) 16%, var(--surface))', borderBottom: '1px solid var(--amber)' }}>
          <div className="col" style={{ gap: 2, minWidth: 0 }}>
            <span className="display" style={{ fontSize: 13, color: 'var(--amber)' }}>{t('shm.warnTitle')}</span>
            <Mono style={{ fontSize: 10.5, color: 'var(--text-2)', lineHeight: 1.5 }}>{t('shm.warnBody')}</Mono>
          </div>
          <button className="btn btn-sm btn-ghost" style={{ flexShrink: 0 }} onClick={() => setShmWarnDismissed(true)}>{t('common.dismiss')}</button>
        </div>
      )}

      {/* THE GRID IS STALE. The transfer window has moved seats, but the game read the grid XML
          grid AT STARTUP and does not hot-reload them — measured on 25/07 by changing an XML with the game
          open: the next race kept the previous one. Until it is restarted, what runs on
          track is not what the paddock says, and that happens ALWAYS when the market moves.
          It offers to do it for the player: `play()` already closes the game, puts the overlay back, relaunches -- and launching regenerates
          the XML files along the way -- and waits for the process. It's never shown during a launched race, which would be the
          worst possible moment to offer to close the game.

          18/09 — IT WAS A THIN BAND AT THE VERY TOP, and it was missed. Now it is a WINDOW, because the
          decision is not cosmetic: either you restart and race against the real paddock, or you keep competing
          against grids from another season without knowing it. That is why the ignore button says out loud what
          you take away. Same mold as the close-the-game dialog (below): background that cancels, zIndex 70. */}
      {shouldOfferGridRestart({ stale: gridsStale, dismissed: gridsWarnDismissed, inRace: raceFlow !== null }) && (
        <div onClick={() => setGridsWarnDismissed(true)}
          style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.6)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 70 }}>
          <div role="alertdialog" aria-modal="true" aria-label={t('grids.staleTitle')} data-grids-stale="1"
            className="panel" onClick={(e) => e.stopPropagation()}
            style={{ width: 440, padding: '20px 22px', display: 'flex', flexDirection: 'column', gap: 12 }}>
            <Kicker style={{ color: 'var(--accent-text)' }}>{t('grids.staleTitle')}</Kicker>
            <Mono style={{ fontSize: 11, color: 'var(--text-2)', lineHeight: 1.55 }}>{t('grids.staleBody')}</Mono>
            <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.55 }}>{t('grids.staleIgnoreWhy')}</Mono>
            <div className="row" style={{ gap: 8, justifyContent: 'flex-end', marginTop: 4 }}>
              <button className="btn btn-sm btn-ghost" onClick={() => setGridsWarnDismissed(true)}>{t('grids.staleIgnore')}</button>
              <button className="btn btn-sm btn-primary" data-grids-stale-restart="1"
                onClick={() => { setGridsWarnDismissed(true); void session.play(); }}>
                {t('grids.staleRestart')}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* the content area now SCROLLS. It used to have no overflow, so whatever didn't fit was
          clipped and unreachable by any means (body has overflow:hidden and #root is pinned to the
          viewport): hence "I can't scroll down to tryouts on the calendar, I can't see them". Screens that already
          bring their own internal scroll are unaffected: since they're height-limited, this outer scroll never overflows. */}
      <div key={refreshNonce} className="grow scroll" role="main" style={{ position: 'relative', minHeight: 0, zIndex: 1 }}>
        {eventsListOpen && !raceFlow ? (
          // "See all": a grid of ALL events over the tab. Entering one = the same flow as the
          // hub's carousel (eventArmed -> EventPrerace); onBack closes the list and returns to the hub.
          <EventsScreen client={client} dataDir={config.paths.data}
            onBack={() => setEventsListOpen(false)}
            onEnterEvent={(ev) => { setEventsListOpen(false); setEventArmed(ev); setRaceFlow('prerace'); }} />
        ) : raceFlow === 'prerace' ? (
          // special event armed from the hub (eventArmed) -> EventPrerace instead of the
          // normal pre-race screen. onFired/onBack mirror Prerace but also clear eventArmed (avoids staying
          // stuck on the event screen if you later enter pre-race via the calendar or a tryout).
          eventArmed ? (
            <EventPrerace client={client} dataDir={config.paths.data} event={eventArmed}
              onBack={() => { setEventArmed(null); setRaceFlow(null); }}
              onFired={() => { setEventArmed(null); setRaceFlow('launched'); }} />
          ) : (
            // in a tryout, "back" cancels and returns to the Market; in a normal race it returns to the hub.
            <Prerace client={client} dataDir={config.paths.data} onBack={() => { if (tryoutOffer) goTab('market'); else setRaceFlow(null); }} onFired={() => setRaceFlow('launched')} />
          )
        ) : raceFlow === 'results' ? (
          // if the race was a TRYOUT, the "result" is the VERDICT (pass/fail + Negotiate), not a race result.
          tryoutOffer ? (
            <Tryout client={client} offerId={tryoutOffer}
              onBack={() => { setTryoutOffer(null); goTab('market'); }}
              onNegotiate={(oid) => { setTryoutOffer(null); setPendingNegotiate(oid); goTab('market'); }} />
          ) : (
            // 05/09 ([tester], note 2): closing Results ALWAYS entered the card shop, whether there was
            // a shop or not. CardShop closes itself when query.cards says there are none, but until
            // that answer arrives it renders null -> depending on how long that takes, either a phantom step that only shows up in
            // the log, or a BLANK SCREEN. The two symptoms reported, and they are the same one. Here it
            // asks BEFORE entering; with no shop it goes to the calendar, which is where it ended up anyway.
            // 19/09: the same question applies to leaving through a tab (leaveResults, next to goTab).
            <Results client={client} onDone={() => leaveResults('calendar')} />
          )
        ) : raceFlow === 'shop' ? (
          <CardShop client={client} onDone={() => enterTab(shopExitTab)}
            doneLabel={shopExitTab === 'calendar' ? undefined : t('common.continue')} />

        ) : raceFlow === 'launched' && vrLaunch ? (
          // with a headset on, launching gets its OWN screen (the mod's overlay notices never reach the headset).
          // Covers the menu navigation while the race is being set up and steps aside on its own once it's done.
          <VrLaunch client={client} launch={vrLaunch} onBack={() => goTab('hub')} />
        ) : raceFlow === 'launched' ? (
          // SIMULATION: its own copy (there's no game to switch to; the result arrives in ~6s) + the
          // CONFIG panel (query.race_config): while simulating there's no game to check what the race would have been configured with,
          // so it's shown here while it "runs". In a REAL race the config is visible in the game itself.
          <div className="col center grow" style={{ height: '100%', gap: 14, padding: 30, textAlign: 'center', overflowY: 'auto' }}>
            <BrandLogo height={48} style={{ marginBottom: 6 }} />
            {/* the race is ALREADY settled and the player is still inside the game: the copy changes to tell them
                exactly what's left to do. Without this, the wait looked like the usual one and nobody knew the
                remaining step was theirs to take. */}
            <Display style={{ fontSize: 30, color: resultReady && !simRace ? 'var(--green)' : simRace ? 'var(--amber)' : 'var(--accent)' }}>
              {t(resultReady && !simRace ? 'race.settledTitle' : simRace ? 'simrace.launchedTitle' : 'race.launchedTitle')}
            </Display>
            <Mono style={{ fontSize: 13, color: 'var(--text-2)', maxWidth: 420 }}>
              {t(resultReady && !simRace ? 'race.settledBody' : simRace ? 'simrace.launchedBody' : 'race.launchedBody')}
            </Mono>
            <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('race.waitingResult')}</Mono>
            {simRace && <RaceConfigPanel client={client} />}
            <button className="btn btn-ghost" data-gamepad-back onClick={() => goTab('hub')}>{t('common.back')}</button>
          </div>
        ) : (
          <>
            {tab === 'hub' && <Home client={client} session={session} onRollover={() => setRolloverOpen(true)} dataDir={config.paths.data}
              onEnterEvent={(ev) => { setEventArmed(ev); setRaceFlow('prerace'); }}
              onViewAllEvents={() => setEventsListOpen(true)} />}
            {tab === 'championships' && <Season client={client} onGoToCalendar={() => setTab('calendar')} />}
            {tab === 'inbox' && <Inbox client={client} onGoMarket={() => goTab('market')} onGoGarage={() => goTab('garage')}
              // special EVENT invitation -- same mechanism as the hub's carousel
              // (onEnterEvent below): looks it up by id in the already-loaded list; no match -> no-op (the
              // event expired/was purged from the file between the email being sent and being opened).
              onOpenEvent={(id) => {
                const ev = specialEvents.find((e) => e.id === id);
                if (ev) { setEventArmed(ev); setRaceFlow('prerace'); }
                // the CTA never dies SILENTLY again -- an id that doesn't resolve (expired event, a file
                // not yet synced) gets logged to the support log along with the list that existed, for diagnostics.
                else supportLog(client, `open event FAILED: '${id}' not in list (${specialEvents.length} loaded)`);
              }}
              // substitution -- the Inbox already synthesizes the showcase event from the
              // email's params (subReady is computed by the engine); here it's just armed like any other event.
              onDriveSub={(ev) => { setEventArmed(ev); setRaceFlow('prerace'); }} />}
            {tab === 'profile' && <Profile client={client} />}
            {tab === 'calendar' && <Calendar client={client} dataDir={config.paths.data} onRollover={() => setRolloverOpen(true)}
              onEnterPrerace={() => { setEventArmed(null); setRaceFlow('prerace'); }} />}
            {tab === 'memories' && <Memories client={client} dataDir={config.paths.data} onGoToCalendar={() => setTab('calendar')}
              onAcquireCar={(seriesId, round) => {
                void client.cmd<{ ok: boolean; launched: boolean }>('cmd.calendar.launch', { seriesId, round })
                  // hub "Acquire car" died SILENTLY in Group C/GT1 (series closed); the engine already exempts
                  // memories from the contract gate -- if it still rejects, log it to zta_support.
                  .then((r) => { if (r?.launched) { setEventArmed(null); setRaceFlow('prerace'); } else supportLog(client, `memories acquire REFUSED: ${seriesId} R${round}`); })
                  .catch(() => { /* mock/offline: no effect */ });
              }} />}
            {tab === 'times' && <Times client={client} />}
            {tab === 'tree' && <Tree client={client} />}
            {tab === 'settings' && <Settings client={client} onEditProfile={() => setIdentityEdit(true)}
              onProfileSwitched={() => { void cs.refetch(); void idq.refetch(); setRefreshNonce((n) => n + 1); }}
              onReplayTutorial={() => { resetTutorial(); setTutorialNonce((n) => n + 1); }}
              onDataSettled={() => setSettingsReady(true)} />}
            {tab === 'liveries' && <Liveries client={client} dataDir={config.paths.data} gameInstall={config.paths.gameInstall} />}
            {tab === 'garage' && <Garage client={client} dataDir={config.paths.data} />}
            {tab === 'sponsors' && <Sponsors client={client} onOpenShop={() => setRaceFlow('shop')} />}
            {/* the Paddock REPLACES the Market (by design: PaddockScreen handles both 'paddock' AND
                'market'). Any "go check the offers" from elsewhere in the app -- emails, rollover, tryout --
                still points at 'market' and lands here, in its Opportunities sub-tab. */}
            {(tab === 'paddock' || tab === 'market') && (
              <Paddock
                client={client}
                dataDir={config.paths.data}
                initialSub={tab === 'market' ? 'opps' : undefined}
                onTryout={startTryout}
                initialNegotiate={pendingNegotiate}
                onNegotiateConsumed={() => setPendingNegotiate(null)}
              />
            )}
          </>
        )}
      </div>

      {/* DEBUG mini-menu (Ctrl+Shift+D) -- save presets. Debug-only; OVERWRITES the save. */}
      {debugOpen && (
        <div onClick={() => setDebugOpen(false)} style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.6)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 60 }}>
          <div className="panel" onClick={(e) => e.stopPropagation()} style={{ width: 320, padding: '18px 20px', display: 'flex', flexDirection: 'column', gap: 10 }}>
            <Kicker style={{ color: 'var(--amber)' }}>{t('debug.title')}</Kicker>
            <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('debug.warn')}</Mono>
            {[['debug.total', 0], ['debug.mid', 1], ['debug.clean', 2], ['debug.dda95', 3]].map(([k, preset]) => (
              <button key={preset as number} className="btn btn-ghost" style={{ justifyContent: 'flex-start' }} onClick={() => applyDebug(preset as number)}>{t(k as string)}</button>
            ))}
            {/* deliberately kept separate from the presets above. Those OVERWRITE the save; this one, by
                default, it doesn't touch anything -- it runs on a copy and shows you what would have happened. */}
            <button className="btn btn-ghost" data-debug-simworld-open="1"
                    style={{ justifyContent: 'flex-start', marginTop: 4, borderTop: '1px solid var(--line)', paddingTop: 10 }}
                    onClick={() => { setDebugOpen(false); setSimWorldOpen(true); }}>
              {t('debug.sim.open', { defaultValue: 'Simulate the world…' })}
            </button>
            <button className="btn btn-ghost" data-debug-contentlog="1" aria-pressed={contentLog}
                    style={{ justifyContent: 'flex-start' }}
                    onClick={toggleContentLog}>
              {t(contentLog ? 'debug.contentLogOn' : 'debug.contentLogOff')}
            </button>
          </div>
        </div>
      )}

      {simWorldOpen && <DebugSimWorld client={client} onClose={() => { setSimWorldOpen(false); setRefreshNonce((n) => n + 1); }} />}

      {/* exit confirmation with the game open -- close it too? */}
      {closeAsk && (
        <div onClick={() => setCloseAsk(false)} style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.6)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 70 }}>
          <div className="panel" onClick={(e) => e.stopPropagation()} style={{ width: 380, padding: '20px 22px', display: 'flex', flexDirection: 'column', gap: 12 }}>
            <Kicker style={{ color: 'var(--accent-text)' }}>{t('closeGame.title')}</Kicker>
            <Mono style={{ fontSize: 11, color: 'var(--text-2)', lineHeight: 1.55 }}>{t('closeGame.desc')}</Mono>
            <div className="row" style={{ gap: 8, justifyContent: 'flex-end', marginTop: 4 }}>
              <button className="btn btn-sm btn-ghost" onClick={() => setCloseAsk(false)}>{t('closeGame.cancel')}</button>
              <button className="btn btn-sm btn-ghost" onClick={() => doClose(false)}>{t('closeGame.appOnly')}</button>
              <button className="btn btn-sm btn-primary" onClick={() => doClose(true)}>{t('closeGame.both')}</button>
            </div>
          </div>
        </div>
      )}

      {/* startup update modal. Never shown over a launched race (raceFlow !== null covers
          the shell); dismissible per session via updateDismissed. */}
      {!updateDismissed && raceFlow === null &&
        (updater.status === 'available' || updater.status === 'downloading' || updater.status === 'restarting') && (
          <UpdateModal u={updater} onDismiss={() => setUpdateDismissed(true)} />
        )}

      {/* Side scroll arrows: essential in the VR panel, where you point with a laser and
          there's no wheel or scrollbar to drag. They mount once and find their own container. */}
      <ScrollRail />

    </div>
  );
}
