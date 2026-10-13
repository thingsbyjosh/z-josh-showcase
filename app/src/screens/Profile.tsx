// Profile / Driver profile: a read-only port of the engine's own profile-drawing code (overlay = source of truth
// for which data is shown and in what order). The design mockup (screen_profile.jsx) only governs the visuals.
// The rating is the ENGINE's ONLY number (ELO = unified REP): the hero, the arc, rivals, and the ranking are 4
// views of the SAME number. Everything arrives via query.profile (read-only); the series filter travels as a
// query parameter (the read-only equivalent of the overlay's own filter state). Translatable strings
// from the engine (band name, rival notes, the arc's "Now" label, the filter's "All") arrive
// as i18n KEYS in the payload and are resolved via t() (bilingual via the translation table; a missing key falls back to itself).
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, ProfilePayload, ProfileRankRow, SuperlicensesPayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Panel, Display, Mono, Kicker, Chip, LockIcon, Btn } from '../ui/components';
import { isDnf, dnfLabel } from '../ui/format';
import { accentText } from '../ui/colors';
import { PlayerAvatar } from '../ui/PlayerAvatar';
import { getBridge } from '../shell/bridge';
import { pickAndSaveAvatar, removeAvatar } from '../ui/avatarStore';

const ARC_W = 560;
const ARC_H = 150;
const ARC_PAD = 8;
// A fixed 600..2600 scale used to flatten ±20 pt deltas (~1px) and the arc "looked frozen".
// Auto-scales to the data's own range with a minimum span, so real progress stays visible.
const ARC_MIN_SPAN = 160; // rating pts: minimum Y-axis span (keeps noise from filling the whole chart)

function initials(s: string): string {
  return s.split(/\s+/).filter(Boolean).map((w) => w[0]).slice(0, 2).join('').toUpperCase();
}

// recent form: gold=P1, cyan=podium, text-2=top10, dimmed=rest/DNF
function formColor(pos: number, finished?: boolean): string {
  if (isDnf(pos, finished)) return 'var(--text-3)';
  if (pos === 1) return 'var(--gold)';
  if (pos <= 3) return 'var(--cyan)';
  if (pos <= 10) return 'var(--text-2)';
  return 'var(--text-3)';
}

