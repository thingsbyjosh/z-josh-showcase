import {
  API_VERSION,
  DEFAULT_API_URL,
  type ApiMessage,
  type ApiRequest,
  type ApiRequestKind,
  type ApiStatus,
} from './types';

export interface ClientOptions {
  backoffMinMs?: number;
  backoffMaxMs?: number;
  requestTimeoutMs?: number;
  /** Local auth token (optional). If provided, the client issues query.auth on connect and
   *  holds back all other requests (except query.api_info) until authenticated. */
  token?: string;
}

// Review 0.7.0 (finding 1): the clock simulates day by day on the game thread, and a forecast or a long advance
// (months, a year) goes past the default 10 s with a large world or in VR. With the global cap, the app said
// “it failed” while the engine kept advancing, and retrying advanced again. These two have their own cap: long
// so as not to lie, but finite so a hung engine does not leave the popup waiting forever.
export const METHOD_TIMEOUT_MS: Readonly<Record<string, number>> = {
  'query.time.preview': 60_000,
  'cmd.time.advance': 120_000,
};

interface Pending {
  resolve: (payload: unknown) => void;
  reject: (err: unknown) => void;
  timer: ReturnType<typeof setTimeout>;
  kind: ApiRequestKind;   // tracks cmd.* calls lost when the connection drops
  method: string;
}

/** API or transport error; `code` distinguishes between the two. */
export class ApiRequestError extends Error {
  constructor(
    public readonly code: string,
    message: string,
  ) {
    super(message);
    this.name = 'ApiRequestError';
  }
}

export class CareerApiClient {
  status: ApiStatus = 'disconnected';

  /** Tracks TRANSPORT failures for cmd.* calls (timeout, no connection, dropped connection) and
   *  corrupt messages. The engine only logs what actually REACHES it; without this, a lost cmd was invisible in every
   *  log. Only cmd (polling queries would spam this) and never cmd.log.note (to avoid recursion). */
  onIncident?: (line: string) => void;

  private sock: WebSocket | null = null;
  private pending = new Map<string, Pending>();
  private listeners = new Map<string, Set<(payload: unknown) => void>>();
  private backoffMs: number;
  private closed = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

  private readonly backoffMin: number;
  private readonly backoffMax: number;
  private readonly requestTimeout: number;
  private readonly authToken: string;
  private authed = false;
  private authFailed = false;
  private authQueue: Array<{ run: () => void; reject: (e: unknown) => void }> = [];

  constructor(
    private readonly url: string = DEFAULT_API_URL,
    opts: ClientOptions = {},
  ) {
    this.backoffMin = opts.backoffMinMs ?? 1000;
    this.backoffMax = opts.backoffMaxMs ?? 5000;
    this.requestTimeout = opts.requestTimeoutMs ?? 10000;
    this.authToken = opts.token ?? '';
    this.backoffMs = this.backoffMin;
  }

  connect(): void {
    if (this.closed || this.sock) return;
    this.setStatus('connecting');
    const sock = new WebSocket(this.url);
    this.sock = sock;

    sock.onopen = () => {
      this.backoffMs = this.backoffMin;
      // VR fix: 'connected' no longer means "socket open" but "ENGINE RESPONDING". The engine's WS
      // server starts as soon as the engine loads, but in VR its pump can take ~15s longer to come up (the game is still loading): the
      // screens were probing as soon as the socket opened, timing out, and an expired query.auth left
      // authFailed STUCK -> app showed "connected" with every tab empty, forever.
      // Now: poll api_info until the engine's FIRST response -> auth -> only then 'connected'
      // (and useQuery's on-connected re-fetch fires every screen at the right moment).
      this.probeEngine();
    };
    sock.onmessage = (ev) => this.onMessage(String(ev.data));
    sock.onclose = () => this.onDrop();
    sock.onerror = () => {
      /* onclose always arrives after; state is managed there */
    };
  }

  close(): void {
    this.closed = true;
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    if (this.probeTimer) { clearTimeout(this.probeTimer); this.probeTimer = null; }
    this.sock?.close();
    this.sock = null;
    this.failAllPending('connection_closed');
    this.setStatus('disconnected');
  }

