// test_dispatch.mjs -- proves the real engine answers a real query with real data.
// This is the point of the exercise: not that it compiles, but that it ANSWERS.
//
// Usage: node engine-wasm/test_dispatch.mjs [OUT_DIR]     (default: app/public/engine)
//
// The module is located at run time rather than named in a static import, for two reasons. It is a
// build ARTEFACT, not a source file: it does not exist until build_wasm.sh has run, it is never
// committed, and a static specifier pointing at it would be an unresolvable dependency for any
// tool that reads this tree as source. And build_wasm.sh already takes its output directory as an
// argument -- a test that hard-codes one place can only ever check the default build.
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { checkFixtureShape } from './fixture_shape.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const fixturesDir = path.join(here, '..', 'app', 'ui-fixtures');
const outDir = process.argv[2] ?? path.join(here, '..', 'app', 'public', 'engine');
const enginePath = path.join(outDir, 'engine.mjs');

if (!existsSync(enginePath)) {
  throw new Error(`no engine at ${enginePath} -- run build_wasm.sh first (optionally with the same OUT_DIR)`);
}

// pathToFileURL, not the bare path: on Windows an absolute path starting with a drive letter is not
// a valid ESM specifier, and Node rejects it before it ever looks at the disk.
const { default: createEngine } = await import(pathToFileURL(enginePath).href);

// locateFile tells the module where its two side files live (engine.wasm, and engine.data holding
// the packaged world). Under Node the default resolves them against the process's working
// directory, not the module's own, so `node engine-wasm/test_dispatch.mjs` from the repository root
// would look for engine.data beside the repository root. A host page has the same job to do.
const engine = await createEngine({ locateFile: (file) => path.join(outDir, file) });
const dispatch = engine.cwrap('engine_dispatch', 'string', ['string']);

const raw = dispatch(JSON.stringify({ v: 1, id: 'a1', kind: 'query', method: 'query.api_info' }));
const res = JSON.parse(raw);
if (res.kind !== 'result') throw new Error(`api_info: expected a result, got ${raw}`);
if (res.payload.build !== 'wasm') throw new Error(`api_info: wrong build: ${raw}`);

// The boot seeds the car catalogue. LoadLadder counts each ladder node's cars from the USER root's
// cache (cache/ams2_car_class.csv), not from the world, and a browser tab starts with an empty user
// root -- so without the seed every node would report zero cars. The first dispatch above booted the
// world; the seeded file must now be there, byte for byte the one the world ships.
{
  const seeded = '/save/cache/ams2_car_class.csv';
  const shipped = '/world/ams2_car_class.csv';
  if (!engine.FS.analyzePath(shipped).exists) throw new Error(`boot: the world ships no ${shipped}`);
  if (!engine.FS.analyzePath(seeded).exists) throw new Error(`boot: ${seeded} was not seeded from ${shipped}`);
  const a = engine.FS.readFile(shipped, { encoding: 'utf8' });
  const b = engine.FS.readFile(seeded, { encoding: 'utf8' });
  if (a !== b) throw new Error(`boot: ${seeded} differs from ${shipped} (${b.length} vs ${a.length} bytes)`);
  const rows = a.split('\n').filter((l) => l.trim() !== '').length - 1;   // minus the header
  if (rows < 1) throw new Error(`boot: ${shipped} has no car rows`);
  console.log(`dispatch: ok - boot seeded the car catalogue (${rows} cars in ${seeded})`);
}

// The one that matters: a real career query, answered from the invented world on the virtual disk.
const hubRaw = dispatch(JSON.stringify({ v: 1, id: 'a2', kind: 'query', method: 'query.hub' }));
const hub = JSON.parse(hubRaw);
if (hub.kind !== 'result') throw new Error(`hub: expected a result, got ${hubRaw}`);
if (typeof hub.payload !== 'object' || hub.payload === null) throw new Error(`hub: empty payload: ${hubRaw}`);

