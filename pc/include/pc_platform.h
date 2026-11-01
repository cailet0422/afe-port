/* pc_platform.h - SDL2/OpenGL platform layer, global state, crash protection */
#ifndef PC_PLATFORM_H
#define PC_PLATFORM_H

#include <stdint.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "pc_gl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "pc_types.h"

/* --- Configuration --- */
#define PC_GC_WIDTH       640
#define PC_GC_HEIGHT      480
#define PC_SCREEN_WIDTH   PC_GC_WIDTH
#define PC_SCREEN_HEIGHT  PC_GC_HEIGHT
#define PC_WINDOW_TITLE   "Animal Crossing"

#define PC_MAIN_MEMORY_SIZE   (24 * 1024 * 1024)
#define PC_ARAM_SIZE          (32 * 1024 * 1024) /* GC has 16MB; e+ also keeps famicom.arc here on PC */
#define PC_FIFO_SIZE          (256 * 1024)

#define PC_PI  3.14159265358979323846
#define PC_PIf 3.14159265358979323846f
#define PC_DEG_TO_RAD (PC_PI / 180.0)
#define PC_DEG_TO_RADf (PC_PIf / 180.0f)

/* GC hardware clocks */
#define GC_BUS_CLOCK          162000000u
#define GC_CORE_CLOCK         486000000u
#define GC_TIMER_CLOCK        (GC_BUS_CLOCK / 4)

/* --- Platform headers --- */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef near
#undef far
#else
#include <signal.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <elf.h>
#endif
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Global state --- */
extern SDL_Window*   g_pc_window;
extern SDL_GLContext  g_pc_gl_context;
extern int           g_pc_running;
extern int           g_pc_restart; /* start the game again once it has shut down */
extern int           g_pc_verbose;
extern int           g_pc_no_framelimit;
extern int           g_pc_skip_draw; /* drop GL draws (fast input-script replay) */
extern int           g_pc_time_override;
extern int           g_pc_deterministic; /* clock driven by frames from a fixed date (testing) */
extern int           g_pc_deterministic_days; /* --days: days after that date */
extern int           g_pc_min_override;
extern int           g_pc_sec_override;

extern int g_pc_window_w;
extern int g_pc_window_h;
void pc_platform_update_window_size(void);

/* Ends the game loop and starts the game again (e.g. after installing the
 * Korean patch, which is read at startup) */
void pc_request_restart(void);

/* --- Widescreen mode (3-state) ---
 * 0 = hor+ (default): full-window viewport, FOV-corrected projection. Resets each frame.
 * 1 = stretch: full-window, no correction. For transitions/inventory BG.
 * 2 = pillarbox: centered 4:3 with black bars. For inventory UI alignment.
 *
 * m_play.c inserts NOOPTag markers in POLY_OPA to switch between states:
 *   ON (0xAC5701) -> 1, OFF (0xAC5700) -> 2. emu64 reads these during DL processing. */
#define PC_NOOP_WIDESCREEN_STRETCH     0xAC5701u
#define PC_NOOP_WIDESCREEN_STRETCH_OFF 0xAC5700u
extern int g_pc_widescreen_stretch;

/* --- Functions --- */
void pc_platform_init(void);
#ifdef __ANDROID__
void pc_android_prepare(void); /* working folder and APK assets (pc_android.c) */
void pc_android_crash(int sig, siginfo_t* info, void* ucontext); /* writes crash.txt, then the system's handler */
#endif
void pc_platform_shutdown(void);
void pc_exit(int code);                  /* ends the process (_exit on Android, see pc_main.c) */
void pc_fatal(const char* fmt, ...);     /* start-up failure: log, message box, end */
void pc_platform_swap_buffers(void);
int  pc_platform_poll_events(void);

/* --- Crash protection (VEH + setjmp/longjmp) --- */
void pc_crash_protection_init(void);
void pc_crash_set_jmpbuf(jmp_buf* buf);  /* NULL to disable */
unsigned int pc_crash_get_addr(void);
unsigned int pc_crash_get_data_addr(void);

/* EXE image range for seg2k0 pointer disambiguation (vs N64 segment addresses) */
extern uintptr_t pc_image_base;
extern uintptr_t pc_image_end;

/* --- Model viewer --- */
extern int g_pc_model_viewer;
extern int g_pc_model_viewer_start;
extern int g_pc_model_viewer_no_cull;

/* --- Per-frame diagnostics --- */
extern int pc_emu64_frame_cmds;
extern int pc_emu64_frame_crashes;
extern int pc_emu64_frame_noop_cmds;
extern int pc_emu64_frame_tri_cmds;
extern int pc_emu64_frame_vtx_cmds;
extern int pc_emu64_frame_dl_cmds;
extern int pc_emu64_frame_cull_visible;
extern int pc_emu64_frame_cull_rejected;
extern int pc_gx_draw_call_count;

/* --- Audio --- */
extern int pc_save_loaded;
int  pc_audio_get_buffer_fill(void);
void pc_audio_frame(void);
int  pc_audio_sleep(u32 ms); /* msleep with --deterministic: makes the sound, returns 1 */
int  pc_audio_is_active(void);
void pc_audio_set_paused(int paused); // Can be used to pause the audio. Not used at the moment.
void pc_audio_shutdown(void);
void pc_audio_start_producer_thread(void);
int  pc_audio_dump_open(const char* path); /* --audio-dump: write the sound to a WAV file */
void pc_audio_mq_init(void);
void pc_audio_mq_shutdown(void);

/* --- OS (pc_os.c): "interrupts off" is a lock the sound thread also takes --- */
BOOL OSDisableInterrupts(void);
BOOL OSRestoreInterrupts(BOOL level);
void pc_os_interrupts_release(void);
/* Move the clock (OSGetTime, so the game's RTC) forward by that many seconds:
 * the input script's CLOCK, to cross into another day without a restart */
void pc_os_advance_clock(long long seconds);

/* --- Graphics (pc_gx.c): a point of the window (0-1) in the 640x480 screen
 * the game's 2D drawing uses, after widescreen correction --- */
void pc_gx_window_to_ui(float wx, float wy, float* x, float* y);
/* How far a window wider than 4:3 reaches past the game's 4:3 2D picture on
 * each side, in that picture's 320-wide pixels (0 if it does not) */
float pc_gx_ui_extra_half_width(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_PLATFORM_H */
