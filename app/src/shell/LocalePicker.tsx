// The topbar language selector.
//
// It is the house `Picker` and not a custom dropdown. The previous one was an absolute div with
// bare buttons —no background, no border, no shadow— floating over the content, and it did not even
// close on clicking outside: it only toggled. Reusing `Picker` fixes both at once and brings
// in addition what that component already solved for a reason: 34 px rows because in the headset you aim
// with your head at a meter and a half, closing on `mousedown` in capture because VR clicks arrive
// as loose synthetic events, and the chosen option centered on open.
//
// And for the same reason `Picker` is not a native `<select>`: in VR the list is drawn by Windows OUTSIDE
// the client area that is mirrored to the headset, so it would not be seen anywhere.
import { LOCALES, LOCALE_LABELS, type Locale } from '../i18n';
import { Picker } from '../ui/Picker';

// 17/09 (Josh's decision) — IN THE BAR, THE FLAG; IN THE LIST, FLAG AND NAME.
// Fixed width, because the topbar cannot rearrange itself on a language change. Before, the longest label fit
// (“Português (BR)”) and that cost 148 px of a row that is tight: now the button only shows the
// flag and it is 52. The list still shows flag AND name, which is where it has to be readable — and there
// the only real ambiguity of this flag set is resolved, Portugal's Portuguese against
// Brazil's.
//
// Flag as PNG from our own set (app/public/flags), not emoji: Windows does not draw flag
// emoji in color — it paints them as two loose letters —, so a 🇪🇸 would look like a misplaced “ES”.
// 72 and not 40: the button spends padding and a little arrow before painting a letter, and with 40 the text was left
// ZERO px — the selector came out empty. With compact mode (reduced padding and arrow) 72 leaves plenty of room
// for the longest label, “PT-BR”, and it is still half of the 148 from before.
const WIDTH = 52;

// Language -> country whose flag represents it. It is not an automatic translation of the code: 'en' is GB and not
// US because the app is written in British English, and 'zh-Hans' is CN because Simplified Chinese is the
// mainland's.
const BANDERA: Record<Locale, string> = {
  en: 'GB', es: 'ES', de: 'DE', it: 'IT', ru: 'RU',
  ko: 'KR', fr: 'FR', 'zh-Hans': 'CN', pt: 'PT', 'pt-BR': 'BR',
  tr: 'TR', pl: 'PL', hu: 'HU',
};

function Bandera({ cc }: { cc: string }) {
  return <img src={`${import.meta.env.BASE_URL}flags/${cc}.png`} alt="" aria-hidden="true" draggable={false}
              style={{ width: 20, height: 14, objectFit: 'cover', flex: 'none',
                       border: '1px solid var(--line)', display: 'block' }} />;
}

// The twelve languages fit WHOLE: 12 x 34 px = 408. With the Picker's general cap (260) rows were
// left over and a vertical scrollbar came out in such a short list — exactly what was pointed out as
// foreign to the design. The general cap stays as it is: the long lists really need it (the years).
const ALTO_LISTA = 416;

export function LocalePicker({ value, onPick, label }: {
  value: Locale;
  onPick: (l: Locale) => void;
  /** Label for screen readers. AppShell sets it with the tutorial's already existing key. */
  label: string;
}) {
  return (
    <Picker
      label={label}
      dataTour="shell.lang"
      value={value}
      onChange={onPick}
      style={{ width: WIDTH, flex: '0 0 auto' }}
      compact
      maxListHeight={ALTO_LISTA}
      options={LOCALES.map((l) => ({ value: l, label: LOCALE_LABELS[l], icon: <Bandera cc={BANDERA[l]} /> }))}
    />
  );
}
