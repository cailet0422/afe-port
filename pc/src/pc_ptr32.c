/* pc_ptr32.c - see pc_ptr32.h
 *
 * Every store takes a new slot and slots are never reused, so a copy of a
 * struct keeps the pointer it had, as with a real pointer. Each load of a
 * resource costs one slot (8 bytes). */
#include "pc_ptr32.h"

#include <SDL.h>
#include <stdlib.h>

static void** s_table = NULL; /* slot 0 is never used */
static uint32_t s_count = 1;
static uint32_t s_capacity = 0;
static SDL_SpinLock s_lock = 0;

uint32_t pc_ptr32_set(uint32_t slot, void* p) {
    if (p == NULL) {
        return 0;
    }
    (void)slot; /* a new slot every time: copies keep what they had */
    SDL_AtomicLock(&s_lock);
    if (s_count >= s_capacity) {
        s_capacity = s_capacity ? s_capacity * 2 : 1024;
        s_table = (void**)realloc(s_table, s_capacity * sizeof(void*));
    }
    slot = s_count++;
    s_table[slot] = p;
    SDL_AtomicUnlock(&s_lock);
    return slot;
}

void* pc_ptr32_get(uint32_t slot) {
    void* p;

    if (slot == 0) {
        return NULL;
    }
    SDL_AtomicLock(&s_lock);
    p = slot < s_count ? s_table[slot] : NULL;
    SDL_AtomicUnlock(&s_lock);
    return p;
}
