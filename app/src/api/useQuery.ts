import { useCallback, useEffect, useRef, useState } from 'react';
import type { ApiClientLike } from './contracts';

export interface QueryState<T> {
  data: T | null;
  error: string | null;
  loading: boolean;
  refetch: () => void;
  // how many good responses have arrived. With stale-while-revalidate `loading` does not turn on when re-requesting, so
  // it is the only way to know that the response of a refetch HAS arrived (the clock popup waits for it after a failure).
  version: number;
}

/**
 * Standard screen pattern: fetch on mount, re-fetch on reconnect,
 * loading/error/data states. Every screen replicates this hook.
 */
export function useQuery<T>(client: ApiClientLike, method: string, params?: object): QueryState<T> {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [version, setVersion] = useState(0);
  const paramsKey = JSON.stringify(params ?? null);
  const alive = useRef(true);
  // refetch WITHOUT flicker. `loading` used to flip to true on EVERY re-fetch, and since every screen
  // does `if (q.loading) return <loading/>`, any toggle that triggered a refresh would unmount the entire screen
  // and remount it: the scroll container became a NEW node, i.e. scrollTop 0, kicking you back to the
  // top of the list. It showed up when toggling weather options in Settings (check one, jump to the top), but it happened to
  // every toggle on every screen.
  // Now "loading" is only announced when there's NOTHING to show yet, or when the query changes SUBJECT
  // (different params = different data: there it does need to clear, or you'd briefly see the previous car's card).
  // A re-fetch of the same subject keeps what's rendered and swaps it in when it arrives. It's stale-while-revalidate.
  const dataRef = useRef<T | null>(null);
  const lastKey = useRef<string | null>(null);

  const run = useCallback(() => {
    const subjectChanged = lastKey.current !== null && lastKey.current !== paramsKey;
    lastKey.current = paramsKey;
    if (subjectChanged) { dataRef.current = null; setData(null); }
    if (subjectChanged || dataRef.current === null) setLoading(true);
    setError(null);
    client
      .query<T>(method, params ? (JSON.parse(paramsKey) as object) : undefined)
      .then((d) => {
        if (!alive.current) return;
        dataRef.current = d;
        setData(d);
        setVersion((v) => v + 1);
        setLoading(false);
      })
      .catch((e: unknown) => {
        if (!alive.current) return;
        setError(e instanceof Error ? e.message : String(e));
        setLoading(false);
      });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [client, method, paramsKey]);

  useEffect(() => {
    alive.current = true;
    run();
    // automatic re-fetch when the connection comes back
    const off = client.on('status', (s) => {
      if (s === 'connected') run();
    });
    return () => {
      alive.current = false;
      off();
    };
  }, [client, run]);

  return { data, error, loading, refetch: run, version };
}
