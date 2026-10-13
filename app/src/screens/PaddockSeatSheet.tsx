// PaddockSeatSheet — the DETAIL view for a seat, in a sliding panel.
//
// Shape taken from the design mockup (its seat-sheet component): a fixed dimmed-background layer,
// a drawer anchored to the right, closable via Escape or clicking outside, plus its own ✕. It opens on top, it doesn't navigate — clicking
// a seat shouldn't pull you away from where you were looking.
//
// The sections are the design mockup's own sections, with the same labels, BUT only the ones the engine can
// actually fill get rendered. What doesn't exist yet — per-candidate status (pre-agreement / tryout), your position in the shortlist, the
// team's lead driver, and its home base — does NOT appear even as an empty slot: a placeholder would be narrating a silly season
// that doesn't exist, and that's worse than not showing it at all.
import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, PaddockSeat, PaddockSeatStatus, SeasonPayload } from '../api/contracts';
import { Display, Mono, Btn } from '../ui/components';
import { useCarLiveries } from '../ui/vehImages';
import { logImageError } from '../support/supportLog';
import { useQuery } from '../api/useQuery';
import { SeasonCalendar } from '../ui/SeasonCalendar';

// team initials for the avatar (mirrors the design mockup's own initials helper)
const iniciales = (n: string) => n.split(' ').map((w) => w[0]).slice(0, 2).join('').toUpperCase();

const STATUS_COLOR: Record<PaddockSeatStatus, string> = {
  free: 'var(--text-3)', sounded: 'var(--cyan)', disputed: 'var(--amber)',
  offered: 'var(--accent)', rejected: 'var(--danger)', renewed: 'var(--text-3)',
  unknown: 'var(--text-3)',
};

