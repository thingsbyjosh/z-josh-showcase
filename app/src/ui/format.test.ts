// race duration reads badly as a bare number of minutes. A player picked 25% of
// a historical-era race and ended up with an hour and a half in a Ashmere Motors: "90" doesn't warn you, "1h 30m" does.
import { describe, it, expect } from 'vitest';
import { formatRaceLength } from './format';

describe('formatRaceLength', () => {
  it('por debajo de la hora se queda en minutos', () => {
    expect(formatRaceLength(0)).toBe('0m');
    expect(formatRaceLength(15)).toBe('15m');
    expect(formatRaceLength(59)).toBe('59m');
  });

  it('a partir de la hora la enseña, que es lo que sorprende', () => {
    expect(formatRaceLength(60)).toBe('1h');
    expect(formatRaceLength(90)).toBe('1h 30m');
    expect(formatRaceLength(360)).toBe('6h');
    expect(formatRaceLength(1440)).toBe('24h');
  });

  it('redondea a minuto entero y no devuelve negativos', () => {
    expect(formatRaceLength(89.6)).toBe('1h 30m');
    expect(formatRaceLength(-5)).toBe('0m');
  });
});
