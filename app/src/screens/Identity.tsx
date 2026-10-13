// Identity / Create your driver: the career-creation GATE, ported from the engine's own identity screen (source of
// truth). It's the app's global gate: it renders ALONE (no topbar/tabs) whenever query.identity.needed==true.
// Captures first/last name/sex/nationality/DOB (local state) and commits via cmd.identity.submit (the only
// creation point: the engine seeds the seed/credits/calendar). The country catalog + flags are app-owned.
// The START button only enables once the gate is valid (first/last name >=2 chars, sex, nationality, full DOB); the
// "unusual" age warning does NOT block. Labels via t() (identity.* keys + the engine's mode_career key).
import { useEffect, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, IdentityPayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Btn, Display, Kicker, Mono, inputStyle } from '../ui/components';
import { BrandLogo } from '../ui/BrandLogo';
import { Picker } from '../ui/Picker';
import { LocalePicker } from '../shell/LocalePicker';
import { loadConfig, saveLocale } from '../config/appConfig';
import { monthNames, normalizeLocale } from '../i18n';
import { COUNTRIES, isoToFlag } from '../ui/countries';
import { useScreenCrumb } from '../support/useScreenCrumb';

const SEX = ['male', 'female', 'other'] as const;
const DAYS = Array.from({ length: 31 }, (_, i) => i + 1);
// the 1965 floor excluded real veteran drivers; it now opens up to 1930.
const YEARS = Array.from({ length: 2010 - 1930 + 1 }, (_, i) => 2010 - i); // 2010 -> 1930 descending

// flag as a real PNG (app/public/flags/<code>.png, extracted from the old overlay's
// assets). Windows doesn't render flag emoji (isoToFlag), so the emoji is kept
// only as a last resort via onError, next to the box showing the country code.
function Flag({ code, size = 22 }: { code: string; size?: number }) {
  const [broken, setBroken] = useState(false);
  const cc = code.toUpperCase();
  if (!broken) {
    return <img src={`${import.meta.env.BASE_URL}flags/${cc}.png`} alt={cc} onError={() => setBroken(true)}
      style={{ width: Math.round(size * 1.4), height: size, objectFit: 'cover', borderRadius: 2, border: '1px solid var(--line)', flex: 'none' }} />;
  }
  const f = isoToFlag(code);
  if (f) return <span style={{ fontSize: size, lineHeight: 1 }}>{f}</span>;
  return (
    <span style={{ fontSize: 9, fontWeight: 700, color: 'var(--text-3)', background: 'var(--surface-3)', borderRadius: 3, padding: '2px 4px' }}>
      {code}
    </span>
  );
}

function Label({ children }: { children: React.ReactNode }) {
  return <Mono style={{ fontSize: 10, letterSpacing: '.1em', color: 'var(--text-3)', textTransform: 'uppercase', display: 'block', marginBottom: 6 }}>{children}</Mono>;
}

