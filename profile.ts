import { defineProfile } from "../../../framework/src/profile.ts";

/**
 * SiFli SF32LB52 OSPTEK reference: 466x466 CO5300 AMOLED, CST820 touch.
 *
 * Keep this profile in sync with the panel and with `pocket_epic_config.scale`
 * in src/main.c. If you prefer a 233x233 logical UI on the same 466x466 panel,
 * use logicalViewports [[233, 233]], rasterDensity 2 and scale 2.
 */
export default defineProfile({
  id: "sf32lb52-osptek",
  platform: "rtthread",
  form: "takeover",
  tickHz: 30,
  display: {
    physicalViewport: [466, 466],
    logicalViewports: [[466, 466]],
    presentations: ["native"],
    rasterDensity: 1,
  },
  capabilities: ["input.touch", "input.buttons", "text.glyphs.baked"],
});