export function PaddockSeatSheet({
  client, seat, busy, dataDir, onClose, onApply, onSign,
}: {
  client: ApiClientLike;   // to request query.season and show the series' full calendar
  seat: PaddockSeat;
  busy: boolean;
  dataDir?: string;   // for the team's car thumbnail (without it, just the name)
  onClose: () => void;
  onApply: (s: PaddockSeat) => void;
  onSign: (s: PaddockSeat) => void;
}) {
  const { t } = useTranslation();
  // "when looking at a seat, which races does the series bring?" -- the same question already answered by the
  // Championship detail (Season.tsx), same component (SeasonCalendar), same query.season{seriesId}.
  const seasonQ = useQuery<SeasonPayload>(client, 'query.season', { seriesId: seat.seriesId });
  const rounds = seasonQ.data?.detail?.rounds ?? [];
  // the seat's CAR (Rules of Hooks: always called; empty slug -> url()='' -> placeholder)
  // urlFor and not url: the NN comes raw from the engine and its numbering doesn't match the PNGs' (see the vehicle-image mapping).
  const { urlFor: carUrl } = useCarLiveries(dataDir ?? '', seat.carSlug || undefined);
  const carImg = seat.carSlug && seat.liveryNN != null && seat.liveryNN >= 0 ? carUrl(seat.liveryNN) : '';
  // Escape closes it, same as the design mockup. A panel that only closes with the mouse forces you to let go of the keyboard
  // right when you're browsing seats with it.
  useEffect(() => {
    const h = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', h);
    return () => window.removeEventListener('keydown', h);
  }, [onClose]);

  const color = STATUS_COLOR[seat.status] ?? STATUS_COLOR.unknown;
  const puedePostular = !seat.applied && (seat.status === 'free' || seat.status === 'sounded');

  return (
    <div
      onClick={onClose}
      data-paddock-sheet="1"
      /* absolute, NOT fixed (same defect as the difficulty panel): with fixed positioning, the panel covered
         the entire window, including the top bar. It's now confined by the Paddock's root element (position:relative,
         height:100%). paddingRight leaves room for the app's own scrollbar rail, which is fixed at right:8 and
         40 wide, and used to land right on top of the panel. */
      style={{ position: 'absolute', inset: 0, zIndex: 40, background: 'rgba(6,7,10,.7)',
               display: 'flex', justifyContent: 'flex-end', paddingRight: 56 }}
    >
      <div
        onClick={(e) => e.stopPropagation()}
        className="col"
        style={{
          position: 'relative', width: 580, maxWidth: '94vw', background: 'var(--bg-2)',
          borderLeft: `1px solid color-mix(in oklab, ${color} 40%, var(--line))`,
          overflowY: 'auto', gap: 20, padding: '24px 28px 32px',
        }}
      >
        <button className="btn btn-sm btn-ghost" onClick={onClose} aria-label={t('common.close')}
          style={{ position: 'absolute', top: 16, right: 18, zIndex: 2 }}>✕</button>

        <div className="col" style={{ gap: 8 }}>
          <div className="row gap12" style={{ alignItems: 'center' }}>
            {/* INITIALS avatar, same as the design mockup: gives the team an identity without needing a logo we
                don't have. The color comes from the seat's status, which is the only splash of color there is today. */}
            <span className="mono" aria-hidden style={{
              width: 42, height: 42, display: 'flex', alignItems: 'center', justifyContent: 'center',
              fontSize: 15, fontWeight: 700, flex: 'none', borderRadius: 4,
              background: `color-mix(in oklab, ${color} 26%, var(--surface-3))`, color,
            }}>{iniciales(seat.team)}</span>
            <Display style={{ fontSize: 22, lineHeight: 1 }}>{seat.team}</Display>
          </div>
          <div className="row gap8" style={{ alignItems: 'center', flexWrap: 'wrap' }}>
            <span className="mono" style={{ fontSize: 9.5, letterSpacing: '.1em', padding: '3px 9px', color, border: `1px solid color-mix(in oklab, ${color} 45%, transparent)`, background: `color-mix(in oklab, ${color} 12%, transparent)` }}>
              {t(`pd_status_${seat.status}`)}
            </span>
            <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{seat.seriesDisplay}</Mono>
            {/* "John Doe (34) is leaving" is the design mockup's phrasing: the seat is understood by WHO's vacating it */}
            {seat.occupantName && (
              <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>
                {t('pd_frees', { name: seat.occupantName, defaultValue: 'frees {{name}}' })}
                {(seat.occupantAge ?? 0) > 0 && ` (${seat.occupantAge})`}
                {seat.expiryYear > 0 && ` · ${t('pd_until', { year: seat.expiryYear, defaultValue: 'until {{year}}' })}`}
              </Mono>
            )}
            <div className="grow" />
            {/* RELATIONSHIP: explains why some teams pursue you and others don't. It comes from the shared history
                the same function that decides who reaches out to you, so it can never contradict it. */}
            {typeof seat.rel === 'number' && (
              <span className="row gap8" style={{ alignItems: 'center', flex: 'none' }} data-paddock-rel={seat.rel}>
                <Mono style={{ fontSize: 9, letterSpacing: '.12em', color: 'var(--text-3)' }}>{t('pd_rel', { defaultValue: 'RELATION' })}</Mono>
                <span style={{ width: 54, height: 4, borderRadius: 2, background: 'var(--surface-3)', overflow: 'hidden', display: 'inline-block' }}>
                  <span style={{ display: 'block', height: '100%', width: `${Math.max(0, Math.min(100, seat.rel))}%`, background: 'var(--accent)' }} />
                </span>
              </span>
            )}
          </div>
        </div>

        {/* THE CAR for the seat — same strip as the offer card in Market. In a multi-car class
            (Jessamy Lum364 has 6) the seat makes no sense without it. Missing carName = an older engine build or a class with no
            cars installed: the section doesn't appear, never an empty slot. */}
        {seat.carName && (
          <div className="row gap8" data-paddock-car={seat.carName}
               style={{ alignItems: 'center', padding: '6px 8px', borderRadius: 3, background: 'var(--surface-2)' }}>
            {carImg
              ? <img src={carImg} alt="" loading="lazy" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ width: 64, height: 36, objectFit: 'contain', borderRadius: 2, flex: 'none' }} />
              : <span style={{ width: 64, height: 36, borderRadius: 2, background: 'var(--surface-3)', flex: 'none' }} />}
            <span className="col" style={{ gap: 1, minWidth: 0 }}>
              <Mono style={{ fontSize: 9, letterSpacing: '.08em', color: 'var(--text-3)' }}>{t('mkt_team_car')}</Mono>
              <Mono style={{ fontSize: 11, color: 'var(--text-2)', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{seat.carName}</Mono>
            </span>
          </div>
        )}

        {/* The series CALENDAR — the same question already answered by the Championship detail ("which
            races does it bring?"), and it was missing here: looking at a seat without knowing the calendar forced you to leave to
            Championships or the Calendar to check. Empty rounds (old payload or series without a
            fixed calendar yet) -> SeasonCalendar draws nothing, never a gap. */}
        <SeasonCalendar rounds={rounds} maxHeight="22vh" />

        {/* WHAT THEY'RE ASKING FOR, OUT LOUD — the reputation bar (hard: you meet it or you don't), and below, what THIS
            team actually values (soft: it weighs more or less). Both come from the same model that decides the
            seat contest, so this section can never promise one thing while the verdict does another. */}
        {((seat.reqRep ?? 0) > 0 || (seat.asks?.length ?? 0) > 0) && (
          <div className="col" style={{ gap: 6 }} data-paddock-req="1">
            <span className="mono" style={{ fontSize: 9, letterSpacing: '.14em', color: 'var(--text-3)' }}>
              {t('pd_asks_title', { defaultValue: 'WHAT THEY ASK, OUT LOUD' })}
            </span>
            {/* the team's CHARACTER, in one sentence. It comes first because it's what gives meaning to the list
                below: without it, four bars are just four bars; with it, they're a team chasing money. */}
            {seat.personality && (
              <span data-paddock-character={seat.personality} style={{ fontSize: 12.5, color: 'var(--text-2)' }}>
                {t(`pd_char_${seat.personality}`, { defaultValue: '' })}
              </span>
            )}
            {(seat.reqRep ?? 0) > 0 && (() => {
              // the requirement is NOT a bare number: what matters is whether you MEET it. With your rating right beside it and
              // a ✓/✕ up front, the seat goes from "requires 1180" to "you're 40 short", which is actionable.
              const tuyo = seat.yourRating ?? 0;
              const cumple = tuyo > 0 && tuyo >= (seat.reqRep ?? 0);
              const col = tuyo === 0 ? 'var(--text-3)' : cumple ? 'var(--green)' : 'var(--danger)';
              return (
                <div className="row between" data-paddock-req-met={tuyo === 0 ? undefined : cumple ? '1' : '0'}
                  style={{ alignItems: 'center', padding: '8px 12px', background: 'var(--surface)', border: `1px solid color-mix(in oklab, ${col} 28%, var(--line))` }}>
                  <span className="row gap8" style={{ alignItems: 'center' }}>
                    {tuyo > 0 && <span style={{ color: col, fontSize: 12 }}>{cumple ? '✓' : '✕'}</span>}
                    <span style={{ fontSize: 12.5, color: 'var(--text-2)' }}>
                      {t('pd_asks_rep', { n: seat.reqRep, defaultValue: 'A rating of at least {{n}}' })}
                    </span>
                  </span>
                  {tuyo > 0 && <Mono style={{ fontSize: 11, color: col }}>{tuyo}</Mono>}
                </div>
              );
            })()}

            {/* WHAT THEY VALUE, and how much you bring. The bar above is hard (you meet it or you don't); this is
                soft, which is why it's rendered as a weight, not a ✓/✕. Two overlaid bars: the faint one is what
                matters to them, the solid one is what you bring. When the solid bar falls short against a factor that
                weighs heavily, that's the reason that team is going to sign someone else. */}
            {(seat.asks?.length ?? 0) > 0 && (
              <div className="col" style={{ gap: 4 }} data-paddock-asks="1">
                {seat.asks!.map((a) => (
                  <div key={a.k} className="row between" data-paddock-ask={a.k} data-paddock-ask-you={a.you}
                    style={{ alignItems: 'center', gap: 10, padding: '3px 0' }}>
                    <span style={{ fontSize: 12, color: 'var(--text-2)', flex: '1 1 auto' }}>
                      {t(`pd_ask_${a.k}`, { defaultValue: a.k })}
                    </span>
                    <span aria-hidden="true"
                      style={{ position: 'relative', flex: 'none', width: 92, height: 6, background: 'var(--surface-2)' }}>
                      {/* what matters to them: this factor's weight for THIS team */}
                      <span style={{ position: 'absolute', inset: 0, width: `${Math.max(0, Math.min(100, a.w))}%`,
                                     background: 'color-mix(in oklab, var(--accent) 26%, transparent)' }} />
                      {/* what you bring, overlaid on the above */}
                      <span style={{ position: 'absolute', top: 1, bottom: 1, left: 0,
                                     width: `${Math.max(0, Math.min(100, a.you))}%`, background: 'var(--accent)' }} />
                    </span>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}

        {/* THE TEAM'S SHORTLIST — sealed at this point. Names, which is all there is. */}
        {(seat.shortlist?.length ?? 0) > 0 && (
          <div className="col" style={{ gap: 6 }} data-paddock-shortlist="1">
            <div className="row gap8" style={{ alignItems: 'baseline' }}>
              <span className="mono" style={{ fontSize: 9, letterSpacing: '.14em', color: 'var(--text-3)' }}>
                {t('pd_shortlist_title', { defaultValue: 'TEAM SHORTLIST' })}
              </span>
              <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
                {t('pd_shortlist_n', { n: seat.shortlist!.length, defaultValue: '{{n}} candidates' })}
              </Mono>
            </div>
            {seat.shortlist!.map((c) => (
              <div key={c.id} className="row" style={{ alignItems: 'center', gap: 8, padding: '5px 0', borderTop: '1px solid var(--line)' }}>
                <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--text-3)', flex: 'none' }} />
                <span style={{ fontSize: 12.5, color: 'var(--text-2)' }}>{c.name}</span>
              </div>
            ))}
          </div>
        )}

        {/* TERMS ON THE TABLE — only shown when they're real (an offered seat). */}
        {seat.perRace > 0 && (
          <div className="col" style={{ gap: 6 }} data-paddock-terms="1">
            <span className="mono" style={{ fontSize: 9, letterSpacing: '.14em', color: 'var(--text-3)' }}>
              {t('pd_terms_title', { defaultValue: 'CONDITIONS ON THE TABLE' })}
            </span>
            {/* a three-cell grid, same as the design mockup. A window-seat's signing bonus is 0 today and it's
                shown with a dash instead of hiding the cell: "no bonus" is itself information. */}
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 8 }}>
              {[
                [t('pd_on_signing', { defaultValue: 'ON SIGNING' }), '—', 'var(--text-3)'],
                [t('pd_per_race_k', { defaultValue: 'PER RACE' }), `₡ ${seat.perRace.toLocaleString('en-US')}`, 'var(--text)'],
                [t('pd_commitment', { defaultValue: 'COMMITMENT' }), t('pd_one_season', { defaultValue: '1 season' }), 'var(--text)'],
              ].map(([k, v, c]) => (
                <div key={k} className="col" style={{ gap: 3, padding: '10px 12px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
                  <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)' }}>{k}</Mono>
                  <Display style={{ fontSize: 17, color: c }}>{v}</Display>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* "X took it" once it's already resolved against you: the detail view also reports the losses */}
        {seat.winnerName && (
          <Mono style={{ fontSize: 12, color: 'var(--danger)' }}>
            {t('pd_taken_by', { name: seat.winnerName, defaultValue: 'taken by {{name}}' })}
          </Mono>
        )}

        <div className="row gap8" style={{ alignItems: 'center', flexWrap: 'wrap' }}>
          {seat.signed ? (
            <Mono data-paddock-sheet-signed="1" style={{ fontSize: 11.5, color: 'var(--green)' }}>
              ✓ {t('pd_seat_signed', { defaultValue: 'Seat signed' })}
            </Mono>
          ) : seat.status === 'offered' ? (
            <Btn variant="primary" disabled={busy} onClick={() => onSign(seat)}>{t('pd_sign', { defaultValue: 'Sign' })}</Btn>
          ) : puedePostular ? (
            <Btn disabled={busy} onClick={() => onApply(seat)}>{t('pd_apply', { defaultValue: 'Apply' })}</Btn>
          ) : seat.applied ? (
            <Mono style={{ fontSize: 11.5, color: 'var(--accent-text)' }}>{t('pd_applied_long', { defaultValue: 'You are in the fight for this seat' })}</Mono>
          ) : null}
        </div>
      </div>
    </div>
  );
}
