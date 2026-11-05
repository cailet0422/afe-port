/* pc_vi.c - video interface → SDL window swap + frame pacing */
#include "pc_platform.h"
#include "pc_input_script.h"
#include "pc_touch.h"
#include "pc_minimap.h"
#include "pc_update.h"
#include "pc_gui.h"
#include "pc_texture_pack.h"

#define VI_TVMODE_NTSC_INT    0
#define VI_TVMODE_NTSC_DS     1
#define VI_TVMODE_PAL_INT     4
#define VI_TVMODE_MPAL_INT    8
#define VI_TVMODE_EURGB60_INT 20

static u32 retrace_count = 0;
u32 pc_frame_counter = 0;
static Uint64 frame_start_time = 0;
static Uint64 perf_freq = 0;
static void (*vi_pre_callback)(u32) = NULL;
static void (*vi_post_callback)(u32) = NULL;

/* --- Frame pacing ---
 * The game takes one step per retrace (60 a second, NTSC), so it goes as
 * fast as the frames go by. Each frame has its slot on a 60 Hz schedule, and
 * a frame done early waits for the end of its slot. Frames that come late
 * used to slow the whole game down with them, so where drawing and showing a
 * frame takes longer than 1/60 s (a phone's GPU, more so with HD textures, or
 * VSync on a display that shows fewer frames) the game went in slow motion at
 * a steady frame rate that still looked smooth. Now the frames after a late
 * one start at once, and once the game is a whole frame behind, the next
 * frame is played without being drawn or shown: the game keeps its speed and
 * the screen shows fewer frames instead. Playing a frame without drawing it
 * costs little (about 1.5 ms on a desktop CPU): drawing is the slow part. */
#define VI_RATE         60  /* retraces a second */
#define VI_MAX_SKIP     2   /* frames in a row not shown: at least every third frame is */
#define VI_MAX_BEHIND   6   /* frames: further behind, the schedule starts over (no fast-forward) */
#define VI_SKIP_SAMPLES 8   /* frames not shown that tell whether skipping helps */
#define VI_SKIP_HOLD    300 /* frames all shown after skipping did not help */
#define VI_GIVE         256 /* a late frame moves the schedule by up to 1/256 of a frame: VSync on a
                               display a little slower than 60 Hz keeps its pace without skipping */
#define VI_STATS_SECS   60  /* how often a line says frames were skipped */

int g_pc_vi_skip = 0;          /* this frame is played, not drawn or shown */
int g_pc_sim_refresh = 0;      /* --sim-refresh: Hz of a VSync display to imitate (tests) */
static Uint64 sched_base = 0;  /* the schedule's start */
static u64 sched_frame = 0;    /* frames since then */
static int skip_run = 0;       /* frames not shown in a row */
static int skip_hold = 0;      /* frames to show before skipping again */
static Uint64 skip_cost = 0;   /* time the last frames not shown took */
static int skip_samples = 0;
static Uint64 stats_start = 0, stats_skip_cost = 0;
static unsigned stats_frames = 0, stats_shown = 0, stats_resets = 0, stats_holds = 0;

/* The frame being played must be drawn and shown after all: it copies the
 * screen (GXCopyTex, e.g. the scene behind a menu), which a frame that is not
 * drawn would leave black. Called while the game builds the frame, before any
 * of it is drawn. */
void pc_vi_frame_needs_draw(void) {
    if (g_pc_vi_skip && g_pc_verbose) {
        printf("[VI] frame %u: shown after all (it copies the screen)\n", (unsigned)pc_frame_counter);
    }
    g_pc_vi_skip = 0;
}

static Uint64 vi_deadline(void) {
    return sched_base + (Uint64)(sched_frame * (u64)perf_freq / VI_RATE);
}

static void vi_wait_until(Uint64 until) {
    for (;;) {
        Uint64 now = SDL_GetPerformanceCounter();

        if (now >= until) {
            return;
        }
        /* sleep while more than 2 ms remain, spin the rest for precision */
        if ((until - now) * 1000000 / perf_freq > 2000) {
            SDL_Delay(1);
        }
    }
}

