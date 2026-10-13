// Metadata for the 6 disciplines (color + EN/ES name) shared by the screens that color by
// discipline (Calendar, Tree, ...). The engine sends the discipline as a TOKEN (query.calendar/query.tree), and
// the app paints it with this -- the same colors as the native overlay's discipline palette. It does NOT come from the payload.
import type { CalendarDiscipline } from '../api/contracts';

export const DISC_META: Record<CalendarDiscipline, { color: string; es: string; en: string }> = {
  single:    { color: '#5ec8ff', es: 'Monoplaza',   en: 'Single-seater' },
  touring:   { color: '#ffb84d', es: 'Turismo',     en: 'Touring' },
  gt:        { color: '#ff4d2e', es: 'GT',          en: 'GT' },
  endurance: { color: '#b94bff', es: 'Resistencia', en: 'Endurance' },
  entry:     { color: '#19d27c', es: 'Inicio',      en: 'Entry' },
  offroad:   { color: 'var(--steel)', es: 'Todoterreno', en: 'Offroad' },
};

export const DISC_ORDER: CalendarDiscipline[] = ['single', 'touring', 'gt', 'endurance', 'entry', 'offroad'];
