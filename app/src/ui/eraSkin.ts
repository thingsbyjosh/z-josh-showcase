// Era skin of a card (04/10, Josh): a season before 2000 is recognised by the MATERIAL of its
// card. The class is consumed by design-system.css (.era-60 ... .era-90), the Memories rail and the Calendar.
// '' = modern series or no year: the card stays as it always was.
export type EraSkin = 'era-60' | 'era-70' | 'era-80' | 'era-90';

export function eraSkin(year: number | undefined | null): EraSkin | '' {
  if (!year || year < 1900 || year >= 2000) return '';
  if (year < 1970) return 'era-60';   // printed programme (anything before the 60s shares paper)
  if (year < 1980) return 'era-70';   // paddock sticker
  if (year < 1990) return 'era-80';   // teletext
  return 'era-90';                    // TV caption
}
