/* pc_mwcc_compat.h - stand-ins for CodeWarrior/MSL features used by decomp code.
 * Included from types.h on TARGET_PC builds. */
#ifndef PC_MWCC_COMPAT_H
#define PC_MWCC_COMPAT_H

#include <ctype.h>
#include <errno.h>
#include <math.h>

/* OSContPad and friends have a member named errno; glibc/MSVCRT make errno a
 * macro. errno.h is included above so its guard is set; drop the macro so the
 * member name survives. */
#undef errno

/* MSL's stddef.h extension (include/libc is not on the PC include path) */
#ifndef membersize
#define membersize(type, member) (sizeof(((type*)0)->member))
#endif

/* MSL fpclassify value for a quiet NaN */
#ifndef FP_QNAN
#define FP_QNAN FP_NAN
#endif

/* MSL ctype helpers without EOF/locale handling */
#ifndef _tolower
#define _tolower(c) tolower(c)
#endif
#ifndef _toupper
#define _toupper(c) toupper(c)
#endif

#ifdef __cplusplus
#include <cmath>
/* MSL's <cmath> puts the C float functions in std; libstdc++ only has overloads */
namespace std {
using ::acosf;
using ::asinf;
using ::atan2f;
using ::ceilf;
using ::cosf;
using ::fabsf;
using ::floorf;
using ::fmodf;
using ::powf;
using ::sinf;
using ::sqrtf;
using ::tanf;
}
#endif

/* Gekko reciprocal square root estimate */
static inline double __frsqrte(double x) {
    return 1.0 / sqrt(x);
}

/* Gekko count leading zeros */
static inline int __cntlzw(unsigned int x) {
    return x == 0 ? 32 : __builtin_clz(x);
}

#endif
