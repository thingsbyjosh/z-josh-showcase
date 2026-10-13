// Profile photo: piece shared between the Hub (Home.tsx, read-only) and the Profile (Profile.tsx, with
// the picker). If there is no saved photo (or we are in the browser's mock bridge, which gives no access to
// local files), it falls back to initials — the "default avatar" as always, breaking nothing.
import { useEffect, useState } from 'react';
import { getBridge } from '../shell/bridge';
import { avatarFilePath } from './avatarStore';

export function PlayerAvatar({ initials, size, radius = 4, fontSize, version = 0, style }: {
  initials: string;
  size: number;
  radius?: number;
  fontSize?: number;
  /** Goes up after saving/deleting to force the reload: the file name (avatar.png) never changes. */
  version?: number;
  style?: React.CSSProperties;
}) {
  const [url, setUrl] = useState('');
  useEffect(() => {
    let alive = true;
    // the root is %LOCALAPPDATA% (config/appConfig.ts), not dataDir (the GAME's userdata): the photo is
    // the app's identity, not the career's, so it is resolved here and does not travel as a prop.
    void getBridge().then(async (b) => {
      const lad = await b.paths.localAppData();
      const path = avatarFilePath(lad);
      const has = await b.fs.exists(path).catch(() => false);
      if (!alive) return;
      const base = has ? b.fs.assetUrl(path) : '';
      setUrl(base ? `${base}${base.includes('?') ? '&' : '?'}v=${version}` : '');
    }).catch(() => { if (alive) setUrl(''); });
    return () => { alive = false; };
  }, [version]);

  const box: React.CSSProperties = { width: size, height: size, flex: 'none', overflow: 'hidden', ...style };
  if (url) {
    return <img src={url} alt="" className="mail-avatar" style={{ ...box, objectFit: 'cover', borderRadius: radius }} />;
  }
  return (
    <span className="mail-avatar" style={{ ...box, borderRadius: radius, fontSize: fontSize ?? Math.round(size * 0.42) }}>
      {initials}
    </span>
  );
}