// "A non-null object" was the whole of this test's assertion once, and it was not enough. Every
// fidelity defect the browser host shipped in its first round -- a localized label where the app
// looks up a key, band arithmetic cut from the wrong constant, a doubled weekday -- produced a
// perfectly non-null object. What follows checks the SHAPE of real fields instead, so a wrong value
// has somewhere to fail.
const fail = (why) => { throw new Error(`hub: ${why}\n${hubRaw}`); };
const p = hub.payload;

// Identity, stats, progress and rating: present, and the right types.
if (typeof p.identity?.first !== 'string' || typeof p.identity?.last !== 'string') fail('no identity');
for (const k of ['wins', 'podiums', 'races']) {
  if (!Number.isInteger(p.stats?.[k])) fail(`stats.${k} is not an integer`);
}
if (!Number.isInteger(p.progress?.ownedCount)) fail('progress.ownedCount is not an integer');
if (!Number.isInteger(p.rating?.elo)) fail('rating.elo is not an integer');

// Reputation. tierKey is a KEY the app resolves against its i18n bundle (market.tier.<token>), not a
// label -- shipping a localized string here was the first round's critical defect, and it looked
// exactly as valid as this does until something checked the vocabulary.
const TIER_KEYS = ['unknown', 'prospect', 'known', 'established', 'star', 'legend'];
const rep = p.reputation;
if (!rep) fail('no reputation block');
if (!Number.isInteger(rep.score) || rep.score < 0) fail('reputation.score is not a count');
if (!TIER_KEYS.includes(rep.tierKey)) fail(`tierKey "${rep.tierKey}" is not one of the six tokens`);
if (rep.tierIdx !== TIER_KEYS.indexOf(rep.tierKey)) fail('tierIdx and tierKey disagree');
if (!Number.isInteger(rep.pct) || rep.pct < 0 || rep.pct > 100) fail(`pct out of range: ${rep.pct}`);
if (rep.next !== null && !TIER_KEYS.includes(rep.next?.tierKey)) fail('next.tierKey is not a token');
if ((rep.next === null) !== (rep.tierIdx === 5)) fail('only the top tier may report no next tier');

// Followers, against the published curve rather than against "is a number". The engine's own
// thresholds are REP_BAND (career_balance.h) and the overlay anchors one follower count to each; the
// value must land inside the segment its own tier names. This is what catches a formula swapped for
// a different one: the linear count this host shipped first would answer 1200 at score 0, where the
// curve's floor anchor is 500.
const REP_BAND = [0, 15, 40, 120, 300, 600];
const FOLLOWERS_AT_BAND = [500, 6000, 35000, 200000, 1500000, 9000000];
if (!Number.isInteger(rep.followers)) fail('followers is not an integer');
const b = rep.tierIdx;
if (rep.followers < FOLLOWERS_AT_BAND[b]) {
  fail(`followers ${rep.followers} is below the ${TIER_KEYS[b]} anchor ${FOLLOWERS_AT_BAND[b]}`);
}
if (b < 5 && rep.followers > FOLLOWERS_AT_BAND[b + 1]) {
  fail(`followers ${rep.followers} is above the next anchor ${FOLLOWERS_AT_BAND[b + 1]}`);
}
if (rep.score === REP_BAND[b] && rep.followers !== FOLLOWERS_AT_BAND[b]) {
  fail(`at the ${TIER_KEYS[b]} floor followers must be exactly ${FOLLOWERS_AT_BAND[b]}`);
}

// The date and the weekday are TWO fields, and the app renders them joined ("Wed" + " " + "1 Jan").
// A cursorDate that already carries the weekday therefore prints it twice -- which is exactly what
// this host did before, because it filled the field from the wrong one of two nearly-identical
// engine functions. Both halves are checked, and so is the fact that one does not contain the other.
const life = p.lifecycle;
if (!life) fail('no lifecycle block');
if (!/^\d{1,2} [A-Z][a-z]{2}$/.test(life.cursorDate)) fail(`cursorDate "${life.cursorDate}" is not "D Mon"`);
if (!/^[A-Z][a-z]{2}$/.test(life.weekday)) fail(`weekday "${life.weekday}" is not a three-letter day`);
if (life.cursorDate.startsWith(life.weekday)) fail('cursorDate repeats the weekday the app adds itself');
if (!Number.isInteger(life.year) || life.year < 1900) fail(`year out of range: ${life.year}`);