export function Profile({ client }: { client: ApiClientLike }) {
  const { t } = useTranslation();
  // series filter: "" = All. Travels as a query parameter -> refetches on change (read-only).
  const [filterId, setFilterId] = useState('');
  const q = useQuery<ProfilePayload>(client, 'query.profile', { filter: filterId });
  // super-licenses — status + both progress paths. An older engine build without this query -> error -> section hidden.
  const supl = useQuery<SuperlicensesPayload>(client, 'query.superlicenses').data;
  // a simple %d/%s formatter for the engine bundle's string templates (same helper as the Season screen).
  const sfmt = (s: string, ...vals: (string | number)[]): string => { let i = 0; return s.replace(/%[ds]/g, () => String(vals[i++] ?? '')); };
  // "see more": the panel shows a trimmed view (top-10 + your neighborhood); the modal shows the FULL table.
  // It's fetched ON DEMAND (query.profile with full:true) because it's hundreds of rows: it shouldn't travel with the
  // panel's regular poll. null while loading; [] = the engine doesn't provide it (an older build) -> falls back to the trimmed view.
  const [rankOpen, setRankOpen] = useState(false);
  const [allRows, setAllRows] = useState<ProfileRankRow[] | null>(null);
  useEffect(() => {
    if (!rankOpen) return;
    let live = true;
    setAllRows(null);
    void client.query<ProfilePayload>('query.profile', { filter: filterId, full: true })
      .then((d) => { if (live) setAllRows(d.worldRanking.all ?? []); })
      .catch(() => { if (live) setAllRows([]); });
    return () => { live = false; };
  }, [rankOpen, filterId, client]);
  useEffect(() => {
    if (!rankOpen) return;
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') setRankOpen(false); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [rankOpen]);

  // Profile photo (thread "Custom Avatar picture"): goes up after saving/deleting to force PlayerAvatar
  // to reread the file (avatar.png does not change its name, so without this it would keep showing the old one
  // until the screen remounts). `avatarBusy` disables both buttons while the picker/save
  // is in progress; `avatarErr` is the only notice needed (cancelling the picker is not an error).
  const [avatarVersion, setAvatarVersion] = useState(0);
  const [avatarBusy, setAvatarBusy] = useState(false);
  const [avatarErr, setAvatarErr] = useState('');
  const changeAvatar = () => {
    if (avatarBusy) return;
    setAvatarBusy(true); setAvatarErr('');
    void getBridge().then((b) => b.paths.localAppData())
      .then((lad) => pickAndSaveAvatar(lad, t('profile.avatar.pickTitle')))
      .then((path) => { if (path) setAvatarVersion((v) => v + 1); })
      .catch((e) => setAvatarErr(String(e)))
      .finally(() => setAvatarBusy(false));
  };
  const clearAvatar = () => {
    if (avatarBusy) return;
    setAvatarBusy(true); setAvatarErr('');
    void getBridge().then((b) => b.paths.localAppData())
      .then((lad) => removeAvatar(lad))
      .then(() => setAvatarVersion((v) => v + 1))
      .catch((e) => setAvatarErr(String(e)))
      .finally(() => setAvatarBusy(false));
  };

  // no loading state on refetch: if data already exists, it's kept while reloading (no flicker while filtering)
  if (!q.data) {
    const msg = q.error ? t('home.noCareer') : t('common.loading');
    return (
      <div className="col center grow" style={{ height: '100%' }}>
        <Mono style={{ color: 'var(--text-3)' }}>{msg}</Mono>
      </div>
    );
  }
  const p = q.data;
  const band = p.rating.band;
  // "see all" only makes sense if the real table is LONGER than what's already visible.
  // missing total (older engine build) -> the option is still offered: the modal falls back to the trimmed view instead of misleading with an empty list.
  const shownRanks = p.worldRanking.top.length + p.worldRanking.neighborhood.length;
  const rankTotal = p.worldRanking.total;
  const showViewAll = shownRanks > 0 && (rankTotal == null || rankTotal > shownRanks);
  // modal rows: the engine's full table; if it didn't arrive, the same trimmed view as the panel (never empty).
  const fullRows: ProfileRankRow[] | null =
    allRows == null ? null
    : allRows.length > 0 ? allRows
    : [...p.worldRanking.top, ...p.worldRanking.neighborhood];

  // arc data points (a hand-rolled line chart, Y-axis auto-scaled to the data's range with a minimum span)
  const ratings = p.arc.map((a) => a.rating);
  const rLo = ratings.length ? Math.min(...ratings) : 0;
  const rHi = ratings.length ? Math.max(...ratings) : 0;
  const span = Math.max(rHi - rLo, ARC_MIN_SPAN);
  const yMin = (rLo + rHi) / 2 - span / 2 - span * 0.1;
  const yMax = (rLo + rHi) / 2 + span / 2 + span * 0.1;
  const pts = p.arc.map((a, i) => {
    const x = ARC_PAD + (p.arc.length > 1 ? i / (p.arc.length - 1) : 0) * (ARC_W - ARC_PAD * 2);
    const c = Math.max(yMin, Math.min(yMax, a.rating));
    const y = ARC_PAD + (1 - (c - yMin) / (yMax - yMin)) * (ARC_H - ARC_PAD * 2);
    return { x, y, season: a.season };
  });
  const linePath = pts.map((pt, i) => (i ? 'L' : 'M') + pt.x.toFixed(1) + ',' + pt.y.toFixed(1)).join(' ');
  const areaPath = pts.length
    ? `${linePath} L${pts[pts.length - 1].x.toFixed(1)},${ARC_H - ARC_PAD} L${pts[0].x.toFixed(1)},${ARC_H - ARC_PAD} Z`
    : '';

  // career record: wins/podiums/races/titles/experience/age (titles and xp are engine placeholders)
  const stats: Array<{ n: number; label: string; color: string }> = [
    { n: p.palmares.wins, label: t('profile.wins'), color: 'var(--gold)' },
    { n: p.palmares.podiums, label: t('profile.podiums'), color: 'var(--cyan)' },
    { n: p.palmares.races, label: t('profile.races'), color: 'var(--accent)' },
    { n: p.palmares.titles, label: t('profile.titles'), color: 'var(--green)' },
    { n: p.palmares.xp, label: t('profile.xp'), color: 'var(--text-2)' },
    { n: p.palmares.age, label: `${t('profile.age')} · ${t('profile.years')}`, color: 'var(--text-2)' },
  ];

  // ranking row = # + name + (rating · band letter), the letter tinted with the band's color.
  const renderRank = (row: ProfileRankRow) => (
    <div
      key={`${row.rank}-${row.name}`}
      className="row between"
      style={{
        padding: '5px 8px',
        borderRadius: 3,
        background: row.isPlayer ? 'color-mix(in oklab, var(--accent) 14%, transparent)' : 'transparent',
      }}
    >
      <span className="row gap8" style={{ minWidth: 0 }}>
        <Mono style={{ fontSize: 11, color: 'var(--text-3)', width: 22, flex: 'none' }}>{row.rank}</Mono>
        <span
          style={{
            fontSize: 13,
            fontWeight: row.isPlayer ? 700 : 500,
            color: row.isPlayer ? 'var(--accent-text)' : 'var(--text)',
            whiteSpace: 'nowrap',
            overflow: 'hidden',
            textOverflow: 'ellipsis',
          }}
        >
          {row.isPlayer ? '▸ ' : ''}
          {row.name}
        </span>
        {row.isPlayer && row.nationality && (
          <Mono style={{ fontSize: 9, letterSpacing: '.08em', color: 'var(--text-3)', flex: 'none' }}>{row.nationality}</Mono>
        )}
      </span>
      <span className="row gap8" style={{ flex: 'none' }}>
        <Mono style={{ fontSize: 12, color: 'var(--text-2)' }}>{row.rating}</Mono>
        <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>·</Mono>
        <Mono style={{ fontSize: 11, fontWeight: 700, color: accentText(row.bandColor) }}>{row.bandLetter}</Mono>
      </span>
    </div>
  );

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div className="row between" style={{ padding: '18px 30px 12px', alignItems: 'flex-end' }}>
        <div className="row gap12" style={{ alignItems: 'center' }}>
          {/* Profile photo: click on the avatar (or the button) opens the native picker; crops to a square and
              saves in the app's data folder (avatarStore.ts). Without a photo, it falls back to initials. */}
          <button onClick={changeAvatar} disabled={avatarBusy} title={t('profile.avatar.pickTitle')}
            style={{ padding: 0, border: 'none', background: 'none', cursor: avatarBusy ? 'default' : 'pointer', borderRadius: 8 }}>
            <PlayerAvatar initials={initials(p.playerName)} size={56} fontSize={22} version={avatarVersion} />
          </button>
          <div>
            <Display as="h1" style={{ fontSize: 30, whiteSpace: 'nowrap' }}>{t('nav.profile')}</Display>
            <Mono style={{ fontSize: 12, color: 'var(--text-3)', marginTop: 3, display: 'block' }}>{t('profile.sub')}</Mono>
          </div>
        </div>
        <div className="col" style={{ gap: 4, alignItems: 'flex-end' }}>
          <div className="row gap8">
            <Btn variant="ghost" size="sm" onClick={changeAvatar} disabled={avatarBusy}>{t('profile.avatar.change')}</Btn>
            <Btn variant="ghost" size="sm" onClick={clearAvatar} disabled={avatarBusy}>{t('profile.avatar.remove')}</Btn>
          </div>
          {avatarErr && <Mono style={{ fontSize: 10, color: 'var(--danger)', maxWidth: 320, textAlign: 'right' }}>{avatarErr}</Mono>}
        </div>
      </div>

      <div className="scroll" style={{ flex: 1, padding: '8px 30px 34px' }}>
        <div style={{ display: 'grid', gridTemplateColumns: '1.4fr 1fr', gap: 18, alignItems: 'start' }}>
          {/* ================= LEFT ================= */}
          <div className="col" style={{ gap: 18 }}>
            {/* rating HERO */}
            <Panel data-tour="profile.hero" style={{ padding: '22px 24px', position: 'relative', overflow: 'hidden' }}>
              <div
                style={{
                  position: 'absolute',
                  inset: 0,
                  background: `radial-gradient(90% 80% at 100% 0%, color-mix(in oklab, ${band.color} 20%, transparent), transparent 60%)`,
                }}
              />
              <div className="row gap24" style={{ position: 'relative' }}>
                {/* band hexagon with the letter */}
                <div
                  style={{
                    flex: 'none',
                    width: 96,
                    height: 110,
                    clipPath: 'polygon(50% 0,100% 25%,100% 75%,50% 100%,0 75%,0 25%)',
                    background: `color-mix(in oklab, ${band.color} 26%, var(--surface-3))`,
                    border: `2px solid ${band.color}`,
                    display: 'grid',
                    placeItems: 'center',
                  }}
                >
                  <Display style={{ fontSize: 58, fontStyle: 'italic', color: band.color, lineHeight: 0.8 }}>{band.letter}</Display>
                </div>
                <div className="col" style={{ gap: 4 }}>
                  <Kicker>{t('profile.rating')}</Kicker>
                  <div className="row gap12" style={{ alignItems: 'baseline' }}>
                    <Display style={{ fontSize: 56, fontStyle: 'italic', lineHeight: 0.85 }}>{p.rating.score}</Display>
                    {/* per-race delta (no "provisional" badge); hidden if 0 */}
                    {p.rating.delta !== 0 && (
                      <Mono style={{ fontSize: 13, fontWeight: 700, color: p.rating.delta > 0 ? 'var(--green)' : 'var(--danger)' }}>
                        {p.rating.delta > 0 ? '+' : ''}
                        {p.rating.delta} {p.rating.delta > 0 ? '↑' : '↓'}
                      </Mono>
                    )}
                  </div>
                  <Chip style={{ alignSelf: 'flex-start', marginTop: 4, color: accentText(band.color), borderColor: `color-mix(in oklab, ${band.color} 42%, transparent)` }}>
                    {t(band.nameKey)}
                  </Chip>
                  {p.rating.next && (
                    <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', marginTop: 4 }}>
                      {t('profile.nextBand', { gap: p.rating.next.gap, name: t(p.rating.next.nameKey), letter: p.rating.next.letter })}
                    </Mono>
                  )}
                  {/* what the rating actually IS, in one line (not just another number with no context) */}
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)', marginTop: 6, maxWidth: 360, lineHeight: 1.55 }}>
                    {t('profile.ratingHint')}
                  </Mono>
                </div>
                <div className="grow" />
                <div className="col center" style={{ flex: 'none' }}>
                  <Display style={{ fontSize: 40, fontStyle: 'italic' }}>#{p.rating.worldRank}</Display>
                  <Mono style={{ fontSize: 9.5, letterSpacing: '.14em', color: 'var(--text-3)', marginTop: 4, textTransform: 'uppercase' }}>
                    {t('profile.worldRank')}
                  </Mono>
                </div>
              </div>
            </Panel>

            {/* CAREER ARC */}
            <Panel data-tour="profile.arc" style={{ padding: '18px 22px' }}>
              <div className="row between" style={{ marginBottom: 12 }}>
                <Kicker>{t('profile.careerArc')}</Kicker>
                {/* delta from the FIRST real point on the arc (it used to say a hardcoded "since S3") */}
                {p.arc.length >= 2 && (
                  <Mono style={{ fontSize: 11, color: p.arcDelta >= 0 ? 'var(--green)' : 'var(--danger)' }}>
                    {p.arcDelta >= 0 ? '+' : ''}
                    {p.arcDelta} {t('profile.since', { s: t(p.arc[0].season) })}
                  </Mono>
                )}
              </div>
              <svg viewBox={`0 0 ${ARC_W} ${ARC_H}`} style={{ width: '100%', height: 150 }}>
                <defs>
                  <linearGradient id="arcfill" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%" stopColor="var(--accent)" stopOpacity="0.32" />
                    <stop offset="100%" stopColor="var(--accent)" stopOpacity="0" />
                  </linearGradient>
                </defs>
                {[0.25, 0.5, 0.75].map((g) => (
                  <line
                    key={g}
                    x1={ARC_PAD}
                    x2={ARC_W - ARC_PAD}
                    y1={ARC_PAD + g * (ARC_H - ARC_PAD * 2)}
                    y2={ARC_PAD + g * (ARC_H - ARC_PAD * 2)}
                    stroke="var(--line)"
                    strokeWidth="1"
                  />
                ))}
                {areaPath && <path d={areaPath} fill="url(#arcfill)" />}
                {linePath && <path d={linePath} fill="none" stroke="var(--accent)" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" />}
                {pts.map((pt, i) => (
                  <g key={i}>
                    <circle
                      cx={pt.x}
                      cy={pt.y}
                      r={i === pts.length - 1 ? 5 : 3.5}
                      fill={i === pts.length - 1 ? 'var(--accent)' : 'var(--bg)'}
                      stroke="var(--accent)"
                      strokeWidth="2"
                    />
                    <text x={pt.x} y={ARC_H - 1} textAnchor="middle" fontFamily="var(--font-mono)" fontSize="9" fill="var(--text-3)">
                      {t(pt.season)}
                    </text>
                  </g>
                ))}
              </svg>
            </Panel>

            {/* RIVALS & NEMESIS (relocated below the arc, compact cards) */}
            <Panel style={{ padding: '16px 22px' }}>
              <Kicker style={{ marginBottom: 12 }}>{t('profile.rivals')}</Kicker>
              <div className="col gap12">
                {p.rivals.map((rv, i) => {
                  const tot = rv.h2hWins + rv.h2hLosses;
                  const noData = tot === 0; // no head-to-heads -> "—" + a flat bar (so it doesn't look like a won duel)
                  const winPct = noData ? 0 : (rv.h2hWins / tot) * 100;
                  return (
                    <div
                      key={i}
                      className="panel"
                      style={{ padding: '12px 14px', background: 'var(--bg-2)', borderColor: `color-mix(in oklab, ${rv.color} 25%, var(--line))` }}
                    >
                      <div className="row between" style={{ marginBottom: 8 }}>
                        <div className="row gap12" style={{ minWidth: 0 }}>
                          <span
                            className="mail-avatar"
                            style={{ width: 32, height: 32, fontSize: 12, background: `color-mix(in oklab, ${rv.color} 24%, var(--surface-3))`, color: accentText(rv.color) }}
                          >
                            {initials(rv.name)}
                          </span>
                          <div className="col" style={{ gap: 2, minWidth: 0 }}>
                            <span style={{ fontSize: 13.5, fontWeight: 700, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{rv.name}</span>
                            <Mono style={{ fontSize: 9.5, color: 'var(--text-3)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                              {rv.team} · {rv.rating} · {t(rv.noteKey)}
                            </Mono>
                          </div>
                        </div>
                        <Mono
                          style={{
                            fontSize: 13,
                            fontWeight: 700,
                            flex: 'none',
                            color: noData ? 'var(--text-3)' : rv.h2hWins >= rv.h2hLosses ? 'var(--green)' : 'var(--danger)',
                          }}
                        >
                          {noData ? '—' : `${rv.h2hWins}–${rv.h2hLosses}`}
                        </Mono>
                      </div>
                      <div className="bar" style={{ height: 4 }}>
                        <i style={{ width: `${winPct}%`, background: noData ? 'var(--line)' : rv.color }} />
                      </div>
                    </div>
                  );
                })}
              </div>
            </Panel>

            {/* RECENT FORM (oldest on the left) */}
            <Panel style={{ padding: '16px 22px' }}>
              <Kicker style={{ marginBottom: 12 }}>{t('profile.form')}</Kicker>
              {p.recentForm.length === 0 ? (
                <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('profile.noRaces')}</Mono>
              ) : (
                <div className="row gap8 wrap">
                  {p.recentForm.slice(0, 8).map((f, i) => {
                    const c = formColor(f.pos, f.finished);
                    // P2 (F15): a retirement says DNF and, below, the position you retired in
                    const dnf = isDnf(f.pos, f.finished);
                    return (
                      <div
                        key={i}
                        title={dnf ? dnfLabel(f.pos) : undefined}
                        style={{
                          width: 36,
                          height: 40,
                          borderRadius: 3,
                          background: 'var(--bg-2)',
                          border: `1px solid color-mix(in oklab, ${c} 40%, var(--line))`,
                          display: 'flex',
                          flexDirection: 'column',
                          alignItems: 'center',
                          justifyContent: 'center',
                          gap: 1,
                        }}
                      >
                        {dnf ? (
                          <>
                            <Display style={{ fontSize: 12, color: c }}>DNF</Display>
                            {f.pos > 0 && <Mono style={{ fontSize: 9, color: c }}>P{f.pos}</Mono>}
                          </>
                        ) : (
                          <Display style={{ fontSize: 17, color: c }}>{f.pos}</Display>
                        )}
                        <span style={{ width: 16, height: 2, background: c, borderRadius: 1 }} />
                      </div>
                    );
                  })}
                </div>
              )}
            </Panel>
          </div>

          {/* ================= RIGHT ================= */}
          <div className="col" style={{ gap: 18 }}>
            {/* CAREER RECORD */}
            <Panel style={{ padding: '18px 22px' }}>
              <Kicker style={{ marginBottom: 14 }}>{t('profile.palmares')}</Kicker>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px 18px' }}>
                {stats.map((s, i) => (
                  <div key={i} className="row gap12" style={{ alignItems: 'baseline' }}>
                    <Display style={{ fontSize: 28, fontStyle: 'italic', color: s.color, minWidth: 52 }}>{s.n}</Display>
                    <Mono style={{ fontSize: 10, letterSpacing: '.12em', textTransform: 'uppercase', color: 'var(--text-3)' }}>{s.label}</Mono>
                  </div>
                ))}
              </div>
            </Panel>

            {/* SUPER-LICENSES — status, requirements with live progress, and what's blocking each one. The
                path texts reuse the engine's own string bundle (via_qualifier/via_season/gate_season_fmt…): they're the
                SAME phrases the Championships Access panel uses, so both say the same thing. */}
            {supl && supl.licenses.length > 0 && (
              <Panel style={{ padding: '18px 22px' }}>
                <Kicker style={{ marginBottom: 12 }}>{t('supl_title', { defaultValue: 'Superlicenses' })}</Kicker>
                <div className="col" style={{ gap: 12 }}>
                  {supl.licenses.map((l, i) => (
                    <div key={l.champId} className="col" style={{ gap: 4, paddingBottom: i < supl.licenses.length - 1 ? 12 : 0,
                                                                  borderBottom: i < supl.licenses.length - 1 ? '1px solid var(--line)' : 'none' }}>
                      <div className="row between" style={{ alignItems: 'center', gap: 10 }}>
                        <Mono style={{ fontSize: 12.5 }}>{l.champDisplay}</Mono>
                        {l.unlocked
                          ? <Chip kind="green" style={{ fontSize: 8.5 }}>{t('supl_held', { defaultValue: 'HELD' })}</Chip>
                          : <Chip kind="gold" style={{ fontSize: 8.5 }}><LockIcon size={8} /> {t('supl_pending', { defaultValue: 'PENDING' })}</Chip>}
                      </div>
                      {!l.unlocked && (
                        <>
                          {l.qualHasCal && (
                            <Mono style={{ fontSize: 10.5, color: 'var(--text-2)' }}>
                              {/* tester notes 29/08: you have to WIN the test (gate_win_note, the same phrase as the
                                  Access panel) and the test opens at ITS OWN tier even if its class arrives earlier */}
                              {t('via_qualifier')}: {l.qualDisplay} · {l.qualNextDate}
                              {(l.qualLevel ?? 0) > 0 ? ` · ${sfmt(t('supl_opens_tier_fmt', { defaultValue: 'opens at Tier %d' }), l.qualLevel ?? 0)}` : ''}
                              {` — ${t('gate_win_note', { defaultValue: 'WIN the race · max AI' })}`}
                            </Mono>
                          )}
                          {l.hasFeeder && (
                            <Mono style={{ fontSize: 10.5, color: 'var(--text-2)' }}>
                              {t('via_season')}: {sfmt(t('gate_season_fmt'), l.feederTop, l.feederClass)}
                              {l.feederHasContext ? ` — ${sfmt(t('gate_context_fmt'), l.feederContextPos, l.feederContextSeries)}` : ''}
                            </Mono>
                          )}
                          <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
                            {t('supl_blocks', { defaultValue: 'Until earned: its calendar rounds stay locked and its market offers stay held.' })}
                          </Mono>
                        </>
                      )}
                    </div>
                  ))}
                </div>
              </Panel>
            )}

            {/* WORLD RANKING: world top + the player's own neighborhood window, with a series filter */}
            <Panel style={{ padding: '16px 22px' }}>
              <div className="row between" style={{ marginBottom: 12, gap: 10 }}>
                <Kicker>{t('profile.worldRank')}</Kicker>
                <span className="row gap8" style={{ alignItems: 'center', flex: 'none' }}>
                  {/* the panel is a trimmed view (top-10 + your neighborhood); "see all" opens the full table. */}
                  {showViewAll && (
                    <button className="btn btn-sm btn-ghost" onClick={() => setRankOpen(true)}>
                      {t('profile.viewAll')} →
                    </button>
                  )}
                  {p.filter.options.length > 0 && (
                    <select
                      value={filterId}
                      onChange={(e) => setFilterId(e.target.value)}
                      className="mono"
                      style={{ fontSize: 11, background: 'var(--surface-2)', color: 'var(--text-2)', border: '1px solid var(--line-2)', borderRadius: 3, padding: '2px 6px' }}
                    >
                      {p.filter.options.map((o) => (
                        <option key={o.id} value={o.id}>
                          {t(o.label)}
                        </option>
                      ))}
                    </select>
                  )}
                </span>
              </div>
              <div className="col" style={{ gap: 2 }}>
                {p.worldRanking.top.map(renderRank)}
                {p.worldRanking.neighborhood.length > 0 && (
                  <>
                    <div className="row center" style={{ padding: '2px 0' }}>
                      <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>…</Mono>
                    </div>
                    {p.worldRanking.neighborhood.map(renderRank)}
                  </>
                )}
              </div>
            </Panel>
          </div>
        </div>
      </div>

      {/* FULL RANKING. Clicking outside or Escape closes it; the list scrolls inside the panel so
          the modal doesn't grow with the size of the world. The player's own row reuses renderRank (same highlight). */}
      {rankOpen && (
        <div
          role="dialog"
          aria-modal="true"
          aria-label={t('profile.rankingAll')}
          onClick={() => setRankOpen(false)}
          style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.6)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 60 }}
        >
          <div
            className="panel"
            onClick={(e) => e.stopPropagation()}
            style={{ width: 460, maxWidth: '92vw', maxHeight: '80vh', padding: '18px 20px', display: 'flex', flexDirection: 'column', gap: 12 }}
          >
            <div className="row between" style={{ alignItems: 'flex-start', gap: 10 }}>
              <div className="col" style={{ gap: 3, minWidth: 0 }}>
                <Kicker>{t('profile.rankingAll')}</Kicker>
                <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>
                  {t(p.filter.options.find((o) => o.id === filterId)?.label ?? 'profile.filterAll')}
                  {fullRows ? ` · ${t('profile.driversCount', { n: fullRows.length })}` : ''}
                </Mono>
              </div>
              <button className="btn btn-sm btn-ghost" style={{ flex: 'none' }} onClick={() => setRankOpen(false)}>✕</button>
            </div>
            <div className="scroll col" style={{ gap: 2, overflowY: 'auto', minHeight: 0 }}>
              {fullRows == null ? (
                <Mono style={{ fontSize: 11, color: 'var(--text-3)', padding: '10px 0' }}>{t('common.loading')}</Mono>
              ) : fullRows.length === 0 ? (
                <Mono style={{ fontSize: 11, color: 'var(--text-3)', padding: '10px 0' }}>{t('profile.rankingEmpty')}</Mono>
              ) : (
                fullRows.map(renderRank)
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}