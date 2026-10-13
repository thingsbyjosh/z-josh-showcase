// Weather photos from AMS2 itself (GUI/WeatherPhotos, a loose folder in the install), converted to
// WebP by tools/conv_wximg.py. They're the same images the game shows in its own selector, so the
// player recognizes the weather without reading the label.
//
// Shipped in the bundle (108KB for all 15) instead of via paths.Data like vehimages/trackmaps: they're small, so the
// selector works without depending on converted data being present on the machine. The codes are the ones from the
// canonical weather catalog (same as wxname_*); 9/10/11 (snow) and 18 aren't used in races.
import wx0 from '../assets/weather/wx_0.svg';
import wx1 from '../assets/weather/wx_1.svg';
import wx2 from '../assets/weather/wx_2.svg';
import wx3 from '../assets/weather/wx_3.svg';
import wx4 from '../assets/weather/wx_4.svg';
import wx5 from '../assets/weather/wx_5.svg';
import wx6 from '../assets/weather/wx_6.svg';
import wx7 from '../assets/weather/wx_7.svg';
import wx8 from '../assets/weather/wx_8.svg';
import wx12 from '../assets/weather/wx_12.svg';
import wx13 from '../assets/weather/wx_13.svg';
import wx14 from '../assets/weather/wx_14.svg';
import wx15 from '../assets/weather/wx_15.svg';
import wx16 from '../assets/weather/wx_16.svg';
import wx17 from '../assets/weather/wx_17.svg';

const WX_IMAGE: Record<string, string> = {
  '0': wx0, '1': wx1, '2': wx2, '3': wx3, '4': wx4,
  '5': wx5, '6': wx6, '7': wx7, '8': wx8,
  '12': wx12, '13': wx13, '14': wx14, '15': wx15, '16': wx16, '17': wx17,
};

/** Weather photo, or undefined if the code has no image (snow and other unused weather types). */
export function wxImage(code: number | string | undefined | null): string | undefined {
  return code === undefined || code === null ? undefined : WX_IMAGE[String(code)];
}
