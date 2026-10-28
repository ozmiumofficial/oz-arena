import type { Metadata } from "next";
import localFont from "next/font/local";
import { Barlow } from "next/font/google";
import "./globals.css";

// Quake 3's menu font (font1_prop), traced to a real font by tools/make_ui.py
const q3 = localFont({ src: "./fonts/q3_prop.ttf", variable: "--font-q3", display: "block" });
// body text: Barlow (SIL Open Font License), condensed-industrial, reads well at small sizes
const body = Barlow({ subsets: ["latin"], weight: ["400", "500", "600"], variable: "--font-body" });

export const metadata: Metadata = { title: "Oz Arena" };

// Nothing outside the bundled menu may load or run (Ozmium, 2026-10-22: no code injection through
// the web UI). Everything is served from the game's own files (http://res.webui), so 'self' covers
// scripts, styles, fonts and images; nothing connects anywhere else (Godot does all networking)
// and nothing can be framed, posted or re-based. Next's static export needs inline scripts to
// hydrate. Production builds only: `npm run dev` needs its hot-reload socket.
const CSP = [
  "default-src 'self'",
  "script-src 'self' 'unsafe-inline'",
  "style-src 'self' 'unsafe-inline'",
  "img-src 'self' data: blob:",
  "font-src 'self' data:",
  "media-src 'self' blob:",
  "connect-src 'self'",
  "frame-src 'none'",
  "object-src 'none'",
  "base-uri 'none'",
  "form-action 'none'",
].join("; ");

export default function RootLayout({ children }: LayoutProps<"/">) {
  return (
    <html lang="en" className={`${q3.variable} ${body.variable} h-full`}>
      {process.env.NODE_ENV === "production" && (
        <head>
          <meta httpEquiv="Content-Security-Policy" content={CSP} />
        </head>
      )}
      <body className="h-full overflow-hidden">{children}</body>
    </html>
  );
}
