import { Text, View } from "@pocketjs/framework/components";
import { createGesture } from "@pocketjs/framework/gesture";
import { mount } from "@pocketjs/framework/solid";
import { createSignal } from "solid-js";

function TouchTest() {
  const [status, setStatus] = createSignal("tap or drag the screen");
  const [point, setPoint] = createSignal("");

  createGesture({
    onTap: (c) => {
      setStatus("TAP");
      setPoint(`${Math.round(c.x)}, ${Math.round(c.y)}`);
    },
    onPanStart: (c) => {
      setStatus("DRAG");
      setPoint(`${Math.round(c.x)}, ${Math.round(c.y)}`);
    },
    onPanMove: (c) => {
      setStatus("DRAG");
      setPoint(`${Math.round(c.x)}, ${Math.round(c.y)}`);
    },
    onPanEnd: () => {
      setStatus("RELEASED");
    },
  });

  return (
    <View class="w-full h-full flex-col items-center justify-center gap-[10] bg-[#101820]">
      <Text class="text-2xl text-white">PocketJS Touch</Text>
      <Text class="text-lg text-[#00ff88]">{status()}</Text>
      <Text class="text-sm text-[#9aa4b2]">{point()}</Text>
      <Text class="text-xs text-[#667085]">tap / drag anywhere</Text>
    </View>
  );
}

mount(() => <TouchTest />);
