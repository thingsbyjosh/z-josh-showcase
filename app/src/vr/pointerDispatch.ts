// This showcase does not include VR pointer dispatch (see the licence and README): the real
// pointerDispatch.ts turns a normalised VR pointer event into synthetic DOM mouse events -- the
// mechanism that lets a VR controller or headset gaze click through this app without ever taking
// OS-level input focus. There is no VR runtime in this repository's scope. This stub keeps the
// same exported shape; dispatching is a no-op.
export type VrPointerKind = 'move' | 'down' | 'up' | 'scroll';
export interface VrPointerEvent {
  x: number; y: number; kind: VrPointerKind; dy?: number; src?: 'gaze' | 'laser';
}

export function dispatchPointer(_ev: VrPointerEvent): void {
}
