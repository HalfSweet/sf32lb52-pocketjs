/*
 * PocketJS composition for an SF32LB52 board (reference: sf32lb52_osptek,
 * 466x466 CO5300 AMOLED + CST820 touch).
 *
 * This program owns the display and EPIC engine:
 *   - CONFIG_BSP_USING_EPIC must be off (the platform installs EPIC_IRQHandler)
 *   - CONFIG_BSP_USING_LCD_FRAMEBUFFER and LVGL must be off (the platform opens
 *     the "lcd" device and presents whole framebuffers with draw_rect_async)
 *
 * The package and native texture table are linked by PocketjsEmbed():
 *   objs.extend(PocketjsEmbed('app',  '.../app.pocket'))
 *   objs.extend(PocketjsEmbed('epic', '.../app.epic'))
 */
#include <rtthread.h>
#include <string.h>

#include "mem_section.h"
#include "pocket/sifli.h"
#include "pocket_guest_quickjs.h"
#include "pocket_profile.h"

#define FRAMEBUFFER_COUNT 3u
#define POCKETJS_BUILD_MARKER "watch-rotate2-2026-10-28-19"

/* Logical UI size and physical/logical scale. They match the profile:
 * 466x466 at scale 1, or 233x233 at scale 2 on the same panel. */
#define UI_LOGICAL_WIDTH POCKET_PROFILE_WIDTH
#define UI_LOGICAL_HEIGHT POCKET_PROFILE_HEIGHT
#define UI_LOGICAL_SCALE 1u

/* Shared Rust/QuickJS heap in cached PSRAM. POCKETJS_HEAP_KB is 5120 in the
 * project config; keep the two runtime limits below that budget. */
#define RUNTIME_HEAP_LIMIT (2560u * 1024u)
/* 3.5 MiB leaves QuickJS GC headroom without exceeding the 6.5 MiB pool
 * shared with the 2.5 MiB runtime limit. */
#define GUEST_HEAP_LIMIT (3584u * 1024u)
#define GUEST_STACK_LIMIT (128u * 1024u)

extern const uint8_t pocketjs_app_data[];
extern const uint32_t pocketjs_app_size;
extern const uint8_t pocketjs_epic_data[];
extern const uint32_t pocketjs_epic_size;

/* The LCD DMA and EPIC read these buffers directly; PSRAM write-through is
 * the 52x default. L2_CACHE_NON_RET_BSS places them in PSRAM and outside the
 * retained region. 64-byte alignment is required by both drivers. */
/* 64-byte alignment is required by EPIC and the LCD DMA. Pad each
 * framebuffer to a 64-byte multiple: 466*466*2 is 8 bytes past a 64-byte
 * boundary, so the second and third buffers would otherwise be misaligned. */
#define FRAMEBUFFER_STRIDE ((POCKET_PROFILE_WIDTH * POCKET_PROFILE_HEIGHT + 31u) & ~31u)

ALIGN(64) static uint16_t
    g_framebuffer_storage[FRAMEBUFFER_COUNT][FRAMEBUFFER_STRIDE]
        L2_CACHE_NON_RET_BSS_SECT(pocketjs_fb);

static uint16_t *const g_framebuffers[FRAMEBUFFER_COUNT] = {
    g_framebuffer_storage[0],
    g_framebuffer_storage[1],
    g_framebuffer_storage[2],
};

static pocket_sifli_package g_package;

#if defined(POCKETJS_INPUT_KEYS)
/* sf32lb52_osptek board keys: KEY1=GPIO34, KEY2=GPIO11, active high. */
static const pocket_sifli_key g_keys[] = {
    {.pin = 34, .buttons = POCKET_BUTTON_UP, .active_high = true},
    {.pin = 11, .buttons = POCKET_BUTTON_DOWN, .active_high = true},
};
#endif

typedef struct {
  pocket_sifli_epic *epic;
  pocket_render_sifli_epic *renderer;
  pocket_sifli_display *display;
  pocket_sifli_touch *touch;
#if defined(POCKETJS_INPUT_KEYS)
  pocket_sifli_keys *keys;
#endif
  pocket_sifli_guest *guest;
  pocket_runtime *runtime;
  pocket_sifli_runner *runner;
} app_state;

