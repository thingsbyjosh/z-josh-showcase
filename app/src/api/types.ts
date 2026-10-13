// career_api v1 protocol. Same envelope in TS (client), C++ (engine), and fixtures.

export const API_VERSION = 1;
export const DEFAULT_API_URL = 'ws://127.0.0.1:47123';

export type ApiRequestKind = 'query' | 'cmd';

export interface ApiRequest {
  v: typeof API_VERSION;
  id: string;
  kind: ApiRequestKind;
  method: string;
  params?: object;
}

export interface ApiResult {
  v: typeof API_VERSION;
  id: string;
  kind: 'result';
  payload: unknown;
}

export interface ApiError {
  v: typeof API_VERSION;
  id: string;
  kind: 'error';
  code: 'bad_method' | 'bad_params' | 'not_ready' | 'internal';
  message: string;
}

export interface ApiEvent {
  v: typeof API_VERSION;
  kind: 'event';
  method: string;
  payload: unknown;
}

export type ApiMessage = ApiResult | ApiError | ApiEvent;

export type ApiStatus = 'disconnected' | 'connecting' | 'connected';
