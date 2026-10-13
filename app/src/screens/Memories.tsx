// Memories (query.memories): a master-detail view over the calendar's ERA
// series (pre-2000), ported from the engine's own memories-drawing code.
// A rail of series (left) + a detail pane with the next round and the round list (right). The engine precomputes
// states (locked / availableThisWeek / per-round status / next round); this screen just renders them.
// Standings, prizes, laps and positions from the design mockup's JSX are NOT ported: that's demo-only logic unrelated
// to the engine. Memories are NOT run here; the CTA hands off to the Calendar (a deliberate UX decision).
// UI chrome via t('memories.*') (EN-default bilingual rule). Series/track names = real data
// from the catalog, rendered raw; discipline = an engine token, uppercased.
import { useState } from 'react';
import { logImageError } from '../support/supportLog';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, MemoriesPayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Chip, Panel, Btn, Kicker, LockIcon } from '../ui/components';
import { useTrackMapUrl } from '../ui/trackImages';
import { useCarLiveries, useOverridePreviews } from '../ui/vehImages';
import { formatCreditsCr } from '../ui/format';
import { eraSkin } from '../ui/eraSkin';

// Color by decade (design-system tokens): 60s/70s gold · 80s amber · 90s cyan.
function decadeColor(year: number): string {
  if (year < 1980) return '#d8b46a';
  if (year < 1990) return '#ffb84d';
  return '#5ec8ff';
}

// Status dot: replaces the "●" glyph from the overlay (design rule: no text glyphs).
function Dot({ color, size = 8 }: { color: string; size?: number }) {
  return <span style={{ width: size, height: size, borderRadius: '50%', background: color, display: 'inline-block', flex: 'none' }} />;
}

// The curated gift's vehicle hash (the F1'67 Highley Racing car) is a FIXED value the engine sets
// (in the engine's memories-input provider: a fixed vehicle hash / livery 50), not something that varies by save. Unlike
// GarageCar/PreraceCar, MemoriesPayload does NOT come with `slug` pre-resolved -> there's no generic hash->slug catalog on the
// client. The one known real value is mapped here directly (from the vehicle catalog: that hash = the vintage Formula car);
// if the engine ever gifts a different car, the hash won't match and the photo falls back to the placeholder (nothing breaks).
const GIFT_VEH_SLUG: Record<number, string> = { 759059246: 'formula_vintage_g1m2' };

// "Gift revealed" popup: same modal pattern as the Calendar's conflict-confirmation dialog (a fixed-position
// overlay + a dialog panel with role="dialog" aria-modal). Real photo via the same pipeline as Garage/EventPrerace
// (useCarLiveries + useOverridePreviews, "real skins" first); the livery ordinal is treated as an index over the NN files
// found on disk, same as Garage's car card. No resolved photo -> a generic placeholder.
function GiftPopup({ dataDir, vehHash, livery, onClose }: { dataDir?: string; vehHash?: number; livery?: number; onClose: () => void }) {
  const { t } = useTranslation();
  const slug = vehHash != null ? GIFT_VEH_SLUG[vehHash] : undefined;
  const { nns, url } = useCarLiveries(dataDir, slug);
  const ov = useOverridePreviews(dataDir);
  const ord = livery ?? -1;
  const nn = nns.length ? (ord >= 0 && ord < nns.length ? nns[ord] : nns[0]) : null;
  const photo = nn != null && slug ? (ov.url(slug, nn) || url(nn)) : '';

  return (
    <div style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.82)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 50 }}>
      <div className="panel" role="dialog" aria-modal="true" aria-label={t('memories.giftTitle')}
        style={{ width: 440, padding: '26px 28px', display: 'flex', flexDirection: 'column', gap: 14 }}>
        <div className="img-slot" style={{ height: 160, display: 'flex', alignItems: 'center', justifyContent: 'center', overflow: 'hidden', background: 'var(--surface-2)' }}>
          {photo
            ? <img src={photo} alt="" loading="lazy" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain' }} />
            : <Display style={{ fontSize: 32, opacity: 0.5 }}>F1</Display>}
        </div>
        <Display style={{ fontSize: 24 }}>{t('memories.giftTitle')}</Display>
        <Mono style={{ fontSize: 13, color: 'var(--text)', lineHeight: 1.5 }}>{t('memories.giftBody')}</Mono>
        <Btn variant="primary" autoFocus onClick={onClose} style={{ marginTop: 4 }}>{t('memories.giftCta')}</Btn>
      </div>
    </div>
  );
}