// contractTeams names the teams a player would strand by breaking their contracts. A blank entry is
// a nameless row in that list, never a team, so the engine drops one -- and so must this host.
if (!Array.isArray(life.contractTeams)) fail('contractTeams is not an array');
if (life.contractTeams.some((t) => typeof t !== 'string' || t === '')) fail('a contract team is blank');
if (life.contractTeams.length > life.contractCount) fail('more contract teams than contracts');

// nextRace is legitimately null (nothing is enrolled in a career that has not signed anything yet),
// but when it IS present every field it documents must be there.
if (p.nextRace !== null) {
  const nr = p.nextRace;
  if (typeof nr.series !== 'string' || nr.series === '') fail('nextRace.series is empty');
  if (typeof nr.track !== 'string' || nr.track === '') fail('nextRace.track is empty');
  if (!Number.isInteger(nr.round) || !Number.isInteger(nr.doy)) fail('nextRace round/doy are not integers');
}

console.log(`dispatch: ok - api_info and hub both answered `
  + `(${TIER_KEYS[rep.tierIdx]}, rep ${rep.score}, ${rep.followers} followers, `
  + `${life.weekday} ${life.cursorDate} ${life.year})`);

// ---- host view providers, each against the fixture the real engine dumped for it --------------------
//
// One call per provider in engine-wasm/*_view_wasm.cpp. `query` dispatches and requires a result (a
// not_ready here means the provider was never installed, or its view came back loaded=false);
// `expectShape` holds the payload to the fixture's field names and JSON types (fixture_shape.mjs) and
// prints what it could not check, so a pass never silently means "checked nothing".
let seq = 100;
// One definition of an open calendar round, for every check below that asks whether a championship can
// be entered: the slot is not locked by any of the calendar's own reasons (isLocked) and its status
// does not say locked either. Two checks with two definitions could each pass on a world they disagree on.
const isOpenSlot = (s) => !s.isLocked && s.status !== 'locked';
function query(method, params) {
  const req = { v: 1, id: `q${seq++}`, kind: 'query', method };
  if (params !== undefined) req.params = params;
  const raw = dispatch(JSON.stringify(req));
  const res = JSON.parse(raw);
  if (res.kind !== 'result') throw new Error(`${method}: expected a result, got ${raw.slice(0, 400)}`);
  return res.payload;
}
function expectShape(method, payload, fixtureName, opts) {
  const fixture = JSON.parse(readFileSync(path.join(fixturesDir, fixtureName), 'utf8'));
  const { defects, extras, unwitnessed } = checkFixtureShape(payload, fixture, opts);
  if (defects.length) {
    throw new Error(`${method}: shape differs from ${fixtureName}:\n  ${defects.join('\n  ')}`);
  }
  if (extras.length) console.log(`  ${method}: keys the fixture never shows: ${extras.join(', ')}`);
  if (unwitnessed.length) console.log(`  ${method}: not shape-checked: ${unwitnessed.join(', ')}`);
}

