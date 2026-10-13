// "opened <screen>" breadcrumb to the support log, once per mount. Complements the
// per-tab breadcrumb from goTab: FLOW screens (pre-race, negotiation, rollover...) mount
// via the shell's internal state, not a tab, and were invisible in the user's session trail.
import { useEffect } from 'react';
import { supportLog } from './supportLog';
import type { ApiClientLike } from '../api/contracts';

export function useScreenCrumb(client: ApiClientLike | null, name: string, detail?: string): void {
  useEffect(() => {
    supportLog(client, detail ? `open ${name}: ${detail}` : `open ${name}`);
    // Only ON MOUNT: re-renders don't duplicate the breadcrumb (a changing client/detail doesn't re-fire it).
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
}
