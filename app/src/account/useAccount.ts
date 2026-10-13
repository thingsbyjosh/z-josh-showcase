// This showcase does not include account linking (see the licence and README): the real
// useAccount.ts drives Patreon OAuth (a loopback listener, silent token refresh, disconnect)
// against this project's own backend, none of which is part of this repository. This hook keeps
// the same exported shape and returns a stable, disconnected state; every action is a no-op.
export type AccountStatus = 'none' | 'connecting' | 'connected' | 'expired' | 'error';

export interface UseAccount {
  status: AccountStatus;
  tier: 'free' | 'supporter' | 'plus';
  error: string | null;
  connect(): Promise<void>;
  cancel(): Promise<void>;
  disconnect(): Promise<void>;
  refresh(): Promise<void>;
}

export function useAccount(): UseAccount {
  return {
    status: 'none',
    tier: 'free',
    error: null,
    connect: async () => {},
    cancel: async () => {},
    disconnect: async () => {},
    refresh: async () => {},
  };
}
