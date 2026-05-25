/// <reference types="vite/client" />
import React from 'react';
import ReactDOM from 'react-dom/client';
import { Provider } from 'react-redux';
import { configureStore } from '@reduxjs/toolkit';
import App from './App';
import { silApi } from './api/silApi';
import './styles/tokens.css';

const store = configureStore({
  reducer: {
    [silApi.reducerPath]: silApi.reducer,
  },
  middleware: (getDefaultMiddleware) =>
    getDefaultMiddleware().concat(silApi.middleware),
});

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <Provider store={store}>
      <App />
    </Provider>
  </React.StrictMode>,
);

// Expose Zustand telemetry store to Playwright E2E in dev mode
if (import.meta.env.DEV) {
  import('./store/telemetryStore').then(({ useTelemetryStore }) => {
    (window as any).__ZUSTAND_TELEMETRY_STORE__ = useTelemetryStore;
  });
}
