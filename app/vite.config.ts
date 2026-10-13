import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// GitHub Pages serves this project from a sub-path, so every asset URL has to be prefixed with the
// repository name. Getting this wrong produces a page that loads and then renders nothing, because
// the bundle 404s at the site root.
export default defineConfig({
  plugins: [react()],
  base: "/z-josh-showcase/",
  build: { outDir: "dist" },
});
