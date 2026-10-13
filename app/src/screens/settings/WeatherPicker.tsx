// The weather conditions the player allows in their races. The grid uses the
// game's own photos (ui/weatherImages), the same ones the game shows in its own picker: you recognize
// the weather by the image, with the name as backup. Checked = photo lit + checkmark box; un-
// checked = photo dimmed. A weight of 0 means "that weather never happens".
import { useTranslation } from 'react-i18next';
import { Mono } from '../../ui/components';
import { wxImage } from '../../ui/weatherImages';

// Codes from the game's own catalog (the NAMES live in wxname_*, not duplicated here). Code 17 (the game's
// native random) is NOT included: this whole block exists to stop delegating to it.
export const WX_CODES = [0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 12, 13, 14, 15] as const;

export function WeatherPicker({ weights, defaults, onChange }: {
  weights: Record<string, number>;
  defaults: Record<string, number>;
  onChange: (weights: Record<string, number>) => void;
}) {
  const { t } = useTranslation();
  // with no saved selection, it starts from the factory weights: the player sees checked whatever was already
  // happening, and unchecks whatever they don't want.
  const base = Object.keys(weights).length ? weights : defaults;
  const isOn = (code: number) => (base[String(code)] ?? 0) > 0;

  const toggle = (code: number) => {
    const next: Record<string, number> = {};
    for (const c of WX_CODES) {
      const want = c === code ? !isOn(c) : isOn(c);
      next[String(c)] = want ? (defaults[String(c)] ?? 1) || 1 : 0;
    }
    // unchecking everything would leave a race with no possible weather: the last one is protected from that.
    if (Object.values(next).some((v) => v > 0)) onChange(next);
  };

  return (
    <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(150px, 1fr))', gap: 8 }}>
      {WX_CODES.map((code) => {
        const on = isOn(code);
        const img = wxImage(code);
        return (
          <button
            key={code}
            type="button"
            aria-pressed={on}
            onClick={() => toggle(code)}
            style={{
              display: 'block', padding: 0, textAlign: 'left', cursor: 'pointer', overflow: 'hidden',
              background: 'var(--surface-2)',
              border: `1px solid ${on ? 'var(--line-2)' : 'var(--line)'}`,
              opacity: on ? 1 : 0.6,
              transition: 'opacity .12s, border-color .12s',
            }}
          >
            {img && (
              // half a dozen weather types in the catalog are already gray to begin with (overcast, drizzle, fog):
              // desaturating wouldn't tell them apart, so "off" is marked by actually dimming the photo.
              <img
                src={img}
                alt=""
                style={{
                  display: 'block', width: '100%', aspectRatio: '320 / 211', objectFit: 'cover',
                  filter: on ? 'none' : 'grayscale(1) brightness(.3)',
                  transition: 'filter .12s',
                }}
              />
            )}
            <span className="row" style={{ alignItems: 'center', gap: 6, padding: '7px 8px' }}>
              {/* the state doesn't rely on color alone: filled box = checked, hollow = unchecked */}
              <span style={{
                width: 8, height: 8, flexShrink: 0,
                background: on ? 'var(--accent)' : 'transparent',
                border: `1px solid ${on ? 'var(--accent)' : 'var(--line-2)'}`,
              }} />
              <Mono style={{ fontSize: 10.5, lineHeight: 1.3, color: on ? 'var(--text)' : 'var(--text-3)' }}>
                {t(`wxname_${code}`)}
              </Mono>
            </span>
          </button>
        );
      })}
    </div>
  );
}