  /** Poll query.api_info until the engine ANSWERS. An APPLICATION response (a result, or an error like
   *  bad_method) also proves the pump is alive; only a timeout keeps polling. Socket drops
   *  are handled by onDrop (which reconnects and polls again). */
  private probeTimer: ReturnType<typeof setTimeout> | null = null;

  private probeEngine(): void {
    if (this.closed || !this.sock || this.sock.readyState !== WebSocket.OPEN) return;
    this.rawSend('query', 'query.api_info', undefined, { preConnect: true, timeoutMs: 2000 })
      .then(() => this.onEngineAlive())
      .catch((e: unknown) => {
        const code = e instanceof ApiRequestError ? e.code : '';
        if (code === 'timeout') { this.probeTimer = setTimeout(() => this.probeEngine(), 500); return; }
        if (code === 'not_connected' || code === 'connection_lost' || code === 'connection_closed') return;
        this.onEngineAlive();   // an application-level response means the engine is alive
      });
  }

  private onEngineAlive(): void {
    if (this.closed || !this.sock) return;
    if (this.authToken) this.doAuth();
    else this.setStatus('connected');
  }

  query<T>(method: string, params?: object): Promise<T> {
    return this.send<T>('query', method, params);
  }

  cmd<T>(method: string, params?: object): Promise<T> {
    // 'cmd_done' (a LOCAL event, not from the engine): fires for every COMPLETED cmd so the shell can refresh
    // query.career_state right away — a payment shows up in the topbar without waiting for the 4s poll.
    // cmd.log.note is excluded (it can't move money; same exclusion as incident), and a rejected cmd
    // doesn't fire this: nothing changed.
    return this.send<T>('cmd', method, params).then((r) => {
      if (method !== 'cmd.log.note') this.emit('cmd_done', method);
      return r;
    });
  }

  /** Subscribe to 'status' or to 'event.*' push events. Returns the unsubscribe function. */
  on(event: string, cb: (payload: unknown) => void): () => void {
    let set = this.listeners.get(event);
    if (!set) {
      set = new Set();
      this.listeners.set(event, set);
    }
    set.add(cb);
    return () => set.delete(cb);
  }

  private incident(kind: ApiRequestKind, method: string, line: string): void {
    if (kind !== 'cmd' || method === 'cmd.log.note') return;
    this.onIncident?.(line);
  }

  private send<T>(kind: ApiRequestKind, method: string, params?: object): Promise<T> {
    if (this.status !== 'connected' || !this.sock) {
      this.incident(kind, method, `cmd ${method} while disconnected (never reached the engine)`);
      return Promise.reject(new ApiRequestError('not_connected', `cannot ${kind} ${method}: not connected`));
    }
    // Auth hold: with a token but not yet authenticated, hold everything except auth itself and api_info.
    if (this.authToken && !this.authed && method !== 'query.auth' && method !== 'query.api_info') {
      if (this.authFailed) {
        return Promise.reject(new ApiRequestError('unauthorized', `cannot ${kind} ${method}: auth failed`));
      }
      return new Promise<T>((resolve, reject) => {
        this.authQueue.push({
          run: () => this.rawSend<T>(kind, method, params).then(resolve, reject),
          reject,
        });
      });
    }
    return this.rawSend<T>(kind, method, params);
  }

  private doAuth(): void {
    this.rawSend('query', 'query.auth', { token: this.authToken }, { preConnect: true })
      .then(() => {
        this.authed = true;
        this.authFailed = false;
        const q = this.authQueue;
        this.authQueue = [];
        q.forEach((x) => x.run());
        this.setStatus('connected');   // full handshake: engine alive + authenticated
      })
      .catch((e) => {
        const code = e instanceof ApiRequestError ? e.code : '';
        // VR fix: an auth TIMEOUT is not a bad token — the engine just wasn't pumping yet. Before this,
        // it latched authFailed and everything stayed 'unauthorized' until the socket dropped (empty tabs
        // forever). Now the whole handshake retries; only a REAL rejection from the engine latches authFailed.
        if (code === 'timeout') { this.probeTimer = setTimeout(() => this.probeEngine(), 500); return; }
        if (code === 'not_connected' || code === 'connection_lost' || code === 'connection_closed') return;
        this.authFailed = true;
        const q = this.authQueue;
        this.authQueue = [];
        q.forEach((x) => x.reject(e));
        this.setStatus('connected');   // connected-but-unauthorized: same visible behavior as always
      });
  }

