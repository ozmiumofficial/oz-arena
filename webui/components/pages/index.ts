// Page id (lib/menu.json "pages") -> component. "play" and "quit" are drawn by MenuApp itself.
import type { ComponentType } from "react";
import SettingsPage from "./SettingsPage";
import CrosshairEditor from "./CrosshairEditor";
import SinglePlayerPage from "./SinglePlayerPage";
import CinematicsPage from "./CinematicsPage";
import CreateMatchPage, { OfflineBotsPage } from "./CreateMatchPage";
import { QuickPlayPage, ServerBrowserPage, TrainingPage } from "./PlayPages";
import RankedPage from "./RankedPage";
import LeaderboardPage from "./LeaderboardPage";
import ProfilePage from "./ProfilePage";
import { CreditsPage, WorkshopPage } from "./OtherPages";

export interface PageProps {
  back: () => void;
  open: (id: string) => void;
}

export const PAGES: Record<string, ComponentType<PageProps>> = {
  single_player: SinglePlayerPage,
  cinematics: CinematicsPage,
  settings: SettingsPage,
  crosshair: CrosshairEditor,
  create_match: CreateMatchPage,
  offline_bots: OfflineBotsPage,
  quick_play: QuickPlayPage,
  ranked: RankedPage,
  leaderboards: LeaderboardPage,
  server_browser: ServerBrowserPage,
  training: TrainingPage,
  profile: ProfilePage,
  workshop: WorkshopPage,
  credits: CreditsPage,
};
