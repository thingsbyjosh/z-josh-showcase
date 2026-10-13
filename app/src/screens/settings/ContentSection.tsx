// Settings > Content and DLC. Settings only holds the ENTRY POINT (a row with a button); the toggles
// live in their own POPUP (user feedback: "you go into another screen or popup and turn
// dlcs on or off"). Auto-detection from live catalogs (query.content); turning a pack off simulates it being absent
// (track substitutions and series locks as if it weren't installed). The engine persists the override in the
// install directory (cache\dlc_overrides.psv), never in the save. Modal = the app's standard pattern (Home.tsx's
// confirmBreak: fixed backdrop + .panel role=dialog + click-outside closes it).
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, ContentPayload } from '../../api/contracts';
import { useQuery } from '../../api/useQuery';
import { Display, Mono, Btn } from '../../ui/components';

// THREE states per pack, not two. AUTO (default) = installed-content detection wins; ON/OFF
// = the player pinned it by hand, and that choice survives game updates and detection changes. Before, there were only
// on/off, and "untouched" meant ON, so packs the player did NOT have showed up turned on: the
// screen looked like it was telling them they owned everything (one report noted "lots show not detected but are on").
type Mode = 'auto' | 'on' | 'off';
const MODES: Mode[] = ['auto', 'on', 'off'];

export function ContentSection({ client }: { client: ApiClientLike }) {
  const { t } = useTranslation();
  const { data, refetch } = useQuery<ContentPayload>(client, 'query.content');
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  // Old DLL without query.content (or no products yet): the whole section disappears (additive contract)
  if (!data || data.products.length === 0) return null;

  // `enabled` still travels alongside `mode` so an older DLL (which only understands {product, enabled}) still
  // does the sensible thing with the click instead of ignoring it.
  const setMode = async (id: string, mode: Mode, enabled: boolean) => {
    setBusy(id);
    try {
      await client.cmd('cmd.content.toggle', { product: id, mode, enabled });
      refetch();
    } finally {
      setBusy(null);
    }
  };

  return (
    <div className="col" style={{ gap: 10 }}>
      {/* entry point in Settings: summary row + button that opens the popup (same idiom as profile_edit) */}
      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <span className="display" style={{ fontSize: 15 }}>{t('content.title')}</span>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('content.sub')}</Mono>
        </div>
        {/* 31/08: on OPEN it asks again. Steam's ownership data takes a few seconds to be ready
            after the game starts, and this view used to be fetched only once on connect: whoever opened it
            afterward kept seeing the old snapshot, everything on, even though the engine already knew packs were missing. */}
        <Btn variant="ghost" size="sm" onClick={() => { refetch(); setOpen(true); }} style={{ flexShrink: 0 }}>{t('content.open')}</Btn>
      </div>

      {open && (
        <div onClick={() => setOpen(false)} style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.82)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 50 }}>
          <div className="panel" role="dialog" aria-modal="true" aria-label={t('content.title')} onClick={(e) => e.stopPropagation()}
               style={{ width: 560, maxHeight: '78vh', padding: '24px 26px', display: 'flex', flexDirection: 'column', gap: 12 }}>
            <div>
              <Display style={{ fontSize: 22 }}>{t('content.title')}</Display>
              <div><Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('content.sub')}</Mono></div>
            </div>
            {!data.scanned && (
              <Mono style={{ fontSize: 10.5, color: 'var(--amber)' }}>{t('content.unscanned')}</Mono>
            )}
            <Mono style={{ fontSize: 10, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('content.hint')}</Mono>
            <div className="col" style={{ gap: 0, overflowY: 'auto', minHeight: 0 }}>
              {data.products.map((p) => {
                const mode: Mode = p.override ?? 'auto';   // Older DLL without the field: everything defaults to AUTO
                // 31/08: Steam says this pack is NOT yours. It's explained (otherwise a pack that's off for no
                // visible reason looks like a detection failure) and the On button becomes unusable: the engine no
                // longer turns it on -- turning it on would mean running unpurchased content -- and a button that does
                // nothing is the same sin as the dead-click bug. Turning off and going back to AUTO are still available.
                const notOwned = p.owned === 'not_owned';
                return (
                <div key={p.id} className="row between" style={{ alignItems: 'center', gap: 12, padding: '7px 0', borderBottom: '1px solid var(--line)' }}>
                  <div className="col" style={{ minWidth: 0, gap: 2 }}>
                    <span className="display" style={{ fontSize: 14 }}>{p.display}</span>
                    <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>
                      {t(`content.kind.${p.kind}`)}
                      {data.scanned ? ` · ${p.detected ? t('content.detected') : t('content.notDetected')}` : ''}
                      {/* In AUTO the highlighted button doesn't say what's actually happening (detection decides that), so
                          it's spelled out: it's the line that answers "it's not detected but it's on, so what does that mean?" */}
                      {mode === 'auto' ? ` · ${p.enabled ? t('content.autoOn') : t('content.autoOff')}` : ''}
                    </Mono>
                    {notOwned && (
                      <Mono style={{ fontSize: 9.5, color: 'var(--amber)' }}>{t('content.notOwned', { defaultValue: 'Not in your Steam account' })}</Mono>
                    )}
                  </div>
                  <div className="row" role="group" aria-label={p.display} style={{ flexShrink: 0 }}>
                    {MODES.map((m) => {
                      const blocked = notOwned && m === 'on';   // you cannot switch on what is not yours
                      return (
                      <Btn key={m} variant={mode === m ? 'primary' : 'ghost'} size="sm" disabled={busy === p.id || blocked}
                           title={blocked ? t('content.notOwnedHint', { defaultValue: 'Steam says this pack is not on your account, so it cannot be turned on.' }) : undefined}
                           onClick={() => !blocked && mode !== m && setMode(p.id, m, m === 'off' ? false : m === 'on' ? true : p.detected)}
                           aria-pressed={mode === m}>{t(`content.${m}`)}</Btn>
                      );
                    })}
                  </div>
                </div>
                );
              })}
            </div>
            <Btn variant="ghost" data-gamepad-back onClick={() => setOpen(false)} style={{ alignSelf: 'flex-end' }}>{t('content.close')}</Btn>
          </div>
        </div>
      )}
    </div>
  );
}
