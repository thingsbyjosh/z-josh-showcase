// This showcase does not include entitlements decoding (see the licence and README): the real
// token.ts reads this project's own entitlements JWT, whose format is not part of this
// repository. This stub keeps the same exported shape; decoding always reports "no token".
export type ModTier = 'free' | 'supporter' | 'plus';

export interface TokenPayload { sub: string; tier: ModTier; iat: number; exp: number }

export function decodeTokenPayload(_token: string): TokenPayload | null {
  return null;
}

export function tierFromToken(_token: string | null, _nowSec: number): ModTier {
  return 'free';
}
