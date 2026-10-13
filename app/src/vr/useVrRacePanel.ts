// This showcase does not include the VR launch panel (see the licence and README): the real
// useVrRacePanel.ts drives the headset panel during a race launch by polling a live
// engine, which is not part of this repository. This hook keeps the same exported shape and
// always reports "nothing to show".
export interface VrRaceLaunch {
  stage: string;
  failN: number;
  failGroups: string;
  shmOff: boolean;
}

export type VrRaceFlow = null | 'prerace' | 'launched' | 'results' | 'shop';

export function useVrRacePanel(
  _client: unknown,
  _raceFlow: VrRaceFlow,
  _opts: { vr: boolean; simRace: boolean },
): VrRaceLaunch | null {
  return null;
}