// query.calendar (calendar_view_wasm.cpp). `calibration` is null in the fixture (that career was
// calibrated) and an object for an uncalibrated one, so its shape is supplied from the contract
// (contracts.ts CalendarCalibration); `field` is an empty array in every fixture slot, so its element
// shape comes from the same file. `calibration` may legitimately be null.
{
  const cal = query('query.calendar');
  expectShape('query.calendar', cal, 'calendar.json', {
    witness: {
      calibration: { carName: '', trackName: '', trackHash: 0, trackSlug: '' },
      'slots[].field[]': { name: '', slug: '' },
    },
    nullable: ['calibration'],
  });
  // calibration and calibrated must agree: the serializer emits the object exactly when not calibrated.
  if ((cal.calibration === null) !== cal.calibrated) {
    throw new Error(`calendar: calibrated=${cal.calibrated} but calibration is ${JSON.stringify(cal.calibration)}`);
  }
  if (!Array.isArray(cal.slots) || cal.slots.length === 0) {
    throw new Error('calendar: no slots -- the world on the virtual disk has a calendar, so an empty one is a host defect');
  }
  // The enrolled flags and the commitments count come from the same slots and must agree (pending
  // rounds of this year only; the next-year queue is appended to the same array). For the demo career,
  // which has signed nothing, both are honestly zero -- nothing here pads them.
  const enrolled = cal.slots.filter((s) => s.enrolled && s.year === cal.today.year
    && s.status !== 'completed' && s.status !== 'dnf').length;
  if (enrolled !== cal.commitments.enrolled) {
    throw new Error(`calendar: ${enrolled} enrolled slots but commitments.enrolled=${cal.commitments.enrolled}`);
  }
  // A new career must have a way in: at least one round it could enter. The calendar locks by level, by
  // gate, by missing DLC and -- for an era-memory series -- by not owning a car of its class, which a new
  // career never does. A world whose every entry-level door is an era series is therefore all locked,
  // which is what the published world once was (113 of 113) while the season rail still called two
  // championships available.
  const open = cal.slots.filter(isOpenSlot);
  if (open.length === 0) throw new Error(`calendar: all ${cal.slots.length} slots locked -- a new career has no round it can enter`);
  const statuses = {};
  for (const s of cal.slots) statuses[s.status] = (statuses[s.status] ?? 0) + 1;
  console.log(`dispatch: ok - calendar answered (${cal.slots.length} slots, `
    + `${Object.entries(statuses).map(([k, n]) => `${n} ${k}`).join(', ')}; `
    + `calibrated=${cal.calibrated}, today ${cal.today.weekday} ${cal.today.date} ${cal.today.year})`);
}

// query.market (market_view_wasm.cpp). `contract` is null for a career with no active contract (the
// fixture career had one). The demo career is uncalibrated, so the board is honestly empty: the overlay's
// own gate gives a rookie no offers before the measurement race -- an empty `offers` is the answer, and
// is reported as not shape-checked rather than passed.
{
  const mk = query('query.market');
  expectShape('query.market', mk, 'market.json', { nullable: ['contract'] });
  const rep = mk.reputation;
  if (!TIER_KEYS.includes(rep.tierKey)) throw new Error(`market: tierKey "${rep.tierKey}" is not one of the six tokens`);
  if (rep.tierIdx !== TIER_KEYS.indexOf(rep.tierKey)) throw new Error('market: tierIdx and tierKey disagree');
  if (rep.dots.length !== 6) throw new Error(`market: ${rep.dots.length} tier dots, expected 6`);
  if (rep.dots.some((d, i) => d.filled !== (i <= rep.tierIdx))) throw new Error('market: dots.filled disagrees with tierIdx');
  if ((rep.next === null) !== (rep.tierIdx === 5)) throw new Error('market: only the top tier may report no next tier');
  // Same badge as the hub: the two screens read one reputation.
  if (rep.tierKey !== p.reputation.tierKey || rep.score !== p.reputation.score) {
    throw new Error(`market: reputation ${rep.tierKey}/${rep.score} disagrees with hub ${p.reputation.tierKey}/${p.reputation.score}`);
  }
  if (mk.seekingYouCount + mk.openSeatsCount !== mk.offers.length) {
    throw new Error(`market: ${mk.seekingYouCount}+${mk.openSeatsCount} counted but ${mk.offers.length} offers listed`);
  }
  if (mk.marketOpen && mk.daysToWindow !== 0) throw new Error(`market: open but ${mk.daysToWindow} days to the window`);
  console.log(`dispatch: ok - market answered (${rep.tierKey} ${rep.pct}%, rating ${mk.playerRating}, `
    + `${mk.marketOpen ? 'open' : `closed, ${mk.daysToWindow} days to window`}, ${mk.offers.length} offers, `
    + `${mk.watching.length} watching, contract ${mk.contract === null ? 'none' : mk.contract.count})`);
}

