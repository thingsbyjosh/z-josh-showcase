// The "hands off" notice, between pressing Rent/Race in pre-race and the race actually starting.
//
// Why it exists: on confirm, the mod navigates the game's menus and writes Custom Race using synthetic clicks and
// keystrokes. During those seconds the game can't tell ours apart from the player's, so a
// stray click of theirs, or a manual Alt+Tab that hands focus back to the game mid-sequence, sneaks into the
// same navigation and undoes what was just written. The race starts with a different configuration, and there's no
// way to tell from inside the app that it happened.
//
// And with a headset on there's a third path the player wouldn't connect to a click at all: the game fires its pointer
// on sustained GAZE (PointerTriggerTime in the game's own settings), so staring at one spot
// on the menu ends up clicking whatever's underneath it. That line is only shown to someone about to play with a headset: for
// a monitor player it's irrelevant, and a notice padded with noise gets read less.
//
// It does NOT block: it counts down and releases the race on its own. It repeats on every race, and a confirm button placed
// in the middle of the path just ends up clicked without reading. Anyone who already knows it has the "don't show again" check, which
// can be turned back on from Settings.
import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Display, Mono, Btn } from './components';
import { launchMode, setLaunchNoticeEnabled } from './prefs';

export const LAUNCH_NOTICE_MS = 5000;

export function LaunchNotice({ onDone }: { onDone: () => void }) {
  const { t } = useTranslation();
  const vr = launchMode() !== 'flat';
  const [left, setLeft] = useState(Math.ceil(LAUNCH_NOTICE_MS / 1000));
  const [hide, setHide] = useState(false);
  // The timer is armed ONCE: if it depended on onDone, every render of the parent would re-arm it and the countdown
  // would never reach zero. The ref keeps the callback fresh without touching the effect.
  const doneRef = useRef(onDone);
  doneRef.current = onDone;
  const fired = useRef(false);
  const fire = () => { if (!fired.current) { fired.current = true; doneRef.current(); } };

  useEffect(() => {
    const iv = setInterval(() => setLeft((n) => (n > 1 ? n - 1 : 1)), 1000);
    const to = setTimeout(() => { clearInterval(iv); fire(); }, LAUNCH_NOTICE_MS);
    return () => { clearInterval(iv); clearTimeout(to); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const points = ['hands', 'alttab', ...(vr ? ['gaze'] : [])];
  const toggleHide = () => { const v = !hide; setHide(v); setLaunchNoticeEnabled(!v); };

  return (
    <div style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.82)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 60 }}>
      <div className="panel" role="alertdialog" aria-modal="true" aria-label={t('launch_notice_title')}
           style={{ width: 520, maxHeight: '86vh', overflowY: 'auto', padding: '26px 28px', border: '1px solid color-mix(in oklab, var(--amber) 55%, transparent)', display: 'flex', flexDirection: 'column', gap: 14 }}>
        <div className="col" style={{ gap: 4 }}>
          <Display style={{ fontSize: 24, color: 'var(--amber)' }}>{t('launch_notice_title')}</Display>
          <Mono style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.6 }}>{t('launch_notice_why')}</Mono>
        </div>

        <div className="col" style={{ gap: 8, padding: '12px 14px', background: 'var(--bg-2)', border: '1px solid var(--line)' }}>
          {points.map((k) => (
            <Mono key={k} style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.5 }}>· {t(`launch_notice_${k}`)}</Mono>
          ))}
        </div>

        <div className="row between" style={{ alignItems: 'center', gap: 16, marginTop: 4 }}>
          <label className="row" style={{ alignItems: 'center', gap: 8, cursor: 'pointer', minWidth: 0 }}>
            <input type="checkbox" checked={hide} onChange={toggleHide} style={{ accentColor: 'var(--accent)', width: 15, height: 15, flexShrink: 0 }} />
            <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('launch_notice_hide')}</Mono>
          </label>
          <Btn variant="primary" autoFocus onClick={fire} style={{ flexShrink: 0 }}>
            {t('launch_notice_skip')} · {left}
          </Btn>
        </div>
      </div>
    </div>
  );
}
