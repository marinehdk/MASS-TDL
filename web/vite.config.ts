/// <reference types="vitest" />
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import fs from 'fs';

const isDocker = fs.existsSync('/.dockerenv');
const target = isDocker ? 'https://host.docker.internal:8000' : 'https://127.0.0.1:8000';
const wsTarget = isDocker ? 'wss://host.docker.internal:8000' : 'wss://127.0.0.1:8000';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/ws': {
        target: wsTarget,
        ws: true,
        changeOrigin: true,
        secure: false,
      },
      '/foxglove-ws': {
        target: isDocker ? 'ws://host.docker.internal:8765' : 'ws://127.0.0.1:8765',
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
