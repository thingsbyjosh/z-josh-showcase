// engine rejection code -> i18n key. cmd.market.accept and cmd.negotiate.acceptTable/sign/walk
// emit {ok:false, code} with the FIRST blocker that actually hit (in the same order the engine checks them); before, the
// "no" traveled silently and the screen just refetched. Unknown or missing code -> falls back to the same generic
// message as before, which is exactly the previous behavior.
export function rejectReasonKey(code?: string): string {
  switch (code) {
    case 'already': return 'mkt_reject_already';       // you already have a contract for that series that season
    case 'exclusive': return 'mkt_reject_exclusive';   // an exclusive contract covers the target season
    case 'afford': return 'mkt_reject_afford';         // the seat's buy-in is more than your credits
    case 'superlicense': return 'mkt_reject_superlicense';   // via Paddock: series gated without the license
    case 'window': return 'mkt_reject_window';         // the transfer market is closed
    case 'expired': return 'mkt_reject_expired';       // the offer no longer exists (expired/changed)
    case 'refused': return 'mkt_reject_refused';       // the engine said no with no identifiable blocker
    case 'closed': return 'neg_closed_generic';        // the negotiation thread is no longer open
    default: return 'neg_cmd_failed';
  }
}
