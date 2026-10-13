// Global ErrorBoundary: a throw during render is NO LONGER a silent
// blank screen -- a visible fallback with a restart option + a persisted breadcrumb via the safety net (safetyNet).
import React from 'react';
import i18n from '../i18n';
import { reportUiError } from './safetyNet';
import { Btn, Display } from '../ui/components';

interface State {
  error: Error | null;
}

// robust t(): if i18n hasn't initialized yet (a very early crash) it falls back to the EN text.
function tr(key: string, fallback: string): string {
  try {
    const s = i18n.t(key, { defaultValue: fallback });
    return typeof s === 'string' && s ? s : fallback;
  } catch {
    return fallback;
  }
}

export class ErrorBoundary extends React.Component<{ children: React.ReactNode }, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, info: React.ErrorInfo): void {
    const at = String(info.componentStack ?? '').trim().split('\n')[0]?.trim() ?? '';
    reportUiError('react', `${error.message}${at ? ` | ${at}` : ''}`);
  }

  render(): React.ReactNode {
    if (!this.state.error) return this.props.children;
    return (
      <div className="col center" style={{ height: '100%', gap: 14, padding: 32, textAlign: 'center' }}>
        <Display as="h1">{tr('crash.title', 'Something went wrong')}</Display>
        <div style={{ opacity: 0.72, maxWidth: 460, lineHeight: 1.5 }}>
          {tr('crash.body', 'The app hit an unexpected error. It has been recorded in the support log (Settings, Support section). Restarting usually fixes it.')}
        </div>
        <Btn variant="primary" onClick={() => window.location.reload()}>
          {tr('crash.reload', 'Restart the app')}
        </Btn>
      </div>
    );
  }
}
