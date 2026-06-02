/// <reference types="vitest" />
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import fs from 'fs';

const isDocker = fs.existsSync('/.dockerenv');
const ORCH_PORT = process.env.ORCH_PORT ?? '8000';
const FOX_PORT = process.env.FOX_PORT ?? '8765';
const apiHost = isDocker ? 'host.docker.internal' : '127.0.0.1';
const target = `https://${apiHost}:${ORCH_PORT}`;
const wsTarget = `wss://${apiHost}:${ORCH_PORT}`;

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    host: process.env.VITE_HOST ?? 'localhost',
    proxy: {
      '/ws': {
        target: wsTarget,
        ws: true,
        changeOrigin: true,
        secure: false,
      },
      '/foxglove-ws': {
        target: `ws://${apiHost}:${FOX_PORT}`,
        ws: true,
        changeOrigin: true,
        secure: false,
        configure: (proxy, _options) => {
          proxy.on('error', (err, req, res) => {
            console.error('[Vite Proxy Error] /foxglove-ws:', err);
          });
          proxy.on('open', (proxySocket) => {
            console.log('[Vite Proxy Open] /foxglove-ws socket opened');
          });
          proxy.on('close', (res, socket, head) => {
            console.log('[Vite Proxy Close] /foxglove-ws socket closed');
          });
        }
      },
      '/api': {
        target: target,
        changeOrigin: true,
        secure: false,
      },
      '/sil': {
        target: target,
        changeOrigin: true,
        secure: false,
      },
      '/tiles': {
        target: target,
        changeOrigin: true,
        secure: false,
      },
      '/exports': {
        target: target,
        changeOrigin: true,
        secure: false,
      },
    },
  },
  build: {
    outDir: 'dist',
    sourcemap: true,
  },
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./src/test-setup.ts'],
    exclude: ['e2e/**', 'node_modules/**'],
  },
});
