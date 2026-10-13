// This showcase does not include the desktop shell bridge (see the licence and README): the real
// tauriBridge.ts wires every ShellBridge call to the desktop app's own native commands, which are
// not part of this repository. The published app runs in a browser, where shell/bridge.ts never
// loads this module; if anything ever does, it gets the same demo bridge a browser gets.
import type { ShellBridge } from './bridge';
import { createMockBridge } from './mockBridge';

export const tauriBridge: ShellBridge = createMockBridge();
