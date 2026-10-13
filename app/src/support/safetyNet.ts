// UI safety net: catches global JS errors, unhandled promise rejections, and
// render errors (ErrorBoundary), persisting them to the support log via supportLog. Without this, a UI
// logic bug on an end user's machine was invisible: console/devtools output doesn't persist.
// Anti-spam: collapses consecutive repeats (an error in a render loop would otherwise fire every frame) and
// caps out per session, leaving a record that the cutoff happened.
import { supportLog } from './supportLog';
import type { ApiClientLike } from '../api/contracts';

const MAX_PER_SESSION = 30;

let getClient: () => ApiClientLike | null = () => null;
let installed = false;
let emitted = 0;
let lastLine = '';

/** The shell registers here how to get the live client: with the engine connected, the breadcrumb goes through cmd.log.note. */
export function setSafetyNetClient(get: () => ApiClientLike | null): void {
  getClient = get;
}

export function reportUiError(kind: string, message: string): void {
  const line = `ui error ${kind}: ${message}`;
  if (line === lastLine || emitted >= MAX_PER_SESSION) return;
  lastLine = line;
  emitted++;
  supportLog(getClient(), line);
  if (emitted === MAX_PER_SESSION) supportLog(getClient(), 'ui error: session cap reached, further errors are dropped');
}

function onWindowError(ev: ErrorEvent): void {
  if (!ev.message) return; // resource-loading errors fire 'error' with no message: they aren't logic bugs
  reportUiError('js', `${ev.message} @ ${ev.filename || '?'}:${ev.lineno || 0}`);
}

function onRejection(ev: Event): void {
  const reason = (ev as PromiseRejectionEvent).reason;
  const msg = reason instanceof Error ? (reason.stack?.split('\n').slice(0, 2).join(' | ') ?? reason.message) : String(reason);
  reportUiError('promise', msg);
}

export function installGlobalErrorHandlers(): void {
  if (installed) return;
  installed = true;
  window.addEventListener('error', onWindowError);
  window.addEventListener('unhandledrejection', onRejection);
}

/** Tests only: resets the module's state (cap, dedupe, handlers). */
export function __resetSafetyNet(): void {
  if (installed) {
    window.removeEventListener('error', onWindowError);
    window.removeEventListener('unhandledrejection', onRejection);
  }
  installed = false;
  emitted = 0;
  lastLine = '';
  getClient = () => null;
}
