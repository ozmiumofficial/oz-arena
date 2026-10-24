import { Config } from "@remotion/cli/config";

// 1080p60 H.264 masters (TRAILER_PLAN.md decision 5). The footage is already 1920x1080 at 60 fps.
Config.setVideoImageFormat("jpeg");
Config.setJpegQuality(95);
Config.setCodec("h264");
Config.setCrf(14);
Config.setPixelFormat("yuv420p");
Config.setOverwriteOutput(true);
// six workers: the work PC has 16 GB, and each worker decodes footage
Config.setConcurrency(6);
