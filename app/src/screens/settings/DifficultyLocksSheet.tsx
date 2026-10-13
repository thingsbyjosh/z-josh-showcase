// The DDA-lock panel you reach via "see more". This is where you actually CHANGE things; the hover on the
// card is just for reading.
//
// REDESIGN (approved design, `screen_diff_locks.jsx`). The first version was a list of raw ids
// with native checkboxes: it just wasn't legible. What the new design brings, and why each piece is here:
//   · The 70-120 SCALE with your own level marked on it. A category's number only means something compared to the
//     rest and to your level; a column of bare numbers doesn't tell you whether 104 is high or not.
//   · IDENTITY per row: discipline dot + readable name. Both come from the engine (className/classColor).
//   · The lock as its OWN CONTROL (role="switch" with its own icon), not a checkbox: it's the screen's main action.
//   · The three states the design calls for: seeded, locked on its own, and covered by the master lock.
// Same pattern as PaddockSeatSheet: overlay + side panel + Escape. No new dependencies.
import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import type { SettingsDiffClass } from '../../api/contracts';
import { Display, Mono } from '../../ui/components';

const AI_MIN = 70, AI_MAX = 120;   // domain matches the game's own AI slider range (career_engine.h: dda::kAiMin/kAiMax)

// The lock, hand-drawn. Closed = filled; open = shackle lifted. A text glyph can't tell the two
// states apart at a glance, and this control is the screen's main action.
function Padlock({ locked, size = 15 }: { locked: boolean; size?: number }) {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"
         style={{ width: size, height: size, display: 'block' }}
         fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
      <rect x="5.5" y="11" width="13" height="9" rx="1" fill={locked ? 'currentColor' : 'none'} />
      {locked ? <path d="M8.5 11V8a3.5 3.5 0 0 1 7 0v3" /> : <path d="M8.5 11V8a3.5 3.5 0 0 1 6.8-1.2" />}
    </svg>
  );
}

// The row's scale: the 70-120 rail, a tick at YOUR LEVEL, and the category's dot. A seeded category gets an
// hollow dot (its number isn't really its own yet), and a locked one gets a halo (it's frozen there).
function LevelBar({ value, level, color, locked, seeded }: {
  value: number; level: number; color: string; locked: boolean; seeded: boolean;
}) {
  const pct = (v: number) => ((Math.min(AI_MAX, Math.max(AI_MIN, v)) - AI_MIN) / (AI_MAX - AI_MIN)) * 100;
  return (
    <div style={{ position: 'relative', height: 18, flex: 1, minWidth: 90 }} aria-hidden="true">
      <span style={{ position: 'absolute', left: 0, right: 0, top: 8, height: 3, background: 'var(--surface-3)' }} />
      <span style={{ position: 'absolute', left: `${pct(level)}%`, top: 3, width: 1.5, height: 13, background: 'var(--text-3)' }} />
      <span style={{ position: 'absolute', left: `calc(${pct(value)}% - 4.5px)`, top: 5, width: 9, height: 9, borderRadius: '50%',
                     background: seeded ? 'transparent' : color,
                     border: seeded ? `1.5px dashed ${color}` : 'none',
                     boxShadow: locked ? `0 0 0 2.5px color-mix(in oklab, ${color} 35%, transparent)` : 'none' }} />
    </div>
  );
}