let seasonRail = null;   // kept for the tree check below: the ladder and the rail describe the same championships
// query.season (season_view_wasm.cpp), parameterized by {seriesId}. The demo career is enrolled in
// nothing, so "my championships" is empty and so is every standings table; the default selection is the
// first available championship (the INFO detail). A gated championship, asked for by id, must answer
// with the ACCESS detail -- the second variant -- naming its qualifier.
{
  const se = query('query.season');
  expectShape('query.season', se, 'season.json');
  if (!Array.isArray(se.rail) || se.rail.length === 0) throw new Error('season: empty rail -- the world has championships');
  const selected = se.rail.filter((it) => it.selected);
  if (selected.length !== 1 || selected[0].seriesId !== se.selectedSeriesId) {
    throw new Error(`season: ${selected.length} rail items selected, selectedSeriesId=${se.selectedSeriesId}`);
  }
  const mine = se.rail.filter((it) => it.section === 0).length;
  const enrolledNow = query('query.calendar').commitments.enrolled;
  if (mine > 0 && enrolledNow === 0) throw new Error(`season: ${mine} championships of mine, calendar enrolled none`);
  if (se.detail.kind !== 1 && se.detail.standings.length !== 0) throw new Error('season: standings outside the enrolled detail');
  if (se.bonusShown) throw new Error('season: a season-end bonus with no race ever settled');
  // An explicit selection is honoured, and an unknown id falls back to the default.
  const other = se.rail.find((it) => !it.selected);
  if (other) {
    const so = query('query.season', { seriesId: other.seriesId });
    if (so.selectedSeriesId !== other.seriesId) throw new Error(`season: asked for ${other.seriesId}, got ${so.selectedSeriesId}`);
  }
  if (query('query.season', { seriesId: 'no_such_series' }).selectedSeriesId !== se.selectedSeriesId) {
    throw new Error('season: an unknown seriesId did not fall back to the default selection');
  }
  let access = 'no gated championship in this world';
  const gated = se.rail.find((it) => it.section === 2 && it.gateLocked);
  if (gated) {
    const sg = query('query.season', { seriesId: gated.seriesId });
    expectShape('query.season {gated}', sg, 'season.json');
    if (sg.selectedSeriesId !== gated.seriesId) throw new Error(`season: asked for ${gated.seriesId}, got ${sg.selectedSeriesId}`);
    if (sg.detail.kind !== 2 || sg.detail.qualDisplay === '') {
      throw new Error(`season: gated ${gated.seriesId} gave detail kind ${sg.detail.kind}, qual "${sg.detail.qualDisplay}"`);
    }
    access = `${gated.seriesId} -> access via "${sg.detail.qualDisplay}"`;
  }
  // The published world gates championships by level and by super-licence unlocks (series.psv), so a new
  // career at frontier 1 must find at least one of them locked -- by level above its frontier, or by a
  // gate not yet passed. A rail with nothing locked means the gating columns were lost on the way in.
  // Every locked entry must say why, and the reason must be one of the two the overlay knows.
  const frontier = p.rating.phase;
  const lockedBy = { level: 0, gate: 0 };
  for (const it of se.rail) {
    if (it.section !== 2) continue;
    if (it.gateLocked) lockedBy.gate++;
    else if (it.level > frontier) lockedBy.level++;
    else throw new Error(`season: ${it.seriesId} is locked at level ${it.level} <= frontier ${frontier} with no gate`);
  }
  if (lockedBy.level + lockedBy.gate === 0) throw new Error('season: no championship locked by level or unlock');
  // The rail and the calendar must agree on what is open, both ways: a championship the rail offers as
  // available has to have at least one round the calendar lets the player enter, and one the rail shows
  // locked must have none.
  const calSlots = query('query.calendar').slots;
  for (const it of se.rail) {
    const openRounds = calSlots.filter((s) => s.seriesId === it.seriesId && isOpenSlot(s)).length;
    if (it.section === 1 && openRounds === 0) {
      throw new Error(`season: ${it.seriesId} is available on the rail but every calendar round of it is locked`);
    }
    if (it.section === 2 && openRounds > 0) {
      throw new Error(`season: ${it.seriesId} is locked on the rail but the calendar opens ${openRounds} round(s) of it`);
    }
  }
  // A new career opens the season screen on its default championship, and that must be one it can enter:
  // the rail's first entry in series order, so a world listing a gated championship first lands the
  // player on a locked door.
  const def = se.rail.find((it) => it.seriesId === se.selectedSeriesId);
  if (!def || def.section === 2) {
    throw new Error(`season: the default selection ${se.selectedSeriesId} is a locked championship (section ${def?.section})`);
  }
  if (se.detail.kind === 2) throw new Error(`season: the default selection ${se.selectedSeriesId} opens on the access detail`);
  seasonRail = se.rail;
  const bySection = [0, 1, 2].map((s) => se.rail.filter((it) => it.section === s).length);
  console.log(`dispatch: ok - season answered (rail ${se.rail.length}: ${bySection[0]} mine, ${bySection[1]} available, `
    + `${bySection[2]} locked (${lockedBy.level} by level, ${lockedBy.gate} by unlock); default ${se.selectedSeriesId} `
    + `(section ${def.section}) kind ${se.detail.kind} with ${se.detail.rounds.length} rounds; ${access}; `
    + `rail and calendar agree both ways)`);
}

