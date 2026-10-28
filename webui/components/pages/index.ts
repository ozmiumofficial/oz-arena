// Page id (lib/menu.json "pages") -> component. "play" and "quit" are drawn by MenuApp itself.
import type { ComponentType } from "react";
import SettingsPage from "./SettingsPage";
import CrosshairEditor from "./CrosshairEditor";
import SinglePlayerPage from "./SinglePlayerPage";
import CinematicsPage from "./CinematicsPage";
import CreateMatchPage, { OfflineBotsPage } from "./CreateMatchPage";
import { QuickJoinPage, QuickPlayPage, ServerBrowserPage, TrainingPage } from "./PlayPages";
import RankedPage from "./RankedPage";
import LeaderboardPage from "./LeaderboardPage";
import ReplaysPage from "./ReplaysPage";
import ProfilePage from "./ProfilePage";
import { CreditsPage } from "./OtherPages";
import DedicatedPage from "./DedicatedPage";
import RacePage from "./RacePage";

export interface PageProps {
  back: () => void;
  open: (id: string) => void;
}

export const PAGES: Record<string, ComponentType<PageProps>> = {
  campaign: SinglePlayerPage,
  cinematics: CinematicsPage,
  settings: SettingsPage,
  crosshair: CrosshairEditor,
  create_match: CreateMatchPage,
  offline_bots: OfflineBotsPage,
  quick_play: QuickPlayPage,
  quick_join: QuickJoinPage,
  ranked: RankedPage,
  race: RacePage,
  leaderboards: LeaderboardPage,
  replays: ReplaysPage,
  server_browser: ServerBrowserPage,
  dedicated: DedicatedPage,
  training: TrainingPage,
  profile: ProfilePage,
  credits: CreditsPage,
};
