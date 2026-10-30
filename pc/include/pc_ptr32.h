/* pc_ptr32.h - a pointer kept in 32 bits, for structs that overlay disc data
 *
 * Some JSystem structs are read straight from disc files and hold a
 * runtime pointer in a 4-byte slot (zero on disc), e.g. a RARC file
 * entry's data pointer. A 64-bit pointer does not fit without moving every
 * field after it, so in 64-bit builds the slot holds an index into a table
 * of pointers instead (0 = null). 32-bit builds store the pointer itself.
 *
 * PCPtr32 converts to and from void* so code reads as if the field were a
 * plain pointer. */
#ifndef PC_PTR32_H
#define PC_PTR32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stores p in slot (0 = take a new one); returns the slot, or 0 for NULL */
uint32_t pc_ptr32_set(uint32_t slot, void* p);
void* pc_ptr32_get(uint32_t slot);

#ifdef __cplusplus
}

struct PCPtr32 {
#if UINTPTR_MAX > 0xFFFFFFFFu
    uint32_t slot;

    operator void*() const {
        return pc_ptr32_get(slot);
    }
    PCPtr32& operator=(void* p) {
        slot = pc_ptr32_set(slot, p);
        return *this;
    }
#else
    void* ptr;

    operator void*() const {
        return ptr;
    }
    PCPtr32& operator=(void* p) {
        ptr = p;
        return *this;
    }
#endif
};
#endif

#endif /* PC_PTR32_H */
