// Liveries/Skins (a MAIN TAB in the shell; used to live under Settings > Content): a catalog of AMS2 skin RESOURCES
// (packs OR standalone liveries) read from the engine via cmd.skins.status. Ported from the design mockup's liveries screen
// (source of truth for the VISUALS) but WITHOUT its mock logic (downloads/MB/rating/tags/POPULAR/TOP/RECENT): in v1 there are no
// downloads, ratings, or tags -> the engine drives the functional side. The closest analog in the codebase:
// the Garage screen (a card grid with real car photos via useCarLiveries + dataDir). 100% READ-ONLY: INSTALL is
// a link to the resource's page on lantern_series_8801 (auto-download comes later); "detected" comes from scanning the installed
// overrides (an INSTALLED badge). Unit = resource: a 1-entry pack is presented as a standalone livery.
import { useEffect, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, SkinsStatus, SkinPack } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { useCarLiveries, useOverridePreviews } from '../ui/vehImages';
import { Display, Mono, Kicker, Chip } from '../ui/components';
import { classDisplayName } from '../ui/classNames';
import { accentText } from '../ui/colors';
import { getBridge, type ArchiveInfo } from '../shell/bridge';
import { supportLog, logImageError } from '../support/supportLog';
import { useFileDrop } from '../shell/useFileDrop';
import { readInstalledPacks, recordInstalledPack, forgetInstalledPack, type InstalledPack } from '../game/installedPacks';

const PAGE = 48;
// the extractor returns the CODE 'NO_7Z' (from the installer's own constant) when there's no 7-Zip anywhere
// — neither the bundled one nor a system install. It's the ONLY warning that gets translated: everything else is
// raw technical detail shown as-is, because that's what's actually useful for support.
const NO_7Z = 'NO_7Z';
const SEVEN_ZIP_URL = 'https://www.7-zip.org/';

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

