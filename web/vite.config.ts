/// <reference types="vitest" />
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import fs from 'fs';

const isDocker = fs.existsSync('/.dockerenv');
const target = isDocker ? 'http://host.docker.internal:8000' : 'http://127.0.0.1:8000';
const wsTarget = isDocker ? 'ws://host.docker.internal:8000' : 'ws://127.0.0.1:8000';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/ws': {
        target: wsTarget,
        ws: true,
        changeOrigin: true,
      },
      '/api': {
        target: target,
        changeOrigin: true,
      },
      '/sil': {
        target: target,
        changeOrigin: true,
      },
      '/tiles': {
        target: target,
        changeOrigin: true,
      },
      '/exports': {
        target: target,
        changeOrigin: true,
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
  },
});
