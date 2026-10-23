import type { NextConfig } from "next";

// Static export: the menu ships as plain files that Godot's web view (godot_wry) loads from
// res://webui/. tools/build_webui.py rewrites absolute /_next/ paths to relative ones.
const nextConfig: NextConfig = {
  output: "export",
  images: { unoptimized: true },
  devIndicators: false,
  turbopack: {
    rules: {
      "*.css": {
        loaders: ["@tailwindcss/turbopack"],
        as: "*.css",
      },
    },
  },
};

export default nextConfig;