export function DifficultyLocksSheet({ classes, lockAll, level, trim, onClose, onToggleClass, onToggleAll }: {
  classes: SettingsDiffClass[];
  lockAll: boolean;
  level: number;
  trim: number;
  onClose: () => void;
  onToggleClass: (classId: string, locked: boolean) => void;
  onToggleAll: (locked: boolean) => void;
}) {
  const { t } = useTranslation();
  useEffect(() => {
    const h = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', h);
    return () => window.removeEventListener('keydown', h);
  }, [onClose]);

  const corridas = classes.filter((k) => !k.seeded).length;
  const bloqueadas = classes.filter((k) => lockAll || !!k.locked).length;

  return (
    <div onClick={onClose} data-dda-locks="1"
         /* absolute, NOT fixed: the design confines the panel to the screen, BELOW the top
            bar. With fixed it covered the entire window. The ancestor that confines it is the root of the
            Settings screen (position:relative, height:100%), which does NOT scroll -- its inner section does --,
            so the panel doesn't move along with the content. */
         /* paddingRight: the app's scroll rail (ScrollRail) is fixed at right:8 with a width of 40, so
            flush against the edge its arrows land right on top of each row's lock, which also lives on the
            right side. 56 = the 48 the rail takes up + some breathing room. */
         style={{ position: 'absolute', inset: 0, zIndex: 40, background: 'rgba(6,7,10,.72)',
                  display: 'flex', justifyContent: 'flex-end', paddingRight: 56 }}>
      <div onClick={(e) => e.stopPropagation()} className="col scroll"
           style={{ position: 'relative', width: 600, maxWidth: '94vw', background: 'var(--bg-2)',
                    borderLeft: '1px solid color-mix(in oklab, var(--purple) 40%, var(--line))', overflowY: 'auto' }}>

        {/* -- header: what this is and what the whole scale is measured against -- */}
        <div style={{ padding: '20px 26px 16px', borderBottom: '1px solid var(--line)', position: 'relative',
                      background: 'linear-gradient(150deg, color-mix(in oklab, var(--purple) 12%, var(--surface)), var(--surface) 70%)' }}>
          <button className="btn btn-sm btn-ghost" onClick={onClose} aria-label={t('common.close')}
                  style={{ position: 'absolute', top: 16, right: 18, zIndex: 2 }}>✕</button>
          {/* col: kicker, display and mono are INLINE spans (design-system.css doesn't set `display` on them), so
              left alone all three ran together on the same line. The design stacks them, and a flex column
              guarantees that without relying on each class to bring its own display. paddingRight leaves room for the ✕. */}
          <div className="col" style={{ gap: 4, paddingRight: 28 }}>
            <span className="kicker" style={{ color: 'var(--purple)' }}>{t('diff_locks_kicker')}</span>
            <Display as="h2" style={{ fontSize: 26, fontStyle: 'italic', lineHeight: 1, margin: 0 }}>
              {t('diff_locks_title')}
            </Display>
            <Mono style={{ fontSize: 11, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('diff_locks_sub')}</Mono>
          </div>

          {/* YOUR LEVEL: without this, the row numbers have nothing to be read against */}
          <div className="row" style={{ gap: 18, alignItems: 'center', marginTop: 14, padding: '12px 16px',
                                        background: 'var(--surface)', border: '1px solid var(--line)' }}>
            <div className="col" style={{ gap: 2, flex: 'none' }}>
              <span className="row" style={{ gap: 7, alignItems: 'baseline' }}>
                <Display style={{ fontSize: 38, lineHeight: 1, color: 'var(--purple)' }}>{level}</Display>
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>/ {AI_MAX}</Mono>
              </span>
              <Mono style={{ fontSize: 9.5, letterSpacing: '.1em', color: 'var(--text-3)' }}>{t('diff_locks_level')}</Mono>
            </div>
            <div className="col" style={{ gap: 5, flex: 1, minWidth: 0 }}>
              <Mono style={{ fontSize: 10.5, color: 'var(--text-2)', lineHeight: 1.55 }}>
                {t('diff_locks_level_note', { n: corridas })}
              </Mono>
              {/* the trim is explained RIGHT HERE because it's the immediate follow-up question: if I lock this, does the trim freeze too? */}
              <span className="mono" data-dda-trim={trim} style={{ fontSize: 10, color: 'var(--text-3)' }}>
                {t('diff_locks_trim_note', { trim: trim >= 0 ? `+${trim}` : String(trim) })}
              </span>
            </div>
          </div>
        </div>

        <div className="col" style={{ padding: '16px 26px 26px', gap: 14 }}>

          {/* -- the master lock -- */}
          <div className="panel" style={{ padding: '13px 16px',
                 borderColor: lockAll ? 'color-mix(in oklab, var(--purple) 50%, var(--line))' : 'var(--line)',
                 background: lockAll ? 'color-mix(in oklab, var(--purple) 7%, var(--surface))' : 'var(--surface)' }}>
            <div className="row" style={{ gap: 12, alignItems: 'center' }}>
              <button role="switch" aria-checked={lockAll} data-testid="lock-all" className="focusable"
                      aria-label={t('diff_lock_all')}
                      onClick={() => onToggleAll(!lockAll)}
                      style={{ cursor: 'pointer', width: 40, height: 30, display: 'grid', placeItems: 'center',
                               background: lockAll ? 'color-mix(in oklab, var(--purple) 18%, var(--surface-3))' : 'var(--surface-3)',
                               border: `1px solid ${lockAll ? 'var(--purple)' : 'var(--line)'}`,
                               color: lockAll ? 'var(--purple)' : 'var(--text-3)' }}>
                <Padlock locked={lockAll} size={16} />
              </button>
              <div className="col" style={{ gap: 3, flex: 1, minWidth: 0 }}>
                <span style={{ fontSize: 14.5, fontWeight: 700 }}>{t('diff_lock_all')}</span>
                {/* mandatory note: it's the only thing that explains why a brand-new category is born frozen */}
                <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('diff_lock_all_note')}</Mono>
              </div>
              <Mono style={{ fontSize: 10, flex: 'none', color: bloqueadas ? 'var(--purple)' : 'var(--text-3)' }}>
                {t('diff_locks_count', { n: bloqueadas, total: classes.length })}
              </Mono>
            </div>
          </div>

          {/* -- the rows, in the order the engine sends them (races desc); the app does NOT reorder them -- */}
          {classes.length === 0 ? (
            <div className="panel" style={{ padding: '18px 20px', background: 'var(--surface)' }}>
              <Mono style={{ fontSize: 11.5, color: 'var(--text-3)', lineHeight: 1.65 }}>{t('diff_locks_empty')}</Mono>
            </div>
          ) : (
            <div className="col" style={{ gap: 5 }}>
              {corridas === 0 && (
                <div className="panel" style={{ padding: '11px 14px', marginBottom: 4,
                       borderColor: 'color-mix(in oklab, var(--amber) 38%, var(--line))',
                       background: 'color-mix(in oklab, var(--amber) 6%, var(--surface))' }}
                     data-dda-allseeded="1">
                  <Mono style={{ fontSize: 10.5, color: 'var(--text-2)', lineHeight: 1.55 }}>{t('diff_locks_all_seeded')}</Mono>
                </div>
              )}
              <div className="row" style={{ padding: '0 14px', gap: 12 }}>
                <Mono style={{ fontSize: 8.5, letterSpacing: '.12em', color: 'var(--text-3)', width: 128 }}>{t('diff_locks_col_cat')}</Mono>
                <Mono style={{ fontSize: 8.5, letterSpacing: '.12em', color: 'var(--text-3)', flex: 1 }}>
                  {AI_MIN} — {AI_MAX} · <span style={{ color: 'var(--text-2)' }}>|</span> {t('diff_locks_col_level')}
                </Mono>
                <Mono style={{ fontSize: 8.5, letterSpacing: '.12em', color: 'var(--text-3)', width: 104, textAlign: 'right' }}>{t('diff_locks_col_num')}</Mono>
                <span style={{ width: 38, flex: 'none' }} />
              </div>

              {classes.map((k) => {
                const propio = !!k.locked;
                const efectivo = lockAll || propio;
                // final review: a SEEDED category can't be locked on its own -- the engine recalculates its number
                // from your level on every query, so the lock wouldn't freeze anything. The master lock does cover it, though.
                const bloqueable = !lockAll && !k.seeded;
                const nombre = k.className || k.classId;
                const color = k.classColor || 'var(--accent)';
                return (
                  <div key={k.classId} data-testid={`lock-row-${k.classId}`} className="row"
                       style={{ gap: 12, alignItems: 'center', padding: '10px 14px',
                                background: efectivo ? 'color-mix(in oklab, var(--purple) 5%, var(--surface))' : 'var(--surface)',
                                border: `1px solid ${efectivo ? 'color-mix(in oklab, var(--purple) 38%, var(--line))' : 'var(--line)'}`,
                                opacity: k.seeded && !efectivo ? 0.8 : 1 }}>
                    {/* identity */}
                    <span className="row" style={{ gap: 8, alignItems: 'center', width: 128, flex: 'none', minWidth: 0 }}>
                      <span aria-hidden="true" style={{ width: 8, height: 8, borderRadius: '50%', background: color, flex: 'none' }} />
                      <span className="col" style={{ gap: 1, minWidth: 0 }}>
                        <span title={k.classId} style={{ fontSize: 12.5, fontWeight: 700, whiteSpace: 'nowrap',
                                                         overflow: 'hidden', textOverflow: 'ellipsis' }}>{nombre}</span>
                        {k.seeded && (
                          <Mono style={{ fontSize: 8.5, letterSpacing: '.08em', color: 'var(--text-3)' }}>{t('diff_locks_seeded')}</Mono>
                        )}
                        {/* 09/09 (, 4): saturation is shown, not hidden () */}
                        {!!k.saturation && (
                          <Mono data-testid="lock-sat" style={{ fontSize: 8.5, letterSpacing: '.08em', color: 'var(--amber)' }}>
                            {t(k.saturation > 0 ? 'diff_locks_sat_high' : 'diff_locks_sat_low')}
                          </Mono>
                        )}
                      </span>
                    </span>

                    <LevelBar value={k.difficulty} level={level} color={color} locked={efectivo} seeded={!!k.seeded} />

                    {/* the number, how far it strays from your level, and how many races back it up */}
                    <span className="row" style={{ gap: 8, alignItems: 'baseline', width: 104, flex: 'none', justifyContent: 'flex-end' }}>
                      <Display style={{ fontSize: 19, lineHeight: 1, color: efectivo ? 'var(--purple)' : 'var(--text)' }}>
                        {k.seeded ? `≈${k.difficulty}` : k.difficulty}
                      </Display>
                      <Mono style={{ fontSize: 9.5, width: 26, textAlign: 'right',
                                     color: k.delta === 0 ? 'var(--text-3)' : k.delta > 0 ? 'var(--amber)' : 'var(--cyan)' }}>
                        {k.delta === 0 ? '±0' : `${k.delta > 0 ? '+' : ''}${k.delta}`}
                      </Mono>
                      <span className="mono" title={t('diff_locks_races_t')}
                            style={{ fontSize: 9.5, color: 'var(--text-3)', width: 20, textAlign: 'right' }}>
                        {k.seeded ? '—' : k.races}
                      </span>
                    </span>

                    {/* the row's lock */}
                    <button role="switch" aria-checked={efectivo} disabled={!bloqueable}
                            className={bloqueable ? 'focusable' : ''}
                            aria-label={`${t('diff_lock_one')} ${nombre}`}
                            title={k.seeded ? t('diff_lock_seeded') : lockAll ? t('diff_lock_by_master') : undefined}
                            onClick={() => bloqueable && onToggleClass(k.classId, !propio)}
                            style={{ cursor: bloqueable ? 'pointer' : 'default', width: 38, height: 30, flex: 'none',
                                     display: 'grid', placeItems: 'center',
                                     background: efectivo ? 'color-mix(in oklab, var(--purple) 16%, var(--surface-3))' : 'var(--surface-3)',
                                     border: `1px solid ${efectivo ? 'var(--purple)' : 'var(--line)'}`,
                                     color: efectivo ? 'var(--purple)' : 'var(--text-3)',
                                     opacity: bloqueable || efectivo ? 1 : 0.45 }}>
                      <Padlock locked={efectivo} size={14} />
                    </button>
                  </div>
                );
              })}
            </div>
          )}

          {/* what the lock does NOT freeze: spelled out here so nobody finds out the hard way, mid-race */}
          <Mono style={{ fontSize: 10, color: 'var(--text-3)', lineHeight: 1.6 }}>{t('diff_locks_foot')}</Mono>
        </div>
      </div>
    </div>
  );
}
