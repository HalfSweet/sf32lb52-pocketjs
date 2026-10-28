import { Text, View } from "@pocketjs/framework/components";

const CENTER = 233;
const RADIUS = 466 * 0.4;
const COLOR = "#FF5E00";

// Build the static geometry once. The native scene retains these nodes;
// advancing the clock only changes the hand transforms in watch.tsx.
const TICKS = Array.from({ length: 60 }, (_, index) => {
  const angle = index * Math.PI / 30;
  const major = index % 5 === 0;
  const width = major ? 3 : 1;
  const height = major ? 11 : 4;
  const radius = RADIUS - height / 2;
  return {
    x: CENTER + Math.sin(angle) * radius - width / 2,
    y: CENTER - Math.cos(angle) * radius - height / 2,
    width,
    height,
    rotate: index * 6,
  };
});

const LABELS = Array.from({ length: 12 }, (_, index) => {
  const angle = index * Math.PI / 6;
  const radius = RADIUS - 32;
  return {
    text: String(index * 5).padStart(2, "0"),
    x: CENTER + Math.sin(angle) * radius - 13,
    y: CENTER - Math.cos(angle) * radius - 9,
  };
});

export function SecondDial() {
  return (
    <View class="absolute inset-0">
      {TICKS.map((tick) => (
        <View
          class="absolute"
          style={{
            insetL: tick.x,
            insetT: tick.y,
            width: tick.width,
            height: tick.height,
            rotate: tick.rotate,
            bgColor: COLOR,
          }}
        />
      ))}
      {LABELS.map((label) => (
        <Text
          class="absolute text-xs font-bold text-center text-[#FF5E00]"
          style={{ insetL: label.x, insetT: label.y, width: 26, height: 18, lineHeight: 18 }}
        >
          {label.text}
        </Text>
      ))}
    </View>
  );
}