  private rawSend<T>(kind: ApiRequestKind, method: string, params?: object,
                     opts?: { preConnect?: boolean; timeoutMs?: number }): Promise<T> {
    // preConnect: HANDSHAKE requests (api_info probe + auth), which run with the socket open but
    // BEFORE declaring 'connected'. Everything else requires the usual connected state.
    const sockOpen = !!this.sock && this.sock.readyState === WebSocket.OPEN;
    if (opts?.preConnect ? !sockOpen : (this.status !== 'connected' || !this.sock)) {
      return Promise.reject(new ApiRequestError('not_connected', `cannot ${kind} ${method}: not connected`));
    }
    const timeoutMs = opts?.timeoutMs ?? METHOD_TIMEOUT_MS[method] ?? this.requestTimeout;
    const id = crypto.randomUUID();
    const req: ApiRequest = { v: API_VERSION, id, kind, method, params };
    return new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        this.incident(kind, method, `cmd ${method} TIMEOUT after ${timeoutMs}ms (no answer from the engine)`);
        reject(new ApiRequestError('timeout', `${method} timed out after ${timeoutMs}ms`));
      }, timeoutMs);
      this.pending.set(id, { resolve: resolve as (p: unknown) => void, reject, timer, kind, method });
      this.sock!.send(JSON.stringify(req));
    });
  }

  private onMessage(raw: string): void {
    let msg: ApiMessage;
    try {
      msg = JSON.parse(raw) as ApiMessage;
    } catch {
      // corrupt message: the protocol is still best-effort, but no longer silently so
      this.onIncident?.('ws: corrupt message received from the engine');
      return;
    }
    if (msg.kind === 'event') {
      this.emit(msg.method, msg.payload);
      return;
    }
    const pending = this.pending.get(msg.id);
    if (!pending) return;
    this.pending.delete(msg.id);
    clearTimeout(pending.timer);
    if (msg.kind === 'result') {
      pending.resolve(msg.payload);
    } else {
      pending.reject(new ApiRequestError(msg.code, msg.message));
    }
  }

  private onDrop(): void {
    this.sock = null;
    this.authed = false;
    this.authFailed = false;
    if (this.probeTimer) { clearTimeout(this.probeTimer); this.probeTimer = null; }
    this.failAllPending('connection_lost');
    if (this.authQueue.length) {
      const q = this.authQueue;
      this.authQueue = [];
      q.forEach((x) => x.reject(new ApiRequestError('connection_lost', 'connection dropped before auth')));
    }
    if (this.closed) return;
    this.setStatus('disconnected');
    this.reconnectTimer = setTimeout(() => {
      this.backoffMs = Math.min(this.backoffMs * 2, this.backoffMax);
      this.connect();
    }, this.backoffMs);
  }

  private failAllPending(code: string): void {
    for (const [, p] of this.pending) {
      clearTimeout(p.timer);
      // 'connection_closed' is a deliberate app-side close: not an incident worth reporting
      if (code === 'connection_lost') {
        this.incident(p.kind, p.method, `cmd ${p.method} lost: connection dropped before the reply`);
      }
      p.reject(new ApiRequestError(code, 'connection dropped'));
    }
    this.pending.clear();
  }

  private setStatus(status: ApiStatus): void {
    if (this.status === status) return;
    this.status = status;
    this.emit('status', status);
  }

  private emit(event: string, payload: unknown): void {
    this.listeners.get(event)?.forEach((cb) => cb(payload));
  }
}