// edit=true reuses this SAME screen as a post-creation EDITOR (not just the sign-up gate). The submit
// (cmd.identity.submit) is NON-DESTRUCTIVE on an already-established career: the engine only re-seeds credits/calendar
// on a FRESH profile (career_seed==0 && credits==0); with a career in progress it only updates name/sex/nationality/DOB.
export function Identity({ client, onDone, edit = false, onCancel }: { client: ApiClientLike; onDone: () => void; edit?: boolean; onCancel?: () => void }) {
  const { t, i18n } = useTranslation();
  useScreenCrumb(client, edit ? 'identity editor' : 'identity creation');   // audit note, 07/20
  const q = useQuery<IdentityPayload>(client, 'query.identity');
  const d = q.data;
  // NOTE: `lang` is NOT the interface language, it is the language of the COUNTRY NAMES: the
  // COUNTRIES table only has `en` and `es`. A German sees the countries in English, which is the fallback of
  // the whole system. The screen's language is handled by the dropdown at the top right.
  const lang = i18n.language.startsWith('es') ? 'es' : 'en';

  const [first, setFirst] = useState('');
  const [last, setLast] = useState('');
  const [sex, setSex] = useState('');
  const [nat, setNat] = useState('');
  const [day, setDay] = useState(0);
  const [month, setMonth] = useState(0);
  const [year, setYear] = useState(0);
  const [search, setSearch] = useState('');
  const [open, setOpen] = useState(false);
  const [prefilled, setPrefilled] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);   // hub why it wasn't saved (a driver's name)

  // One-time prefill: raw fields from the save if they exist, otherwise the AMS2 profile name (split on the first space).
  useEffect(() => {
    if (!d || prefilled) return;
    if (d.first) setFirst(d.first);
    else if (d.amsName) setFirst(d.amsName.split(' ')[0] ?? '');
    if (d.last) setLast(d.last);
    else if (d.amsName && d.amsName.includes(' ')) setLast(d.amsName.slice(d.amsName.indexOf(' ') + 1));
    if (d.sex) setSex(d.sex);
    if (d.nationality) setNat(d.nationality);
    if (d.birthDay) setDay(d.birthDay);
    if (d.birthMonth) setMonth(d.birthMonth);
    if (d.birthYear) setYear(d.birthYear);
    setPrefilled(true);
  }, [d, prefilled]);

  const months = monthNames(t);
  const startYear = d?.startYear ?? 2025;
  const age = year ? Math.max(startYear, year) - year : 0;
  const ageWarn = year !== 0 && (age < 14 || age > 70);
  const valid = first.trim().length >= 2 && last.trim().length >= 2 && !!sex && !!nat && !!day && !!month && !!year;

  const filtered = useMemo(() => {
    const s = search.trim().toLowerCase();
    if (!s) return COUNTRIES;
    return COUNTRIES.filter((c) => c.code.toLowerCase().includes(s) || c.en.toLowerCase().includes(s) || c.es.toLowerCase().includes(s));
  }, [search]);
  const natC = COUNTRIES.find((c) => c.code === nat);

  const submit = () => {
    if (!valid || busy) return;
    setBusy(true);
    setErr(null);
    void client.cmd('cmd.identity.submit', { first: first.trim(), last: last.trim(), sex, nationality: nat, year, month, day })
      .then(() => onDone())
      // hub the engine rejects a world driver's name (standings tables accumulate by name: naming
      // yourself the same as a rival would merge you with him). The error was SWALLOWED here -- the
      // form went silent without saying why, the same sin as the dead-click bug.
      .catch((e: unknown) => {
        const code = (e as { code?: string } | null)?.code;
        setErr(code === 'name_taken'
          ? t('identity.nameTaken', { defaultValue: 'That name belongs to a driver on the grid. Pick another one.' })
          : t('identity.saveFailed', { defaultValue: 'Could not save your driver card. Please try again.' }));
        setBusy(false);
      });
  };

  const initials = ((first[0] ?? '') + (last[0] ?? '')).toUpperCase() || '?';

  return (
    <div style={{ position: 'relative', height: '100%', display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 24, overflow: 'auto' }}>
      <div className="app-bg" />
      {/* It was a TWO-position switch (`lang === 'en' ? 'es' : 'en'`) that survived the move
          from two languages to ten: a German who pressed it ended up in Spanish, and there was no way to
          reach the other seven from this screen. */}
      <div style={{ position: 'absolute', top: 20, right: 24, zIndex: 2 }}>
        <LocalePicker value={normalizeLocale(i18n.language)}
                      onPick={(l) => {
                        void i18n.changeLanguage(l);
                        // 01/10: it is also saved (see saveLocale); this screen does not receive the config,
                        // so it is reread from disk. Best-effort: the language changes anyway in the session.
                        void loadConfig().then((c) => (c ? saveLocale(c, l) : undefined)).catch(() => {});
                      }}
                      label={t('tour.shell.lang.t')} />
      </div>

      <div className="panel" style={{ width: 'min(560px, 100%)', padding: '28px 32px', zIndex: 1, display: 'flex', flexDirection: 'column', gap: 16 }}>
        {/* Brand header */}
        <div className="row gap8" style={{ alignItems: 'center' }}>
          <span style={{ width: 4, height: 22, background: 'var(--accent)', borderRadius: 1 }} />
          <BrandLogo height={20} />
          <Kicker style={{ marginLeft: 2 }}>{t('mode_career')}</Kicker>
        </div>
        <Display as="h1" style={{ fontSize: 30 }}>{edit ? t('identity.editTitle') : t('identity.title')}</Display>
        <Mono style={{ fontSize: 12, color: 'var(--text-3)', lineHeight: 1.5, marginTop: -8 }}>{edit ? t('identity.editIntro') : t('identity.intro')}</Mono>

        {/* First + last name */}
        <div className="row gap12">
          <div style={{ flex: 1 }}>
            <Label>{t('identity.firstName')}</Label>
            <input style={inputStyle} value={first} maxLength={40} placeholder={t('identity.firstHint')} onChange={(e) => setFirst(e.target.value)} />
          </div>
          <div style={{ flex: 1 }}>
            <Label>{t('identity.surname')}</Label>
            <input style={inputStyle} value={last} maxLength={40} placeholder={t('identity.surnameHint')} onChange={(e) => setLast(e.target.value)} />
          </div>
        </div>

        {/* Sex */}
        <div>
          <Label>{t('identity.sex')}</Label>
          <div className="row gap8">
            {SEX.map((s) => (
              <button key={s} className="focusable" onClick={() => setSex(s)} style={{
                flex: 1, padding: '9px 0', borderRadius: 3, cursor: 'pointer', fontSize: 13,
                background: sex === s ? 'var(--accent)' : 'var(--surface)',
                color: sex === s ? '#fff' : 'var(--text-2)',
                border: sex === s ? '1px solid var(--accent)' : '1px solid var(--line)',
              }}>{t(`identity.${s}`)}</button>
            ))}
          </div>
        </div>

        {/* Nationality */}
        <div style={{ position: 'relative' }}>
          <Label>{t('identity.nationality')}</Label>
          <div className="row gap8" style={{ alignItems: 'center' }}>
            <input
              style={inputStyle}
              value={open ? search : natC ? natC[lang] : ''}
              placeholder={t('identity.searchCountry')}
              onFocus={() => { setOpen(true); setSearch(''); }}
              onChange={(e) => { setSearch(e.target.value); setOpen(true); }}
              onBlur={() => setTimeout(() => setOpen(false), 120)}
            />
            {nat && <Flag code={nat} />}
          </div>
          {open && (
            <div className="scroll" style={{ position: 'absolute', top: '100%', left: 0, right: 0, zIndex: 5, marginTop: 4, maxHeight: 220, overflowY: 'auto', background: 'var(--surface-2)', border: '1px solid var(--line)', borderRadius: 4 }}>
              {filtered.length === 0 && <Mono style={{ fontSize: 12, color: 'var(--text-3)', padding: '10px 12px', display: 'block' }}>{t('identity.noCountry')}</Mono>}
              {filtered.map((c) => (
                <button key={c.code} onMouseDown={() => { setNat(c.code); setOpen(false); }} style={{
                  display: 'flex', width: '100%', alignItems: 'center', gap: 8, padding: '8px 12px', cursor: 'pointer',
                  background: c.code === nat ? 'color-mix(in oklab, var(--accent) 14%, transparent)' : 'transparent',
                  border: 'none', color: 'var(--text)', fontSize: 13, textAlign: 'left',
                }}>
                  <Flag code={c.code} size={18} />
                  <span style={{ flex: 1 }}>{c[lang]}</span>
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{c.code}</Mono>
                </button>
              ))}
            </div>
          )}
        </div>

        {/* Date of birth */}
        <div>
          <Label>{t('identity.dob')}</Label>
          <div className="row gap8">
            {/* Custom dropdowns, NOT native <select> elements: Windows opens a select's list in a
                separate window, outside the area the headset panels mirror, so in VR it's neither visible nor
                openable with a synthetic click. Reported on this very date-of-birth field. */}
            <Picker label={t('identity.day')} style={{ flex: '0 0 28%' }} value={day} onChange={setDay}
              options={[{ value: 0, label: t('identity.day') }, ...DAYS.map((n) => ({ value: n, label: String(n) }))]} />
            <Picker label={t('identity.month')} style={{ flex: '0 0 40%' }} value={month} onChange={setMonth}
              options={[{ value: 0, label: t('identity.month') }, ...months.map((m, i) => ({ value: i + 1, label: m }))]} />
            <Picker label={t('identity.year')} style={{ flex: 1 }} value={year} onChange={setYear}
              options={[{ value: 0, label: t('identity.year') }, ...YEARS.map((y) => ({ value: y, label: String(y) }))]} />
          </div>
          {year !== 0 && (
            <Mono style={{ fontSize: 11, color: ageWarn ? 'var(--amber)' : 'var(--text-3)', marginTop: 6, display: 'block' }}>
              {ageWarn ? t('identity.unusualAge', { n: age }) : t('identity.ageYears', { n: age })}
            </Mono>
          )}
        </div>

        {/* Footer: preview + START */}
        <div className="row between" style={{ alignItems: 'center', marginTop: 4, paddingTop: 14, borderTop: '1px solid var(--line)' }}>
          <div className="row gap8" style={{ alignItems: 'center' }}>
            <span className="mail-avatar" style={{ width: 44, height: 44, fontSize: 18, borderRadius: 4, background: 'color-mix(in oklab, var(--accent) 20%, var(--surface-3))', color: 'var(--text)' }}>{initials}</span>
            <div className="col" style={{ gap: 2 }}>
              <span style={{ fontWeight: 700, fontSize: 15 }}>
                <span style={{ color: first ? 'var(--text)' : 'var(--text-3)' }}>{first || t('identity.firstName')}</span>{' '}
                <span style={{ color: last ? 'var(--text)' : 'var(--text-3)' }}>{last || t('identity.surname')}</span>
              </span>
              {nat && <span className="row gap4" style={{ alignItems: 'center' }}><Flag code={nat} size={14} /><Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{natC ? natC[lang] : nat}</Mono></span>}
            </div>
          </div>
          <div className="row gap8">
            {edit && onCancel && <Btn variant="ghost" size="lg" disabled={busy} onClick={onCancel}>{t('common.cancel')}</Btn>}
            <Btn variant="primary" size="lg" disabled={!valid || busy} onClick={submit}>{edit ? t('identity.save') : `${t('identity.start')} →`}</Btn>
          </div>
        </div>
        {/* hub the reason for the rejection, next to the button that did nothing */}
        {err && (
          <Mono style={{ fontSize: 11.5, color: 'var(--amber)', lineHeight: 1.5, marginTop: -4 }}>{err}</Mono>
        )}
      </div>
    </div>
  );
}
