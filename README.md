# PocketJS on SF32LB52 (reference: sf32lb52_osptek)

This directory is the missing **program composition** for
`platforms/sifli`. The platform is a driver/Rust-runtime library, not a
firmware image: you still need a SiFli SDK project, a profile, an application
package, and a `main` that wires the drivers together.

## Code-drawn outer dial

The code-drawn outer dial in `app/second-dial.tsx` replaces `rings-second.png`
with 60 retained rectangle nodes and 12 text labels. Geometry is computed at
mount; clock updates still change the hands. The outer dial is centered at
(233, 233), and labels use the project's baked font rather than the PNG's
lettering. Normal and locked layouts were checked with the shared software
renderer. The outer PNG is no longer included in the application package.
On the 525, **2,260 frames over 75.240 seconds averaged 30.04 FPS**, with
**14.67 ms renderer time** and **7.47 ms guest time**, zero errors and zero
EPIC rejections. This uses the existing 30 Hz schedule and mock display;
it does not measure LCD transfer. Package/native texture sizes fell to
875,688/540,864 bytes. **Boot increased from about 12.6 to 36.4 seconds**
with the added scene nodes, so initialization remains an optimization target.
The RTC-driven performance captures are not paired at identical hand angles.
These measurements use PocketJS `25099a73` from
[pocket-nexus/pocketjs#627](https://github.com/pocket-nexus/pocketjs/pull/627)
and SiFli-SDK `5fb28c60`, with the local headless validation harness.

## What runs on the board

```
app.pocket ──┐
             ├─ pocket_runtime (Rust archive from programs/sifli/archive)
app.epic ────┘        │
                      ├─ pocket_sifli_guest (QuickJS in the shared heap)
                      ├─ pocket_render_sifli_epic (DrawList -> EPIC commands)
                      ├─ pocket_sifli_display ("lcd" + draw_rect_async)
                      ├─ pocket_sifli_touch  ("touch", CST820)
                      └─ 30 Hz tick loop on the main thread
```

The application `main` is `src/main.c`. It ticks on the main thread rather
than starting `pocket_sifli_runner`: after QuickJS has filled PSRAM, a second
160 KiB thread stack can fail to allocate, and the main thread already owns a
160 KiB stack that booted the guest.

## Hard constraints on SF32LB52

`platforms/sifli` installs its own `EPIC_IRQHandler` and presents whole
framebuffers through the RT-Thread `lcd` device. Therefore:

- `CONFIG_BSP_USING_EPIC` **must be off**. `POCKETJS_EPIC` depends on
  `!BSP_USING_EPIC`; if you leave it on, Kconfig silently drops the executor
  and the link fails on `pocket_sifli_epic_create`.
- `CONFIG_BSP_USING_LCD_FRAMEBUFFER` and LVGL **must be off**. LVGL's
  framebuffer thread also opens the `lcd` device and calls
  `draw_rect_async`.
- `CONFIG_POCKETJS_MPU_OVERRIDE` is 58x-only; `hooks/mpu.c` has
  `#error` on 52x. Leave it off.
- VG Lite does not exist on 52x. Leave `CONFIG_POCKETJS_EPIC_VGLITE` off.
- Keep `CONFIG_PSRAM_CACHE_WB` off (the SDK default is write-through). The
  platform only enforces a non-cacheable PSRAM2 window on 58x; on 52x the
  CPU never writes the framebuffer, so write-through PSRAM is the safe
  configuration.
- The program vendors **QuickJS 2025-09-13** under `quickjs/`. The SDK's
  2021 QuickJS cannot parse the `??=` / `||=` syntax the PocketJS compiler
  emits, so `CONFIG_PKG_USING_QUICKJS` is off and the project compiles the
  in-tree engine. Keep `CONFIG_POCKETJS_QUICKJS_BELLARD=y`; the guest uses
  Bellard's allocator API.

If the existing `sf-claw/watch` firmware must keep its LVGL UI, do **not**
add PocketJS to that firmware. Build a separate PocketJS project (or a
separate boot slot) as described below. Sharing EPIC and the LCD with LVGL
would require a different executor/display design; the current platform
does not support it.

### 52x rendering constraint: no rotated texture quads

EPIC on 52x supports fills, A8 blends and **axis-aligned** scaled blits. It
has no VG Lite, so `caps.blit_quad` is off and the renderer sends rotated
texture quads (and non-axis-aligned solid hands) to its CPU rasterizer. A
full-screen 256x256 ring rotating every frame is therefore the worst case:
the CPU redraws it through two 64 KiB tiles per band. Keep large textures
axis-aligned; rotate only small elements, or add an EPIC hardware-rotation
command (`HAL_EPIC` transform `angle`) before rotating full-screen art.

## 1. Prepare the PocketJS tree

Use Bun 1.4.2 (`.bun-version`) and the pinned Rust toolchain:

```sh
cd /home/weixun/sf-pocketjs/pocketjs-main
bun install --frozen-lockfile
rustup target add --toolchain 1.98.1 thumbv8m.main-none-eabihf
```

## 2. Export the profile

The profile in `profile.ts` is the 466x466 OSPTEK panel with touch input.
Export it before running `scons`; the platform SConscript refuses to build
without `pocket_profile.h`.

```sh
cd /home/weixun/sf-pocketjs/pocketjs-main
bun tools/pocket.ts profile export \
  --config programs/sifli/sf32lb52-osptek/pocket.runtime-config.ts \
  --out build/sf32lb52-osptek/profile
```

If you want a 233x233 logical UI on the same panel, change
`logicalViewports` to `[[233, 233]]`, `rasterDensity` to `2`, and set
`UI_LOGICAL_WIDTH`/`UI_LOGICAL_HEIGHT` to 233 and `UI_LOGICAL_SCALE` to 2 in
`src/main.c`. The physical framebuffer stays 466x466.

## 3. Compile and bake the application

`app/pocket.json` compiles the Vue Vapor `apps/clear` touch calculator at
466x466. It is the touch smoke test; `app/settings.json` remains a Solid
button-driven test, and the settings bundle is smaller if you only need to
verify the display or the runner.

```sh
cd /home/weixun/sf-pocketjs/pocketjs-main
mkdir -p build/sf32lb52-osptek
bun tools/pocket.ts compile \
  --manifest programs/sifli/sf32lb52-osptek/app/pocket.json \
  --project-root . \
  --runtime-config programs/sifli/sf32lb52-osptek/pocket.runtime-config.ts \
  --output build/sf32lb52-osptek/app.pocket
bun tools/build/sifli/epic.ts \
  --pocket build/sf32lb52-osptek/app.pocket \
  --out build/sf32lb52-osptek/app.epic
```

The application manifest must use a viewport in the profile's
`logicalViewports`; for a fixed `native` viewport, the logical size must fill
the 466x466 panel (or be a 1/N integer fit, e.g. 233x233 with scale 2).

## 4. Create the SDK project

The simplest path is to copy the existing watch project and replace its
application sources; the board configuration (`ptab.json`, CO5300, CST820,
flash layout) stays the same:

```sh
cp -a /home/weixun/sf-claw/watch /home/weixun/sf-claw/watch-pocket
cd /home/weixun/sf-claw/watch-pocket
rm -rf project/build_* project/.sconsign.dblite
rm -rf src
mkdir -p src
cp /home/weixun/sf-pocketjs/pocketjs-main/programs/sifli/sf32lb52-osptek/src/main.c src/
cp /home/weixun/sf-pocketjs/pocketjs-main/programs/sifli/sf32lb52-osptek/src/SConscript src/
```

Then edit the project files:

- `project/Kconfig.proj`: remove the watch's `rsource
  "../third_party/rt-claw-port-sf32/Kconfig"` and its app menus, and add
  `source "$POCKETJS_ROOT/platforms/sifli/Kconfig"`.
- `project/SConscript`: keep the SDK include, include the platform
  SConscript, and add the two `PocketjsEmbed` calls. See
  `integration/SConscript.fragment`.
- `project/proj.conf`: replace the watch's application config (LVGL,
  rt-claw, Bluetooth, Wi-Fi, `DRV_EPIC_NEW_API`, ...) with
  `integration/proj.conf.fragment` (plus the board BSP options you want to
  keep).
- `project/SConstruct`: keep `AddBootLoader`/`AddLCPU` as in the watch
  project, and add the QuickJS warning relaxation from
  `integration/SConstruct.fragment`.

A minimal `project/Kconfig.proj` looks like:

```kconfig
config CUSTOM_MEM_MAP
    bool
    select custom_mem_map
    default y if !SOC_SIMULATOR

source "$POCKETJS_ROOT/platforms/sifli/Kconfig"
```

The `PocketjsEmbed` paths in the fragment read `POCKET_BUILD`; it must name
the directory containing `app.pocket` and `app.epic` (the commands above use
`build/sf32lb52-osptek`).

## 5. Build and flash

```sh
export SIFLI_SDK=/home/weixun/sf32/SiFli-SDK-2.5.0
source "$SIFLI_SDK/export.sh"

export POCKETJS_ROOT=/home/weixun/sf-pocketjs/pocketjs-main
export POCKET_PROFILE_EXPORT_DIR="$POCKETJS_ROOT/build/sf32lb52-osptek/profile"
export POCKET_BUILD="$POCKETJS_ROOT/build/sf32lb52-osptek"
export POCKET_RUNTIME_ARCHIVE="$POCKET_BUILD/cargo/thumbv8m.main-none-eabihf/release/libpocket_sifli_runtime.a"
export PATH="$HOME/.cargo/bin:$HOME/.bun/bin:$PATH"

cd "$POCKETJS_ROOT/programs/sifli/sf32lb52-osptek/project"
scons --board=sf32lb52_osptek -j8
```

The reference `proj.conf` has `CONFIG_POCKETJS_RUST_FROM_SOURCE` off and
links `POCKET_RUNTIME_ARCHIVE`. To rebuild the Rust archive from source,
enable `CONFIG_POCKETJS_RUST_FROM_SOURCE` and run:

```sh
cargo build --release --locked --no-default-features \
  --target thumbv8m.main-none-eabihf \
  --manifest-path programs/sifli/archive/Cargo.toml
```

The archive is linked at the end of the image. Flash the generated
`main.bin`/`bootloader` with the SDK's `download.sh`/`sftool` as usual.

Expected UART output:

```
[PocketJS] running 466x466 at 30 Hz
```

If boot fails, `pocket_sifli_guest_error()` prints the QuickJS error after
`[PocketJS] boot failed:`.

## 6. Memory tuning for 52x

The 52x HCPU has 512 KiB SRAM and 8 MiB PSRAM on this board. The reference
configuration uses:

| Setting | Value | Why |
|---|---|---|
| `POCKETJS_HEAP_KB` | 6656 | shared Rust + QuickJS heap in PSRAM |
| `RUNTIME_HEAP_LIMIT` | 2.5 MiB | runtime tables/textures; also the package-size ceiling |
| `GUEST_HEAP_LIMIT` | 3 MiB | QuickJS heap |
| `FRAMEBUFFER_COUNT` | 3 | two alternate; third covers a slow present |
| framebuffer bytes | 466*466*2 = 424 KiB each | 1.27 MiB total |
| `POCKETJS_EPIC_MASK_TILE_KB` | 32 | two A8 planes in SRAM |
| `POCKETJS_EPIC_CPU_TILE_KB` | 64 | two RGB565 fallback tiles in SRAM; fewer bands for CPU quads |
| `POCKETJS_EPIC_MIN_PIXELS` | 256 | EPIC transaction overhead on 52x |
| guest JS stack limit | 128 KiB | `JS_SetMaxStackSize` |
| main thread stack | 160 KiB | QuickJS boot/eval and the 30 Hz tick loop |

If the linker overflows PSRAM, reduce `POCKETJS_HEAP_KB` to 4096 or use two
framebuffers. If the system heap is exhausted before the first frame, reduce
the EPIC tiles; the program does not allocate a runner stack.

## 7. What to change for another 52x board

- `profile.ts`: `physicalViewport`, `logicalViewports`, `rasterDensity` and
  capabilities (`input.touch`, `input.buttons`) must match the panel and
  input devices.
- `src/main.c`: `POCKET_PROFILE_WIDTH`/`HEIGHT` come from the exported
  profile; `display_config.device`, `touch_config.device`, touch `scale` and
  the framebuffer section may differ.
- If the touch panel reports rotated coordinates, swap/rotate in
  `platforms/sifli/input/touch.c` or adjust the panel driver; the platform's
  driver uses the RT-Thread `touch` device coordinates directly.

## Files

| File | Purpose |
|---|---|
| `profile.ts` | 466x466 SF32LB52 profile |
| `pocket.runtime-config.ts` | profile export/compile config |
| `app/pocket.json` | 466x466 smoke-test application |
| `src/main.c` | runtime + EPIC + display + touch + guest composition |
| `src/SConscript` | application source group |
| `integration/*.fragment` | SDK project edits |
