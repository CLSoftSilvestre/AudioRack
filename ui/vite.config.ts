import { defineConfig } from "vite";

// Deterministic output names: the CMake build embeds exactly index.html,
// app.js and app.css as BinaryData (see src/CMakeLists.txt).
export default defineConfig({
  base: "./",
  build: {
    outDir: "dist",
    target: "es2019", // WKWebView on macOS 11 / WebView2 on Windows 10
    rollupOptions: {
      output: {
        entryFileNames: "app.js",
        chunkFileNames: "chunk-[name].js",
        assetFileNames: "app[extname]",
      },
    },
  },
});
