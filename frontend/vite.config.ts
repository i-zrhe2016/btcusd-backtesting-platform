import { defineConfig } from 'vitest/config'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      '/api/market': 'http://localhost:8081',
      '/api/backtests': 'http://localhost:8082',
      '/api/configs': 'http://localhost:8082',
    },
  },
  test: {
    environment: 'jsdom',
  },
})
