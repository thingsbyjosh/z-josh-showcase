// Settings > Controller (reworked). This section used to just say "connected/disconnected" and didn't
// detect button presses, so there was no way to tell whether the controller, the browser, or us was at fault.
// Now it shows what the BROWSER actually reports -- which devices it sees, under what name, and which button
// you're pressing right now -- so it doubles as configuration and diagnostics at the same time.
//
// Two things made it look broken:
//  Chromium will NOT reveal a controller until a button is pressed with the window focused. An indicator
//    that only polls will say "disconnected" forever even if the controller is working fine -> hence the
//    explicit notice when the list is empty, which is exactly when the user needs to know it.
//  Only the FIRST connected controller was being read. With a wheel plugged in, that's the wheel, and the
//    desktop gamepad became invisible (see game/gamepads.ts).
//
// Each device has its own button configuration and all of them work at once: no need to pick
// which one is active (see game/gamepadProfiles.ts).
import { useState, useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { gamepadEnabled, setGamepadEnabled, type GamepadAction } from '../../ui/prefs';
import { connectedPads, activeInputs, inputLabel, padInfo, AxisRest, CaptureGate, type PadInfo } from '../../game/gamepads';
import { bindingsFor, setBindingFor, resetProfileFor, hasProfile } from '../../game/gamepadProfiles';
import { suspendGamepadNav } from '../../game/useGamepadNav';
import { Display, Mono, Btn } from '../../ui/components';

const GP_ACTIONS: { key: GamepadAction; labelKey: string }[] = [
  { key: 'up',       labelKey: 'gamepad.up' },
  { key: 'down',     labelKey: 'gamepad.down' },
  { key: 'left',     labelKey: 'gamepad.left' },
  { key: 'right',    labelKey: 'gamepad.right' },
  { key: 'activate', labelKey: 'gamepad.activate' },
  { key: 'back',     labelKey: 'gamepad.back' },
  { key: 'prevTab',  labelKey: 'gamepad.prevTab' },
  { key: 'nextTab',  labelKey: 'gamepad.nextTab' },
];

// 27/09: every frame, not every 100 ms. With 100 ms a short press fell between two reads and was “not
// detected”, while the navigation loop (which runs per frame) did see it and moved the focus. The repaint
// stays cheap: the states below only change when what is seen changes.
const POLL_MS = 16;

export function GamepadSection() {
  const { t } = useTranslation();
  const [on, setOn] = useState(gamepadEnabled());
  const [devices, setDevices] = useState<PadInfo[]>([]);
  const [selected, setSelected] = useState<string | null>(null);
  // What EACH device is pressing (padId -> label). Before only the first of all was shown, and a
  // stuck wheel button hid whatever you did with the controller.
  const [live, setLive] = useState<Record<string, string>>({});
  const [binds, setBinds] = useState(() => bindingsFor(''));
  const [capturing, setCapturing] = useState<GamepadAction | null>(null);
  const capturingRef = useRef<GamepadAction | null>(null);
  const selectedRef = useRef<string | null>(null);
  const gateRef = useRef<CaptureGate | null>(null);
  // Axis rest position is calibrated from when the section mounts, not when capture starts: if it
  // were calibrated at capture time, whatever axis the user happened to be moving right then would get locked in AS rest.
  const restRef = useRef(new AxisRest());
  const tRef = useRef(t);
  capturingRef.current = capturing;
  selectedRef.current = selected;
  tRef.current = t;

  // 27/09 — while capturing, the controller does NOT navigate the app (same suspension as the VR panel's
  // confirm key, ). Without this, configuring “up” moved the focus up, and A or B pressed or closed whatever
  // was behind. Escape cancels without changing anything. The “release first” gate is born with each capture.
  useEffect(() => {
    if (!capturing) { gateRef.current = null; return; }
    gateRef.current = new CaptureGate();
    const resumeNav = suspendGamepadNav();
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); setCapturing(null); } };
    window.addEventListener('keydown', onKey, true);
    return () => { window.removeEventListener('keydown', onKey, true); resumeNav(); gateRef.current = null; };
  }, [capturing]);

  // Single polling loop: device list + pressed button + capture. It's all done in one place so the
  // screen can't contradict itself (saying "no controllers" while capturing a button press).
  useEffect(() => {
    let stop = false;
    let lastSig = '';
    let lastLiveSig = '';
    const tick = () => {
      if (stop) return;
      const pads = connectedPads();
      const infos = pads.map(padInfo);
      const sig = infos.map((d) => `${d.index}|${d.id}|${d.buttons}|${d.axes}`).join('~');
      if (sig !== lastSig) {
        lastSig = sig;
        setDevices(infos);
        // Auto-selection: if nothing is chosen, or the chosen device disconnected, it falls back to the first one available.
        setSelected((cur) => (cur && infos.some((d) => d.id === cur) ? cur : (infos[0]?.id ?? null)));
      }

      // Live reading: button OR axis, per device. With buttons only, a wheel's D-pad did not show up
      // anywhere and there was no way to know whether the device was arriving or not.
      const acts = activeInputs(pads, restRef.current);
      const liveSig = acts.map((a) => `${a.padId}=${inputLabel(a.input, tRef.current)}`).join('~');
      if (liveSig !== lastLiveSig) {
        lastLiveSig = liveSig;
        const next: Record<string, string> = {};
        for (const a of acts) if (!(a.padId in next)) next[a.padId] = inputLabel(a.input, tRef.current);
        setLive(next);
      }

      // Capture: ONLY from the selected device. With several connected, brushing the wheel while
      // configuring the gamepad shouldn't steal your binding -- it's the device whose bindings you're
      // seeing on screen. And “release first” (CaptureGate): the button the capture was opened with is not
      // valid, and until that device passes through rest nothing is accepted.
      const target = selectedRef.current;
      const action = capturingRef.current;
      const gate = gateRef.current;
      if (action && target && gate) {
        const mine = acts.find((a) => a.padId === target)?.input ?? null;
        const hit = gate.feed(mine);
        if (hit) {
          setBindingFor(target, action, hit);
          setBinds(bindingsFor(target));
          setCapturing(null);
        }
      }
    };
    tick();
    const iv = setInterval(tick, POLL_MS);
    return () => { stop = true; clearInterval(iv); };
  }, []);

  useEffect(() => { setBinds(bindingsFor(selected ?? '')); }, [selected]);

  const toggle = () => { const v = !on; setGamepadEnabled(v); setOn(v); };
  const resetDevice = () => {
    if (!selected) return;
    resetProfileFor(selected);
    setBinds(bindingsFor(selected));
  };

  return (
    <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
      <div className="row between" style={{ alignItems: 'flex-start' }}>
        <div className="col" style={{ gap: 2 }}>
          <Display style={{ fontSize: 19 }}>{t('gamepad.title')}</Display>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('gamepad.sub')}</Mono>
        </div>
        <Mono style={{ fontSize: 10, color: devices.length ? 'var(--green)' : 'var(--text-3)' }}>
          {devices.length ? t('gamepad.connected') : t('gamepad.disconnected')}
        </Mono>
      </div>

      {/* Detected devices. Empty doesn't mean "you have no controller": it means the browser hasn't
          revealed it yet, and that has to be said or the user will assume the feature is broken. */}
      <div className="col" style={{ gap: 6 }}>
        <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('gamepad.devices')}</Mono>
        {!devices.length && (
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.6, padding: '10px 14px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
            {t('gamepad.noDevices')}
          </Mono>
        )}
        {devices.map((d) => {
          const isSel = d.id === selected;
          const pressedHere = live[d.id] ?? null;
          return (
            <button
              key={`${d.index}:${d.id}`}
              className="focusable row between"
              aria-pressed={isSel}
              onClick={() => setSelected(d.id)}
              style={{
                alignItems: 'center', gap: 14, padding: '10px 14px', textAlign: 'left', width: '100%',
                background: 'var(--surface)', border: `1px solid ${isSel ? 'var(--accent)' : 'var(--line)'}`,
                color: 'inherit', cursor: 'pointer',
              }}
            >
              <div className="col" style={{ gap: 2, minWidth: 0 }}>
                <span className="display" style={{ fontSize: 13, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{d.id}</span>
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
                  {d.buttons}b · {d.axes}ax · {d.mapping}{hasProfile(d.id) ? ` · ${t('gamepad.customised')}` : ''}
                </Mono>
              </div>
              <Mono style={{ fontSize: 10, flexShrink: 0, color: pressedHere ? 'var(--accent)' : 'var(--text-3)' }}>
                {pressedHere ?? '—'}
              </Mono>
            </button>
          );
        })}
      </div>

      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <span className="display" style={{ fontSize: 15 }}>{t('gamepad.enable')}</span>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('gamepad.enableDesc')}</Mono>
        </div>
        <div className="row" role="group" aria-label={t('gamepad.enable')} style={{ flexShrink: 0 }}>
          <Btn variant={on ? 'primary' : 'ghost'} size="sm" aria-pressed={on} onClick={() => !on && toggle()}>{t('gamepad.on')}</Btn>
          <Btn variant={!on ? 'primary' : 'ghost'} size="sm" aria-pressed={!on} onClick={() => on && toggle()}>{t('gamepad.off')}</Btn>
        </div>
      </div>

      {on && selected && (
        <div className="col" style={{ gap: 2, padding: '4px 0' }}>
          {GP_ACTIONS.map((a) => (
            <div key={a.key} className="row between" style={{ alignItems: 'center', gap: 16, padding: '9px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
              <span className="display" style={{ fontSize: 13 }}>{t(a.labelKey)}</span>
              {/* Pressing it again cancels: "press a button..." should never be a dead end. */}
              <Btn variant="ghost" size="sm" style={{ minWidth: 150, flexShrink: 0 }}
                   onClick={() => setCapturing((cur) => (cur === a.key ? null : a.key))}>
                {capturing === a.key ? t('gamepad.pressButton') : inputLabel(binds[a.key], t)}
              </Btn>
            </div>
          ))}
          <div className="row" style={{ justifyContent: 'flex-end', marginTop: 6 }}>
            <Btn variant="ghost" size="sm" onClick={resetDevice}>{t('gamepad.reset')}</Btn>
          </div>
        </div>
      )}
    </div>
  );
}
