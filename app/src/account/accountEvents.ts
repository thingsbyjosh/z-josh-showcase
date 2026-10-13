// This showcase does not include account linking (see the licence and README): the real
// accountEvents.ts is a small pub/sub channel that tells the shell when the linked account's tier
// changed. With no real account module behind it here, that change never happens. This stub
// keeps the same exported shape; both functions are no-ops.
export type ModTier = 'free' | 'supporter' | 'plus';

export function onAccountTierChange(_cb: (tier: ModTier) => void): () => void {
  return () => {};
}

export function emitAccountTierChange(_tier: ModTier): void {
}
