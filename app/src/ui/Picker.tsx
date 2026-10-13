// A custom dropdown, drawn in the DOM.
//
// Why a plain <select> won't work: WINDOWS opens its list in a separate window that sits OUTSIDE the app's
// client area. Both in-headset panels mirror that same area, so the list is invisible everywhere,
// and on top of that a synthetic click (all that ever arrives from the headset) can't even open it: browsers
// require a real user gesture to open a native select. In other words, in VR a plain <select> doesn't fail
// sometimes -- it can never work at all. Reported against the date-of-birth field on the sign-up screen.
//
// This is the same idea but inside the document: the list is just another div, clicked like any button, and it travels
// to the headset along with the rest of the window.
import type { ReactNode } from 'react';
import { useEffect, useRef, useState, type CSSProperties } from 'react';

export interface PickerOption<T> {
  value: T;
  label: string;
  /** 17/09: ornament to the left of the label (a flag, e.g.). It shows in the LIST and, if the button
   *  has no `buttonLabel`, also in the button, which is how a single-icon selector is achieved. */
  icon?: ReactNode;
}

export interface PickerProps<T> {
  value: T;
  options: PickerOption<T>[];
  onChange: (v: T) => void;
  /** Text for screen readers; also the button's aria-label. */
  label: string;
  style?: CSSProperties;
  /** Max height of the open list. Defaults to fitting a long list (years) without eating the whole screen. */
  maxListHeight?: number;
  /** Guided tutorial anchor. It goes on the BUTTON, which is what driver.js highlights and what the player
   *  has to find; putting it on the container would also highlight the open list. */
  dataTour?: string;
  /** 17/09: what the BUTTON shows when it must not be the option's long label. The list still
   *  shows `label`, which is where the whole name has to be readable (see LocalePicker). */
  buttonLabel?: string;
  /** 17/09: narrow button (reduced padding and arrow). Without this, the fixed 12 px padding per side
   *  plus the little arrow eat the entire width of a short button and the text gets clipped to zero. */
  compact?: boolean;
}

export function Picker<T extends string | number>({
  value, options, onChange, label, style, maxListHeight = 260, dataTour, buttonLabel, compact,
}: PickerProps<T>) {
  const [open, setOpen] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const list = useRef<HTMLDivElement>(null);
  const current = options.find((o) => o.value === value);

  // Close on an outside click. Listened for in the capture phase, on 'mousedown', because clicks from the headset are
  // synthetic events: they arrive as loose mousedown/mouseup/click, not a native interaction.
  useEffect(() => {
    if (!open) return;
    const onDown = (e: Event) => {
      if (e.target instanceof Node && root.current?.contains(e.target)) return;
      setOpen(false);
    };
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') setOpen(false); };
    document.addEventListener('mousedown', onDown, true);
    document.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('mousedown', onDown, true);
      document.removeEventListener('keydown', onKey);
    };
  }, [open]);

  // On open, keep the current selection in view. With 81 years in the list, opening at the very top would force
  // blind scrolling, and scrolling inside a headset is exactly the expensive part.
  useEffect(() => {
    if (!open) return;
    const sel = list.current?.querySelector('[data-selected="1"]');
    // The typeof check isn't test paranoia: if scrollIntoView is missing, this would blow up the entire dropdown's
    // render over a mere convenience detail. Not being centered is worse than nothing -- not not being visible at all.
    if (sel && typeof sel.scrollIntoView === 'function') sel.scrollIntoView({ block: 'center' });
  }, [open]);

  return (
    // The look lives in design-system.css (.pick*), not in inline `style`: here it was with
    // `borderRadius: 8` and rounded surfaces, and the house design system says
    // `border-radius: 0` in full ("not a web card"). That is why the dropdown read
    // as a pile of loose buttons: it did not look like anything else in the app. What stays
    // inline is what the caller decides (width) or what is a number, not a style (max height).
    <div ref={root} className="pick" data-open={open ? '1' : '0'} data-compact={compact ? '1' : undefined} style={style}>
      <button
        type="button"
        className="pick-btn"
        aria-label={label}
        aria-expanded={open}
        data-tour={dataTour}
        onClick={() => setOpen((v) => !v)}
      >
        {current?.icon}
        <span className="pick-val">{buttonLabel ?? (current?.icon ? '' : current?.label ?? '')}</span>
        <span aria-hidden="true" className="pick-caret">▾</span>
      </button>
      {open && (
        <div ref={list} role="listbox" aria-label={label} className="pick-list"
             style={{ maxHeight: maxListHeight }}>
          {options.map((o) => {
            const sel = o.value === value;
            return (
              <button
                key={String(o.value)}
                type="button"
                role="option"
                aria-selected={sel}
                data-selected={sel ? '1' : '0'}
                className={sel ? 'pick-opt on' : 'pick-opt'}
                onClick={() => { onChange(o.value); setOpen(false); }}
              >
                {o.icon}
                <span>{o.label}</span>
              </button>
            );
          })}
        </div>
      )}
    </div>
  );
}