// query.garage (garage_view_wasm.cpp). `contract` is null with no active contract. The owned-car list
// is held against the hub's owned count: the cars come from GarageOwnedCars, which leaves out a car it
// cannot resolve, so a car the save holds and the catalog drops fails here instead of the garage quietly
// showing fewer cars than the player has. A new career owns none, so the demo's list is empty.
{
  const ga = query('query.garage');
  expectShape('query.garage', ga, 'garage.json', { nullable: ['contract'] });
  if (ga.tier.token !== p.reputation.tierKey || ga.tier.band !== p.reputation.tierIdx) {
    throw new Error(`garage: tier ${ga.tier.band}/${ga.tier.token} disagrees with hub ${p.reputation.tierIdx}/${p.reputation.tierKey}`);
  }
  if (ga.cars.length !== p.progress.ownedCount) {
    throw new Error(`garage: ${ga.cars.length} cars listed, hub says ${p.progress.ownedCount} owned`);
  }
  if ((ga.contract === null) !== (p.lifecycle.contractCount === 0)) {
    throw new Error(`garage: contract ${JSON.stringify(ga.contract)} but hub counts ${p.lifecycle.contractCount}`);
  }
  console.log(`dispatch: ok - garage answered (${ga.credits} credits, ${ga.tier.token}, ${ga.cars.length} cars, `
    + `contract ${ga.contract === null ? 'none' : ga.contract.series})`);
}

