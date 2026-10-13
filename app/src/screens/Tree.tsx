// Tree: progression tree, ported from screen_tree.cpp (source of truth for WHAT is shown
// and in WHAT ORDER). Discipline(row)×level(column) matrix from the REAL ladder + rating-band badge +
// an inert LEGACY tile. 100% READ-ONLY (query.tree): the app groups the flat nodes by discipline×level and colors
// the disciplines with DISC_META (same as Calendar). Clicking a card (to open a championship) and the LEGACY tile are
// navigation DEFERRED to the shell -- inert here. Labels via t() (engine keys + new tree.* keys).
import { useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, TreePayload, TreeNode, CalendarDiscipline } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono } from '../ui/components';
import { accentText } from '../ui/colors';
import { DISC_META, DISC_ORDER } from '../ui/disciplines';

const LEVELS = [1, 2, 3, 4, 5, 6, 7];

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

function Card({ node }: { node: TreeNode }) {
  return (
    <div style={{
      display: 'flex', alignItems: 'center', gap: 8, padding: '6px 8px', marginBottom: 6,
      background: 'var(--surface-2)', borderRadius: 4,
      opacity: node.locked ? 0.45 : 1, minWidth: 0,
    }}>
      <span style={{ flex: 1, minWidth: 0, fontSize: 12.5, fontWeight: 600, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{node.display}</span>
      {node.isDlc && <span style={{ fontSize: 8, letterSpacing: '.06em', color: 'var(--purple)', background: 'color-mix(in oklab, var(--purple) 20%, transparent)', borderRadius: 3, padding: '2px 4px', flex: 'none' }}>DLC</span>}
      {node.carCount > 0 && <Mono style={{ fontSize: 10, color: 'var(--text-3)', flex: 'none' }}>{node.carCount}</Mono>}
    </div>
  );
}

export function Tree({ client }: { client: ApiClientLike }) {
  const { t, i18n } = useTranslation();
  const q = useQuery<TreePayload>(client, 'query.tree');
  const nodes = useMemo(() => q.data?.nodes ?? [], [q.data]);
  const frontier = q.data?.frontierPhase ?? 0;
  const es = i18n.language === 'es';

  // sparse discipline×level matrix (built client-side, same as the native screen does when rendering)
  const cells = useMemo(() => {
    const m = new Map<string, TreeNode[]>();
    for (const n of nodes) {
      const k = `${n.disc}|${n.level}`;
      (m.get(k) ?? m.set(k, []).get(k)!).push(n);
    }
    return m;
  }, [nodes]);
  const discsPresent = useMemo(() => DISC_ORDER.filter((d) => nodes.some((n) => n.disc === d)), [nodes]);

  if (q.loading) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || nodes.length === 0) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('tree.empty')}</Mono></Center>;

  const tier = q.data!.ratingTier;

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      {/* header: title + help + rating badge + legacy tile */}
      <div className="row between" style={{ padding: '16px 30px 12px', alignItems: 'flex-start', gap: 16 }}>
        <div className="col" style={{ gap: 3, minWidth: 0 }}>
          <Display as="h1" style={{ fontSize: 30 }}>{t('nav.tree')}</Display>
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('tree.help')}</Mono>
        </div>
        <div className="row gap12" style={{ alignItems: 'stretch', flex: 'none' }}>
          <div style={{ padding: '8px 14px', borderRadius: 4, background: 'color-mix(in oklab, var(--purple) 14%, var(--surface-2))', border: '1px solid var(--line)' }}>
            <Mono style={{ fontSize: 9, letterSpacing: '.14em', color: 'var(--purple)' }}>{t('legacy')}</Mono>
            <div style={{ fontSize: 11, color: 'var(--text-2)', maxWidth: 150 }}>{t('legacy_sub')}</div>
          </div>
          <div style={{ padding: '8px 14px', borderRadius: 4, background: `color-mix(in oklab, ${tier.color} 16%, var(--surface-2))`, border: '1px solid var(--line)' }}>
            <span className="row gap6" style={{ alignItems: 'center' }}>
              <span style={{ width: 7, height: 7, borderRadius: '50%', background: tier.color }} />
              <Mono style={{ fontSize: 9, letterSpacing: '.14em', color: 'var(--text-3)' }}>{t('rating_tier')}</Mono>
            </span>
            <div className="display" style={{ fontSize: 18, color: accentText(tier.color), textTransform: 'uppercase' }}>{t(tier.key)}</div>
          </div>
        </div>
      </div>

      {/* ladder progress bar -- 7 segments (one per level) that fill up to the
          UNLOCKED level (the frontier). Makes "how far you've gotten" and how it advances on leveling up clear at a glance. */}
      <div style={{ padding: '0 30px 14px' }} data-tour="tree.ladder">
        <div className="row between" style={{ alignItems: 'baseline', marginBottom: 6 }}>
          <Mono style={{ fontSize: 10, letterSpacing: '.1em', color: 'var(--text-3)', textTransform: 'uppercase' }}>{t('tree.ladderProgress', { defaultValue: 'Ladder progress' })}</Mono>
          <Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>
            {t('tree.unlockedTo', { defaultValue: 'Unlocked to' })} <span style={{ color: 'var(--accent-text)' }}>L{frontier} · {t(`tree.level.${frontier}`)}</span>
            {frontier < 7 && <span style={{ color: 'var(--text-3)' }}> · {t('tree.next', { defaultValue: 'Next' })} L{frontier + 1}</span>}
          </Mono>
        </div>
        <div className="row" style={{ gap: 4 }}>
          {LEVELS.map((lv) => (
            <div key={lv} title={`L${lv} · ${t(`tree.level.${lv}`)}`}
              style={{ flex: 1, height: 8, borderRadius: 2, background: lv <= frontier ? 'var(--accent)' : 'var(--surface-3)', boxShadow: lv === frontier ? '0 0 0 1px var(--accent-text)' : 'none', opacity: lv <= frontier ? 1 : 0.7 }} />
          ))}
        </div>
      </div>

      {/* discipline×level matrix */}
      <div className="scroll" style={{ flex: 1, padding: '0 30px 24px', minHeight: 0 }} data-tour="tree.matrix">
        <div style={{ display: 'grid', gridTemplateColumns: `132px repeat(7, minmax(120px, 1fr))`, gap: 8, minWidth: 900 }}>
          {/* column header */}
          <div />
          {LEVELS.map((lv) => (
            <div key={lv} style={{ padding: '6px 4px', textAlign: 'center', opacity: lv > frontier ? 0.5 : 1 }}>
              <Mono style={{ fontSize: 11, fontWeight: 700, color: lv === frontier ? 'var(--accent-text)' : 'var(--text-2)' }}>L{lv}</Mono>
              <div style={{ fontSize: 9, color: 'var(--text-3)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{t(`tree.level.${lv}`)}</div>
            </div>
          ))}
          {/* rows by discipline */}
          {discsPresent.map((d: CalendarDiscipline) => {
            const meta = DISC_META[d];
            return (
              <div key={d} style={{ display: 'contents' }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6, padding: '8px 8px', background: `color-mix(in oklab, ${meta.color} 12%, transparent)`, borderRadius: 4 }}>
                  <Mono style={{ fontSize: 11, color: meta.color, fontWeight: 600 }}>{es ? meta.es : meta.en}</Mono>
                </div>
                {LEVELS.map((lv) => (
                  <div key={lv} style={{ padding: '4px 2px', background: `color-mix(in oklab, ${meta.color} 4%, transparent)`, borderRadius: 4 }}>
                    {(cells.get(`${d}|${lv}`) ?? []).map((n) => <Card key={n.id} node={n} />)}
                  </div>
                ))}
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