function LiveryCard({ pack, dataDir, visited, onOpen }: { pack: SkinPack; dataDir?: string; visited: boolean; onOpen: (slug: string) => void }) {
  const { t } = useTranslation();
  // Real car photo from the first entry (same pipeline as Garage/pre-race). sample_livery_id is the NN VALUE
  // (not an index): if it's in the car's list of NNs we use it, otherwise we fall back to the first one available.
  const { nns, url } = useCarLiveries(dataDir, pack.sample_vehicle);
  const ov = useOverridePreviews(dataDir);   // "real skins": if the pack has an installed skin, show the real one in the catalog
  const nn = nns.includes(pack.sample_livery_id) ? pack.sample_livery_id : (nns[0] ?? null);
  const thumb = nn != null ? (ov.url(pack.sample_vehicle, nn) || url(nn)) : '';

  // image badge: primary class; if the resource covers several classes (e.g. Highgate = GT1+LMP1+LANTERN_MASTERS_7489)
  // add "+N" as a multiclass signal (the full breakdown of ALL classes lives in the body chips).
  const primaryCls = classDisplayName(pack.series[0]) || '—';
  const clsLabel = (pack.series.length > 1 ? `${primaryCls} +${pack.series.length - 1}` : primaryCls).toUpperCase();
  // subline: "{N liveries | standalone livery} · by {author}"
  const kind = pack.entries_total === 1 ? t('skins.singleLivery') : t('skins.liveries', { n: pack.entries_total });
  const sub = `${kind} · ${t('skins.byAuthor', { author: pack.author })}`;

  return (
    <div data-slug={pack.slug} className="panel" style={{ padding: 0, overflow: 'hidden', display: 'flex', flexDirection: 'column', minWidth: 0 }}>
      {/* preview (16:9) */}
      <div style={{ position: 'relative', aspectRatio: '16 / 9', flex: 'none', overflow: 'hidden', background: 'linear-gradient(150deg, color-mix(in oklab, var(--accent) 10%, var(--surface-2)), var(--surface-3) 70%)' }}>
        {thumb
          ? <img src={thumb} alt={pack.title} loading="lazy" draggable={false} onError={(e) => logImageError(null, e.currentTarget.src)} style={{ position: 'absolute', inset: '6% 4%', width: '92%', height: '88%', objectFit: 'contain', filter: 'drop-shadow(0 10px 14px rgba(0,0,0,.55))' }} />
          : (
            <div className="col center" style={{ position: 'absolute', inset: 0, gap: 6 }}>
              <span className="display" style={{ fontSize: 26, fontStyle: 'italic', color: 'var(--surface-3)' }}>◈</span>
              <Mono style={{ fontSize: 8.5, letterSpacing: '.18em', color: 'var(--text-3)' }}>{t('skins.noPreview')}</Mono>
            </div>
          )}
        {/* class badge (accent color) */}
        <Mono style={{ position: 'absolute', top: 8, left: 8, fontSize: 8.5, letterSpacing: '.12em', color: accentText('var(--accent)'), background: 'rgba(0,0,0,.55)', padding: '2px 7px', border: '1px solid color-mix(in oklab, var(--accent) 45%, transparent)' }}>{clsLabel}</Mono>
      </div>
      {/* body */}
      <div className="col" style={{ padding: '10px 12px 12px', gap: 7, flex: 1 }}>
        <div className="col" style={{ gap: 2, minWidth: 0 }}>
          <Display style={{ fontSize: 15.5, lineHeight: 1.05, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{pack.title}</Display>
          <Mono style={{ fontSize: 10, color: 'var(--text-3)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{sub}</Mono>
        </div>
        <div className="row gap8" style={{ alignItems: 'center', flexWrap: 'wrap' }}>
          {/* multiclass: one chip per class the resource affects (single-class already shows in the badge). */}
          {pack.series.length > 1 && pack.series.map((s) => (
            <Chip key={s} kind="accent">{(classDisplayName(s) || s).toUpperCase()}</Chip>
          ))}
          <Chip>{pack.season_year}</Chip>
        </div>
        <div className="row between" style={{ alignItems: 'center', marginTop: 'auto', paddingTop: 8, borderTop: '1px solid var(--line)' }}>
          {pack.detected
            ? (
              <span className="row gap6" style={{ alignItems: 'center', minWidth: 0 }}>
                <Mono style={{ fontSize: 9.5, color: 'var(--green)' }}>{pack.detected_entries}/{pack.entries_total}</Mono>
                <Chip kind="green" style={{ fontSize: 8.5 }}>{t('skins.installed')}</Chip>
              </span>
            )
            : <span />}
          <a
            href={pack.url}
            target="_blank"
            rel="noopener noreferrer"
            className="btn btn-sm"
            onClick={() => onOpen(pack.slug)}
            style={visited
              ? { color: 'var(--green)', borderColor: 'color-mix(in oklab, var(--green) 40%, transparent)', background: 'color-mix(in oklab, var(--green) 8%, transparent)' }
              : { color: 'var(--accent-text)', borderColor: 'color-mix(in oklab, var(--accent) 45%, transparent)' }}
          >
            {visited ? t('skins.opened') : t('skins.install')}
          </a>
        </div>
      </div>
    </div>
  );
}

export function Liveries({ client, onBack, dataDir, gameInstall }: { client: ApiClientLike; onBack?: () => void; dataDir?: string; gameInstall?: string }) {
  const { t } = useTranslation();
  const q = useQuery<SkinsStatus>(client, 'cmd.skins.status');
  const packs = useMemo(() => q.data?.packs ?? [], [q.data]);

  const [query, setQuery] = useState('');
  const [cls, setCls] = useState('all');
  const [shown, setShown] = useState(PAGE);
  const [visited, setVisited] = useState<Set<string>>(() => new Set());
  // installing by dragging a file. `drop` = the file that's been inspected and is awaiting confirmation;
  // `busy` = an in-progress step (extract+count / install) for the indicator; `installMsg` = the final result.
  const [drop, setDrop] = useState<{ path: string; info: ArchiveInfo } | null>(null);
  const [busy, setBusy] = useState<null | 'inspecting' | 'installing'>(null);
  const [installMsg, setInstallMsg] = useState<{ ok: boolean; text: string } | null>(null);

  // T349: manifest of packs installed from a file (the engine does not know them, see installedPacks.ts).
  // `manage` opens the management panel; `uninstallTarget` is the pack with the confirmation open (2 steps,
  // like the AMS2 restart: deleting game files is not done with a single click).
  const [installed, setInstalled] = useState<InstalledPack[]>([]);
  const [manage, setManage] = useState(false);
  const [uninstallTarget, setUninstallTarget] = useState<InstalledPack | null>(null);
  const [uninstallBusy, setUninstallBusy] = useState(false);
  const [uninstallMsg, setUninstallMsg] = useState<{ ok: boolean; text: string } | null>(null);

  useEffect(() => {
    if (!dataDir) return;
    void readInstalledPacks(dataDir).then(setInstalled).catch(() => setInstalled([]));
  }, [dataDir]);

  // missing 7-Zip isn't explained with a raw Windows path, it's explained in words (and with a
  // button to download it). `warnText` is for the modal, which already has a title; `failText` is for the standalone warning.
  const warnText = (msg: string) => (msg === NO_7Z ? t('skins.import.no7z') : msg);
  const failText = (msg: string) => (msg === NO_7Z ? t('skins.import.no7z') : t('skins.import.failed', { msg }));

  const onArchive = (paths: string[]) => {
    const path = paths[0];
    if (!path || busy) return;
    setInstallMsg(null);
    setDrop(null);
    setBusy('inspecting');   // opens the modal with a spinner while 7z extracts and we count the liveries
    void getBridge().then((b) => b.skins.inspectArchive(path))
      .then((info) => { setDrop({ path, info }); setBusy(null); })
      .catch((e) => { setBusy(null); setInstallMsg({ ok: false, text: failText(String(e)) }); });
  };
  const hovering = useFileDrop(onArchive, ['zip', 'rar', '7z']);

  const doInstall = () => {
    if (!drop || busy) return;
    setBusy('installing');
    const archivePath = drop.path;
    void getBridge()
      .then((b) => b.skins.installArchive(archivePath, gameInstall ?? ''))
      .then((r) => {
        setBusy(null);
        setDrop(null);
        setInstallMsg(r.ok
          ? { ok: true, text: t('skins.import.done') }
          : { ok: false, text: failText(r.warnings[0] ?? '') });
        if (r.ok) {
          void q.refetch();   // the REAL badge/preview show up after restarting AMS2 (detection happens at boot)
          // T349: without this registry there is no way to uninstall THIS pack later without risking
          // touching someone else's. Best-effort: if dataDir is missing or the save fails, the installation has
          // happened anyway — there is no point undoing it over this.
          if (dataDir) {
            const label = archivePath.split(/[\\/]/).pop() ?? archivePath;
            void recordInstalledPack(dataDir, label, r)
              .then((entry) => setInstalled((list) => [...list, entry]))
              .catch(() => { /* the pack stays installed anyway; it just will not be uninstallable from here */ });
          }
        }
        // installing skins WRITES into the game install and doesn't go through the engine -> a confirmation trail is mandatory
        supportLog(client, r.ok
          ? `skins: installed ${r.installedCars.length} cars, ${r.files} files`
          : `skins: install FAILED (${r.warnings[0] ?? 'no detail'})`);
      })
      .catch((e) => { setBusy(null); setInstallMsg({ ok: false, text: failText(String(e)) });
        supportLog(client, `skins: install FAILED (${String(e)})`); });   //
  };
  const carsCount = drop?.info.cars.length ?? 0;

  // T349: deletes the files THIS pack wrote and forgets the manifest entry. install_at never
  // kept a copy of what it overwrote, so this does not "restore" anything — the car falls back to AMS2's factory livery
  // (or to that of another pack installed BEFORE, only if that specific file was not overwritten). It is explained
  // in the confirmation (uninstallTarget), not here.
  const doUninstall = (pack: InstalledPack) => {
    if (uninstallBusy) return;
    setUninstallBusy(true);
    void getBridge()
      .then((b) => b.skins.uninstallFiles(gameInstall ?? '', pack.files))
      .then(async (r) => {
        if (dataDir) await forgetInstalledPack(dataDir, pack.id).catch(() => {});
        setInstalled((list) => list.filter((p) => p.id !== pack.id));
        setUninstallTarget(null);
        setUninstallBusy(false);
        setUninstallMsg(r.ok
          ? { ok: true, text: t('skins.manage.done') }
          : { ok: false, text: t('skins.manage.failed', { msg: r.warnings[0] ?? '' }) });
        void q.refetch();
        supportLog(client, r.ok
          ? `skins: uninstalled "${pack.label}" (removed=${r.removed} missing=${r.missing})`
          : `skins: uninstall FAILED for "${pack.label}" (${r.warnings[0] ?? 'no detail'})`);
      })
      .catch((e) => {
        setUninstallBusy(false);
        setUninstallMsg({ ok: false, text: t('skins.manage.failed', { msg: String(e) }) });
        supportLog(client, `skins: uninstall FAILED for "${pack.label}" (${String(e)})`);
      });
  };

  // distinct classes present in the series[] of ALL packs (in order of appearance).
  const clsOptions = useMemo(() => {
    const seen = new Set<string>();
    for (const p of packs) for (const s of p.series) if (s) seen.add(s);
    return Array.from(seen);
  }, [packs]);

  // filter (search over title/author/series + class chip) + v1 SORT: detected first, then title A-Z.
  // (The mockup sorted by POPULAR/TOP/RECENT; those are omitted because v1 has no downloads/rating/date.)
  const filtered = useMemo(() => {
    const needle = query.trim().toLowerCase();
    const arr = packs.filter((p) => {
      if (cls !== 'all' && !p.series.includes(cls)) return false;
      if (!needle) return true;
      const hay = [p.title, p.author, ...p.series].join(' ').toLowerCase();
      return hay.includes(needle);
    });
    return [...arr].sort((a, b) => {
      if (a.detected !== b.detected) return a.detected ? -1 : 1;
      return a.title.localeCompare(b.title);
    });
  }, [packs, query, cls]);

  useEffect(() => { setShown(PAGE); }, [query, cls]);

  if (q.loading) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || !q.data) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('skins.empty')}</Mono></Center>;

  const onOpen = (slug: string) => setVisited((v) => { const n = new Set(v); n.add(slug); return n; });
  const visible = filtered.slice(0, shown);

  const chipStyle = (on: boolean): React.CSSProperties => on
    ? { borderColor: 'var(--accent)', color: 'var(--text)', background: 'color-mix(in oklab, var(--accent) 14%, var(--surface))' }
    : { color: 'var(--text-3)' };

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column', minHeight: 0 }}>
      {/* header */}
      <div className="row between" style={{ padding: '18px 30px 0', alignItems: 'flex-end', flex: 'none' }}>
        <div className="col" style={{ gap: 4 }}>
          <Kicker style={{ color: 'var(--accent-text)' }}>{t('skins.kicker')}</Kicker>
          <Display as="h1" style={{ fontSize: 30, lineHeight: 1 }}>{t('skins.title')}</Display>
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('skins.sub')}</Mono>
        </div>
        <div className="row" style={{ gap: 8, alignItems: 'center' }}>
          {/* T349: only appears if there is something hand-installed to manage — nobody who only uses the
              curated catalog ever needs to see this button. */}
          {installed.length > 0 && (
            <button className="btn btn-sm btn-ghost" onClick={() => setManage(true)}>
              {t('skins.manage.title')} · {installed.length}
            </button>
          )}
          {onBack && <button className="btn btn-sm btn-ghost" onClick={onBack}>← {t('common.back')}</button>}
        </div>
      </div>

      {/* drop zone for installing a pack by dragging it (.zip/.rar/.7z). A LARGE, prominent
          zone (a flagship feature) — the real drop target is at the WINDOW level (useFileDrop); this is just the visual magnet. */}
      <div data-tour="liveries.drop" style={{ margin: '10px 30px 0', padding: '22px 20px', flex: 'none', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 14,
        border: `2px dashed ${hovering ? 'var(--accent)' : 'var(--line)'}`,
        background: hovering ? 'color-mix(in oklab, var(--accent) 10%, transparent)' : 'var(--surface)', transition: 'background .12s, border-color .12s' }}>
        <span className="display" style={{ fontSize: 28, lineHeight: 1, color: hovering ? 'var(--accent)' : 'var(--text-3)' }}>⤓</span>
        <div className="col" style={{ gap: 3, alignItems: 'center' }}>
          <span className="display" style={{ fontSize: 15, color: hovering ? 'var(--accent-text)' : 'var(--text-2)' }}>
            {hovering ? t('skins.import.dropActive') : t('skins.import.drop')}
          </span>
          <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('skins.import.dropHint')}</Mono>
        </div>
        {(installMsg || uninstallMsg) && <span className="grow" />}
        {installMsg && <Mono style={{ fontSize: 10.5, color: installMsg.ok ? 'var(--green)' : 'var(--amber)' }}>{installMsg.text}</Mono>}
        {!installMsg && uninstallMsg && <Mono style={{ fontSize: 10.5, color: uninstallMsg.ok ? 'var(--green)' : 'var(--amber)' }}>{uninstallMsg.text}</Mono>}
      </div>

      {/* filters */}
      <div className="row" style={{ padding: '14px 30px 12px', gap: 8, alignItems: 'center', flexWrap: 'wrap', flex: 'none', borderBottom: '1px solid var(--line)' }}>
        <input
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          placeholder={t('skins.search')}
          style={{ width: 280, padding: '7px 12px', fontSize: 12, fontFamily: 'var(--font-mono)', background: 'var(--surface-2)', border: '1px solid var(--line)', color: 'var(--text)', outline: 'none' }}
        />
        <span style={{ width: 1, height: 22, background: 'var(--line)', margin: '0 4px' }} />
        <button className="btn btn-sm" onClick={() => setCls('all')} style={chipStyle(cls === 'all')}>{t('skins.classAll')}</button>
        {clsOptions.map((k) => (
          <button key={k} className="btn btn-sm" onClick={() => setCls(k)} style={chipStyle(cls === k)}>{classDisplayName(k)}</button>
        ))}
        <span className="grow" />
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('skins.count', { shown: visible.length, total: filtered.length })}</Mono>
      </div>

      {/* grid */}
      <div className="scroll" style={{ flex: 1, minHeight: 0, padding: '16px 30px 30px' }} data-tour="liveries.grid">
        {filtered.length === 0 ? (
          <Center><Mono style={{ color: 'var(--text-3)' }}>{t('skins.empty')}</Mono></Center>
        ) : (
          <>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(236px, 1fr))', gap: 12 }}>
              {visible.map((p) => (
                <LiveryCard key={p.slug} pack={p} dataDir={dataDir} visited={visited.has(p.slug)} onOpen={onOpen} />
              ))}
            </div>
            {shown < filtered.length && (
              <div className="row center" style={{ marginTop: 18 }}>
                <button className="btn" onClick={() => setShown((s) => s + PAGE)}>
                  {t('skins.loadMore')} · {Math.min(PAGE, filtered.length - shown)} →
                </button>
              </div>
            )}
          </>
        )}
      </div>

      {/* install modal — spinner while analyzing/installing, then a preview + confirm */}
      {(busy || drop) && (
        <div onClick={() => { if (!busy) setDrop(null); }} style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.6)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 60 }}>
          <div className="panel" onClick={(e) => e.stopPropagation()} style={{ width: 460, maxHeight: '80vh', display: 'flex', flexDirection: 'column', padding: '18px 20px', gap: 12 }}>
            <Kicker style={{ color: 'var(--accent-text)' }}>{t('skins.import.title')}</Kicker>
            {busy ? (
              /* progress indicator: extract+count (inspecting) or install (installing) */
              <div className="col center" style={{ gap: 14, padding: '26px 0' }}>
                <div className="spinner" style={{ width: 34, height: 34, borderWidth: '3px' }} />
                <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>
                  {busy === 'inspecting' ? t('skins.import.analyzing') : t('skins.import.installingN', { n: carsCount })}
                </Mono>
              </div>
            ) : drop && !drop.info.ok ? (
              <Mono style={{ fontSize: 11, color: 'var(--amber)', lineHeight: 1.5 }}>{drop.info.warnings[0] ? warnText(drop.info.warnings[0]) : t('skins.import.invalid')}</Mono>
            ) : drop ? (
              <>
                <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('skins.import.cars', { n: drop.info.cars.length })}</Mono>
                <div className="scroll col" style={{ gap: 6, maxHeight: '46vh', minHeight: 0 }}>
                  {drop.info.cars.map((c) => (
                    <div key={c.slug} className="row between" style={{ padding: '6px 10px', border: '1px solid var(--line)', background: 'var(--surface)', alignItems: 'center' }}>
                      <span style={{ fontSize: 12.5, color: 'var(--text)' }}>{c.slug}</span>
                      <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{t('skins.import.liveriesN', { n: c.liveries.length })}</Mono>
                    </div>
                  ))}
                </div>
                {drop.info.hasCustomAi && <Mono style={{ fontSize: 9.5, color: 'var(--text-3)', lineHeight: 1.45 }}>{t('skins.import.aiNote')}</Mono>}
              </>
            ) : null}
            {!busy && drop && (
              <div className="row between" style={{ marginTop: 4 }}>
                <button className="btn btn-sm btn-ghost" onClick={() => setDrop(null)}>{t('skins.import.cancel')}</button>
                {drop.info.ok && (
                  <button className="btn btn-sm" onClick={doInstall}
                    style={{ color: 'var(--accent-text)', borderColor: 'color-mix(in oklab, var(--accent) 45%, transparent)' }}>
                    {t('skins.import.installBtn')}
                  </button>
                )}
                {/* the pack can't even be inspected without 7-Zip -> the only useful button here is to go get it. */}
                {drop.info.warnings[0] === NO_7Z && (
                  <button className="btn btn-sm" data-testid="get-7zip"
                    onClick={() => void getBridge().then((b) => b.opener.openUrl(SEVEN_ZIP_URL))}
                    style={{ color: 'var(--accent-text)', borderColor: 'color-mix(in oklab, var(--accent) 45%, transparent)' }}>
                    {t('skins.import.get7z')}
                  </button>
                )}
              </div>
            )}
          </div>
        </div>
      )}

      {/* T349: management of hand-installed packs — list + 2-step confirmation before deleting anything
          from the game install (same pattern as the AMS2 restart in the connection gate). */}
      {manage && (
        <div onClick={() => { if (!uninstallBusy) { setManage(false); setUninstallTarget(null); } }}
          style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.6)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 60 }}>
          <div className="panel" onClick={(e) => e.stopPropagation()} style={{ width: 460, maxHeight: '80vh', display: 'flex', flexDirection: 'column', padding: '18px 20px', gap: 12 }}>
            <Kicker style={{ color: 'var(--accent-text)' }}>{t('skins.manage.title')}</Kicker>
            {installed.length === 0 ? (
              <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('skins.manage.empty')}</Mono>
            ) : (
              <div className="scroll col" style={{ gap: 8, maxHeight: '56vh', minHeight: 0 }}>
                {installed.map((pack) => (
                  <div key={pack.id} className="col" style={{ gap: 6, padding: '10px 12px', border: '1px solid var(--line)', background: 'var(--surface)' }}>
                    <div className="row between" style={{ alignItems: 'flex-start', gap: 10 }}>
                      <div className="col" style={{ gap: 2, minWidth: 0 }}>
                        <span style={{ fontSize: 12.5, color: 'var(--text)', overflowWrap: 'anywhere' }}>{pack.label}</span>
                        <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>
                          {t('skins.manage.carsN', { n: pack.cars.length })} · {t('skins.manage.installedOn', { date: new Date(pack.installedAt).toLocaleDateString() })}
                        </Mono>
                      </div>
                      {uninstallTarget?.id !== pack.id && (
                        <button className="btn btn-sm btn-ghost" disabled={uninstallBusy}
                          onClick={() => setUninstallTarget(pack)}
                          style={{ color: 'var(--amber)', flex: 'none' }}>
                          {t('skins.manage.uninstall')}
                        </button>
                      )}
                    </div>
                    {/* confirmation IN place: explains the no-backup case before deleting anything */}
                    {uninstallTarget?.id === pack.id && (
                      <div className="col" style={{ gap: 8, paddingTop: 6, borderTop: '1px solid var(--line)' }}>
                        <Mono style={{ fontSize: 10.5, color: 'var(--text-2)', lineHeight: 1.5 }}>
                          {t('skins.manage.confirmBody')}
                        </Mono>
                        <div className="row" style={{ gap: 8 }}>
                          <button className="btn btn-sm btn-ghost" disabled={uninstallBusy} onClick={() => setUninstallTarget(null)}>
                            {t('skins.import.cancel')}
                          </button>
                          <button className="btn btn-sm" disabled={uninstallBusy} onClick={() => doUninstall(pack)}
                            style={{ color: 'var(--amber)', borderColor: 'color-mix(in oklab, var(--amber) 45%, transparent)' }}>
                            {uninstallBusy ? '…' : t('skins.manage.confirmBtn')}
                          </button>
                        </div>
                      </div>
                    )}
                  </div>
                ))}
              </div>
            )}
            <div className="row" style={{ marginTop: 4 }}>
              <button className="btn btn-sm btn-ghost" onClick={() => { setManage(false); setUninstallTarget(null); }}>
                {t('common.close')}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
