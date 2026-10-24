import type { NextConfig } from "next";

// The preview page only: the trailer itself renders through Remotion (npm run render).
const nextConfig: NextConfig = {
  reactStrictMode: true,
};

export default nextConfig;
