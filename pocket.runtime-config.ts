import { defineRuntimeConfig } from "../../../framework/src/runtime-config.ts";
import profile from "./profile.ts";

/** No native build recipe: the SiFli SDK project compiles the firmware and
 * embeds the package produced by `pocket compile`. */
export default defineRuntimeConfig({ profile });
