// This showcase does not include account persistence (see the licence and README): the real
// accountStore.ts saves a Patreon-linked account (JWT + stable device id) to disk through the
// desktop bridge, which is not part of this repository. This stub keeps the same exported shape;
// every function is a safe no-op that never touches disk and always reports "no account".
export interface AccountData { schemaVersion: 1; token: string | null; deviceId: string }

export function accountFilePath(_localAppData: string): string {
  return '';
}

export async function loadAccount(): Promise<AccountData | null> {
  return null;
}

export async function saveAccount(_a: AccountData): Promise<void> {
}

export async function ensureAccount(): Promise<AccountData> {
  return { schemaVersion: 1, token: null, deviceId: '' };
}

export async function machineHash(): Promise<string> {
  return '';
}
