import { Image, Text, View, Screen, type NodeMirror } from "@pocketjs/framework/components";
import { createGesture } from "@pocketjs/framework/gesture";
import { jump } from "@pocketjs/framework/animation";
import { onFrame } from "@pocketjs/framework/lifecycle";
import { mount } from "@pocketjs/framework/solid";
import { createSignal } from "solid-js";
import { SecondDial } from "./second-dial";

// Aurora lock watch face, ported from the canvas prototype.
// Canvas coordinates are 466x466; the lock animation rotates the three dials
// so the current hour/minute/second sit at twelve o'clock, while the camera
// zooms and shifts left for the HUD.

const CX = 233;
const CY = 233;
const SIZE = 466;

const R_H = SIZE * 0.16;
const R_M = SIZE * 0.28;
const R_S = SIZE * 0.4;

const C_HOUR = "#FFFFFF";
const C_MIN = "#B4FF29";
const C_SEC = "#FF5E00";

type Dial = {
  key: "h" | "m" | "s";
  R: number;
  max: number;
  hour: boolean;
  color: string;
  handWidth: number;
  handSrc: string;
  handW: number;
  handH: number;
  imgSize: number;
};

const DIALS: Dial[] = [
  { key: "h", R: R_H, max: 12, hour: true, color: C_HOUR, handWidth: 6, handSrc: "hand-hour.png", handW: 8, handH: 128, imgSize: 204.8 },
  { key: "m", R: R_M, max: 60, hour: false, color: C_MIN, handWidth: 4, handSrc: "hand-minute.png", handW: 8, handH: 256, imgSize: 358.0 },
  { key: "s", R: R_S, max: 60, hour: false, color: C_SEC, handWidth: 2, handSrc: "hand-second.png", handW: 4, handH: 256, imgSize: 512.0 },
];

function easeInOutCubic(x: number): number {
  return x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2;
}

