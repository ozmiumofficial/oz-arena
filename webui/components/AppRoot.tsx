"use client";
// One web app, two uses: the main menu, or the in-match pause menu when Godot's init says
// context "pause" (Gameplay keeps a hidden web view for it). The rem scale and the accessibility
// classes apply to both.
import { useEffect, useLayoutEffect } from "react";
import { connect, useMenu } from "@/lib/store";
import { installNav } from "@/lib/nav";
import MenuApp from "./MenuApp";
import PauseApp from "./PauseApp";

export default function AppRoot() {
  const context = useMenu((s) => s.init?.context);
  const uiScale = useMenu((s) => Number(s.values["accessibility/ui_scale"] ?? 1));
  const reduced = useMenu((s) => !!s.values["accessibility/reduced_flashing"]);
  const hc = useMenu((s) => !!s.values["accessibility/high_contrast"]);
  useEffect(() => {
    installNav();
    connect();
  }, []);
  // every size is in rem: 16 px at 1080p, scaled with the window and the menu-size setting
  useLayoutEffect(() => {
    const fit = () => {
      const s = Math.min(window.innerHeight / 1080, window.innerWidth / 1700) * uiScale;
      document.documentElement.style.fontSize = `${16 * s}px`;
    };
    fit();
    window.addEventListener("resize", fit);
    return () => window.removeEventListener("resize", fit);
  }, [uiScale]);
  useEffect(() => {
    document.documentElement.classList.toggle("reduced", reduced);
    document.documentElement.classList.toggle("hc", hc);
  }, [reduced, hc]);
  return context === "pause" ? <PauseApp /> : <MenuApp />;
}