// query.profile (profile_view_wasm.cpp), parameterized by {filter, full}. The demo career has raced
// nothing, so its arc is the single "now" point, its form and rivals' records are empty, and its delta is
// 0; the world ranking is the real rating pool of the invented world, with the player placed in it.
{
  const pr = query('query.profile', { filter: '' });
  expectShape('query.profile', pr, 'profile.json', { nullable: ['rating.next'] });
  const prFull = query('query.profile', { filter: '', full: true });
  expectShape('query.profile {full}', prFull, 'profile.full.json', { nullable: ['rating.next'] });
  const mk = query('query.market');
  if (pr.rating.score !== mk.playerRating) throw new Error(`profile: score ${pr.rating.score} but market rating ${mk.playerRating}`);
  if (pr.rating.delta !== 0) throw new Error(`profile: delta ${pr.rating.delta} with no race ever settled`);
  const nowPt = pr.arc[pr.arc.length - 1];
  if (!nowPt || nowPt.season !== 'profile.now' || nowPt.rating !== pr.rating.score) {
    throw new Error(`profile: the arc does not end on "now" at the live score: ${JSON.stringify(pr.arc)}`);
  }
  const wr = pr.worldRanking;
  const shown = [...wr.top, ...wr.neighborhood];
  if (shown.filter((r) => r.isPlayer).length !== 1) throw new Error('profile: the player is not shown exactly once in the ranking');
  if (prFull.worldRanking.all.length !== wr.total) throw new Error(`profile: full ranking ${prFull.worldRanking.all.length} rows, total ${wr.total}`);
  const me = prFull.worldRanking.all.find((r) => r.isPlayer);
  if (me.rank !== pr.rating.worldRank) throw new Error(`profile: hero rank ${pr.rating.worldRank} but row rank ${me.rank}`);
  if (pr.filter.options[0]?.id !== '' || pr.filter.options[0]?.label !== 'profile.filterAll') {
    throw new Error(`profile: the first filter option is not "All": ${JSON.stringify(pr.filter.options[0])}`);
  }
  // The hero's rank is the GLOBAL one whatever filter the table uses.
  let filtered = 'no series filter in this world';
  const opt = pr.filter.options.find((o) => o.id !== '');
  if (opt) {
    const pf = query('query.profile', { filter: opt.id });
    if (pf.filter.selectedId !== opt.id) throw new Error(`profile: asked for filter ${opt.id}, got ${pf.filter.selectedId}`);
    if (pf.rating.worldRank !== pr.rating.worldRank) {
      throw new Error(`profile: hero rank ${pf.rating.worldRank} under filter ${opt.id}, ${pr.rating.worldRank} unfiltered`);
    }
    filtered = `filter ${opt.id}: ${pf.worldRanking.total} rows`;
  }
  console.log(`dispatch: ok - profile answered (${pr.rating.score} ${pr.rating.band.letter}, world #${pr.rating.worldRank} of ${wr.total}, `
    + `${pr.rivals.length} rivals, ${pr.recentForm.length} recent races, ${pr.filter.options.length} filter options; ${filtered})`);
}