/* --sim-refresh: a shown frame waits for the next refresh of an imitated
 * display, as a VSync swap does */
static void vi_sim_refresh(void) {
    static Uint64 origin = 0;
    Uint64 now = SDL_GetPerformanceCounter();
    u64 tick;

    if (origin == 0) {
        origin = now;
    }
    tick = (u64)(now - origin) * (u64)g_pc_sim_refresh / perf_freq + 1;
    vi_wait_until(origin + (Uint64)(tick * perf_freq / (u64)g_pc_sim_refresh));
}

static void vi_stats(Uint64 now, int shown) {
    double secs;

    if (stats_start == 0) {
        stats_start = now;
    }
    stats_frames++;
    stats_shown += shown;
    secs = (double)(now - stats_start) / (double)perf_freq;
    if (secs < (g_pc_verbose ? 5.0 : (double)VI_STATS_SECS)) {
        return;
    }
    /* without --verbose (and on Android in log.txt) only when frames were skipped */
    if (g_pc_verbose || stats_shown < stats_frames) {
        unsigned skipped = stats_frames - stats_shown;

        printf("[VI] %.0f s: %u game frames (%.0f%% of full speed), %u shown (%.1f a second); "
               "%u not shown (%.1f ms each), %u schedule resets, %u holds\n",
               secs, stats_frames, stats_frames * 100.0 / (secs * VI_RATE), stats_shown, stats_shown / secs,
               skipped, skipped ? (double)stats_skip_cost * 1000.0 / (double)perf_freq / skipped : 0.0,
               stats_resets, stats_holds);
    }
    stats_start = now;
    stats_frames = stats_shown = stats_resets = stats_holds = 0;
    stats_skip_cost = 0;
}

/* After a frame (begun at frame_begin, shown or not): wait for the end of its
 * slot, and choose whether the next frame is shown. */
static void vi_pace(Uint64 frame_begin, int shown) {
    Uint64 period = perf_freq / VI_RATE;
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 deadline, late = 0;
    int skip_next = 0;

    if (skip_hold > 0) {
        skip_hold--;
    }
    if (!shown && frame_begin != 0) {
        /* A frame not shown saves the drawing and the wait to show it. If
         * such frames still take most of a frame's time, the CPU itself is
         * too slow and skipping only makes the picture choppy: then all
         * frames are shown for a while (smooth and slow, as before). */
        skip_cost += now - frame_begin;
        stats_skip_cost += now - frame_begin;
        if (++skip_samples >= VI_SKIP_SAMPLES) {
            if (skip_cost / VI_SKIP_SAMPLES >= period * 3 / 4) {
                skip_hold = VI_SKIP_HOLD;
                stats_holds++;
            }
            skip_cost = 0;
            skip_samples = 0;
        }
    }
    if (g_pc_no_framelimit) {
        sched_base = 0; /* starts over when the limiter is back on (F3) */
        g_pc_vi_skip = 0;
        return;
    }
    if (sched_base == 0) {
        sched_base = now;
        sched_frame = 0;
    }
    sched_frame++;
    deadline = vi_deadline();
    if (now < deadline) {
        vi_wait_until(deadline);
    } else {
        late = now - deadline;
        if (late > VI_MAX_BEHIND * period) {
            /* too far behind (a pause for loading or saving, or frames slower
             * than skipping makes up for): start the schedule over from now
             * rather than rushing through the backlog */
            sched_base = now;
            sched_frame = 0;
            late = 0;
            stats_resets++;
        } else if (late < period) {
            Uint64 give = period / VI_GIVE;

            if (give > late) {
                give = late;
            }
            sched_base += give;
            late -= give;
        }
    }

    /* the next frame is not shown when the game is a whole frame behind */
    if (late >= period && skip_run < VI_MAX_SKIP && skip_hold == 0 &&
        !pc_input_script_draws_next()) {
        skip_next = 1;
    }
    g_pc_vi_skip = skip_next;
}

void VIInit(void) { }

void VIConfigure(void* rm) { (void)rm; }