function WatchFace() {
  const [locked, setLocked] = createSignal(false);
  const [tapped, setTapped] = createSignal(false);
  const [hStr, setHStr] = createSignal("00");
  const [mStr, setMStr] = createSignal("00");
  const [sStr, setSStr] = createSignal("00");

  let camera: NodeMirror | null = null;
  let hud: NodeMirror | null = null;
  let tip: NodeMirror | null = null;
  let focus: NodeMirror | null = null;
  const handNodes: Partial<Record<Dial["key"], NodeMirror | null>> = {};

  let raw = 0;

  createGesture({
    onTap: () => {
      setTapped(true);
      setLocked((v) => !v);
    },
  });

  onFrame(() => {
    const now = new Date();
    const ms = now.getMilliseconds();
    const sec = now.getSeconds() + ms / 1000;
    const min = now.getMinutes() + sec / 60;
    const hour = (now.getHours() % 12) + min / 60;

    const target = locked() ? 1 : 0;
    if (raw < target) raw = Math.min(target, raw + 0.05);
    else if (raw > target) raw = Math.max(target, raw - 0.05);
    const e = easeInOutCubic(raw);

    const hh = String(now.getHours()).padStart(2, "0");
    const mm = String(now.getMinutes()).padStart(2, "0");
    const ss = String(now.getSeconds()).padStart(2, "0");
    if (hStr() !== hh) setHStr(hh);
    if (mStr() !== mm) setMStr(mm);
    if (sStr() !== ss) setSStr(ss);

    if (camera) {
      jump(camera, "scale", 1 + 0.28 * e);
      jump(camera, "translateX", -35 * e);
    }

    const values: Record<Dial["key"], number> = { h: hour, m: min, s: sec };

    // The three tick rings stay axis-aligned: EPIC on 52x has no VG Lite
    // quad path, so a rotated 256x256 texture would fall back to the CPU
    // every frame. Only the thin hands rotate; they are cheap even on the
    // CPU, and at rest (locked) they are axis-aligned fills.
    for (const d of DIALS) {
      const native = (values[d.key] / d.max) * 2 * Math.PI - Math.PI / 2;
      const handRot = native * (1 - e);
      const hand = handNodes[d.key];
      if (hand) jump(hand, "rotate", (handRot * 180) / Math.PI);
    }

    if (hud) jump(hud, "opacity", e * e);
    if (focus) jump(focus, "opacity", 0.7 * e);
    if (tip) jump(tip, "opacity", tapped() ? 0 : 1 - e);
  });

  return (
    <Screen class="relative w-full h-full overflow-hidden bg-[#030303]">
      <View class="absolute inset-0" ref={(node) => (camera = node)} style={{ scale: 1, translateX: 0 }}>
        {DIALS.map((d) => (
          <View class="absolute inset-0">
            {d.key === "h" ? (
              <Image src="rings-hour.png" style={{ insetL: CX - d.imgSize / 2, insetT: CY - d.imgSize / 2, width: d.imgSize, height: d.imgSize }} />
            ) : d.key === "m" ? (
              <Image src="rings-minute.png" style={{ insetL: CX - d.imgSize / 2, insetT: CY - d.imgSize / 2, width: d.imgSize, height: d.imgSize }} />
            ) : (
              <SecondDial />
            )}
          </View>
        ))}

        <View
          class="absolute inset-0"
          ref={(node) => (focus = node)}
          style={{ opacity: 0, gradFrom: "#00000000", gradTo: "#000000A0", gradDir: 1 }}
        />

        {DIALS.map((d) => (
          <View class="absolute inset-0" ref={(node) => (handNodes[d.key] = node)} style={{ rotate: 0 }}>
            <Image
              class="absolute"
              src={d.handSrc}
              style={{
                insetL: CX - d.handW / 2,
                insetT: CY - d.R,
                width: d.handW,
                height: d.handH,
              }}
            />
          </View>
        ))}

        <View
          class="absolute"
          style={{ insetL: CX - 5, insetT: CY - 5, width: 10, height: 10, bgColor: C_SEC, radius: 5 }}
        />
        <View
          class="absolute"
          style={{ insetL: CX - 2, insetT: CY - 2, width: 4, height: 4, bgColor: "#000000", radius: 2 }}
        />
      </View>

      <View class="absolute inset-0" ref={(node) => (hud = node)} style={{ opacity: 0 }}>
        {/* mechanical bracket */}
        <View class="absolute" style={{ insetL: 40, insetT: 215, width: 8, height: 1.5, bgColor: "#FFFFFF" }} />
        <View class="absolute" style={{ insetL: 40, insetT: 215, width: 1.5, height: 8, bgColor: "#FFFFFF" }} />
        <View class="absolute" style={{ insetL: 212, insetT: 215, width: 8, height: 1.5, bgColor: "#FFFFFF" }} />
        <View class="absolute" style={{ insetL: 218.5, insetT: 215, width: 1.5, height: 8, bgColor: "#FFFFFF" }} />
        <View class="absolute" style={{ insetL: 40, insetT: 249.5, width: 8, height: 1.5, bgColor: "#FFFFFF" }} />
        <View class="absolute" style={{ insetL: 40, insetT: 243, width: 1.5, height: 8, bgColor: "#FFFFFF" }} />
        <View class="absolute" style={{ insetL: 212, insetT: 249.5, width: 8, height: 1.5, bgColor: "#FFFFFF" }} />
        <View class="absolute" style={{ insetL: 218.5, insetT: 243, width: 1.5, height: 8, bgColor: "#FFFFFF" }} />

        {/* laser line */}
        <View
          class="absolute"
          style={{
            insetL: 40,
            insetT: 232.25,
            width: 210,
            height: 1.5,
            gradFrom: "#00000000",
            gradVia: "#FFFFFF",
            gradViaPos: 0.72,
            gradTo: "#00000000",
            gradDir: 3,
          }}
        />

        {/* split-colour digital clock */}
        <View class="absolute flex-row items-center gap-[2]" style={{ insetL: 250, insetT: 188 }}>
          <Text class="text-2xl font-bold text-white">{hStr()}</Text>
          <Text class="text-2xl font-bold text-[#6b7280]">:</Text>
          <Text class="text-2xl font-bold text-[#B4FF29]">{mStr()}</Text>
          <Text class="text-2xl font-bold text-[#6b7280]">:</Text>
          <Text class="text-2xl font-bold text-[#FF5E00]">{sStr()}</Text>
        </View>
      </View>

      <Text
        class="absolute text-sm font-bold text-center text-[#666666]"
        ref={(node) => (tip = node)}
        style={{ insetL: 0, insetT: 420, width: SIZE, opacity: 1, tracking: 3 }}
      >
        TAP TO LOCK
      </Text>
    </Screen>
  );
}

mount(() => <WatchFace />);
