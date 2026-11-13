import { defineConfig } from 'vite'

const port = Number(process.env.PORT ?? 3002)
const previewHost = process.env.PREVIEW_HOST

export default defineConfig({
  server: {
    host: true,
    port,
    strictPort: true,
    allowedHosts: previewHost ? [previewHost] : undefined,
  },
})