/* The runner thread needs another 160 KiB stack after QuickJS has already
 * filled PSRAM; on this board that allocation can fail. The main thread
 * already owns a 160 KiB stack that booted QuickJS, so tick the runtime here.
 * The schedule keeps 30 Hz deadlines instead of sleeping 33 ms after each
 * frame, which would turn a 20 ms frame into a 53 ms one. */
static void tick_loop(app_state *state) {
  const rt_tick_t period =
      RT_TICK_PER_SECOND / (POCKET_PROFILE_TICK_HZ != 0u ? POCKET_PROFILE_TICK_HZ : 30u);
  rt_tick_t next = rt_tick_get();
  rt_tick_t log_at = next;
  uint32_t frames = 0;
  uint32_t max_ms = 0;
  for (;;) {
    next += period;
    const rt_tick_t before_wait = rt_tick_get();
    const rt_int32_t wait = (rt_int32_t)(next - before_wait);
    if (wait > 0)
      rt_thread_delay((rt_uint32_t)wait);
    else
      next = before_wait;
    const rt_tick_t started = rt_tick_get();
    const int32_t result = pocket_runtime_tick(state->runtime);
    const uint32_t elapsed =
        (uint32_t)((rt_tick_get() - started) * 1000u / RT_TICK_PER_SECOND);
    if (elapsed > max_ms)
      max_ms = elapsed;
    ++frames;
    if (result != POCKET_STATUS_OK)
      rt_kprintf("[PocketJS] tick failed: %d\n", (int)result);
    const rt_tick_t now = rt_tick_get();
    if ((uint32_t)(now - log_at) * 1000u >= 5000u * RT_TICK_PER_SECOND) {
      const uint32_t elapsed_ms =
          (uint32_t)((now - log_at) * 1000u / RT_TICK_PER_SECOND);
      pocket_sifli_epic_profile epic = {.struct_size = sizeof(epic)};
      pocket_sifli_epic_profile_get(state->epic, &epic, true);
      pocket_sifli_guest_stats guest_stats = {0};
      pocket_sifli_guest_stats_get(state->guest, &guest_stats, true);
      pocket_sifli_heap_stats heap = {0};
      pocket_sifli_heap_stats_get(&heap);
      pocket_sifli_display_stats display = {0};
      pocket_sifli_display_stats_get(state->display, &display, true);
      rt_kprintf("[PocketJS] fps=%u max=%ums js=%ums heap=%uk/%uk guest=%uk/%uk obj=%u | disp=%u/%ums/%ums/%u | epic tx=%u rej=%u tex=%u busy=%uk/%uk\n",
                 elapsed_ms != 0u ? frames * 1000u / elapsed_ms : 0u, max_ms,
                 guest_stats.frame_max_ms, (unsigned)(heap.used / 1024u),
                 (unsigned)(heap.total / 1024u), (unsigned)(guest_stats.malloc_size / 1024u),
                 (unsigned)(GUEST_HEAP_LIMIT / 1024u), (unsigned)guest_stats.object_count,
                 display.presents, display.flush_max_ms, display.wait_max_ms,
                 display.timeouts, epic.transactions, epic.rejected, epic.textures,
                 (unsigned)(epic.submit_cycles / 1000u),
                 (unsigned)(epic.wait_cycles / 1000u));
      frames = 0;
      max_ms = 0;
      log_at = now;
    }
  }
}

/* Runtime first, then drivers: the runtime drains every display/EPIC
 * submission before the contexts below disappear. */
static void app_release(app_state *state) {
  if (state->runner != NULL) {
    (void)pocket_sifli_runner_stop(state->runner);
    state->runner = NULL;
  }
  if (state->runtime != NULL) {
    pocket_runtime_destroy(state->runtime);
    state->runtime = NULL;
  }
  if (state->guest != NULL) {
    pocket_sifli_guest_destroy(state->guest);
    state->guest = NULL;
  }
  if (state->display != NULL) {
    pocket_sifli_display_destroy(state->display);
    state->display = NULL;
  }
  if (state->renderer != NULL) {
    pocket_render_sifli_epic_destroy(state->renderer);
    state->renderer = NULL;
  }
  if (state->epic != NULL) {
    pocket_sifli_epic_destroy(state->epic);
    state->epic = NULL;
  }
#if defined(POCKETJS_INPUT_KEYS)
  if (state->keys != NULL) {
    pocket_sifli_keys_destroy(state->keys);
    state->keys = NULL;
  }
#endif
  if (state->touch != NULL) {
    pocket_sifli_touch_destroy(state->touch);
    state->touch = NULL;
  }
}