void VISetNextFrameBuffer(void* fb) { (void)fb; }

void VIFlush(void) {}

void VIWaitForRetrace(void) {
    int shown = !g_pc_vi_skip;

    if (!perf_freq) perf_freq = SDL_GetPerformanceFrequency();

    /* --- frame time diagnostic --- */
    Uint64 vi_enter = SDL_GetPerformanceCounter();
    double frame_ms = 0.0;
    if (frame_start_time) {
        frame_ms = (double)(vi_enter - frame_start_time) * 1000.0 / (double)perf_freq;
    }

    if (!pc_platform_poll_events()) {
        g_pc_running = 0;
        return;
    }

    pc_audio_frame();
    pc_update_frame();
    pc_texture_pack_frame(); /* HD textures the thread loaded */
    pc_minimap_draw();
    pc_touch_draw();
    pc_gui_frame(); /* the menus over everything */
    if (!pc_input_script_frame(pc_frame_counter)) {
        g_pc_running = 0;
    }

    Uint64 t_before_swap = SDL_GetPerformanceCounter();
    if (shown) {
        pc_platform_swap_buffers();
        if (g_pc_sim_refresh > 0) {
            vi_sim_refresh();
        }
        skip_run = 0;
    } else {
        skip_run++;
    }
    Uint64 t_after_swap = SDL_GetPerformanceCounter();

    Uint64 t_before_pace = SDL_GetPerformanceCounter();
    vi_pace(frame_start_time, shown);
    Uint64 t_after_pace = SDL_GetPerformanceCounter();

    /* report slow frames (>20ms = missed 60fps by >4ms) */
    if (frame_ms > 20.0 && g_pc_verbose) {
        double swap_ms = (double)(t_after_swap - t_before_swap) * 1000.0 / (double)perf_freq;
        double pace_ms = (double)(t_after_pace - t_before_pace) * 1000.0 / (double)perf_freq;
        double work_ms = (double)(vi_enter - frame_start_time) * 1000.0 / (double)perf_freq;
        int audio_fill = pc_audio_get_buffer_fill();
        printf("[STUTTER] frame %u: total=%.1fms work=%.1fms swap=%.1fms pace=%.1fms audio_fill=%d%s\n",
               (unsigned)pc_frame_counter, frame_ms, work_ms - swap_ms - pace_ms, swap_ms, pace_ms, audio_fill,
               shown ? "" : " (not shown)");
    }

    {
        /* the title shows the frames shown a second */
        static Uint64 fps_start = 0;
        static int fps_count = 0, fps_frames = 0;
        if (fps_start == 0) fps_start = SDL_GetPerformanceCounter();
        fps_count += shown;
        fps_frames++;
        if (fps_frames >= 60) {
            Uint64 now = SDL_GetPerformanceCounter();
            double secs = (double)(now - fps_start) / (double)perf_freq;
            double fps = (double)fps_count / secs;
            char title[64];
            snprintf(title, sizeof(title), "Dobutsu no Mori e+ - %.1f FPS", fps);
            SDL_SetWindowTitle(g_pc_window, title);
            fps_start = now;
            fps_count = 0;
            fps_frames = 0;
        }
    }

    frame_start_time = SDL_GetPerformanceCounter();
    vi_stats(frame_start_time, shown);

    retrace_count++;
    pc_frame_counter++;
}

u32 VIGetRetraceCount(void) { return retrace_count; }

void VISetBlack(BOOL black) { (void)black; }

u32 VIGetTvFormat(void) { return 0; /* VI_NTSC */ }
u32 VIGetDTVStatus(void) { return 0; }

void* VISetPreRetraceCallback(void* cb) {
    void* old = (void*)vi_pre_callback;
    vi_pre_callback = (void (*)(u32))cb;
    return old;
}

void* VISetPostRetraceCallback(void* cb) {
    void* old = (void*)vi_post_callback;
    vi_post_callback = (void (*)(u32))cb;
    return old;
}

u32 VIGetCurrentLine(void) { return 0; }

void VISetNextXFB(void* xfb) { (void)xfb; }
