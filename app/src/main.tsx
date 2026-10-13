import ReactDOM from "react-dom/client";
import App from "./App";
import { initI18n } from "./i18n";
import { ErrorBoundary } from "./support/ErrorBoundary";
import { installGlobalErrorHandlers } from "./support/safetyNet";
import { applyUiScale } from "./ui/prefs";
import "./design-system.css";
import "driver.js/dist/driver.css";
import "./tutorial/tutorial.css";

initI18n("en"); // English by default; the app switches to the config's locale once it's loaded
installGlobalErrorHandlers(); // tracks JS/promise errors to zta_support.log (previously invisible)
void applyUiScale();  // the scale chosen in Settings. Webview zoom (via IPC), requested right at startup

// WITHOUT React.StrictMode: in dev it would mount the tree TWICE -> two CareerApiClients -> two WS connections
// fighting over the ws_server's SINGLE slot (churn/disconnects). career_api is single-client; only one
// stable connection is allowed. (No double-mount happens in production; this is purely a dev-mode issue.)
ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <ErrorBoundary>
    <App />
  </ErrorBoundary>,
);