static int app_configure(app_state *state) {
  /* 1. The allocator both the Rust runtime and QuickJS use. */
  if (pocket_sifli_heap_init() != POCKET_STATUS_OK) {
    rt_kprintf("[PocketJS] pocket_sifli_heap_init failed\n");
    return -1;
  }

  g_package.data = pocketjs_app_data;
  g_package.size = (size_t)pocketjs_app_size;

  /* 2. EPIC executor and the DrawList renderer. The executor and the display
   * must receive the same framebuffer array in the same order. */
  pocket_sifli_epic_config epic_config = {
      .struct_size = sizeof(epic_config),
      .framebuffers = g_framebuffers,
      .framebuffer_count = FRAMEBUFFER_COUNT,
      .width = POCKET_PROFILE_WIDTH,
      .height = POCKET_PROFILE_HEIGHT,
  };
  if (pocket_sifli_epic_create(&epic_config, &state->epic) != POCKET_STATUS_OK) {
    rt_kprintf("[PocketJS] pocket_sifli_epic_create failed\n");
    return -1;
  }
  pocket_epic_config renderer_config = {
      .struct_size = sizeof(renderer_config),
      .scale = UI_LOGICAL_SCALE, /* physical pixels per logical pixel */
      .native_textures = pocketjs_epic_data,
      .native_texture_bytes = (size_t)pocketjs_epic_size,
      .max_textures = POCKETJS_EPIC_MAX_TEXTURES,
  };
  state->renderer =
      pocket_render_sifli_epic_create(&renderer_config, pocket_sifli_epic_ops(state->epic));
  if (state->renderer == NULL) {
    rt_kprintf("[PocketJS] pocket_render_sifli_epic_create failed\n");
    return -1;
  }

  /* 3. Display: opens the RT-Thread "lcd" device and takes the framebuffers. */
  pocket_sifli_display_config display_config = {
      .struct_size = sizeof(display_config),
      .device = "lcd",
      .framebuffers = g_framebuffers,
      .framebuffer_count = FRAMEBUFFER_COUNT,
      .width = POCKET_PROFILE_WIDTH,
      .height = POCKET_PROFILE_HEIGHT,
      .brightness = 100,
      .wait_timeout_ms = 100,
  };
  state->display = pocket_sifli_display_create(&display_config);
  if (state->display == NULL) {
    rt_kprintf("[PocketJS] pocket_sifli_display_create failed\n");
    return -1;
  }

  /* 4. Touch (CST820 on the OSPTEK board). The driver divides panel
   * coordinates by `scale` and clamps to the logical viewport. */
  pocket_sifli_touch_config touch_config = {
      .struct_size = sizeof(touch_config),
      .device = "touch",
      .scale = UI_LOGICAL_SCALE,
      .logical_width = UI_LOGICAL_WIDTH,
      .logical_height = UI_LOGICAL_HEIGHT,
  };
  state->touch = pocket_sifli_touch_create(&touch_config);
  if (state->touch == NULL) {
    rt_kprintf("[PocketJS] pocket_sifli_touch_create failed\n");
    return -1;
  }

#if defined(POCKETJS_INPUT_KEYS)
  /* Optional board keys. They satisfy the profile's input.buttons capability;
   * the settings smoke app uses them for navigation. */
  pocket_sifli_keys_config keys_config = {
      .struct_size = sizeof(keys_config),
      .keys = g_keys,
      .key_count = sizeof(g_keys) / sizeof(g_keys[0]),
      .debounce_ms = 20,
  };
  state->keys = pocket_sifli_keys_create(&keys_config);
  if (state->keys == NULL) {
    rt_kprintf("[PocketJS] pocket_sifli_keys_create failed\n");
    return -1;
  }
#endif

  /* 5. QuickJS guest. Its heap is carved from the shared pocket_host heap. */
  pocket_sifli_guest_config guest_config = {
      .struct_size = sizeof(guest_config),
      .heap_limit = GUEST_HEAP_LIMIT,
      .stack_limit = GUEST_STACK_LIMIT,
  };
  state->guest = pocket_sifli_guest_create(&guest_config);
  if (state->guest == NULL) {
    rt_kprintf("[PocketJS] pocket_sifli_guest_create failed\n");
    return -1;
  }

  /* 6. Runtime: profile facts are generated by `pocket profile export`. */
  pocket_runtime_config runtime_config = {
      .struct_size = sizeof(runtime_config),
      .host_abi = pocket_profile.host_abi,
      .tick_hz = pocket_profile.tick_hz,
      .heap_limit = RUNTIME_HEAP_LIMIT,
      .frame_buffer_budget =
          (size_t)FRAMEBUFFER_COUNT * POCKET_PROFILE_WIDTH * POCKET_PROFILE_HEIGHT * 2u,
      .profile = pocket_profile.facts,
  };
  memcpy(runtime_config.profile_hash, pocket_profile.profile_hash,
         sizeof(runtime_config.profile_hash));
  state->runtime = pocket_runtime_create(&runtime_config);
  if (state->runtime == NULL) {
    rt_kprintf("[PocketJS] pocket_runtime_create failed\n");
    return -1;
  }

  pocket_renderer_id renderer_id =
      pocket_runtime_add_renderer(state->runtime, pocket_render_sifli_epic_vtable(),
                                  state->renderer);
  if (renderer_id == POCKET_RENDERER_INVALID ||
      pocket_runtime_add_display(state->runtime, pocket_sifli_display_vtable(), state->display,
                                 renderer_id) != POCKET_STATUS_OK ||
      pocket_runtime_add_input_source(state->runtime, pocket_sifli_touch_vtable(),
                                      state->touch) != POCKET_STATUS_OK ||
      pocket_runtime_set_package_source(state->runtime, pocket_sifli_package_vtable(),
                                        &g_package) != POCKET_STATUS_OK ||
      pocket_runtime_set_guest(state->runtime, pocket_sifli_guest_vtable(),
                               state->guest) != POCKET_STATUS_OK) {
    rt_kprintf("[PocketJS] runtime registration failed\n");
    return -1;
  }
#if defined(POCKETJS_INPUT_KEYS)
  if (pocket_runtime_add_input_source(state->runtime, pocket_sifli_keys_vtable(),
                                      state->keys) != POCKET_STATUS_OK) {
    rt_kprintf("[PocketJS] key input registration failed\n");
    return -1;
  }
#endif

  const rt_tick_t boot_started = rt_tick_get();
  rt_kprintf("[PocketJS] boot start\n");
  if (pocket_runtime_boot(state->runtime) != POCKET_STATUS_OK) {
    rt_kprintf("[PocketJS] boot failed: %s\n", pocket_sifli_guest_error(state->guest));
    return -1;
  }
  rt_kprintf("[PocketJS] boot done in %u ms\n",
             (unsigned)((rt_tick_get() - boot_started) * 1000u / RT_TICK_PER_SECOND));
  pocket_sifli_heap_stats heap = {0};
  pocket_sifli_heap_stats_get(&heap);
  rt_kprintf("[PocketJS] heap used=%u/%u max=%u\n", (unsigned)heap.used,
             (unsigned)heap.total, (unsigned)heap.max_used);
  return 0;
}

int main(void) {
  static app_state state;
  rt_kprintf("[PocketJS] marker=%s app=%u epic=%u\n", POCKETJS_BUILD_MARKER,
             (unsigned)pocketjs_app_size, (unsigned)pocketjs_epic_size);

  if (app_configure(&state) != 0) {
    app_release(&state);
    rt_kprintf("[PocketJS] startup failed\n");
    return -1;
  }

  rt_kprintf("[PocketJS] running %ux%u at %u Hz\n", (unsigned)POCKET_PROFILE_WIDTH,
             (unsigned)POCKET_PROFILE_HEIGHT, (unsigned)POCKET_PROFILE_TICK_HZ);
  tick_loop(&state);
  return 0;
}