// query.tree (tree_view_wasm.cpp). The header (frontier, band) is the career's; the nodes are the ladder
// LoadLadder reads off the world (career_ladder.psv), with car counts from the catalogue the boot seeded.
// The band key must be the tree's i18n key for the hub's own band. A node whose class has no car in the
// catalogue counts zero, and the view must still answer -- query() above already requires a result.
{
  const tr = query('query.tree');
  expectShape('query.tree', tr, 'tree.json');
  if (tr.ratingTier.index !== p.reputation.tierIdx) throw new Error(`tree: band ${tr.ratingTier.index}, hub ${p.reputation.tierIdx}`);
  if (tr.ratingTier.key !== `tree.tier.${p.reputation.tierKey}`) throw new Error(`tree: band key "${tr.ratingTier.key}"`);
  if (tr.nodes.length === 0) throw new Error('tree: no nodes -- the world ships a ladder, so an empty one is a host defect');
  const badLock = tr.nodes.find((n) => n.locked !== (n.level > tr.frontierPhase));
  if (badLock) throw new Error(`tree: ${badLock.id} level ${badLock.level} locked=${badLock.locked}, frontier ${tr.frontierPhase}`);
  const withCars = tr.nodes.filter((n) => n.carCount > 0).length;
  if (withCars === 0) throw new Error('tree: every node counts zero cars -- the seeded catalogue was not read');
  // The world ships one add-on node whose class has no car in the catalogue (an uninstalled DLC class).
  // It must come through with zero cars, and the view must still have answered -- query() threw otherwise.
  if (!tr.nodes.some((n) => n.carCount === 0)) throw new Error('tree: no node with zero cars -- the add-on node did not come through');
  // The ladder and the series tables describe the same championships, so a node that is a series must
  // agree with that series' rail entry: same level, same name, and the same discipline. The rail carries
  // no discipline name, only the colour the engine paints it with (Col::Disc of DiscForSeries) -- so the
  // tree's discipline and the rail's colour must map one to one: two nodes of one discipline share a
  // colour, two disciplines never do. The tree groups by its own discipline (Tree.tsx), the calendar and
  // the rail colour by the engine's; disagreeing, one championship shows up in two families.
  const railById = new Map(seasonRail.map((it) => [it.seriesId, it]));
  const colourOf = new Map(), discOf = new Map();
  let paired = 0;
  for (const n of tr.nodes) {
    const it = railById.get(n.id);
    if (!it) continue;
    paired++;
    if (n.level !== it.level) throw new Error(`tree: ${n.id} at level ${n.level}, the rail says ${it.level}`);
    if (n.display !== it.display) throw new Error(`tree: ${n.id} named "${n.display}", the rail says "${it.display}"`);
    if (colourOf.has(n.disc) && colourOf.get(n.disc) !== it.discColor) {
      throw new Error(`tree: discipline ${n.disc} is painted ${colourOf.get(n.disc)} and ${it.discColor} on the rail`);
    }
    if (discOf.has(it.discColor) && discOf.get(it.discColor) !== n.disc) {
      throw new Error(`tree: ${n.id} is ${n.disc} but the rail paints it like ${discOf.get(it.discColor)} (${it.discColor})`);
    }
    colourOf.set(n.disc, it.discColor);
    discOf.set(it.discColor, n.disc);
  }
  if (paired !== seasonRail.length) throw new Error(`tree: ${paired} of ${seasonRail.length} rail championships have a ladder node`);
  const lockedNodes = tr.nodes.filter((n) => n.locked).length;
  console.log(`dispatch: ok - tree answered (frontier ${tr.frontierPhase}, ${tr.ratingTier.key}, ${tr.nodes.length} nodes: `
    + `${lockedNodes} locked, ${withCars} with cars, ${tr.nodes.length - withCars} with none; `
    + `${paired} agree with the rail on level, name and discipline (${[...colourOf.keys()].sort().join('/')}))`);
}

// query.prerace (prerace_view_wasm.cpp), parameterized by {carId}. This host cannot arm a race -- the
// arming commands have no executor here, which is checked below rather than assumed -- so the answer is
// the engine's "pick a race" form: the serializer emits `clsEmpty` alone. The fixture witnesses the armed
// form, so the shape check holds the payload to the keys that form shares with it (just `clsEmpty`), and
// anything else in the payload is a defect.
{
  const pre = query('query.prerace');
  const preFixture = JSON.parse(readFileSync(path.join(fixturesDir, 'prerace.json'), 'utf8'));
  const { defects } = checkFixtureShape(pre, { clsEmpty: preFixture.clsEmpty });
  if (defects.length) throw new Error(`query.prerace: shape differs from prerace.json:\n  ${defects.join('\n  ')}`);
  if (pre.clsEmpty !== true || Object.keys(pre).length !== 1) {
    throw new Error(`prerace: expected the unarmed form {clsEmpty:true}, got ${JSON.stringify(pre).slice(0, 300)}`);
  }
  if (JSON.stringify(query('query.prerace', { carId: 'any_car' })) !== JSON.stringify(pre)) {
    throw new Error('prerace: a carId changed the unarmed answer');
  }
  const arm = JSON.parse(dispatch(JSON.stringify({ v: 1, id: `c${seq++}`, kind: 'cmd', method: 'cmd.calendar.launch',
    params: { seriesId: query('query.season').selectedSeriesId, round: 1 } })));
  if (arm.kind === 'result') throw new Error(`prerace: cmd.calendar.launch armed a race in a host that cannot: ${JSON.stringify(arm)}`);
  console.log(`dispatch: ok - prerace answered (clsEmpty; cmd.calendar.launch -> ${arm.code})`);
}