export function Memories({ client, dataDir, onGoToCalendar, onAcquireCar }: { client: ApiClientLike; dataDir?: string; onGoToCalendar?: () => void; onAcquireCar?: (seriesId: string, round: number) => void }) {
  const { t, i18n } = useTranslation();
  const es = i18n.language === 'es';   // bilingual lore -> pick the localized field
  const q = useQuery<MemoriesPayload>(client, 'query.memories');
  const trackUrl = useTrackMapUrl(dataDir);   // Real circuit map keyed by trackSlug (PNG served from data/)
  const [selId, setSelId] = useState<string | null>(null);
  const [giftDismissed, setGiftDismissed] = useState(false);   // local only: don't re-show the popup after acknowledging it this session

  if (q.loading) return <div className="col center grow" style={{ height: '100%' }}><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></div>;
  if (q.error) return <div className="col center grow" style={{ height: '100%' }}><Mono style={{ color: 'var(--text-3)' }}>{t('home.noCareer')}</Mono></div>;

  const data = q.data;
  const series = data?.series ?? [];
  if (series.length === 0) {
    return (
      <div className="col center grow" style={{ height: '100%' }}>
        <Mono style={{ color: 'var(--text-3)' }}>{t('memories.empty')}</Mono>
      </div>
    );
  }

  const sel = series.find((s) => s.id === selId) ?? series[0];
  const mc = decadeColor(sel.year);
  const totalAvailable = series.reduce((acc, s) => acc + s.availableThisWeek, 0);
  const sub = t('memories.subtitle') + (data && data.lineage ? ` · ${data.lineage}` : '');
  const featured = sel.nextRoundNumber != null ? sel.rounds.find((r) => r.round === sel.nextRoundNumber) : undefined;
  const featColor = sel.nextRoundOpen ? 'var(--accent-text)' : mc;
  // access = owning the car (locked === !owned), not phase/tier -> the featured label
  // no longer says "Locked" for a series you don't own; it uses the same "requires its car" wording as the showcase panel.
  const featLabel = sel.owned ? (sel.nextRoundOpen ? t('memories.available') : t('memories.next')) : t('memories.requiresCar');
  // the round used to arm "Acquire car" -> the engine's featured one, or if none, the first one on the calendar
  const acquireRound = sel.nextRoundNumber ?? (sel.rounds[0]?.round ?? 0);
  const discountedCarPrice = Math.round(sel.carPrice * (100 - sel.discountPct) / 100);

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      {/* header (lineage in the subtitle) */}
      <div className="row between" style={{ padding: '18px 30px 12px', alignItems: 'flex-end' }}>
        <div>
          <Display as="h1" style={{ fontSize: 30, whiteSpace: 'nowrap' }}>{t('memories.title')}</Display>
          <Mono style={{ fontSize: 12, color: 'var(--text-3)', marginTop: 3, display: 'block' }}>{sub}</Mono>
        </div>
        <div className="row gap12" style={{ alignItems: 'center' }}>
          <Chip>{series.length} {t('memories.eras')}</Chip>
          {totalAvailable > 0 && <Chip kind="accent"><Dot color="var(--accent)" size={7} /> {totalAvailable} {t('memories.available')}</Chip>}
        </div>
      </div>

      <div style={{ flex: 1, display: 'grid', gridTemplateColumns: '340px 1fr', minHeight: 0 }}>
        {/* RAIL: era series */}
        <div className="scroll" style={{ padding: '8px 16px 24px 30px', borderRight: '1px solid var(--line)', display: 'flex', flexDirection: 'column', gap: 10 }} data-tour="memories.rail">
          {series.map((s) => {
            const c = decadeColor(s.year);
            const skin = eraSkin(s.year);   // 04/10: the card takes the material of its decade
            const active = s.id === sel.id;
            const hasAvail = s.owned && s.availableThisWeek > 0;   // "available this week" only applies to already-owned series
            return (
              <div key={s.id} role="button" tabIndex={0}
                onClick={() => setSelId(s.id)}
                onKeyDown={(e) => e.key === 'Enter' && setSelId(s.id)}
                className={'panel focusable' + (skin ? ` era-skin era-rail ${skin}` : '') + (active ? ' is-active' : '') + (s.locked ? ' is-locked' : '')}
                data-yr={skin ? String(s.year).slice(-2) : undefined}
                style={skin ? { cursor: 'pointer', padding: 0, overflow: 'hidden', display: 'flex', flex: 'none', position: 'relative' } : {
                  cursor: 'pointer', padding: 0, overflow: 'hidden', display: 'flex', flex: 'none', position: 'relative',
                  // 27/09: selected = background tinted with the decade's color + border, which is distinguished from the halo
                  // of the FOCUS (red, via .is-focus). Before it was only a 1 px ring and you could not tell which was which.
                  background: active ? `color-mix(in oklab, ${c} 14%, var(--surface))` : undefined,
                  borderColor: active ? c : hasAvail ? 'color-mix(in oklab, var(--accent) 40%, var(--line))' : 'var(--line)',
                  boxShadow: active ? `0 0 0 1px ${c}` : 'none',
                }}>
                {!skin && <div style={{ width: 4, flex: 'none', background: c }} />}
                <div className="era-body" style={skin ? undefined : { flex: 1, padding: '11px 13px', minWidth: 0 }}>
                  <div className="row between era-top" style={{ marginBottom: 5 }}>
                    <Mono style={{ fontSize: 10, letterSpacing: '.12em', color: skin ? undefined : c }}>{s.era} · {s.discipline.toUpperCase()}</Mono>
                    {s.locked ? <Dot color="var(--text-3)" /> : hasAvail ? <Dot color="var(--accent)" /> : null}
                  </div>
                  <Display className={skin ? 'era-name' : undefined} style={{ fontSize: 17, lineHeight: 1, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', display: 'block' }}>{s.name}</Display>
                  <div className="row" style={{ marginTop: 8 }}>
                    {/* access = owning the car (locked === !owned for every era series) ->
                        the rail chip is decided by !s.owned, never by a tier field (that was the old model). */}
                    {!s.owned
                      ? <Chip kind="lock" style={{ fontSize: 9, padding: '2px 7px' }}><LockIcon size={8} /> {t('memories.carFrom', { price: formatCreditsCr(s.discountPct > 0 ? Math.round(s.carPrice * (100 - s.discountPct) / 100) : s.carPrice) })}</Chip>
                      : hasAvail
                        ? <Chip kind="accent" style={{ fontSize: 9, padding: '2px 7px' }}><Dot color="var(--accent)" size={6} /> {s.availableThisWeek} {t('memories.thisWeek')}</Chip>
                        : <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{s.rounds.length} {t('memories.rounds')}</Mono>}
                  </div>
                </div>
              </div>
            );
          })}
        </div>

        {/* DETAIL of the selected series */}
        <div className="scroll" style={{ padding: '16px 30px 30px', minWidth: 0 }} data-tour="memories.detail">
          {/* series header */}
          <div className="row gap16" style={{ alignItems: 'center', marginBottom: 16, minWidth: 0 }}>
            <div style={{ width: 52, height: 52, flex: 'none', borderRadius: '50%', background: mc, display: 'grid', placeItems: 'center' }}>
              <Display style={{ fontSize: 18, color: '#1a1712' }}>{sel.era.length > 2 ? sel.era.slice(2) : sel.era}</Display>
            </div>
            <div className="col" style={{ minWidth: 0 }}>
              <Mono style={{ fontSize: 10, letterSpacing: '.14em', color: mc }}>{sel.era} · {sel.discipline.toUpperCase()} · {sel.region}</Mono>
              <Display style={{ fontSize: 28, lineHeight: 1, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', display: 'block' }}>{sel.name}</Display>
              <Mono style={{ fontSize: 11, color: 'var(--text-3)', marginTop: 4 }}>{sel.rounds.length} {t('memories.roundsOnCalendar')}</Mono>
            </div>
          </div>

          {/* showcase — owned (green chip) vs locked for lack of the car (price + Acquire car) */}
          {sel.owned ? (
            <div className="row" style={{ marginBottom: 16 }}>
              <Chip kind="green">{t('memories.inCollection')}</Chip>
            </div>
          ) : (
            <Panel style={{ display: 'flex', gap: 16, alignItems: 'center', flexWrap: 'wrap', padding: '12px 16px', marginBottom: 16 }}>
              <div className="col" style={{ flex: 1, gap: 4, minWidth: 0 }}>
                <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('memories.requiresCar')}</Mono>
                <div className="row gap8" style={{ alignItems: 'center', flexWrap: 'wrap' }}>
                  {sel.discountPct > 0 && (
                    <Display style={{ fontSize: 16, lineHeight: 1, color: 'var(--text-3)', textDecoration: 'line-through' }}>{formatCreditsCr(sel.carPrice)}</Display>
                  )}
                  <Display style={{ fontSize: 18, lineHeight: 1 }}>
                    {t('memories.carFrom', { price: formatCreditsCr(sel.discountPct > 0 ? discountedCarPrice : sel.carPrice) })}
                  </Display>
                  {sel.discountPct > 0 && <Chip kind="gold">{t('memories.discountBadge', { pct: sel.discountPct })}</Chip>}
                </div>
              </div>
              <Btn size="sm" type="button" style={{ flex: 'none' }}
                onClick={() => { if (onAcquireCar && acquireRound > 0) onAcquireCar(sel.id, acquireRound); }}>
                {t('memories.acquireCar')}
              </Btn>
            </Panel>
          )}

          {/* ancestor (one per decade, using your last name) + real historical lore for the series */}
          {sel.ancestorName && (
            <div style={{ marginBottom: 16, padding: '14px 16px', border: `1px solid color-mix(in oklab, ${mc} 30%, var(--line))`, borderRadius: 4, background: 'var(--surface-2)' }}>
              <Mono style={{ fontSize: 11, letterSpacing: '.04em', color: mc, display: 'block', marginBottom: (es ? sel.loreDescEs : sel.loreDescEn) ? 8 : 0 }}>
                {t('memories.racedBy', { relation: t('memories.relation.' + (sel.ancestorRelation ?? 'ancestor')), name: sel.ancestorName, defaultValue: 'Raced by your {{relation}}, {{name}}' })}
              </Mono>
              {(es ? sel.loreDescEs : sel.loreDescEn) && (
                <div style={{ fontSize: 13, color: 'var(--text-2)', lineHeight: 1.6 }}>{es ? sel.loreDescEs : sel.loreDescEn}</div>
              )}
              {(es ? sel.loreHlEs : sel.loreHlEn) && (
                <div style={{ fontSize: 12, color: 'var(--text-3)', fontStyle: 'italic', lineHeight: 1.55, marginTop: 8, paddingLeft: 10, borderLeft: `2px solid color-mix(in oklab, ${mc} 45%, transparent)` }}>{es ? sel.loreHlEs : sel.loreHlEn}</div>
              )}
            </div>
          )}

          {/* the phase/level lock warning was removed from this screen -- access = owning
              the car (locked === !owned), already covered above by the "requires its car" / price panel. */}

          {/* next round: map (placeholder) + status + jump to the calendar */}
          {featured && (
            <Panel style={{ display: 'flex', gap: 16, alignItems: 'center', padding: '12px 16px', marginBottom: 20, borderColor: sel.nextRoundOpen ? 'color-mix(in oklab, var(--accent) 45%, var(--line))' : 'var(--line)' }}>
              <div className="img-slot" style={{ width: 132, height: 74, flex: 'none', display: 'flex', alignItems: 'center', justifyContent: 'center', overflow: 'hidden' }}>
                {trackUrl(featured.trackSlug)
                  ? <img src={trackUrl(featured.trackSlug)} alt="" loading="lazy" onError={(e) => { logImageError(null, e.currentTarget.src); e.currentTarget.style.display = 'none'; }} style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain' }} />
                  : <span className="lbl">{t('memories.map')}</span>}
              </div>
              <div className="col" style={{ flex: 1, gap: 3, minWidth: 0 }}>
                <Mono style={{ fontSize: 10, letterSpacing: '.14em', color: featColor }}>{featLabel}</Mono>
                <Display style={{ fontSize: 21, lineHeight: 1, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', display: 'block' }}>{featured.trackName}</Display>
                <Mono style={{ fontSize: 11.5, color: 'var(--text-3)' }}>{featured.date} · {t('memories.roundLabel')} {featured.round}</Mono>
              </div>
              {sel.nextRoundOpen && (
                // the button had no onClick — now it navigates to the Calendar tab (via the shell's setTab).
                <Btn size="sm" type="button" style={{ flex: 'none' }} onClick={onGoToCalendar}>{t('memories.goToCalendar')} →</Btn>
              )}
            </Panel>
          )}

          {/* ROUNDS: all rounds with their status */}
          <div className="set-section">
            <Kicker>{t('memories.roundsSection')}</Kicker>
            <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{sel.rounds.length}</Mono>
          </div>
          <div className="col gap8">
            {sel.rounds.map((r) => {
              const rc = r.state === 'open' ? 'var(--accent)' : r.state === 'upcoming' ? mc : 'var(--text-3)';
              const rcText = r.state === 'open' ? 'var(--accent-text)' : r.state === 'upcoming' ? mc : 'var(--text-3)';   // small text: AA-legible accent color
              return (
                <Panel key={r.round} style={{ display: 'flex', overflow: 'hidden', alignItems: 'center', opacity: r.state === 'locked' ? 0.7 : 1, borderColor: r.state === 'open' ? 'color-mix(in oklab, var(--accent) 45%, var(--line))' : 'var(--line)' }}>
                  <div style={{ width: 3, alignSelf: 'stretch', background: rc, flex: 'none' }} />
                  <div className="row between" style={{ flex: 1, padding: '10px 13px', minWidth: 0, alignItems: 'center' }}>
                    <span className="row gap8" style={{ minWidth: 0, alignItems: 'center' }}>
                      <span className="lg-wk">R{r.round}</span>
                      <Mono style={{ fontSize: 11, color: 'var(--text-2)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{r.trackName}</Mono>
                    </span>
                    <span className="row gap12" style={{ flex: 'none', alignItems: 'center' }}>
                      <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{r.date}</Mono>
                      <Mono style={{ fontSize: 10, color: rcText, whiteSpace: 'nowrap' }}>{t('memories.round.' + r.state)}</Mono>
                    </span>
                  </div>
                </Panel>
              );
            })}
          </div>
        </div>
      </div>

      {/* Gift popup (the F1'67 Highley Racing car) shown on entering Memories with an
          unseen pending gift (engine: a starter-gift-pending flag via claimed events, no new field or migration needed).
          Coordinated with the tutorial: the Memories tour (tour.memories.intro, in both locale files)
          is ALREADY gift-aware -- its copy
          mentions the F1'67 gift -- and this popup is a focus-trapping modal (fixed overlay, high z-index,
          same as the Calendar's conflict-confirmation dialog) and closes via its CTA (ack -> giftPending flips to
          false in the engine); there's no need to delay the tour's start from here, it's enough that the popup
          covers it while it's open. No fragile mid-tour locale logic gets added. */}
      {data?.giftPending && !giftDismissed && (
        <GiftPopup
          dataDir={dataDir}
          vehHash={data.giftVehHash}
          livery={data.giftLivery}
          onClose={() => {
            setGiftDismissed(true);
            void client.cmd('cmd.memories.ackgift').then(() => q.refetch()).catch(() => q.refetch());
          }}
        />
      )}
    </div>
  );
}